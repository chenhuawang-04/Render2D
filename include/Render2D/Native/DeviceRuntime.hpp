#pragma once

#include "Render2D/Native/NativeComponents.hpp"
#include "Render2D/Native/NativeResult.hpp"

#include <vector>

namespace Render2D {

template<class Provider, class Dim>
class NativeDeviceRuntime {
public:
    NativeCapacityResult reserveDevices(U32 capacity_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {
                .code = NativeStatusCode::UnsupportedDomain,
                .requested_count = capacity_,
                .available_count = 0U,
            };
        } else {
            try {
                device_slots.reserve(capacity_);
                free_device_ids.reserve(capacity_);
            } catch (...) {
                return {
                    .code = NativeStatusCode::OutOfMemory,
                    .requested_count = capacity_,
                    .available_count = static_cast<U32>(device_slots.capacity()),
                };
            }

            return {
                .code = NativeStatusCode::Ok,
                .requested_count = capacity_,
                .available_count = static_cast<U32>(device_slots.capacity()),
            };
        }
    }

    NativeCapacityResult reserveQueues(U32 capacity_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {
                .code = NativeStatusCode::UnsupportedDomain,
                .requested_count = capacity_,
                .available_count = 0U,
            };
        } else {
            try {
                queue_slots.reserve(capacity_);
                free_queue_ids.reserve(capacity_);
            } catch (...) {
                return {
                    .code = NativeStatusCode::OutOfMemory,
                    .requested_count = capacity_,
                    .available_count = static_cast<U32>(queue_slots.capacity()),
                };
            }

            return {
                .code = NativeStatusCode::Ok,
                .requested_count = capacity_,
                .available_count = static_cast<U32>(queue_slots.capacity()),
            };
        }
    }

    NativeResult createDeviceHandle(NativeHandle handle_, DeviceHandle<Provider, Dim>& out_handle_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, NativeObjectKind::Device, 0U, 0U);
        } else {
            U32 device_id = 0U;
            if (!acquireDeviceSlot(device_id)) {
                return makeResult(NativeStatusCode::OutOfCapacity, NativeObjectKind::Device, 0U, 0U);
            }

            auto& slot = device_slots[device_id];
            slot.occupied = 1U;
            slot.handle = {
                .handle = handle_.value,
                .device_id = device_id,
                .generation = slot.generation.value,
            };
            ++active_device_count;
            out_handle_ = slot.handle;

            return makeResult(
                NativeStatusCode::Ok,
                NativeObjectKind::Device,
                device_id,
                slot.generation.value);
        }
    }

    NativeResult createQueueHandle(
        NativeHandle handle_,
        U32 queue_family_index_,
        U32 queue_index_,
        U32 flags_,
        QueueHandle<Provider, Dim>& out_handle_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, NativeObjectKind::Queue, 0U, 0U);
        } else {
            U32 queue_id = 0U;
            if (!acquireQueueSlot(queue_id)) {
                return makeResult(NativeStatusCode::OutOfCapacity, NativeObjectKind::Queue, 0U, 0U);
            }

            auto& slot = queue_slots[queue_id];
            slot.occupied = 1U;
            slot.handle = {
                .handle = handle_.value,
                .queue_id = queue_id,
                .generation = slot.generation.value,
                .queue_family_index = queue_family_index_,
                .queue_index = queue_index_,
                .flags = flags_,
            };
            ++active_queue_count;
            out_handle_ = slot.handle;

            return makeResult(
                NativeStatusCode::Ok,
                NativeObjectKind::Queue,
                queue_id,
                slot.generation.value);
        }
    }

    NativeResult resolveDeviceHandle(
        const DeviceHandle<Provider, Dim>& handle_,
        DeviceHandle<Provider, Dim>& out_handle_) const noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, NativeObjectKind::Device, 0U, 0U);
        } else {
            if (!isLiveDeviceHandle(handle_)) {
                return makeResult(
                    NativeStatusCode::StaleReference,
                    NativeObjectKind::Device,
                    handle_.device_id,
                    handle_.generation);
            }

            out_handle_ = device_slots[handle_.device_id].handle;
            return makeResult(
                NativeStatusCode::Ok,
                NativeObjectKind::Device,
                handle_.device_id,
                handle_.generation);
        }
    }

    NativeResult resolveQueueHandle(
        const QueueHandle<Provider, Dim>& handle_,
        QueueHandle<Provider, Dim>& out_handle_) const noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, NativeObjectKind::Queue, 0U, 0U);
        } else {
            if (!isLiveQueueHandle(handle_)) {
                return makeResult(
                    NativeStatusCode::StaleReference,
                    NativeObjectKind::Queue,
                    handle_.queue_id,
                    handle_.generation);
            }

            out_handle_ = queue_slots[handle_.queue_id].handle;
            return makeResult(
                NativeStatusCode::Ok,
                NativeObjectKind::Queue,
                handle_.queue_id,
                handle_.generation);
        }
    }

    NativeResult releaseDeviceHandle(const DeviceHandle<Provider, Dim>& handle_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, NativeObjectKind::Device, 0U, 0U);
        } else {
            if (!isLiveDeviceHandle(handle_)) {
                return makeResult(
                    NativeStatusCode::StaleReference,
                    NativeObjectKind::Device,
                    handle_.device_id,
                    handle_.generation);
            }

            const NativeStatusCode release_code = pushFreeDeviceId(handle_.device_id);
            if (release_code != NativeStatusCode::Ok) {
                return makeResult(
                    release_code,
                    NativeObjectKind::Device,
                    handle_.device_id,
                    handle_.generation);
            }

            auto& slot = device_slots[handle_.device_id];
            slot.occupied = 0U;
            slot.generation.value = nextGeneration(slot.generation.value);
            slot.handle = {};
            --active_device_count;

            return makeResult(
                NativeStatusCode::Ok,
                NativeObjectKind::Device,
                handle_.device_id,
                slot.generation.value);
        }
    }

    NativeResult releaseQueueHandle(const QueueHandle<Provider, Dim>& handle_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, NativeObjectKind::Queue, 0U, 0U);
        } else {
            if (!isLiveQueueHandle(handle_)) {
                return makeResult(
                    NativeStatusCode::StaleReference,
                    NativeObjectKind::Queue,
                    handle_.queue_id,
                    handle_.generation);
            }

            const NativeStatusCode release_code = pushFreeQueueId(handle_.queue_id);
            if (release_code != NativeStatusCode::Ok) {
                return makeResult(
                    release_code,
                    NativeObjectKind::Queue,
                    handle_.queue_id,
                    handle_.generation);
            }

            auto& slot = queue_slots[handle_.queue_id];
            slot.occupied = 0U;
            slot.generation.value = nextGeneration(slot.generation.value);
            slot.handle = {};
            --active_queue_count;

            return makeResult(
                NativeStatusCode::Ok,
                NativeObjectKind::Queue,
                handle_.queue_id,
                slot.generation.value);
        }
    }

    U32 deviceCount() const noexcept
    {
        return active_device_count;
    }

    U32 queueCount() const noexcept
    {
        return active_queue_count;
    }

    U32 deviceCapacity() const noexcept
    {
        return static_cast<U32>(device_slots.capacity());
    }

    U32 queueCapacity() const noexcept
    {
        return static_cast<U32>(queue_slots.capacity());
    }

