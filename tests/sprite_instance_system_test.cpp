#include <Render2D/Render2D.hpp>

#include "support/TestHarness.hpp"

#include <array>
#include <cstdio>
#include <exception>
#include <span>

namespace {

namespace R2D = Render2D;
namespace R2DT = Render2D::TestSupport;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using BatchCommand = R2D::BatchCommand<Provider, Dim>;
using DrawCommand = R2D::DrawCommand<Provider, Dim>;
using Sprite = R2D::Sprite<Provider, Dim>;
using SpriteDrawPacket = R2D::SpriteDrawPacket<Provider, Dim>;
using SpriteInstance = R2D::SpriteInstance<Provider, Dim>;
using SpriteMaterialBinding = R2D::SpriteMaterialBinding<Provider, Dim>;
using SpriteTextureBinding = R2D::SpriteTextureBinding<Provider, Dim>;
using SpriteVertex = R2D::SpriteVertex<Provider, Dim>;
using TextureAtlasItem = R2D::TextureAtlasItem<Provider, Dim>;
using TextureAtlasRegion = R2D::TextureAtlasRegion<Provider, Dim>;
using WorldTransform = R2D::WorldTransform<Provider, Dim>;

static_assert(R2D::StrictPodComponent<SpriteVertex>);
static_assert(R2D::StrictPodComponent<SpriteInstance>);
static_assert(R2D::StrictPodComponent<SpriteDrawPacket>);
static_assert(R2D::StrictPodComponent<SpriteMaterialBinding>);
static_assert(R2D::StrictPodComponent<SpriteTextureBinding>);
static_assert(R2D::StrictPodComponent<TextureAtlasItem>);
static_assert(R2D::StrictPodComponent<TextureAtlasRegion>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, SpriteVertex>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, SpriteInstance>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, SpriteDrawPacket>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, SpriteMaterialBinding>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, SpriteTextureBinding>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, TextureAtlasItem>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, TextureAtlasRegion>);

constexpr R2D::Mat3 makeAffine(
    float m00_,
    float m01_,
    float m02_,
    float m10_,
    float m11_,
    float m12_) noexcept
{
    return {
        .m00 = m00_,
        .m01 = m01_,
        .m02 = m02_,
        .m10 = m10_,
        .m11 = m11_,
        .m12 = m12_,
        .m20 = 0.0F,
        .m21 = 0.0F,
        .m22 = 1.0F,
    };
}

constexpr DrawCommand makeDraw(
    R2D::U32 source_index_,
    R2D::U32 instance_first_,
    R2D::U32 material_id_,
    R2D::U32 texture_id_,
    R2D::U32 layer_,
    R2D::U32 flags_) noexcept
{
    return {
        .source_index = source_index_,
        .material_id = material_id_,
        .material_generation = 0U,
        .texture_id = texture_id_,
        .texture_generation = 0U,
        .vertex_first = 0U,
        .vertex_count = 4U,
        .index_first = 0U,
        .index_count = 6U,
        .instance_first = instance_first_,
        .instance_count = 1U,
        .sort_key = R2D::makeDrawSortKey(layer_, material_id_, texture_id_, flags_),
        .layer = layer_,
        .flags = flags_,
    };
}

constexpr BatchCommand makeBatch(
    R2D::U32 draw_first_,
    R2D::U32 draw_count_,
    R2D::U32 material_id_,
    R2D::U32 material_generation_,
    R2D::U32 texture_id_,
    R2D::U32 texture_generation_,
    R2D::U32 flags_) noexcept
{
    return {
        .draw_first = draw_first_,
        .draw_count = draw_count_,
        .material_id = material_id_,
        .material_generation = material_generation_,
        .texture_id = texture_id_,
        .texture_generation = texture_generation_,
        .pipeline_id = 0U,
        .pipeline_generation = 0U,
        .descriptor_id = 0U,
        .descriptor_generation = 0U,
        .sort_key = R2D::makeDrawSortKey(0U, material_id_, texture_id_, flags_),
        .flags = flags_,
    };
}

void testBuildSpriteInstances(R2DT::TestContext& context_)
{
    constexpr std::array<WorldTransform, 3U> kWorldTransforms{{
        {.source_id = 10U, .affine = makeAffine(1.0F, 0.0F, 2.0F, 0.0F, 1.0F, 3.0F)},
        {.source_id = 11U, .affine = makeAffine(2.0F, 0.0F, 4.0F, 0.0F, 2.0F, 5.0F)},
        {.source_id = 12U, .affine = makeAffine(3.0F, 1.0F, 6.0F, 1.0F, 3.0F, 7.0F)},
    }};
    constexpr std::array<Sprite, 3U> kSprites{{
        {
            .source_id = 10U,
            .texture_id = 100U,
            .texture_generation = 0U,
            .texture_region_id = 0U,
            .texture_region_generation = 0U,
            .material_id = 200U,
            .material_generation = 0U,
            .color_rgba8 = 0x11223344U,
            .layer = 1U,
            .flags = 0U,
        },
        {
            .source_id = 11U,
            .texture_id = 101U,
            .texture_generation = 0U,
            .texture_region_id = 0U,
            .texture_region_generation = 0U,
            .material_id = 201U,
            .material_generation = 0U,
            .color_rgba8 = 0x55667788U,
            .layer = 2U,
            .flags = 1U,
        },
        {
            .source_id = 12U,
            .texture_id = 102U,
            .texture_generation = 0U,
            .texture_region_id = 0U,
            .texture_region_generation = 0U,
            .material_id = 202U,
            .material_generation = 0U,
            .color_rgba8 = 0x99AABBCCU,
            .layer = 3U,
            .flags = 2U,
        },
    }};
    constexpr std::array<DrawCommand, 2U> kDraws{{
        makeDraw(2U, 1U, 202U, 102U, 3U, 2U),
        makeDraw(0U, 0U, 200U, 100U, 1U, 0U),
    }};
    std::array<SpriteInstance, 2U> instances{};

    const auto result = R2D::SpriteInstanceBuildSystem<Provider, Dim>::run(
        kDraws,
        kWorldTransforms,
        kSprites,
        instances);

    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, result.read_count, 2U);
    R2D_TEST_CHECK_EQ(context_, result.write_count, 2U);

