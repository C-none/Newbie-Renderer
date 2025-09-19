module;
#include <vulkan/vulkan_raii.hpp>
export module nr.rhi;
export namespace nr
{
namespace rhi
{
class Device
{
  public:
    vk::raii::Instance instance();
    Device()
    {
    }
    void createInstaceVK(std::string const &appName = {"DefaultApp"}, std::string const &engineName = {"DefaultEngine"}, std::vector<std::string> const &layers = {}, std::vector<std::string> const &extensions = {}, uint32_t apiVersion = VK_API_VERSION_1_4);

    void destroy();
};

void rhiTest();
} // namespace rhi
} // namespace nr
