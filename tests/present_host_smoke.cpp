// Stage 22A: SDL3 present-host build/link smoke test.
//
// Proves the present-host plumbing works end to end: the SDL submodule built as
// a static library, the internal render2d_present_host_support target links it,
// and the SDL headers are reachable through the target. Touches no window,
// display, or GPU -- it only reads the compile-time and linked SDL versions, so
// it runs anywhere (including headless CI). The real window/surface/swapchain
// path arrives in Stage 22B.

#include <Render2D/Present/PresentHost.hpp>

#include "support/TestHarness.hpp"

#include <cstdio>
#include <exception>

namespace {

[[nodiscard]] auto runTest() -> int
{
    auto context = Render2D::TestSupport::TestContext{};

    const Render2D::PresentHostSdlVersion compiled = Render2D::presentHostCompiledSdlVersion();
    const Render2D::PresentHostSdlVersion linked = Render2D::presentHostLinkedSdlVersion();

    std::printf(
        "present-host: SDL compiled %d.%d.%d, linked %d.%d.%d\n",
        compiled.major, compiled.minor, compiled.patch,
        linked.major, linked.minor, linked.patch);

    // SDL3 headers and the SDL3 static library are both present.
    R2D_TEST_CHECK_EQ(context, compiled.major, 3);
    R2D_TEST_CHECK_EQ(context, linked.major, 3);

    // Statically linked SDL: the version compiled against must equal the version
    // linked in (a mismatch would mean the headers and the library diverged).
    R2D_TEST_CHECK_EQ(context, compiled.major, linked.major);
    R2D_TEST_CHECK_EQ(context, compiled.minor, linked.minor);
    R2D_TEST_CHECK_EQ(context, compiled.patch, linked.patch);

    return context.result();
}

} // namespace

int main() noexcept
{
    try {
        return runTest();
    }
    catch (const std::exception& exception) {
        std::fputs("present_host_smoke exception: ", stderr);
        std::fputs(exception.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
    catch (...) {
        std::fputs("present_host_smoke unknown exception\n", stderr);
        return 1;
    }
}