    R2D_TEST_CHECK_EQ(context_, instances[0U].source_index, 0U);
    R2D_TEST_CHECK_EQ(context_, instances[0U].source_id, 10U);
    R2D_TEST_CHECK_EQ(context_, instances[0U].texture_id, 100U);
    R2D_TEST_CHECK_EQ(context_, instances[0U].material_id, 200U);
    R2D_TEST_CHECK_EQ(context_, instances[0U].color_rgba8, 0x11223344U);
    R2D_TEST_CHECK_EQ(context_, instances[0U].transform_m02, 2.0F);
    R2D_TEST_CHECK_EQ(context_, instances[0U].transform_m12, 3.0F);
    R2D_TEST_CHECK_EQ(context_, instances[0U].uv_min_x, 0.0F);
    R2D_TEST_CHECK_EQ(context_, instances[0U].uv_max_y, 1.0F);

    R2D_TEST_CHECK_EQ(context_, instances[1U].source_index, 2U);
    R2D_TEST_CHECK_EQ(context_, instances[1U].source_id, 12U);
    R2D_TEST_CHECK_EQ(context_, instances[1U].texture_id, 102U);
    R2D_TEST_CHECK_EQ(context_, instances[1U].material_id, 202U);
    R2D_TEST_CHECK_EQ(context_, instances[1U].transform_m00, 3.0F);
    R2D_TEST_CHECK_EQ(context_, instances[1U].transform_m01, 1.0F);
    R2D_TEST_CHECK_EQ(context_, instances[1U].transform_m12, 7.0F);
    R2D_TEST_CHECK_EQ(context_, instances[1U].sort_key, kDraws[0U].sort_key);
    R2D_TEST_CHECK_EQ(context_, instances[1U].flags, 2U);
}

