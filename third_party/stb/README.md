# stb single-file image headers (vendored)

`stb_image.h` and `stb_image_write.h` are Sean Barrett's public-domain
single-file C/C++ libraries for decoding and encoding common image formats
(PNG/JPG/BMP/TGA/… decode; PNG/BMP/TGA/JPG encode). Each is a single,
self-contained header whose license banner (public domain / MIT dual license)
is at the top of the file itself.

They back the **test-only** image-loading path:
`tests/support/ImageFile.{hpp,cpp}` (the `render2d_test_image_io` target) calls
them to load real on-disk PNGs into CPU pixel buffers, which
`tests/support/AssetRegistry.hpp` then turns into texture handles for the
asset/scene render test (`render2d.asset_scene_render`).

- **Source:** https://github.com/nothings/stb — `stb_image.h`, `stb_image_write.h`
- **Versions:** `stb_image` v2.30, `stb_image_write` v1.16
- **Retrieved:** 2026-06-19
- **License:** public domain (or MIT, at your option) — see each header's own banner

## Why this is a plain vendored file, not a submodule

Like `third_party/renderdoc/renderdoc_app.h`, these are header-only, single-file
libraries with no build of their own — there is no upstream CMake to add as a
submodule. Vendoring the two headers is the simplest, reproducible way to pin
them (the committed copy *is* the pin).

## Isolation (do not break)

These headers are **test-only** and must never leak into the rendering core:

- Compiled in exactly one TU (`tests/support/ImageFile.cpp`) via
  `#define STB_IMAGE_IMPLEMENTATION` / `#define STB_IMAGE_WRITE_IMPLEMENTATION`.
- Exposed only through the internal `render2d_test_image_io` target as a
  **SYSTEM** include (so stb's own warnings never trip Render2D's
  `-Wall -Wextra -Wpedantic -Werror`).
- Never included from the public umbrella `Render2D.hpp`, from `include/Render2D`,
  or from `src/` — the same isolation the font / present-host runtimes use.
  Render2D's core stays asset-pipeline-free; a host engine owns the production
  asset pipeline at merge.
