#pragma once

#include "Render2D/Memory/RenderVector.hpp"

#include "ComponentStreamView.hpp"

#include <Render2D/Component/ComponentTraits.hpp>

#include <utility>

namespace Render2D::TestSupport {

template<class Provider, class Dim, class Component>
class ComponentStreamStorage {
public:
    static_assert(SupportedRenderComponent<Provider, Dim, Component>,
        "Temporary ECS storage can only store supported Render2D ECS components.");

    using value_type = Component;

    void reserve(Usize capacity_)
    {
        components.reserve(capacity_);
    }

    void clear() noexcept
    {
        components.clear();
    }

    void reset() noexcept
    {
        clear();
    }

    Component& push(const Component& value_)
    {
        components.push_back(value_);
        return components.back();
    }

    Component& push(Component&& value_)
    {
        components.push_back(std::move(value_));
        return components.back();
    }

    Component* data() noexcept
    {
        return components.data();
    }

    const Component* data() const noexcept
    {
        return components.data();
    }

    Usize size() const noexcept
    {
        return components.size();
    }

    Usize capacity() const noexcept
    {
        return components.capacity();
    }

    Component& at(Usize index_)
    {
        return components[index_];
    }

    const Component& at(Usize index_) const
    {
        return components[index_];
    }

    ComponentStreamView<Provider, Dim, Component> view() noexcept
    {
        return {components.data(), components.size()};
    }

    ConstComponentStreamView<Provider, Dim, Component> view() const noexcept
    {
        return {components.data(), components.size()};
    }

private:
    McVector<Component> components;
};

template<class Provider, class Dim, class Component>
using FrameComponentStorage = ComponentStreamStorage<Provider, Dim, Component>;

template<class Provider, class Dim, class Component>
using PersistentComponentStorage = ComponentStreamStorage<Provider, Dim, Component>;

} // namespace Render2D::TestSupport