void testBuildSpriteDrawPackets(R2DT::TestContext& context_)
{
    std::array<DrawCommand, 3U> draws{{
        makeDraw(0U, 0U, 7U, 11U, 0U, 1U),
        makeDraw(1U, 1U, 7U, 11U, 0U, 1U),
        makeDraw(2U, 2U, 8U, 12U, 0U, 2U),
    }};
    draws[0U].material_generation = 70U;
    draws[0U].texture_generation = 110U;
    draws[1U].material_generation = 70U;
    draws[1U].texture_generation = 110U;
    draws[2U].material_generation = 80U;
    draws[2U].texture_generation = 120U;

    constexpr std::array<BatchCommand, 2U> kBatches{{
        makeBatch(0U, 2U, 7U, 70U, 11U, 110U, 1U),
        makeBatch(2U, 1U, 8U, 80U, 12U, 120U, 2U),
    }};
    constexpr std::array<SpriteMaterialBinding, 2U> kMaterials{{
        {
            .material_id = 7U,
            .material_generation = 70U,
            .pipeline_id = 3U,
            .pipeline_generation = 30U,
            .flags = 0x10U,
        },
        {
            .material_id = 8U,
            .material_generation = 80U,
            .pipeline_id = 4U,
            .pipeline_generation = 40U,
            .flags = 0x20U,
        },
    }};
    constexpr std::array<SpriteTextureBinding, 2U> kTextures{{
        {
            .texture_id = 11U,
            .texture_generation = 110U,
            .descriptor_id = 5U,
            .descriptor_generation = 50U,
            .descriptor_first = 0U,
            .descriptor_count = 1U,
            .flags = 0x100U,
        },
        {
            .texture_id = 12U,
            .texture_generation = 120U,
            .descriptor_id = 6U,
            .descriptor_generation = 60U,
            .descriptor_first = 0U,
            .descriptor_count = 1U,
            .flags = 0x200U,
        },
    }};
    std::array<SpriteDrawPacket, 2U> packets{};

    const auto result = R2D::SpriteDrawPacketBuildSystem<Provider, Dim>::run(
        kBatches,
        draws,
        kMaterials,
        kTextures,
        packets);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, result.read_count, 2U);
    R2D_TEST_CHECK_EQ(context_, result.write_count, 2U);

    R2D_TEST_CHECK_EQ(context_, packets[0U].batch_index, 0U);
    R2D_TEST_CHECK_EQ(context_, packets[0U].draw_first, 0U);
    R2D_TEST_CHECK_EQ(context_, packets[0U].draw_count, 2U);
    R2D_TEST_CHECK_EQ(context_, packets[0U].instance_first, 0U);
    R2D_TEST_CHECK_EQ(context_, packets[0U].instance_count, 2U);
    R2D_TEST_CHECK_EQ(context_, packets[0U].material_id, 7U);
    R2D_TEST_CHECK_EQ(context_, packets[0U].material_generation, 70U);
    R2D_TEST_CHECK_EQ(context_, packets[0U].texture_id, 11U);
    R2D_TEST_CHECK_EQ(context_, packets[0U].texture_generation, 110U);
    R2D_TEST_CHECK_EQ(context_, packets[0U].pipeline_id, 3U);
    R2D_TEST_CHECK_EQ(context_, packets[0U].pipeline_generation, 30U);
    R2D_TEST_CHECK_EQ(context_, packets[0U].descriptor_id, 5U);
    R2D_TEST_CHECK_EQ(context_, packets[0U].descriptor_generation, 50U);
    R2D_TEST_CHECK_EQ(context_, packets[0U].descriptor_count, 1U);
    R2D_TEST_CHECK_EQ(context_, packets[0U].flags, 0x111U);

    R2D_TEST_CHECK_EQ(context_, packets[1U].batch_index, 1U);
    R2D_TEST_CHECK_EQ(context_, packets[1U].instance_first, 2U);
    R2D_TEST_CHECK_EQ(context_, packets[1U].instance_count, 1U);
    R2D_TEST_CHECK_EQ(context_, packets[1U].pipeline_id, 4U);
    R2D_TEST_CHECK_EQ(context_, packets[1U].descriptor_id, 6U);
}

