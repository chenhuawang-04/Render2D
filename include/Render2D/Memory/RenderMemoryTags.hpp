#pragma once

#include <Center/Memory/MemoryCenter.hpp>

namespace Render2D {

using PersistentComponentMemory = Center::Memory::Tags::Container;

using FrameComponentMemory = Center::Memory::Tags::Temporary;

using UploadMemory = Center::Memory::Tags::Container;

using GpuResourceMemory = Center::Memory::Tags::General;

using RuntimeContainerMemory = Center::Memory::Tags::Container;

} // namespace Render2D

static_assert(Center::Memory::CustomerTag<Render2D::PersistentComponentMemory>);
static_assert(Center::Memory::CustomerTag<Render2D::FrameComponentMemory>);
static_assert(Center::Memory::CustomerTag<Render2D::UploadMemory>);
static_assert(Center::Memory::CustomerTag<Render2D::GpuResourceMemory>);
static_assert(Center::Memory::CustomerTag<Render2D::RuntimeContainerMemory>);
