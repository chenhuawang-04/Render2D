#pragma once

// Test-only lightweight ECS -- a small, type-safe entity/component world used to
// drive Render2D's span-only systems from realistic, author-it-yourself scenes.
// It is NOT production architecture and NOT part of the rendering core: like
// TemporaryEcsStorage and HostLikeEcs, it lives entirely under tests/ and the
// host engine's ECS replaces it at merge (ProjectMergeTODO #1). Render2D's scope
// rules keep a production ECS out of include/Render2D; this scaffolding only
// exists so the test suite can prove the renderer is usable end-to-end.
//
// Shape: a generational entity table (id + generation handles, slot recycling)
// plus one packed sparse-set per component type. Every owned array is a
// Render2D::McVector (never the forbidden standard dynamic vector); every component is an
// unchanged Strict POD render component gated by SupportedRenderComponent. The
// systems never see this world -- gatherRenderInputs() packs the renderable input
// set into row-aligned dense columns and the pipeline consumes only the
// std::span over each column, exactly the boundary HostLikeEcs already proves.

#include "Render2D/Memory/RenderVector.hpp"

#include <Render2D/Component/Bounds.hpp>
#include <Render2D/Component/ComponentTraits.hpp>
#include <Render2D/Component/Sprite.hpp>
#include <Render2D/Component/Transform.hpp>
#include <Render2D/Core/Types.hpp>

#include <span>
#include <tuple>

namespace Render2D::TestSupport {

// Sentinel for "no dense slot" / "no entity slot".
inline constexpr U32 kInvalidIndex = 0xFFFF'FFFFU;

// A generational entity handle. Plain POD: a slot index plus the generation that
// was live when the handle was minted. A handle whose generation no longer
// matches its slot is stale -- the world reports it dead and ignores it.
struct MiniEntity {
    U32 index = kInvalidIndex;
    U32 generation = 0U;
};

[[nodiscard]] constexpr bool operator==(MiniEntity left_, MiniEntity right_) noexcept
{
    return left_.index == right_.index && left_.generation == right_.generation;
}

// Packed sparse-set storage for one component type. dense_values is contiguous
// (the array a span is taken over); sparse_slots maps an entity slot index to its
// dense position (or kInvalidIndex); dense_entities maps back so removal can
// swap-pop in O(1) while keeping dense_values packed.
template<class Provider, class Dim, class Component>
class MiniComponentStore {
public:
    static_assert(SupportedRenderComponent<Provider, Dim, Component>,
        "MiniComponentStore requires a supported Strict POD render component.");

    void reserve(Usize capacity_)
    {
        dense_values.reserve(capacity_);
        dense_entities.reserve(capacity_);
    }

    [[nodiscard]] bool has(U32 entity_index_) const noexcept
    {
        return entity_index_ < sparse_slots.size()
            && sparse_slots[entity_index_] != kInvalidIndex;
    }

    // Insert the component for an entity slot, or overwrite it if already present.
    Component& set(U32 entity_index_, const Component& value_)
    {
        if (entity_index_ >= sparse_slots.size()) {
            sparse_slots.resize(static_cast<Usize>(entity_index_) + 1U, kInvalidIndex);
        }
        const U32 existing = sparse_slots[entity_index_];
        if (existing != kInvalidIndex) {
            dense_values[existing] = value_;
            return dense_values[existing];
        }
        const auto dense_index = static_cast<U32>(dense_values.size());
        dense_values.push_back(value_);
        dense_entities.push_back(entity_index_);
        sparse_slots[entity_index_] = dense_index;
        return dense_values[dense_index];
    }

    [[nodiscard]] Component* find(U32 entity_index_) noexcept
    {
        return has(entity_index_) ? &dense_values[sparse_slots[entity_index_]] : nullptr;
    }

    [[nodiscard]] const Component* find(U32 entity_index_) const noexcept
    {
        return has(entity_index_) ? &dense_values[sparse_slots[entity_index_]] : nullptr;
    }

    // O(1) swap-remove: the last dense element backfills the hole, then the dense
    // arrays shrink by one. No-op if the entity has no component of this type.
    void remove(U32 entity_index_) noexcept
    {
        if (!has(entity_index_)) {
            return;
        }
        const U32 dense_index = sparse_slots[entity_index_];
        const auto last_index = static_cast<U32>(dense_values.size() - 1U);
        if (dense_index != last_index) {
            dense_values[dense_index] = dense_values[last_index];
            dense_entities[dense_index] = dense_entities[last_index];
            sparse_slots[dense_entities[last_index]] = dense_index;
        }
        dense_values.pop_back();
        dense_entities.pop_back();
        sparse_slots[entity_index_] = kInvalidIndex;
    }

