#include <Render2D/Render2D.hpp>

#include <string>

namespace R2D = Render2D;

struct BadNonPodComponent {
    std::string value;
};

int main()
{
    R2D::requireSupportedRenderComponent<
        R2D::VulkanNativeProvider,
        R2D::Dim2,
        BadNonPodComponent>();
    return 0;
}
