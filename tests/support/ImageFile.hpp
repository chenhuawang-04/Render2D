#pragma once

// Test-only image-file I/O: a thin, stb-free wrapper used by the asset framework
// and the asset/scene render test to load real on-disk images (and author them).
// The vendored stb_image / stb_image_write single headers (third_party/stb) are
// compiled into exactly one TU (ImageFile.cpp) behind the render2d_test_image_io
// target -- they are never included from this header, never from the umbrella, and
// never from include/Render2D. Render2D's core stays asset-pipeline-free; the host
// engine owns the production asset pipeline at merge.

#include "Render2D/Memory/RenderVector.hpp"

#include <Render2D/Core/Types.hpp>

namespace Render2D::TestSupport {

// A decoded image in CPU memory: tightly packed RGBA8 (4 bytes/pixel), row-major,
// top-to-bottom. pixels.size() == width * height * 4.
struct DecodedImage {
    McVector<U8> pixels;
    U32 width = 0U;
    U32 height = 0U;
};

// Decode an on-disk image file (PNG/JPG/BMP/TGA/...) to RGBA8, forcing 4 channels.
// Returns false on any IO/decode error (out_ is left empty).
[[nodiscard]] bool loadImageRgba8(const char* path_, DecodedImage& out_);

// Encode a tightly packed RGBA8 buffer to a PNG file on disk. Returns false on any
// error. Tests use it to author a real PNG that loadImageRgba8 then reads back.
[[nodiscard]] bool writeImageRgba8Png(
    const char* path_,
    U32 width_,
    U32 height_,
    const U8* rgba_pixels_);

} // namespace Render2D::TestSupport
