#include "support/TestHarness.hpp"

int main()
{
    Render2D::TestSupport::TestContext context;
    R2D_TEST_CHECK_EQ(context, context.checks(), 0U);
    R2D_TEST_REQUIRE(context, context.failures() == 0U);
    R2D_TEST_CHECK(context, true);
    R2D_TEST_CHECK(context, context.ok());
    return context.result();
}
