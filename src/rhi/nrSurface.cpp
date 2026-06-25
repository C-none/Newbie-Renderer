module nr.rhi;
import :surface;
import dependency.window;
import dependency.vulkan;
import nr.utils;
import std;

namespace nr::rhi
{
Surface::GlfwContext::GlfwContext()
{
            glfwSetErrorCallback(&GlfwContext::errorCallback);
            nrAssert(static_cast<bool>(glfwInit()), "Failed to initialize GLFW.");
        }

Surface::GlfwContext::~GlfwContext()
{
            glfwTerminate();
        }

void Surface::GlfwContext::errorCallback(int error, const char *msg)
{
            nrInfo<LogLevel::error>(std::format("glfw: (error number:{}) {}", error, msg));
        }

[[nodiscard]] Surface::GlfwContext &Surface::glfwContext()
{
        static GlfwContext context;
        return context;
    }

Surface::Surface()
{
        ensureGlfwInitialized();
    }

void Surface::ensureGlfwInitialized()
{
        (void)glfwContext();
    }

[[nodiscard]] Surface Surface::create(const vk::raii::Instance &instance, std::string_view windowTitle, vk::Extent2D initialExtent)
{
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        Surface result;
        result.extent = initialExtent;
        result.handle.reset(glfw::createWindow(static_cast<int>(initialExtent.width), static_cast<int>(initialExtent.height), std::string(windowTitle).c_str(), nullptr, nullptr));
        nrAssert(result.handle != nullptr, "Surface::create failed to create GLFW window.");

        VkSurfaceKHR rawSurface{};
        vk::detail::resultCheck(glfw::createWindowSurface(*instance, result.handle.get(), nullptr, &rawSurface), "Failed to create window surface");
        result.surface = vk::raii::SurfaceKHR(instance, rawSurface);
        result.refreshExtentFromFramebuffer();
        return result;
    }

void Surface::refreshExtentFromFramebuffer()
{
        nrAssert(handle != nullptr, "Surface::refreshExtentFromFramebuffer requires a valid window handle.");
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(handle.get(), &width, &height);
        extent = vk::Extent2D{
            static_cast<std::uint32_t>(std::max(width, 1)),
            static_cast<std::uint32_t>(std::max(height, 1)),
        };
    }
} // namespace nr::rhi
