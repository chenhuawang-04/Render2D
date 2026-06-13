#pragma once

#include "Render2D/Component/Sprite.hpp"
#include "Render2D/Memory/RenderVector.hpp"
#include "Render2D/Native/NativeComponents.hpp"
#include "Render2D/Native/NativeResult.hpp"

#include <type_traits>

namespace Render2D {

// Stage 20D POD material parameters. The bindless path resolves the pipeline and
// sampler per material, leaving the rest of the material's look (tint, blend) as
// data carried here. It is a deliberate extension point: new look knobs append
// fields without touching the graph's id+generation bookkeeping. Kept Strict POD
// so it can sit in a runtime record without RAII.
struct VulkanSpriteMaterialParams {
    U32 tint_rgba8;
    U32 blend_mode;
    U32 binding_flags; // copied into SpriteMaterialBinding::flags on resolve
    U32 reserved;
};

struct VulkanSpriteMaterialGraphConfig {
    U32 material_capacity; // identity-indexed slot count; registration past it is rejected
    U32 sampler_capacity;  // bindless sampler array size; sampler_index >= this is rejected
};

// Runtime material graph: maps material_id + generation to { PipelineRef,
// sampler_index, params }. NOT an ECS component — the ECS keeps only
// material_id + generation on each sprite; this turns that into the pipeline to
// bind, the bindless sampler slot to stamp onto instances
// (SpriteInstanceBuildSystem::resolveSamplerIndices reads SpriteMaterialBinding),
// and the POD look params. Identity-indexed by material_id with a CPU McVector
// record, mirroring VulkanBindlessTextureTable; generation is validated on every
// resolve so a stale material can never bind. It owns no Vulkan handles (the
// PipelineRef it stores is an id+generation reference like everywhere else), so
// it needs no device and runs in the CPU-only and GPU paths alike.
template<class Provider, class Dim>
class VulkanSpriteMaterialGraph {
public:
    VulkanSpriteMaterialGraph() = default;
    VulkanSpriteMaterialGraph(const VulkanSpriteMaterialGraph&) = delete;
    VulkanSpriteMaterialGraph& operator=(const VulkanSpriteMaterialGraph&) = delete;
    VulkanSpriteMaterialGraph(VulkanSpriteMaterialGraph&&) = delete;
    VulkanSpriteMaterialGraph& operator=(VulkanSpriteMaterialGraph&&) = delete;
    ~VulkanSpriteMaterialGraph() noexcept = default;

    NativeResult initialize(VulkanSpriteMaterialGraphConfig config_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (config_.material_capacity == 0U ||
                config_.sampler_capacity == 0U ||
                initialized) {
                return makeResult(NativeStatusCode::InvalidInput, 0U, 0U);
            }

            material_capacity = config_.material_capacity;
            sampler_capacity = config_.sampler_capacity;

            // Reserve so the lazy per-material_id growth in registerMaterial never
            // reallocates; CPU memory only.
            try {
                slots.reserve(material_capacity);
            } catch (...) {
                shutdown();
                return makeResult(NativeStatusCode::OutOfMemory, 0U, 0U);
            }
            initialized = true;
            return makeResult(NativeStatusCode::Ok, 0U, 0U);
        }
    }

    void shutdown() noexcept
    {
        slots.clear();
        material_capacity = 0U;
        sampler_capacity = 0U;
        initialized = false;
    }

    // Registers (or overwrites) material_id with its pipeline, bindless sampler
    // slot, and look params. sampler_index is bounds-checked against the bindless
    // sampler array; a zero pipeline generation is rejected so resolve never hands
    // back an unbindable pipeline.
    NativeResult registerMaterial(
        U32 material_id_,
        U32 generation_,
        const PipelineRef<Provider, Dim>& pipeline_,
        U32 sampler_index_,
        const VulkanSpriteMaterialParams& params_) noexcept
    {
        if (!initialized ||
            material_id_ >= material_capacity ||
            sampler_index_ >= sampler_capacity ||
            generation_ == 0U ||
            pipeline_.generation == 0U) {
            return makeResult(NativeStatusCode::InvalidInput, material_id_, generation_);
        }

        if (!ensureSlot(material_id_)) {
            return makeResult(NativeStatusCode::OutOfMemory, material_id_, generation_);
        }

        slots[material_id_] = Slot{
            .pipeline = pipeline_,
            .params = params_,
            .generation = generation_,
            .sampler_index = sampler_index_,
            .resident = 1U,
        };
        return makeResult(NativeStatusCode::Ok, material_id_, generation_);
    }

