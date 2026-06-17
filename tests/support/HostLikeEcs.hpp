#pragma once

// Stage 23A (host-engine merge readiness): a deliberately HOST-SHAPED ECS facade,
// structurally distinct from the per-type ComponentStreamStorage in
// TemporaryEcsStorage.hpp. Renderable INPUT components live as parallel SoA
// columns inside one archetype-like entity table (the shape of a host engine's
// component chunk), and the per-frame DERIVED streams live in a separate frame
// arena. Every stream is handed to Render2D systems as a std::span -- the only
// boundary the systems require (ProjectMergeTODO #1: "production systems should
// continue to consume component streams through non-owning boundaries such as
// std::span, not through the temporary test ECS storage").
//
// Because the systems are span-only, this brand-new store type drives them
// UNCHANGED; tests/host_like_ecs_adapter_test.cpp proves the pipeline output is
// byte-identical to the canonical ComponentStreamStorage path. This is test-only
// scaffolding, exactly like TemporaryEcsStorage -- the merge-time worked
// template, NOT production architecture. McVector-only for any owned array
// (never the forbidden standard dynamic container); the Strict POD components
// are unchanged.

#include "Render2D/Memory/RenderVector.hpp"

#include <Render2D/Component/Batch.hpp>
#include <Render2D/Component/Bounds.hpp>
#include <Render2D/Component/Command.hpp>
#include <Render2D/Component/ComponentTraits.hpp>
#include <Render2D/Component/Sprite.hpp>
#include <Render2D/Component/Transform.hpp>
#include <Render2D/Core/Types.hpp>

#include <span>

namespace Render2D::TestSupport {

// One archetype chunk: parallel SoA columns for the renderable input set, kept
// row-aligned by pushEntity (column[row] always names the same entity). A host
// engine's component store is shaped like this -- columns per component type,
// addressed by a dense entity row -- not like N independent single-component
// containers. The systems never see the table; they see only the std::span over
// each column.
template<class Provider, class Dim>
class HostEntityTable {
public:
    static_assert(SupportedRenderComponent<Provider, Dim, Transform<Provider, Dim>>);
    static_assert(SupportedRenderComponent<Provider, Dim, LocalBounds<Provider, Dim>>);
    static_assert(SupportedRenderComponent<Provider, Dim, VisibilityMask<Provider, Dim>>);
    static_assert(SupportedRenderComponent<Provider, Dim, Sprite<Provider, Dim>>);

    void reserve(Usize capacity_)
    {
        transform_column.reserve(capacity_);
        local_bounds_column.reserve(capacity_);
        visibility_mask_column.reserve(capacity_);
        sprite_column.reserve(capacity_);
    }

    // Append one entity's renderable components across all columns at once,
    // preserving row alignment so column[row] refers to a single entity.
    void pushEntity(
        const Transform<Provider, Dim>& transform_,
        const LocalBounds<Provider, Dim>& local_bounds_,
        const VisibilityMask<Provider, Dim>& visibility_mask_,
        const Sprite<Provider, Dim>& sprite_)
    {
        transform_column.push_back(transform_);
        local_bounds_column.push_back(local_bounds_);
        visibility_mask_column.push_back(visibility_mask_);
        sprite_column.push_back(sprite_);
    }

    [[nodiscard]] Usize size() const noexcept
    {
        return transform_column.size();
    }

    [[nodiscard]] std::span<const Transform<Provider, Dim>> transforms() const noexcept
    {
        return {transform_column.data(), transform_column.size()};
    }

    [[nodiscard]] std::span<const LocalBounds<Provider, Dim>> localBounds() const noexcept
    {
        return {local_bounds_column.data(), local_bounds_column.size()};
    }

    [[nodiscard]] std::span<const VisibilityMask<Provider, Dim>> visibilityMasks() const noexcept
    {
        return {visibility_mask_column.data(), visibility_mask_column.size()};
    }

    [[nodiscard]] std::span<const Sprite<Provider, Dim>> sprites() const noexcept
    {
        return {sprite_column.data(), sprite_column.size()};
    }

private:
    McVector<Transform<Provider, Dim>> transform_column;
    McVector<LocalBounds<Provider, Dim>> local_bounds_column;
    McVector<VisibilityMask<Provider, Dim>> visibility_mask_column;
    McVector<Sprite<Provider, Dim>> sprite_column;
};

// The host's per-frame derived/scratch streams, owned separately from the
// persistent entity table. Sized once per frame; systems write through the
// mutable spans, and downstream stages read them back (a std::span<T> converts
// to std::span<const T> implicitly, so no separate const accessor is needed).
template<class Provider, class Dim>
class HostFrameArena {
public:
    static_assert(SupportedRenderComponent<Provider, Dim, WorldTransform<Provider, Dim>>);
    static_assert(SupportedRenderComponent<Provider, Dim, VisibleItem<Provider, Dim>>);
    static_assert(SupportedRenderComponent<Provider, Dim, DrawCommand<Provider, Dim>>);
    static_assert(SupportedRenderComponent<Provider, Dim, SpriteInstance<Provider, Dim>>);
    static_assert(SupportedRenderComponent<Provider, Dim, BatchCommand<Provider, Dim>>);

    // world_transforms is dense (one per entity); the visible/draw/instance/batch
    // streams are bounded by the entity count (culling compacts, batching merges),
    // so an entity-count capacity is always sufficient for one frame.
    void resizeForEntities(Usize entity_count_)
    {
        world_transforms.resize(entity_count_);
        visible_items.resize(entity_count_);
        draw_commands.resize(entity_count_);
        sprite_instances.resize(entity_count_);
        batch_commands.resize(entity_count_);
    }

    [[nodiscard]] std::span<WorldTransform<Provider, Dim>> worldTransforms() noexcept
    {
        return {world_transforms.data(), world_transforms.size()};
    }

    [[nodiscard]] std::span<VisibleItem<Provider, Dim>> visibleItems() noexcept
    {
        return {visible_items.data(), visible_items.size()};
    }

    [[nodiscard]] std::span<DrawCommand<Provider, Dim>> drawCommands() noexcept
    {
        return {draw_commands.data(), draw_commands.size()};
    }

    [[nodiscard]] std::span<SpriteInstance<Provider, Dim>> spriteInstances() noexcept
    {
        return {sprite_instances.data(), sprite_instances.size()};
    }

    [[nodiscard]] std::span<BatchCommand<Provider, Dim>> batchCommands() noexcept
    {
        return {batch_commands.data(), batch_commands.size()};
    }

private:
    McVector<WorldTransform<Provider, Dim>> world_transforms;
    McVector<VisibleItem<Provider, Dim>> visible_items;
    McVector<DrawCommand<Provider, Dim>> draw_commands;
    McVector<SpriteInstance<Provider, Dim>> sprite_instances;
    McVector<BatchCommand<Provider, Dim>> batch_commands;
};

} // namespace Render2D::TestSupport