void testPacketBuildInvalidInput(R2DT::TestContext& context_)
{
    std::array<DrawCommand, 2U> draws{{
        makeDraw(0U, 0U, 7U, 11U, 0U, 0U),
        makeDraw(1U, 2U, 7U, 11U, 0U, 0U),
    }};
    draws[0U].material_generation = 70U;
    draws[0U].texture_generation = 110U;
    draws[1U].material_generation = 70U;
    draws[1U].texture_generation = 110U;
    constexpr std::array<BatchCommand, 1U> kBatches{{
        makeBatch(0U, 2U, 7U, 70U, 11U, 110U, 0U),
    }};
    constexpr std::array<SpriteMaterialBinding, 1U> kMaterials{{
        {
            .material_id = 7U,
            .material_generation = 70U,
            .pipeline_id = 3U,
            .pipeline_generation = 30U,
            .flags = 0U,
        },
    }};
    constexpr std::array<SpriteTextureBinding, 1U> kTextures{{
        {
            .texture_id = 11U,
            .texture_generation = 110U,
            .descriptor_id = 5U,
            .descriptor_generation = 50U,
            .descriptor_first = 0U,
            .descriptor_count = 1U,
            .flags = 0U,
        },
    }};
    std::array<SpriteDrawPacket, 1U> packets{};

    auto result = R2D::SpriteDrawPacketBuildSystem<Provider, Dim>::run(
        kBatches,
        draws,
        kMaterials,
        kTextures,
        packets);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::InvalidInput);

    draws[1U].instance_first = 1U;
    std::array<SpriteTextureBinding, 1U> stale_textures{kTextures};
    stale_textures[0U].texture_generation = 111U;
    result = R2D::SpriteDrawPacketBuildSystem<Provider, Dim>::run(
        kBatches,
        draws,
        kMaterials,
        stale_textures,
        packets);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::InvalidInput);
}

void testTextureAtlasBuildSystem(R2DT::TestContext& context_)
{
    constexpr std::array<TextureAtlasItem, 3U> kItems{{
        {
            .region_id = 1U,
            .generation = 10U,
            .width = 4U,
            .height = 4U,
            .padding = 0U,
            .flags = 1U,
        },
        {
            .region_id = 2U,
            .generation = 20U,
            .width = 8U,
            .height = 4U,
            .padding = 0U,
            .flags = 2U,
        },
        {
            .region_id = 3U,
            .generation = 30U,
            .width = 4U,
            .height = 4U,
            .padding = 0U,
            .flags = 4U,
        },
    }};
    constexpr R2D::TextureAtlasBuildConfig kConfig{
        .atlas_width = 16U,
        .atlas_height = 8U,
        .atlas_id = 9U,
        .atlas_generation = 90U,
        .texture_id = 5U,
        .texture_generation = 50U,
        .padding = 0U,
        .flags = 0x100U,
    };
    std::array<TextureAtlasRegion, kItems.size()> regions{};

    auto result = R2D::TextureAtlasBuildSystem<Provider, Dim>::run(kItems, regions, kConfig);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, result.read_count, 3U);
    R2D_TEST_CHECK_EQ(context_, result.write_count, 3U);

    R2D_TEST_CHECK_EQ(context_, regions[0U].region_id, 1U);
    R2D_TEST_CHECK_EQ(context_, regions[0U].generation, 10U);
    R2D_TEST_CHECK_EQ(context_, regions[0U].x, 0U);
    R2D_TEST_CHECK_EQ(context_, regions[0U].y, 0U);
    R2D_TEST_CHECK_EQ(context_, regions[0U].uv_min_x, 0.0F);
    R2D_TEST_CHECK_EQ(context_, regions[0U].uv_min_y, 0.0F);
    R2D_TEST_CHECK_EQ(context_, regions[0U].uv_max_x, 0.25F);
    R2D_TEST_CHECK_EQ(context_, regions[0U].uv_max_y, 0.5F);
    R2D_TEST_CHECK_EQ(context_, regions[0U].flags, 0x101U);

    R2D_TEST_CHECK_EQ(context_, regions[1U].x, 4U);
    R2D_TEST_CHECK_EQ(context_, regions[1U].uv_min_x, 0.25F);
    R2D_TEST_CHECK_EQ(context_, regions[1U].uv_max_x, 0.75F);

    R2D_TEST_CHECK_EQ(context_, regions[2U].x, 12U);
    R2D_TEST_CHECK_EQ(context_, regions[2U].uv_min_x, 0.75F);
    R2D_TEST_CHECK_EQ(context_, regions[2U].uv_max_x, 1.0F);

    std::array<TextureAtlasRegion, 1U> short_regions{};
    result = R2D::TextureAtlasBuildSystem<Provider, Dim>::run(kItems, short_regions, kConfig);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::InsufficientCapacity);

    constexpr std::array<TextureAtlasItem, 1U> kTooLarge{{
        {
            .region_id = 4U,
            .generation = 40U,
            .width = 17U,
            .height = 4U,
            .padding = 0U,
            .flags = 0U,
        },
    }};
    result = R2D::TextureAtlasBuildSystem<Provider, Dim>::run(kTooLarge, regions, kConfig);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::InvalidInput);

    constexpr std::array<TextureAtlasItem, 3U> kWrapItems{{
        {.region_id = 5U, .generation = 50U, .width = 4U, .height = 4U, .padding = 0U, .flags = 0U},
        {.region_id = 6U, .generation = 60U, .width = 4U, .height = 4U, .padding = 0U, .flags = 0U},
        {.region_id = 7U, .generation = 70U, .width = 4U, .height = 4U, .padding = 0U, .flags = 0U},
    }};
    constexpr R2D::TextureAtlasBuildConfig kWrapConfig{
        .atlas_width = 8U,
        .atlas_height = 8U,
        .atlas_id = 9U,
        .atlas_generation = 90U,
        .texture_id = 5U,
        .texture_generation = 50U,
        .padding = 0U,
        .flags = 0U,
    };
    result = R2D::TextureAtlasBuildSystem<Provider, Dim>::run(kWrapItems, regions, kWrapConfig);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, regions[0U].x, 0U);
    R2D_TEST_CHECK_EQ(context_, regions[1U].x, 4U);
    R2D_TEST_CHECK_EQ(context_, regions[2U].x, 0U);
    R2D_TEST_CHECK_EQ(context_, regions[2U].y, 4U);

    constexpr std::array<TextureAtlasItem, 1U> kPaddedItems{{
        {.region_id = 8U, .generation = 80U, .width = 4U, .height = 4U, .padding = 1U, .flags = 0U},
    }};
    constexpr R2D::TextureAtlasBuildConfig kPaddedConfig{
        .atlas_width = 8U,
        .atlas_height = 8U,
        .atlas_id = 9U,
        .atlas_generation = 90U,
        .texture_id = 5U,
        .texture_generation = 50U,
        .padding = 1U,
        .flags = 0U,
    };
    result = R2D::TextureAtlasBuildSystem<Provider, Dim>::run(kPaddedItems, regions, kPaddedConfig);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, regions[0U].x, 2U);
    R2D_TEST_CHECK_EQ(context_, regions[0U].y, 2U);
    R2D_TEST_CHECK_EQ(context_, regions[0U].uv_min_x, 0.25F);
    R2D_TEST_CHECK_EQ(context_, regions[0U].uv_max_x, 0.75F);
}

