#pragma once

// Test-only asset framework: a tiny registry that loads real image files (via
// ImageFile -> stb) into stable texture handles, registers logical materials, and
// authors Sprite components that reference them. It is CPU-side and dependency-
// light (no Vulkan); the asset/scene render test feeds image(handle) pixels to the
// GPU. Like MiniEcs this is test-only scaffolding -- the host engine owns the
// production asset pipeline at merge (it is intentionally out of Render2D's core
// scope). McVector only, never the standard dynamic vector.

#include "ImageFile.hpp"

#include "Render2D/Memory/RenderVector.hpp"

#include <Render2D/Component/Sprite.hpp>
#include <Render2D/Core/Types.hpp>

#include <cstring>
#include <utility>

namespace Render2D::TestSupport {

template<class Provider, class Dim>
class AssetRegistry {
public:
    // Stable id + generation handles. id == 0 means "invalid / not found".
    struct TextureHandle {
        U32 id = 0U;
        U32 generation = 0U;
    };
    struct MaterialHandle {
        U32 id = 0U;
        U32 generation = 0U;
    };

    // Load a texture from disk (real decode). Returns an invalid handle on failure.
    // name_ must outlive the registry (string literals in tests do).
    TextureHandle loadTexture(const char* name_, const char* path_)
    {
        DecodedImage decoded{};
        if (!loadImageRgba8(path_, decoded)) {
            return {};
        }
        const U32 id = kFirstTextureId + static_cast<U32>(textures.size());
        TextureEntry entry{};
        entry.name = name_;
        entry.image = std::move(decoded);
        entry.id = id;
        entry.generation = 1U;
        textures.push_back(std::move(entry));
        return {.id = id, .generation = 1U};
    }

    // Register a logical material (no GPU state). name_ must outlive the registry.
    MaterialHandle registerMaterial(const char* name_)
    {
        const U32 id = kFirstMaterialId + static_cast<U32>(materials.size());
        MaterialEntry entry{};
        entry.name = name_;
        entry.id = id;
        entry.generation = 1U;
        materials.push_back(std::move(entry));
        return {.id = id, .generation = 1U};
    }

    [[nodiscard]] static bool valid(TextureHandle handle_) noexcept
    {
        return handle_.id != 0U;
    }

    [[nodiscard]] static bool valid(MaterialHandle handle_) noexcept
    {
        return handle_.id != 0U;
    }

    [[nodiscard]] const DecodedImage* image(TextureHandle handle_) const noexcept
    {
        for (const auto& entry : textures) {
            if (entry.id == handle_.id) {
                return &entry.image;
            }
        }
        return nullptr;
    }

    [[nodiscard]] TextureHandle findTexture(const char* name_) const noexcept
    {
        for (const auto& entry : textures) {
            if (std::strcmp(entry.name, name_) == 0) {
                return {.id = entry.id, .generation = entry.generation};
            }
        }
        return {};
    }

    [[nodiscard]] MaterialHandle findMaterial(const char* name_) const noexcept
    {
        for (const auto& entry : materials) {
            if (std::strcmp(entry.name, name_) == 0) {
                return {.id = entry.id, .generation = entry.generation};
            }
        }
        return {};
    }

    [[nodiscard]] U32 textureCount() const noexcept
    {
        return static_cast<U32>(textures.size());
    }

    [[nodiscard]] U32 materialCount() const noexcept
    {
        return static_cast<U32>(materials.size());
    }

    // Author a Sprite that references the given texture + material handles.
    [[nodiscard]] Sprite<Provider, Dim> makeSprite(
        TextureHandle texture_,
        MaterialHandle material_,
        U32 source_id_,
        U32 color_rgba8_,
        U32 layer_) const noexcept
    {
        return {
            .source_id = source_id_,
            .texture_id = texture_.id,
            .texture_generation = texture_.generation,
            .texture_region_id = 0U,
            .texture_region_generation = 0U,
            .material_id = material_.id,
            .material_generation = material_.generation,
            .color_rgba8 = color_rgba8_,
            .layer = layer_,
            .flags = 0U,
        };
    }

private:
    static constexpr U32 kFirstTextureId = 100U;
    static constexpr U32 kFirstMaterialId = 200U;

    struct TextureEntry {
        const char* name = nullptr;
        DecodedImage image;
        U32 id = 0U;
        U32 generation = 0U;
    };
    struct MaterialEntry {
        const char* name = nullptr;
        U32 id = 0U;
        U32 generation = 0U;
    };

    McVector<TextureEntry> textures;
    McVector<MaterialEntry> materials;
};

} // namespace Render2D::TestSupport
