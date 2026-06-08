#include <Render2D/Render2D.hpp>

#include <vulkan/vulkan.h>

#include <cassert>
#include <vector>

namespace R2D = Render2D;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using NativeCommandBufferRef = R2D::NativeCommandBufferRef<Provider, Dim>;
using Runtime = R2D::VulkanCommandRuntime<Provider, Dim>;
using U32 = R2D::U32;

static_assert(!R2D::StrictPodComponent<Runtime>);
static_assert(R2D::StrictPodComponent<R2D::VulkanCommandRuntimeConfig>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, NativeCommandBufferRef>);

struct VulkanSmokeContext {
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    U32 queue_family_index;
};

VkResult createInstance(VkInstance& out_instance_) noexcept
{
    constexpr char kApplicationName[] = "Render2D VulkanCommandRuntime Test";
    const VkApplicationInfo application_info{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = kApplicationName,
        .applicationVersion = VK_MAKE_VERSION(0U, 1U, 0U),
        .pEngineName = kApplicationName,
        .engineVersion = VK_MAKE_VERSION(0U, 1U, 0U),
        .apiVersion = VK_API_VERSION_1_0,
    };
    const VkInstanceCreateInfo instance_info{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .pApplicationInfo = &application_info,
        .enabledLayerCount = 0U,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = 0U,
        .ppEnabledExtensionNames = nullptr,
    };

    return vkCreateInstance(&instance_info, nullptr, &out_instance_);
}

bool findQueueFamily(
    VkPhysicalDevice physical_device_,
    VkQueueFlags preferred_flags_,
    U32& out_queue_family_index_)
{
    U32 queue_family_count = 0U;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &queue_family_count, nullptr);
    if (queue_family_count == 0U) {
        return false;
    }

    std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(
        physical_device_,
        &queue_family_count,
        queue_families.data());

    for (U32 queue_family_index = 0U; queue_family_index < queue_family_count; ++queue_family_index) {
        const auto& queue_family = queue_families[queue_family_index];
        if (queue_family.queueCount > 0U &&
            (queue_family.queueFlags & preferred_flags_) == preferred_flags_) {
            out_queue_family_index_ = queue_family_index;
            return true;
        }
    }

    for (U32 queue_family_index = 0U; queue_family_index < queue_family_count; ++queue_family_index) {
        if (queue_families[queue_family_index].queueCount > 0U) {
            out_queue_family_index_ = queue_family_index;
            return true;
        }
    }

    return false;
}

bool selectPhysicalDevice(
    VkInstance instance_,
    VkPhysicalDevice& out_physical_device_,
    U32& out_queue_family_index_)
{
    U32 physical_device_count = 0U;
    VkResult result = vkEnumeratePhysicalDevices(instance_, &physical_device_count, nullptr);
    if (result != VK_SUCCESS || physical_device_count == 0U) {
        return false;
    }

    std::vector<VkPhysicalDevice> physical_devices(physical_device_count);
    result = vkEnumeratePhysicalDevices(instance_, &physical_device_count, physical_devices.data());
    if (result != VK_SUCCESS) {
        return false;
    }

    for (auto physical_device : physical_devices) {
        U32 queue_family_index = 0U;
        if (findQueueFamily(physical_device, VK_QUEUE_GRAPHICS_BIT, queue_family_index)) {
            out_physical_device_ = physical_device;
            out_queue_family_index_ = queue_family_index;
            return true;
        }
    }

    return false;
}

VkResult createDevice(
    VkPhysicalDevice physical_device_,
    U32 queue_family_index_,
    VkDevice& out_device_) noexcept
{
    float queue_priority = 1.0F;
    const VkDeviceQueueCreateInfo queue_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .queueFamilyIndex = queue_family_index_,
        .queueCount = 1U,
        .pQueuePriorities = &queue_priority,
    };
    const VkDeviceCreateInfo device_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .queueCreateInfoCount = 1U,
        .pQueueCreateInfos = &queue_info,
        .enabledLayerCount = 0U,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = 0U,
        .ppEnabledExtensionNames = nullptr,
        .pEnabledFeatures = nullptr,
    };

    return vkCreateDevice(physical_device_, &device_info, nullptr, &out_device_);
}