void testSpriteInstancesUseTextureRegions(R2DT::TestContext& context_)
{
    constexpr std::array<WorldTransform, 1U> kWorldTransforms{{
        {.source_id = 1U, .affine = makeAffine(1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F)},
    }};
    constexpr std::array<Sprite, 1U> kSprites{{
        {
            .source_id = 1U,
            .texture_id = 5U,
            .texture_generation = 50U,
            .texture_region_id = 2U,
            .texture_region_generation = 20U,
            .material_id = 7U,
            .material_generation = 70U,
            .color_rgba8 = 0xFFFFFFFFU,
            .layer = 0U,
            .flags = 0U,
        },
    }};
    std::array<DrawCommand, 1U> draws{{
        makeDraw(0U, 0U, 7U, 5U, 0U, 0U),
    }};
    draws[0U].material_generation = 70U;
    draws[0U].texture_generation = 50U;
    constexpr std::array<TextureAtlasRegion, 1U> kRegions{{
        {
            .region_id = 2U,
            .generation = 20U,
            .atlas_id = 9U,
            .atlas_generation = 90U,
            .texture_id = 5U,
            .texture_generation = 50U,
            .x = 4U,
            .y = 0U,
            .width = 8U,
            .height = 4U,
            .uv_min_x = 0.25F,
            .uv_min_y = 0.0F,
            .uv_max_x = 0.75F,
            .uv_max_y = 0.5F,
            .flags = 0U,
        },
    }};
    std::array<SpriteInstance, 1U> instances{};

    auto result = R2D::SpriteInstanceBuildSystem<Provider, Dim>::runWithTextureRegions(
        draws,
        kWorldTransforms,
        kSprites,
        kRegions,
        instances);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, instances[0U].uv_min_x, 0.25F);
    R2D_TEST_CHECK_EQ(context_, instances[0U].uv_max_x, 0.75F);
    R2D_TEST_CHECK_EQ(context_, instances[0U].texture_generation, 50U);
    R2D_TEST_CHECK_EQ(context_, instances[0U].material_generation, 70U);

    std::array<Sprite, 1U> stale_region_sprites{kSprites};
    stale_region_sprites[0U].texture_region_generation = 21U;
    result = R2D::SpriteInstanceBuildSystem<Provider, Dim>::runWithTextureRegions(
        draws,
        kWorldTransforms,
        stale_region_sprites,
        kRegions,
        instances);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::InvalidInput);

    std::array<TextureAtlasRegion, 1U> mismatched_texture_regions{kRegions};
    mismatched_texture_regions[0U].texture_generation = 51U;
    result = R2D::SpriteInstanceBuildSystem<Provider, Dim>::runWithTextureRegions(
        draws,
        kWorldTransforms,
        kSprites,
        mismatched_texture_regions,
        instances);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::InvalidInput);

    std::array<Sprite, 1U> default_region_sprites{kSprites};
    default_region_sprites[0U].texture_region_id = 0U;
    default_region_sprites[0U].texture_region_generation = 0U;
    result = R2D::SpriteInstanceBuildSystem<Provider, Dim>::runWithTextureRegions(
        draws,
        kWorldTransforms,
        default_region_sprites,
        std::span<const TextureAtlasRegion>{},
        instances);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, instances[0U].uv_min_x, 0.0F);
    R2D_TEST_CHECK_EQ(context_, instances[0U].uv_max_x, 1.0F);
}

