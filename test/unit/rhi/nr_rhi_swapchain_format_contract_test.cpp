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
} // namespace