private:
    struct DeviceSlot {
        DeviceHandle<Provider, Dim> handle;
        NativeGeneration generation;
        U32 occupied;
    };

    struct QueueSlot {
        QueueHandle<Provider, Dim> handle;
        NativeGeneration generation;
        U32 occupied;
    };

    static constexpr U32 kFirstGeneration = 1U;

    static NativeResult makeResult(
        NativeStatusCode code_,
        NativeObjectKind object_kind_,
        U32 object_id_,
        U32 generation_) noexcept
    {
        return {
            .code = code_,
            .object_kind = object_kind_,
            .object_id = {.value = object_id_},
            .generation = {.value = generation_},
        };
    }

    static U32 nextGeneration(U32 generation_) noexcept
    {
        return generation_ == 0xFFFFFFFFU ? kFirstGeneration : generation_ + 1U;
    }

    bool acquireDeviceSlot(U32& out_device_id_)
    {
        if (!free_device_ids.empty()) {
            out_device_id_ = free_device_ids.back();
            free_device_ids.pop_back();
            return true;
        }

        if (device_slots.size() >= device_slots.capacity()) {
            return false;
        }

        try {
            out_device_id_ = static_cast<U32>(device_slots.size());
            device_slots.push_back({
                .handle = {},
                .generation = {.value = kFirstGeneration},
                .occupied = 0U,
            });
        } catch (...) {
            return false;
        }

        return true;
    }

    bool acquireQueueSlot(U32& out_queue_id_)
    {
        if (!free_queue_ids.empty()) {
            out_queue_id_ = free_queue_ids.back();
            free_queue_ids.pop_back();
            return true;
        }

        if (queue_slots.size() >= queue_slots.capacity()) {
            return false;
        }

        try {
            out_queue_id_ = static_cast<U32>(queue_slots.size());
            queue_slots.push_back({
                .handle = {},
                .generation = {.value = kFirstGeneration},
                .occupied = 0U,
            });
        } catch (...) {
            return false;
        }

        return true;
    }

    NativeStatusCode pushFreeDeviceId(U32 device_id_)
    {
        if (free_device_ids.size() >= free_device_ids.capacity()) {
            return NativeStatusCode::OutOfCapacity;
        }

        try {
            free_device_ids.push_back(device_id_);
        } catch (...) {
            return NativeStatusCode::OutOfMemory;
        }

        return NativeStatusCode::Ok;
    }

    NativeStatusCode pushFreeQueueId(U32 queue_id_)
    {
        if (free_queue_ids.size() >= free_queue_ids.capacity()) {
            return NativeStatusCode::OutOfCapacity;
        }

        try {
            free_queue_ids.push_back(queue_id_);
        } catch (...) {
            return NativeStatusCode::OutOfMemory;
        }

        return NativeStatusCode::Ok;
    }

    bool isLiveDeviceHandle(const DeviceHandle<Provider, Dim>& handle_) const noexcept
    {
        if (handle_.device_id >= device_slots.size()) {
            return false;
        }

        const auto& slot = device_slots[handle_.device_id];
        return slot.occupied != 0U && slot.generation.value == handle_.generation;
    }

    bool isLiveQueueHandle(const QueueHandle<Provider, Dim>& handle_) const noexcept
    {
        if (handle_.queue_id >= queue_slots.size()) {
            return false;
        }

        const auto& slot = queue_slots[handle_.queue_id];
        return slot.occupied != 0U && slot.generation.value == handle_.generation;
    }

    std::vector<DeviceSlot> device_slots;
    std::vector<QueueSlot> queue_slots;
    std::vector<U32> free_device_ids;
    std::vector<U32> free_queue_ids;
    U32 active_device_count = 0U;
    U32 active_queue_count = 0U;
};

} // namespace Render2D
