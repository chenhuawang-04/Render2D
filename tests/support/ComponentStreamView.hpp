#pragma once

#include <Render2D/Core/Types.hpp>

namespace Render2D::TestSupport {

template<class Provider, class Dim, class Component>
struct ComponentStreamView {
    Component* data;
    Usize count;
};

template<class Provider, class Dim, class Component>
struct ConstComponentStreamView {
    const Component* data;
    Usize count;
};

} // namespace Render2D::TestSupport