    void clear() noexcept
    {
        dense_values.clear();
        dense_entities.clear();
        sparse_slots.clear();
    }

    [[nodiscard]] Usize size() const noexcept
    {
        return dense_values.size();
    }

    [[nodiscard]] std::span<const Component> values() const noexcept
    {
        return {dense_values.data(), dense_values.size()};
    }

private:
    McVector<Component> dense_values;
    McVector<U32> dense_entities;
    McVector<U32> sparse_slots;
};

// The lightweight world: a generational entity table plus one MiniComponentStore
// per registered component type. Templated on the component set so a test can
// register any supported Strict POD components; see the SceneEcs alias for the
// renderable input set the pipeline consumes.
template<class Provider, class Dim, class... Components>
class MiniEcs {
    static_assert(sizeof...(Components) > 0U, "MiniEcs needs at least one component type.");
    static_assert((SupportedRenderComponent<Provider, Dim, Components> && ...),
        "Every MiniEcs component must be a supported Strict POD render component.");

public:
    void reserve(Usize capacity_)
    {
        slot_generations.reserve(capacity_);
        slot_alive.reserve(capacity_);
        (storeFor<Components>().reserve(capacity_), ...);
    }

    // Mint a new entity, recycling a freed slot when one is available so handles
    // stay dense. A recycled slot keeps the generation it was bumped to on
    // destroy, so old handles to it remain stale.
    [[nodiscard]] MiniEntity create()
    {
        U32 index = 0U;
        if (!free_slots.empty()) {
            index = free_slots[free_slots.size() - 1U];
            free_slots.pop_back();
        }
        else {
            index = static_cast<U32>(slot_generations.size());
            slot_generations.push_back(0U);
            slot_alive.push_back(static_cast<U8>(0U));
        }
        slot_alive[index] = static_cast<U8>(1U);
        ++live_count;
        return {.index = index, .generation = slot_generations[index]};
    }

    [[nodiscard]] bool alive(MiniEntity entity_) const noexcept
    {
        return entity_.index < slot_generations.size()
            && slot_alive[entity_.index] != static_cast<U8>(0U)
            && slot_generations[entity_.index] == entity_.generation;
    }

    // Destroy an entity: drop all its components, bump its slot generation so
    // every existing handle to it becomes stale, and recycle the slot. No-op for
    // a stale handle.
    void destroy(MiniEntity entity_)
    {
        if (!alive(entity_)) {
            return;
        }
        (storeFor<Components>().remove(entity_.index), ...);
        slot_alive[entity_.index] = static_cast<U8>(0U);
        ++slot_generations[entity_.index];
        free_slots.push_back(entity_.index);
        --live_count;
    }

    // Attach (or overwrite) a component on a live entity; returns nullptr if the
    // handle is stale.
    template<class Component>
    Component* add(MiniEntity entity_, const Component& value_)
    {
        return alive(entity_) ? &storeFor<Component>().set(entity_.index, value_) : nullptr;
    }

    template<class Component>
    [[nodiscard]] Component* get(MiniEntity entity_) noexcept
    {
        return alive(entity_) ? storeFor<Component>().find(entity_.index) : nullptr;
    }

    template<class Component>
    [[nodiscard]] const Component* get(MiniEntity entity_) const noexcept
    {
        return alive(entity_) ? storeFor<Component>().find(entity_.index) : nullptr;
    }

    template<class Component>
    [[nodiscard]] bool has(MiniEntity entity_) const noexcept
    {
        return alive(entity_) && storeFor<Component>().has(entity_.index);
    }

    template<class Component>
    void remove(MiniEntity entity_)
    {
        if (alive(entity_)) {
            storeFor<Component>().remove(entity_.index);
        }
    }

    [[nodiscard]] U32 liveCount() const noexcept
    {
        return live_count;
    }

    // The number of entity slots ever allocated (live + recycled). Iterate
    // [0, slotCount()) with entityAt() to visit every slot.
    [[nodiscard]] U32 slotCount() const noexcept
    {
        return static_cast<U32>(slot_generations.size());
    }

    // The current handle for a slot index. alive() on the result reports whether
    // the slot is presently occupied.
    [[nodiscard]] MiniEntity entityAt(U32 slot_index_) const noexcept
    {
        if (slot_index_ >= slot_generations.size()) {
            return {};
        }
        return {.index = slot_index_, .generation = slot_generations[slot_index_]};
    }

