#pragma once

#include "Render2D/Core/Types.hpp"

#include <type_traits>

namespace Render2D {

enum class NativeStatusCode : U8 {
    Ok = 0,
    InvalidInput = 1,
    OutOfCapacity = 2,
    OutOfMemory = 3,
    UnsupportedFormat = 4,
    DeviceLost = 5,
    SwapchainOutOfDate = 6,
    StaleReference = 7,
    UnsupportedDomain = 8,
};

enum class NativeObjectKind : U8 {
    Unknown = 0,
    Device = 1,
    Queue = 2,
    Buffer = 3,
    Image = 4,
    ImageView = 5,
    Pipeline = 6,
    Descriptor = 7,
    Swapchain = 8,
    Semaphore = 9,
    Fence = 10,
    CommandPool = 11,
    CommandBuffer = 12,
    Frame = 13,
};

enum class NativeMemoryDomain : U8 {
    Unknown = 0,
    DeviceLocal = 1,
    Upload = 2,
    Readback = 3,
    Transient = 4,
};

struct NativeHandle {
    U64 value;
};

struct NativeId {
    U32 value;
};

struct NativeGeneration {
    U32 value;
};

struct NativeResourceKey {
    NativeId id;
    NativeGeneration generation;
};

struct NativeByteRange {
    U64 offset;
    U64 byte_count;
};

static_assert(std::is_trivial_v<NativeHandle>);
static_assert(std::is_standard_layout_v<NativeHandle>);
static_assert(std::is_trivially_copyable_v<NativeHandle>);
static_assert(std::is_aggregate_v<NativeHandle>);

static_assert(std::is_trivial_v<NativeId>);
static_assert(std::is_standard_layout_v<NativeId>);
static_assert(std::is_trivially_copyable_v<NativeId>);
static_assert(std::is_aggregate_v<NativeId>);

static_assert(std::is_trivial_v<NativeGeneration>);
static_assert(std::is_standard_layout_v<NativeGeneration>);
static_assert(std::is_trivially_copyable_v<NativeGeneration>);
static_assert(std::is_aggregate_v<NativeGeneration>);

static_assert(std::is_trivial_v<NativeResourceKey>);
static_assert(std::is_standard_layout_v<NativeResourceKey>);
static_assert(std::is_trivially_copyable_v<NativeResourceKey>);
static_assert(std::is_aggregate_v<NativeResourceKey>);

static_assert(std::is_trivial_v<NativeByteRange>);
static_assert(std::is_standard_layout_v<NativeByteRange>);
static_assert(std::is_trivially_copyable_v<NativeByteRange>);
static_assert(std::is_aggregate_v<NativeByteRange>);

} // namespace Render2D
