#pragma once

#include <Render2D/Core/Types.hpp>

#include <iostream>

namespace Render2D::TestSupport {

class TestContext {
public:
    void check(bool condition_, const char* expression_, const char* file_, int line_) noexcept
    {
        ++check_count;
        if (!condition_) {
            ++failure_count;
            std::cerr << file_ << ':' << line_ << ": check failed: " << expression_ << '\n';
        }
    }

    [[nodiscard]] bool ok() const noexcept
    {
        return failure_count == 0U;
    }

    [[nodiscard]] int result() const noexcept
    {
        return ok() ? 0 : 1;
    }

    [[nodiscard]] U32 checks() const noexcept
    {
        return check_count;
    }

    [[nodiscard]] U32 failures() const noexcept
    {
        return failure_count;
    }

private:
    U32 check_count = 0U;
    U32 failure_count = 0U;
};

} // namespace Render2D::TestSupport

#define R2D_TEST_CHECK(context_, expression_) \
    (context_).check(static_cast<bool>(expression_), #expression_, __FILE__, __LINE__)

#define R2D_TEST_CHECK_EQ(context_, left_, right_) \
    R2D_TEST_CHECK((context_), ((left_) == (right_)))

#define R2D_TEST_REQUIRE(context_, expression_) \
    do { \
        const bool r2d_test_condition = static_cast<bool>(expression_); \
        (context_).check(r2d_test_condition, #expression_, __FILE__, __LINE__); \
        if (!r2d_test_condition) { \
            return (context_).result(); \
        } \
    } while (false)