    template<class Component>
    [[nodiscard]] const MiniComponentStore<Provider, Dim, Component>& store() const noexcept
    {
        return storeFor<Component>();
    }

private:
    template<class Component>
    [[nodiscard]] MiniComponentStore<Provider, Dim, Component>& storeFor() noexcept
    {
        return std::get<MiniComponentStore<Provider, Dim, Component>>(stores);
    }

    template<class Component>
    [[nodiscard]] const MiniComponentStore<Provider, Dim, Component>& storeFor() const noexcept
    {
        return std::get<MiniComponentStore<Provider, Dim, Component>>(stores);
    }

    std::tuple<MiniComponentStore<Provider, Dim, Components>...> stores;
    McVector<U32> slot_generations;
    McVector<U8> slot_alive;
    McVector<U32> free_slots;
    U32 live_count = 0U;
};

// The default render world: the four input components the sprite pipeline front
// end consumes (Transform / LocalBounds / VisibilityMask / Sprite).
template<class Provider, class Dim>
using SceneEcs = MiniEcs<Provider, Dim,
    Transform<Provider, Dim>,
    LocalBounds<Provider, Dim>,
    VisibilityMask<Provider, Dim>,
    Sprite<Provider, Dim>>;

// Row-aligned dense columns gathered from the ECS: column[row] always names the
// same entity (entities[row]), which is exactly the parallel-array shape the
// span-only systems require. McVector-backed, never the standard dynamic vector.
template<class Provider, class Dim>
struct RenderInputColumns {
    McVector<MiniEntity> entities;
    McVector<Transform<Provider, Dim>> transforms;
    McVector<LocalBounds<Provider, Dim>> local_bounds;
    McVector<VisibilityMask<Provider, Dim>> visibility_masks;
    McVector<Sprite<Provider, Dim>> sprites;

    void clear() noexcept
    {
        entities.clear();
        transforms.clear();
        local_bounds.clear();
        visibility_masks.clear();
        sprites.clear();
    }

    [[nodiscard]] Usize size() const noexcept
    {
        return transforms.size();
    }

    [[nodiscard]] std::span<const Transform<Provider, Dim>> transformSpan() const noexcept
    {
        return {transforms.data(), transforms.size()};
    }

    [[nodiscard]] std::span<const LocalBounds<Provider, Dim>> localBoundsSpan() const noexcept
    {
        return {local_bounds.data(), local_bounds.size()};
    }

    [[nodiscard]] std::span<const VisibilityMask<Provider, Dim>> visibilityMaskSpan() const noexcept
    {
        return {visibility_masks.data(), visibility_masks.size()};
    }

    [[nodiscard]] std::span<const Sprite<Provider, Dim>> spriteSpan() const noexcept
    {
        return {sprites.data(), sprites.size()};
    }
};

// Pack the renderable input set of every entity that carries all four input
// components, in ascending entity-slot order, into row-aligned dense columns
// ready to hand to the span-only systems. This is the bridge from the sparse
// per-entity ECS to the dense parallel arrays the pipeline consumes; the ECS must
// register the four render input components (SceneEcs does).
template<class Provider, class Dim, class... Components>
void gatherRenderInputs(
    const MiniEcs<Provider, Dim, Components...>& ecs_,
    RenderInputColumns<Provider, Dim>& out_columns_)
{
    out_columns_.clear();
    const U32 slot_count = ecs_.slotCount();
    out_columns_.entities.reserve(slot_count);
    out_columns_.transforms.reserve(slot_count);
    out_columns_.local_bounds.reserve(slot_count);
    out_columns_.visibility_masks.reserve(slot_count);
    out_columns_.sprites.reserve(slot_count);

    for (U32 slot_index = 0U; slot_index < slot_count; ++slot_index) {
        const MiniEntity entity = ecs_.entityAt(slot_index);
        if (!ecs_.alive(entity)) {
            continue;
        }
        const auto* transform = ecs_.template get<Transform<Provider, Dim>>(entity);
        const auto* bounds = ecs_.template get<LocalBounds<Provider, Dim>>(entity);
        const auto* mask = ecs_.template get<VisibilityMask<Provider, Dim>>(entity);
        const auto* sprite = ecs_.template get<Sprite<Provider, Dim>>(entity);
        if (transform == nullptr || bounds == nullptr || mask == nullptr || sprite == nullptr) {
            continue;
        }
        out_columns_.entities.push_back(entity);
        out_columns_.transforms.push_back(*transform);
        out_columns_.local_bounds.push_back(*bounds);
        out_columns_.visibility_masks.push_back(*mask);
        out_columns_.sprites.push_back(*sprite);
    }
}

} // namespace Render2D::TestSupport
