// Stage 25A: a standalone downstream consumer of Render2D.
//
// This translation unit lives OUTSIDE the Render2D build. The
// render2d.packaging_consumer test configures+builds+runs it against the
// Render2D source tree (see tests/packaging/run_consumer.cmake), proving the
// public Render2D::Render2D target is consumable by source reuse -- the include
// interface propagates, C++23 is requested, the engine-dependency targets are
// linked transitively, and a real pure system executes -- exactly as a host
// engine reuses it. It deliberately uses ONLY the public umbrella header.

#include <Render2D/Render2D.hpp>

#include <array>
#include <cstdio>
#include <span>
#include <type_traits>

int main()
{
    using Provider = Render2D::VulkanNativeProvider;
    using Dim = Render2D::Dim2;

    // (1) A Render2D-owned native reference is a trivially-copyable POD
    // (id + generation), reachable from a foreign translation unit.
    static_assert(
        std::is_trivially_copyable_v<Render2D::BufferRef<Provider, Dim>>,
        "BufferRef must be a POD ref consumable downstream");

    // (2) A pure system runs on caller-owned spans and returns a SystemResult.
    const std::array<Render2D::Transform<Provider, Dim>, 1> transforms{{
        Render2D::Transform<Provider, Dim>{
            .source_id = 7U,
            .position_x = 3.0F,
            .position_y = -5.0F,
            .rotation_radians = 0.0F,
            .scale_x = 2.0F,
            .scale_y = 4.0F,
        },
    }};
    std::array<Render2D::WorldTransform<Provider, Dim>, 1> world{};

    const Render2D::SystemResult result =
        Render2D::TransformSystem<Provider, Dim>::run(
            std::span<const Render2D::Transform<Provider, Dim>>(transforms),
            std::span<Render2D::WorldTransform<Provider, Dim>>(world));

    if (result.code != Render2D::SystemStatusCode::Ok)
    {
        std::fprintf(stderr, "consumer: TransformSystem did not return Ok\n");
        return 1;
    }

    // rotation 0 -> the world affine is the TRS with scale on the diagonal and
    // translation in the last column.
    const auto& affine = world[0].affine;
    if (affine.m00 != 2.0F || affine.m11 != 4.0F ||
        affine.m02 != 3.0F || affine.m12 != -5.0F)
    {
        std::fprintf(stderr, "consumer: world transform affine mismatch\n");
        return 1;
    }

    std::printf(
        "render2d consumer ok: Render2D %u.%u.%u (m00=%.1f m11=%.1f)\n",
        Render2D::kVersionMajor, Render2D::kVersionMinor, Render2D::kVersionPatch,
        static_cast<double>(affine.m00), static_cast<double>(affine.m11));
    return 0;
}