    // Marks material_id non-resident. The (id, generation) must match the live
    // record, so a stale or double evict is rejected rather than silently dropping
    // a different generation's material.
    NativeResult evict(U32 material_id_, U32 generation_) noexcept
    {
        if (!isRegistered(material_id_, generation_)) {
            return makeResult(NativeStatusCode::StaleReference, material_id_, generation_);
        }
        slots[material_id_].resident = 0U;
        return makeResult(NativeStatusCode::Ok, material_id_, generation_);
    }

    bool isRegistered(U32 material_id_, U32 generation_) const noexcept
    {
        if (material_id_ >= slots.size()) {
            return false;
        }
        const Slot& slot = slots[material_id_];
        return slot.resident != 0U && slot.generation == generation_;
    }

    // Produces the SpriteMaterialBinding row the packet/resolve systems consume.
    // Generation-validated: a missing or stale material yields StaleReference and
    // leaves out_binding_ untouched, never a slot-0 default.
    NativeResult resolveBinding(
        U32 material_id_,
        U32 generation_,
        SpriteMaterialBinding<Provider, Dim>& out_binding_) const noexcept
    {
        if (!isRegistered(material_id_, generation_)) {
            return makeResult(NativeStatusCode::StaleReference, material_id_, generation_);
        }
        const Slot& slot = slots[material_id_];
        out_binding_ = SpriteMaterialBinding<Provider, Dim>{
            .material_id = material_id_,
            .material_generation = generation_,
            .pipeline_id = slot.pipeline.pipeline_id,
            .pipeline_generation = slot.pipeline.generation,
            .flags = slot.params.binding_flags,
            .sampler_index = slot.sampler_index,
        };
        return makeResult(NativeStatusCode::Ok, material_id_, generation_);
    }

    bool resolveSamplerIndex(U32 material_id_, U32 generation_, U32& out_sampler_index_) const noexcept
    {
        if (!isRegistered(material_id_, generation_)) {
            return false;
        }
        out_sampler_index_ = slots[material_id_].sampler_index;
        return true;
    }

    bool resolvePipeline(U32 material_id_, U32 generation_, PipelineRef<Provider, Dim>& out_pipeline_) const noexcept
    {
        if (!isRegistered(material_id_, generation_)) {
            return false;
        }
        out_pipeline_ = slots[material_id_].pipeline;
        return true;
    }

    bool resolveParams(U32 material_id_, U32 generation_, VulkanSpriteMaterialParams& out_params_) const noexcept
    {
        if (!isRegistered(material_id_, generation_)) {
            return false;
        }
        out_params_ = slots[material_id_].params;
        return true;
    }

    bool isInitialized() const noexcept
    {
        return initialized;
    }

    U32 materialCapacity() const noexcept
    {
        return material_capacity;
    }

    U32 samplerCapacity() const noexcept
    {
        return sampler_capacity;
    }

private:
    struct Slot {
        PipelineRef<Provider, Dim> pipeline;
        VulkanSpriteMaterialParams params;
        U32 generation;
        U32 sampler_index;
        U32 resident;
    };

    static NativeResult makeResult(
        NativeStatusCode code_,
        U32 object_id_,
        U32 generation_) noexcept
    {
        return {
            .code = code_,
            .object_kind = NativeObjectKind::Pipeline,
            .object_id = {.value = object_id_},
            .generation = {.value = generation_},
        };
    }

    // Grows the CPU record to cover material_id (lazily, never past the reserved
    // material_capacity), defaulting fresh slots to non-resident.
    bool ensureSlot(U32 material_id_) noexcept
    {
        try {
            while (slots.size() <= material_id_) {
                slots.push_back(Slot{
                    .pipeline = {},
                    .params = {},
                    .generation = 0U,
                    .sampler_index = 0U,
                    .resident = 0U,
                });
            }
        } catch (...) {
            return false;
        }
        return true;
    }

    McVector<Slot> slots; // CPU material record, indexed by material_id
    U32 material_capacity = 0U;
    U32 sampler_capacity = 0U;
    bool initialized = false;
};

static_assert(std::is_trivial_v<VulkanSpriteMaterialParams>);
static_assert(std::is_standard_layout_v<VulkanSpriteMaterialParams>);
static_assert(std::is_trivially_copyable_v<VulkanSpriteMaterialParams>);
static_assert(std::is_aggregate_v<VulkanSpriteMaterialParams>);

static_assert(std::is_trivial_v<VulkanSpriteMaterialGraphConfig>);
static_assert(std::is_standard_layout_v<VulkanSpriteMaterialGraphConfig>);
static_assert(std::is_trivially_copyable_v<VulkanSpriteMaterialGraphConfig>);
static_assert(std::is_aggregate_v<VulkanSpriteMaterialGraphConfig>);

} // namespace Render2D
