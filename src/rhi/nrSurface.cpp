module nr.rhi;
import :surface;
import dependency.window;
import dependency.vulkan;
import nr.utils;
import std;

namespace
{
struct MonitorArea
{
    GLFWmonitor* monitor = nullptr;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int refreshRate = 0;
};

[[nodiscard]] int sanitizeWindowDimension(int value) noexcept
{
        return std::max(value, 1);
    }

[[nodiscard]] nr::rhi::WindowBounds readWindowBounds(GLFWwindow* window)
{
        nr::nrAssert(window != nullptr, "readWindowBounds requires a valid GLFW window.");

        auto x = 0;
        auto y = 0;
        auto width = 0;
        auto height = 0;
        glfwGetWindowPos(window, &x, &y);
        glfwGetWindowSize(window, &width, &height);
        return nr::rhi::WindowBounds{
            .x = x,
            .y = y,
            .width = sanitizeWindowDimension(width),
            .height = sanitizeWindowDimension(height),
        };
    }

[[nodiscard]] MonitorArea makeMonitorArea(GLFWmonitor* monitor)
{
        if (monitor == nullptr)
        {
            return {};
        }

        auto const* videoMode = glfwGetVideoMode(monitor);
        if (videoMode == nullptr)
        {
            return {};
        }

        auto x = 0;
        auto y = 0;
        glfwGetMonitorPos(monitor, &x, &y);
        return MonitorArea{
            .monitor = monitor,
            .x = x,
            .y = y,
            .width = sanitizeWindowDimension(videoMode->width),
            .height = sanitizeWindowDimension(videoMode->height),
            .refreshRate = videoMode->refreshRate,
        };
    }

[[nodiscard]] bool validMonitorArea(const MonitorArea& area) noexcept
{
        return area.monitor != nullptr && area.width > 0 && area.height > 0;
    }

[[nodiscard]] std::int64_t overlapArea(
    const nr::rhi::WindowBounds& window,
    const MonitorArea& monitor) noexcept
{
        auto const windowLeft = static_cast<std::int64_t>(window.x);
        auto const windowTop = static_cast<std::int64_t>(window.y);
        auto const windowRight = windowLeft + static_cast<std::int64_t>(window.width);
        auto const windowBottom = windowTop + static_cast<std::int64_t>(window.height);
        auto const monitorLeft = static_cast<std::int64_t>(monitor.x);
        auto const monitorTop = static_cast<std::int64_t>(monitor.y);
        auto const monitorRight = monitorLeft + static_cast<std::int64_t>(monitor.width);
        auto const monitorBottom = monitorTop + static_cast<std::int64_t>(monitor.height);

        auto const overlapWidth = std::max<std::int64_t>(
            0,
            std::min(windowRight, monitorRight) - std::max(windowLeft, monitorLeft));
        auto const overlapHeight = std::max<std::int64_t>(
            0,
            std::min(windowBottom, monitorBottom) - std::max(windowTop, monitorTop));
        return overlapWidth * overlapHeight;
    }

[[nodiscard]] std::vector<MonitorArea> enumerateMonitorAreas()
{
        auto monitorCount = 0;
        auto* rawMonitors = glfwGetMonitors(&monitorCount);
        if (rawMonitors == nullptr || monitorCount <= 0)
        {
            auto primary = makeMonitorArea(glfwGetPrimaryMonitor());
            if (!validMonitorArea(primary))
            {
                return {};
            }
            return std::vector<MonitorArea>{primary};
        }

        auto monitors = std::span<GLFWmonitor*>{rawMonitors, static_cast<std::size_t>(monitorCount)};
        return monitors |
               std::views::transform([](GLFWmonitor* monitor) { return makeMonitorArea(monitor); }) |
               std::views::filter(validMonitorArea) |
               std::ranges::to<std::vector>();
    }

[[nodiscard]] MonitorArea selectTargetMonitorArea(GLFWwindow* window)
{
        auto monitorAreas = enumerateMonitorAreas();
        nr::nrAssert(!monitorAreas.empty(), "Surface fullscreen requires at least one GLFW monitor.");

        auto const windowBounds = readWindowBounds(window);
        auto bestMonitor = std::ranges::max_element(
            monitorAreas,
            {},
            [&](const MonitorArea& area) { return overlapArea(windowBounds, area); });
        if (bestMonitor != monitorAreas.end() && overlapArea(windowBounds, *bestMonitor) > 0)
        {
            return *bestMonitor;
        }

        auto primary = makeMonitorArea(glfwGetPrimaryMonitor());
        if (validMonitorArea(primary))
        {
            return primary;
        }

        return monitorAreas.front();
    }
} // namespace

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

bool Surface::framebufferAvailable() const noexcept
{
        if (handle == nullptr)
        {
            return false;
        }

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(handle.get(), &width, &height);
        return width > 0 && height > 0;
    }

bool Surface::fullscreenEnabled() const noexcept
{
        return handle != nullptr && glfwGetWindowMonitor(handle.get()) != nullptr;
    }

std::uintptr_t Surface::fullscreenExclusiveMonitor() const noexcept
{
        return handle == nullptr ? 0 : glfw::nativeMonitorFromWindow(handle.get());
    }

void Surface::setFullscreen(bool enabled)
{
        nrAssert(handle != nullptr, "Surface::setFullscreen requires a valid window handle.");
        if (enabled == fullscreenEnabled())
        {
            return;
        }

        if (enabled)
        {
            savedWindowedBounds_ = readWindowBounds(handle.get());
            auto const monitorArea = selectTargetMonitorArea(handle.get());
            glfwSetWindowMonitor(
                handle.get(),
                monitorArea.monitor,
                0,
                0,
                monitorArea.width,
                monitorArea.height,
                monitorArea.refreshRate);
        }
        else
        {
            glfwSetWindowMonitor(
                handle.get(),
                nullptr,
                savedWindowedBounds_.x,
                savedWindowedBounds_.y,
                sanitizeWindowDimension(savedWindowedBounds_.width),
                sanitizeWindowDimension(savedWindowedBounds_.height),
                0);
        }

        nrAssert(fullscreenEnabled() == enabled, "Surface::setFullscreen failed to update GLFW window monitor.");
        swapchainRecreateRequested_ = true;
        refreshExtentFromFramebuffer();
    }

bool Surface::consumeSwapchainRecreateRequest() noexcept
{
        auto requested = swapchainRecreateRequested_;
        swapchainRecreateRequested_ = false;
        return requested;
    }
} // namespace nr::rhi