bool createSmokeContext(VulkanSmokeContext& out_context_)
{
    VkInstance instance = VK_NULL_HANDLE;
    VkResult result = createInstance(instance);
    if (result != VK_SUCCESS) {
        return false;
    }

    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    U32 queue_family_index = 0U;
    if (!selectPhysicalDevice(instance, physical_device, queue_family_index)) {
        vkDestroyInstance(instance, nullptr);
        return false;
    }

    VkDevice device = VK_NULL_HANDLE;
    result = createDevice(physical_device, queue_family_index, device);
    if (result != VK_SUCCESS) {
        vkDestroyInstance(instance, nullptr);
        return false;
    }

    out_context_ = {
        .instance = instance,
        .physical_device = physical_device,
        .device = device,
        .queue_family_index = queue_family_index,
    };
    return true;
}

void destroySmokeContext(const VulkanSmokeContext& context_) noexcept
{
    if (context_.device != VK_NULL_HANDLE) {
        vkDestroyDevice(context_.device, nullptr);
    }
    if (context_.instance != VK_NULL_HANDLE) {
        vkDestroyInstance(context_.instance, nullptr);
    }
}

void testInvalidConfig()
{
    Runtime runtime;
    const auto result = runtime.initialize({
        .device = VK_NULL_HANDLE,
        .queue_family_index = 0U,
        .command_pool_flags = 0U,
    });
    assert(result.code == R2D::NativeStatusCode::InvalidInput);
    assert(!runtime.isInitialized());
}

void testVulkanCommandLifecycle(const VulkanSmokeContext& context_)
{
    Runtime runtime;
    auto result = runtime.initialize({
        .device = context_.device,
        .queue_family_index = context_.queue_family_index,
        .command_pool_flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    });
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(runtime.isInitialized());
    assert(runtime.nativeCommandPool() != VK_NULL_HANDLE);
    assert(runtime.lastVulkanResult() == VK_SUCCESS);

    auto capacity = runtime.reserveCommandBuffers(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);

    NativeCommandBufferRef first_ref{};
    result = runtime.allocateCommandBufferRef(
        3U,
        {.first = 4U, .count = 5U},
        {.first = 6U, .count = 7U},
        8U,
        first_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(first_ref.command_buffer_id == 0U);
    assert(first_ref.generation == 1U);
    assert(runtime.commandBufferCount() == 1U);

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    result = runtime.resolveNativeCommandBuffer(first_ref, command_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(command_buffer != VK_NULL_HANDLE);

    result = runtime.beginCommandBuffer(first_ref, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    assert(result.code == R2D::NativeStatusCode::Ok);

    result = runtime.beginCommandBuffer(first_ref, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    assert(result.code == R2D::NativeStatusCode::InvalidInput);

    result = runtime.endCommandBuffer(first_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);

    result = runtime.resetCommandBuffer(first_ref, 0U);
    assert(result.code == R2D::NativeStatusCode::Ok);

    result = runtime.beginCommandBuffer(first_ref, 0U);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = runtime.endCommandBuffer(first_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);

    result = runtime.resetCommandPool(0U);
    assert(result.code == R2D::NativeStatusCode::Ok);

    result = runtime.releaseCommandBufferRef(first_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(runtime.commandBufferCount() == 0U);

    result = runtime.resolveNativeCommandBuffer(first_ref, command_buffer);
    assert(result.code == R2D::NativeStatusCode::StaleReference);
    assert(command_buffer == VK_NULL_HANDLE);

    NativeCommandBufferRef reused_ref{};
    result = runtime.allocateCommandBufferRef(
        9U,
        {.first = 0U, .count = 1U},
        {.first = 0U, .count = 0U},
        0U,
        reused_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(reused_ref.command_buffer_id == first_ref.command_buffer_id);
    assert(reused_ref.generation == first_ref.generation + 1U);

    result = runtime.releaseCommandBufferRef(reused_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);
    runtime.shutdown();
    assert(!runtime.isInitialized());
}

int main()
{
    try {
        testInvalidConfig();

        VulkanSmokeContext context{};
        if (!createSmokeContext(context)) {
            return 0;
        }

        testVulkanCommandLifecycle(context);
        destroySmokeContext(context);
    } catch (...) {
        return 1;
    }

    return 0;
}
