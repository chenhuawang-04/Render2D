#pragma once

#include "Render2D/Memory/RenderMemoryTags.hpp"

#include <Center/Memory/Container/Vector/McVector.hpp>

namespace Render2D {

template<class T>
using McVector = Center::Memory::mc_vector<T, RuntimeContainerMemory>;

} // namespace Render2D
