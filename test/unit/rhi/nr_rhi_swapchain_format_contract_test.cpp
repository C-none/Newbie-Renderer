import dependency.vulkan;
import nr.rhi;
import nr.test;
import std;

namespace
{
[[nodiscard]] vk::SurfaceFormatKHR select(std::span<const vk::SurfaceFormatKHR> formats)
{
    return nr::rhi::chooseSwapchainSurfaceFormat(formats);
}

[[nodiscard]] vk::QueueFamilyProperties queueFamily(vk::QueueFlags flags)
{
    auto properties = vk::QueueFamilyProperties{};
    properties.queueFlags = flags;
    properties.queueCount = 1;
    return properties;
}

const nr::test::CaseRegistrar scRgbPriorityCase{
    "rhi swapchain format selection prefers R16 scRGB", [] {
        auto formats = std::array{
            vk::SurfaceFormatKHR{
                vk::Format::eB8G8R8A8Srgb,
                vk::ColorSpaceKHR::eSrgbNonlinear,
            },
            vk::SurfaceFormatKHR{
                vk::Format::eR16G16B16A16Sfloat,
                vk::ColorSpaceKHR::eExtendedSrgbLinearEXT,
            },
            vk::SurfaceFormatKHR{
                vk::Format::eA2R10G10B10UnormPack32,
                vk::ColorSpaceKHR::eHdr10St2084EXT,
            },
            vk::SurfaceFormatKHR{
                vk::Format::eA2B10G10R10UnormPack32,
                vk::ColorSpaceKHR::eHdr10St2084EXT,
            },
        };

        auto selected = select(formats);
        nr::test::requireEqual(selected.format, vk::Format::eR16G16B16A16Sfloat);
        nr::test::requireEqual(selected.colorSpace, vk::ColorSpaceKHR::eExtendedSrgbLinearEXT);
        nr::test::require(nr::rhi::isHdrSwapchainColorSpace(selected.colorSpace));
    }};

const nr::test::CaseRegistrar scRgbFallbackCase{
    "rhi swapchain format selection uses scRGB when HDR10 is absent", [] {
        auto formats = std::array{
            vk::SurfaceFormatKHR{
                vk::Format::eB8G8R8A8Srgb,
                vk::ColorSpaceKHR::eSrgbNonlinear,
            },
            vk::SurfaceFormatKHR{
                vk::Format::eR16G16B16A16Sfloat,
                vk::ColorSpaceKHR::eExtendedSrgbLinearEXT,
            },
        };

        auto selected = select(formats);
        nr::test::requireEqual(selected.format, vk::Format::eR16G16B16A16Sfloat);
        nr::test::requireEqual(selected.colorSpace, vk::ColorSpaceKHR::eExtendedSrgbLinearEXT);
        nr::test::require(nr::rhi::isScRgbSwapchainColorSpace(selected.colorSpace));
    }};

const nr::test::CaseRegistrar sdrFallbackCase{
    "rhi swapchain format selection falls back to SDR BGRA", [] {
        auto formats = std::array{
            vk::SurfaceFormatKHR{
                vk::Format::eR8G8B8A8Unorm,
                vk::ColorSpaceKHR::eSrgbNonlinear,
            },
            vk::SurfaceFormatKHR{
                vk::Format::eB8G8R8A8Srgb,
                vk::ColorSpaceKHR::eSrgbNonlinear,
            },
        };

        auto selected = select(formats);
        nr::test::requireEqual(selected.format, vk::Format::eB8G8R8A8Srgb);
        nr::test::requireEqual(selected.colorSpace, vk::ColorSpaceKHR::eSrgbNonlinear);
        nr::test::require(!nr::rhi::isHdrSwapchainColorSpace(selected.colorSpace));
    }};

const nr::test::CaseRegistrar dedicatedPresentComputeCase{
    "rhi queue selection prefers a present-capable dedicated compute family", [] {
        auto queueFamilies = std::array{
            queueFamily(vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute |
                        vk::QueueFlagBits::eTransfer),
            queueFamily(vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer),
            queueFamily(vk::QueueFlagBits::eTransfer),
        };
        auto presentSupport = std::array{vk::True, vk::True, vk::False};

        auto selected = nr::rhi::selectRequiredQueueFamilies(queueFamilies, presentSupport);
        nr::test::require(selected.has_value(), "expected the required queue family contract to be satisfiable");
        nr::test::requireEqual(selected->graphics, 0u);
        nr::test::requireEqual(selected->compute, 1u);
        nr::test::requireEqual(selected->transfer, 2u);
    }};

const nr::test::CaseRegistrar universalPresentComputeFallbackCase{
    "rhi queue selection falls back when dedicated compute cannot present", [] {
        auto queueFamilies = std::array{
            queueFamily(vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute |
                        vk::QueueFlagBits::eTransfer),
            queueFamily(vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer),
            queueFamily(vk::QueueFlagBits::eTransfer),
        };
        auto presentSupport = std::array{vk::True, vk::False, vk::False};

        auto selected = nr::rhi::selectRequiredQueueFamilies(queueFamilies, presentSupport);
        nr::test::require(selected.has_value(), "expected the present-capable universal compute fallback");
        nr::test::requireEqual(selected->compute, 0u);
    }};

const nr::test::CaseRegistrar missingPresentComputeCase{
    "rhi queue selection rejects devices without a present-capable compute family", [] {
        auto queueFamilies = std::array{
            queueFamily(vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute |
                        vk::QueueFlagBits::eTransfer),
            queueFamily(vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer),
            queueFamily(vk::QueueFlagBits::eTransfer),
        };
        auto presentSupport = std::array{vk::False, vk::False, vk::False};

        nr::test::require(!nr::rhi::selectRequiredQueueFamilies(queueFamilies, presentSupport).has_value());
    }};

const nr::test::CaseRegistrar mismatchedPresentSupportCase{
    "rhi queue selection rejects mismatched present-support input", [] {
        auto queueFamilies = std::array{
            queueFamily(vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute),
            queueFamily(vk::QueueFlagBits::eTransfer),
        };
        auto presentSupport = std::array{vk::True};

        nr::test::require(!nr::rhi::selectRequiredQueueFamilies(queueFamilies, presentSupport).has_value());
    }};
} // namespace
