// Stage 17C: Vulkan validation-layer offscreen smoke.
//
// Every other vulkan_*_test smoke builds its instance with no layers via
// support/VulkanSmokeContext.hpp. This test instead builds its OWN instance
// with VK_LAYER_KHRONOS_validation + VK_EXT_debug_utils and a debug messenger,
// then drives a real offscreen workload (buffer upload -> device copy ->
// readback, plus an image create/destroy) through the Render2D Vulkan runtimes.
// The messenger counts validation messages and the test asserts ZERO validation
// errors. It reuses only the physical-device/device selection helpers from the
// smoke context -- never the shared instance -- so the other ~50 smokes are
// untouched.
//
// Graceful skip (return 0) when the validation layer is unavailable (e.g., no
// Vulkan SDK on the runner), when no ICD/instance can be created, or when no
// usable GPU is present -- the same green-without-a-GPU contract as the other
// vulkan_* smokes.

#include <Render2D/Render2D.hpp>

#include "support/VulkanSmokeContext.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

namespace R2D = Render2D;

namespace {

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using BufferRef = R2D::BufferRef<Provider, Dim>;
using ImageRef = R2D::ImageRef<Provider, Dim>;
using FrameSync = R2D::FrameSync<Provider, Dim>;
using NativeCommandBufferRef = R2D::NativeCommandBufferRef<Provider, Dim>;
using ResourceRuntime = R2D::VulkanResourceRuntime<Provider, Dim>;

struct ValidationCounters {
    R2D::U32 error_count;
    R2D::U32 warning_count;
};

VKAPI_ATTR VkBool32 VKAPI_CALL validationDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity_,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data_,
    void* user_data_)
{
    auto* counters = static_cast<ValidationCounters*>(user_data_);
    const char* message = (callback_data_ != nullptr && callback_data_->pMessage != nullptr)
        ? callback_data_->pMessage
        : "(no message)";
    if ((severity_ & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0) {
        if (counters != nullptr) {
            ++counters->error_count;
        }
        std::fprintf(stderr, "[validation ERROR] %s\n", message);
    } else if ((severity_ & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0) {
        if (counters != nullptr) {
            ++counters->warning_count;
        }
        std::fprintf(stderr, "[validation warning] %s\n", message);
    }
    return VK_FALSE;
}

bool validationLayerAvailable()
{
    R2D::U32 layer_count = 0U;
    if (vkEnumerateInstanceLayerProperties(&layer_count, nullptr) != VK_SUCCESS || layer_count == 0U) {
        return false;
    }
    R2D::McVector<VkLayerProperties> layers(layer_count);
    if (vkEnumerateInstanceLayerProperties(&layer_count, layers.data()) != VK_SUCCESS) {
        return false;
    }
    for (const auto& layer : layers) {
        if (std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
            return true;
        }
    }
    return false;
}

bool createValidatedInstance(
    const VkDebugUtilsMessengerCreateInfoEXT& messenger_info_,
    VkInstance& out_instance_)
{
    const R2D::U32 api_version = Render2DTest::queryInstanceApiVersion();
    const VkApplicationInfo application_info{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = "Render2D Validation Smoke",
        .applicationVersion = VK_MAKE_VERSION(0U, 1U, 0U),
        .pEngineName = "Render2D Validation Smoke",
        .engineVersion = VK_MAKE_VERSION(0U, 1U, 0U),
        .apiVersion = api_version >= VK_API_VERSION_1_3 ? VK_API_VERSION_1_3 : VK_API_VERSION_1_0,
    };
    const std::array<const char*, 1U> layers{"VK_LAYER_KHRONOS_validation"};
    const std::array<const char*, 1U> extensions{VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
    // Chaining the messenger info into pNext also validates instance
    // creation and destruction, not just the calls in between.
    const VkInstanceCreateInfo instance_info{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = &messenger_info_,
        .flags = 0U,
        .pApplicationInfo = &application_info,
        .enabledLayerCount = static_cast<R2D::U32>(layers.size()),
        .ppEnabledLayerNames = layers.data(),
        .enabledExtensionCount = static_cast<R2D::U32>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };
    return vkCreateInstance(&instance_info, nullptr, &out_instance_) == VK_SUCCESS;
}

VkResult createDebugMessenger(
    VkInstance instance_,
    const VkDebugUtilsMessengerCreateInfoEXT& messenger_info_,
    VkDebugUtilsMessengerEXT& out_messenger_)
{
    auto create_fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
    if (create_fn == nullptr) {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
    return create_fn(instance_, &messenger_info_, nullptr, &out_messenger_);
}

void destroyDebugMessenger(VkInstance instance_, VkDebugUtilsMessengerEXT messenger_)
{
    if (messenger_ == VK_NULL_HANDLE) {
        return;
    }
    auto destroy_fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
    if (destroy_fn != nullptr) {
        destroy_fn(instance_, messenger_, nullptr);
    }
}

// Real offscreen workload under the validation messenger: an upload buffer is
// copied to a device-local buffer, barriered, copied to a readback buffer,
// submitted, waited, and verified, then an image is created and released.
void runValidatedWorkload(const Render2DTest::VulkanSmokeContext& context_)
{
    ResourceRuntime resource_runtime;
    auto result = resource_runtime.initialize({
        .physical_device = context_.physical_device,
        .device = context_.device,
    });
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(resource_runtime.reserveBuffers(3U).code == R2D::NativeStatusCode::Ok);
    assert(resource_runtime.reserveImages(1U).code == R2D::NativeStatusCode::Ok);

    constexpr R2D::U64 kByteCount = 16U;
    const std::array<R2D::U8, static_cast<R2D::Usize>(kByteCount)> source_bytes{
        0x01U, 0x23U, 0x45U, 0x67U, 0x89U, 0xABU, 0xCDU, 0xEFU,
        0x10U, 0x32U, 0x54U, 0x76U, 0x98U, 0xBAU, 0xDCU, 0xFEU,
    };

    BufferRef upload_buffer{};
    result = resource_runtime.createBufferRef(
        kByteCount, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, R2D::NativeMemoryDomain::Upload, upload_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.writeBuffer(upload_buffer, source_bytes.data(), kByteCount, 0U);
    assert(result.code == R2D::NativeStatusCode::Ok);

    BufferRef device_buffer{};
    result = resource_runtime.createBufferRef(
        kByteCount,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        R2D::NativeMemoryDomain::DeviceLocal,
        device_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);

    BufferRef readback_buffer{};
    result = resource_runtime.createBufferRef(
        kByteCount, VK_BUFFER_USAGE_TRANSFER_DST_BIT, R2D::NativeMemoryDomain::Readback, readback_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);

    R2D::VulkanCommandRuntime<Provider, Dim> command_runtime;
    result = command_runtime.initialize({
        .device = context_.device,
        .queue_family_index = context_.queue_family_index,
        .command_pool_flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    });
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(command_runtime.reserveCommandBuffers(1U).code == R2D::NativeStatusCode::Ok);

    NativeCommandBufferRef command_ref{};
    result = command_runtime.allocateCommandBufferRef(
        0U, {.first = 0U, .count = 0U}, {.first = 0U, .count = 0U}, 0U, command_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = command_runtime.beginCommandBuffer(command_ref, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    assert(result.code == R2D::NativeStatusCode::Ok);

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    result = command_runtime.resolveNativeCommandBuffer(command_ref, command_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);

    result = resource_runtime.recordCopyBuffer(command_buffer, upload_buffer, device_buffer, kByteCount);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.recordBufferBarrier(
        command_buffer,
        device_buffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_TRANSFER_READ_BIT);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.recordCopyBuffer(command_buffer, device_buffer, readback_buffer, kByteCount);
    assert(result.code == R2D::NativeStatusCode::Ok);

    result = command_runtime.endCommandBuffer(command_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);

    R2D::VulkanSyncRuntime<Provider, Dim> sync_runtime;
    result = sync_runtime.initialize({.device = context_.device, .fence_create_flags = 0U});
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(sync_runtime.reserveFrameSyncs(1U).code == R2D::NativeStatusCode::Ok);
    FrameSync frame_sync{};
    result = sync_runtime.createFrameSync(0U, 0U, frame_sync);
    assert(result.code == R2D::NativeStatusCode::Ok);

    R2D::VulkanSubmitRuntime<Provider, Dim> submit_runtime;
    result = submit_runtime.initialize({.queue = context_.queue, .wait_stage_flags = VK_PIPELINE_STAGE_TRANSFER_BIT});
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(submit_runtime.reserveCommandBuffers(1U).code == R2D::NativeStatusCode::Ok);

    const std::array<NativeCommandBufferRef, 1U> command_refs{command_ref};
    result = submit_runtime.submit(command_refs, frame_sync, command_runtime, sync_runtime, 0U);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = sync_runtime.waitFence(frame_sync, 1'000'000'000ULL);
    assert(result.code == R2D::NativeStatusCode::Ok);

    std::array<R2D::U8, static_cast<R2D::Usize>(kByteCount)> copied_bytes{};
    result = resource_runtime.readBuffer(readback_buffer, copied_bytes.data(), kByteCount, 0U);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(copied_bytes == source_bytes);

    ImageRef color_image{};
    result = resource_runtime.createImageRef(
        4U,
        4U,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        color_image);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.releaseImageRef(color_image);
    assert(result.code == R2D::NativeStatusCode::Ok);

    result = command_runtime.releaseCommandBufferRef(command_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = sync_runtime.releaseFrameSync(frame_sync);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.releaseBufferRef(upload_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.releaseBufferRef(device_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.releaseBufferRef(readback_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
}

} // namespace

int main()
{
    try {
        if (!validationLayerAvailable()) {
            return 0;  // validation layer not installed -> graceful skip
        }

        ValidationCounters counters{.error_count = 0U, .warning_count = 0U};
        const VkDebugUtilsMessengerCreateInfoEXT messenger_info{
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .pNext = nullptr,
            .flags = 0U,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = validationDebugCallback,
            .pUserData = &counters,
        };

        VkInstance instance = VK_NULL_HANDLE;
        if (!createValidatedInstance(messenger_info, instance)) {
            return 0;  // no ICD / debug_utils unavailable -> graceful skip
        }

        VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
        createDebugMessenger(instance, messenger_info, messenger);  // best-effort standalone messenger

        VkPhysicalDevice physical_device = VK_NULL_HANDLE;
        R2D::U32 queue_family_index = 0U;
        Render2DTest::VulkanSmokeFeatureSupport feature_support{};
        if (!Render2DTest::selectPhysicalDevice(instance, physical_device, queue_family_index, feature_support)) {
            destroyDebugMessenger(instance, messenger);
            vkDestroyInstance(instance, nullptr);
            return 0;
        }

        VkDevice device = VK_NULL_HANDLE;
        if (Render2DTest::createDevice(physical_device, queue_family_index, feature_support, device) != VK_SUCCESS) {
            destroyDebugMessenger(instance, messenger);
            vkDestroyInstance(instance, nullptr);
            return 0;
        }

        VkQueue queue = VK_NULL_HANDLE;
        vkGetDeviceQueue(device, queue_family_index, 0U, &queue);
        if (queue != VK_NULL_HANDLE) {
            const Render2DTest::VulkanSmokeContext context{
                .instance = instance,
                .physical_device = physical_device,
                .device = device,
                .queue = queue,
                .queue_family_index = queue_family_index,
                .api_version = feature_support.api_version,
                .supports_dynamic_rendering = feature_support.supports_dynamic_rendering,
                .supports_descriptor_indexing = feature_support.supports_descriptor_indexing,
                .supports_bindless = feature_support.supports_bindless,
            };
            runValidatedWorkload(context);
            vkDeviceWaitIdle(device);
        }

        vkDestroyDevice(device, nullptr);
        destroyDebugMessenger(instance, messenger);
        vkDestroyInstance(instance, nullptr);

        // The contract of this smoke: a real offscreen workload through the
        // Render2D runtimes produces ZERO Vulkan validation errors. Warnings are
        // printed for visibility but are not fatal.
        assert(counters.error_count == 0U);
        if (counters.error_count != 0U) {
            return 1;
        }
    } catch (...) {
        return 1;
    }

    return 0;
}
