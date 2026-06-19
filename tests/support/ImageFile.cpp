#include "ImageFile.hpp"

#include <cstring>

// The vendored stb single headers are compiled HERE, in this one TU, and reached
// only as SYSTEM includes through the render2d_test_image_io target so their
// warning-dirty internals never trip Render2D's -Wall -Wextra -Wpedantic -Werror.
// Do not include them anywhere else (never from a header, never from the umbrella).
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>

namespace Render2D::TestSupport {

bool loadImageRgba8(const char* path_, DecodedImage& out_)
{
    out_.pixels.clear();
    out_.width = 0U;
    out_.height = 0U;

    int width = 0;
    int height = 0;
    int source_channels = 0;
    stbi_uc* data = stbi_load(path_, &width, &height, &source_channels, 4);
    if (data == nullptr) {
        return false;
    }
    if (width <= 0 || height <= 0) {
        stbi_image_free(data);
        return false;
    }

    const auto byte_count = static_cast<Usize>(width) * static_cast<Usize>(height) * 4U;
    out_.pixels.resize(byte_count);
    std::memcpy(out_.pixels.data(), data, byte_count);
    stbi_image_free(data);
    out_.width = static_cast<U32>(width);
    out_.height = static_cast<U32>(height);
    return true;
}

bool writeImageRgba8Png(const char* path_, U32 width_, U32 height_, const U8* rgba_pixels_)
{
    const auto row_stride = static_cast<int>(width_ * 4U);
    const int written = stbi_write_png(
        path_,
        static_cast<int>(width_),
        static_cast<int>(height_),
        4,
        rgba_pixels_,
        row_stride);
    return written != 0;
}

} // namespace Render2D::TestSupport