void testCapacityAndInvalidInput(R2DT::TestContext& context_)
{
    constexpr std::array<WorldTransform, 1U> kWorldTransforms{{
        {.source_id = 1U, .affine = makeAffine(1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F)},
    }};
    constexpr std::array<Sprite, 1U> kSprites{{
        {
            .source_id = 1U,
            .texture_id = 2U,
            .texture_generation = 0U,
            .texture_region_id = 0U,
            .texture_region_generation = 0U,
            .material_id = 3U,
            .material_generation = 0U,
            .color_rgba8 = 4U,
            .layer = 5U,
            .flags = 6U,
        },
    }};

    constexpr std::array<DrawCommand, 1U> kSparseDraws{{
        makeDraw(0U, 1U, 3U, 2U, 5U, 6U),
    }};
    std::array<SpriteInstance, 1U> instances{};
    auto result = R2D::SpriteInstanceBuildSystem<Provider, Dim>::run(
        kSparseDraws,
        kWorldTransforms,
        kSprites,
        instances);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::InsufficientCapacity);
    R2D_TEST_CHECK_EQ(context_, result.read_count, 0U);
    R2D_TEST_CHECK_EQ(context_, result.write_count, 0U);

    std::array<DrawCommand, 1U> invalid_draws{{
        makeDraw(1U, 0U, 3U, 2U, 5U, 6U),
    }};
    result = R2D::SpriteInstanceBuildSystem<Provider, Dim>::run(
        invalid_draws,
        kWorldTransforms,
        kSprites,
        instances);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::InvalidInput);

    invalid_draws[0U] = makeDraw(0U, 0U, 3U, 2U, 5U, 6U);
    invalid_draws[0U].instance_count = 2U;
    result = R2D::SpriteInstanceBuildSystem<Provider, Dim>::run(
        invalid_draws,
        kWorldTransforms,
        kSprites,
        instances);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::InvalidInput);
}

void testUnsupportedDomain(R2DT::TestContext& context_)
{
    std::array<R2D::DrawCommand<int, Dim>, 1U> draws{};
    std::array<R2D::WorldTransform<int, Dim>, 1U> world_transforms{};
    std::array<R2D::Sprite<int, Dim>, 1U> sprites{};
    std::array<R2D::SpriteInstance<int, Dim>, 1U> instances{};
    const auto result = R2D::SpriteInstanceBuildSystem<int, Dim>::run(
        draws,
        world_transforms,
        sprites,
        instances);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::UnsupportedDomain);
}

[[nodiscard]] int runTest()
{
    R2DT::TestContext context{};
    testBuildSpriteInstances(context);
    testBuildSpriteDrawPackets(context);
    testPacketBuildInvalidInput(context);
    testTextureAtlasBuildSystem(context);
    testSpriteInstancesUseTextureRegions(context);
    testCapacityAndInvalidInput(context);
    testUnsupportedDomain(context);
    return context.result();
}

} // namespace

int main() noexcept
{
    try {
        return runTest();
    } catch (const std::exception& exception) {
        std::fputs("sprite_instance_system_test exception: ", stderr);
        std::fputs(exception.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    } catch (...) {
        std::fputs("sprite_instance_system_test unknown exception\n", stderr);
        return 1;
    }
}
