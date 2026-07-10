module nr.renderPasses;
import dependency.assets;
import dependency.vulkan;

import :presentNode;
import nr.renderer;
import nr.rhi;
import nr.utils;
import std;
import :nodeType;

namespace nr::renderPasses::detail
{
struct PresentConvertPushConstants
{
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t swizzleBgr = 0u;
    std::uint32_t outputEncoding = 0u;
    std::uint32_t toneMapping = 0u;
    std::uint32_t flipY = 0u;
    float uiOpacity = 1.0f;
};

static_assert(sizeof(PresentConvertPushConstants) <= nr::rhi::kMaxPushConstantBytes);

inline constexpr std::uint32_t kOutputEncodingLinear = 0u;
inline constexpr std::uint32_t kOutputEncodingSrgb = 1u;
inline constexpr std::uint32_t kOutputEncodingHdr10Pq = 2u;
inline constexpr std::uint32_t kOutputEncodingScRgb = 3u;

inline constexpr std::uint32_t kToneMappingNone = 0u;
inline constexpr std::uint32_t kToneMappingReinhard = 1u;
inline constexpr std::uint32_t kToneMappingAcesFilmic = 2u;
inline constexpr std::uint32_t kToneMappingBt2390 = 3u;

// Selection index 0 keeps the per-gamut default; entries 1..N map to explicit methods (index - 1).
inline constexpr std::array<std::string_view, 5u> kToneMappingSelectionLabels = {
    "Auto",
    "None",
    "Reinhard",
    "ACES Filmic",
    "BT.2390 EETF",
};

// Defaults follow the color-space research: SDR sRGB -> ACES filmic, HDR10 ST2084 -> BT.2390 EETF,
// scRGB extended-linear -> None (preserve the scene-referred signal for the compositor).
[[nodiscard]] std::uint32_t defaultToneMappingForColorSpace(vk::ColorSpaceKHR colorSpace) noexcept
{
    if (nr::rhi::isHdr10SwapchainColorSpace(colorSpace))
    {
        return kToneMappingBt2390;
    }
    if (nr::rhi::isScRgbSwapchainColorSpace(colorSpace))
    {
        return kToneMappingNone;
    }
    return kToneMappingAcesFilmic;
}

[[nodiscard]] std::uint32_t resolveToneMappingMethod(std::uint32_t selection, vk::ColorSpaceKHR colorSpace) noexcept
{
    if (selection == 0u)
    {
        return defaultToneMappingForColorSpace(colorSpace);
    }
    return selection - 1u;
}

struct PresentFormatConversion
{
    bool swizzleBgr = false;
    std::uint32_t outputEncoding = kOutputEncodingLinear;
    vk::Format convertedFormat = vk::Format::eR8G8B8A8Unorm;
};

struct PresentRuntimeCache
{
    std::shared_ptr<nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>> pipeline{};

    nr::rhi::Image convertedColorImage{};
    nr::renderer::RetainedImageState convertedColorState{};
    vk::Extent2D allocatedExtent{0, 0};
    vk::Format allocatedFormat = vk::Format::eUndefined;
};

[[nodiscard]] std::optional<PresentFormatConversion> resolvePresentFormatConversion(
    vk::Format format,
    vk::ColorSpaceKHR colorSpace)
{
    switch (format)
    {
    case vk::Format::eB8G8R8A8Srgb:
    case vk::Format::eB8G8R8A8Unorm:
        return PresentFormatConversion{
            .swizzleBgr = true,
            .outputEncoding = kOutputEncodingSrgb,
        };
    case vk::Format::eR8G8B8A8Srgb:
    case vk::Format::eR8G8B8A8Unorm:
        return PresentFormatConversion{
            .outputEncoding = kOutputEncodingSrgb,
        };
    case vk::Format::eA2B10G10R10UnormPack32:
    case vk::Format::eA2R10G10B10UnormPack32:
        if (nr::rhi::isHdr10SwapchainColorSpace(colorSpace))
        {
            return PresentFormatConversion{
                .outputEncoding = kOutputEncodingHdr10Pq,
                .convertedFormat = format,
            };
        }
        return std::nullopt;
    case vk::Format::eR16G16B16A16Sfloat:
        if (nr::rhi::isScRgbSwapchainColorSpace(colorSpace))
        {
            return PresentFormatConversion{
                .outputEncoding = kOutputEncodingScRgb,
                .convertedFormat = format,
            };
        }
        return std::nullopt;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::shared_ptr<PresentRuntimeCache> ensurePresentRuntime(nr::rhi::Device& device)
{
    auto& shaderService = nr::rhi::ShaderService::instance();
    auto program = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
        .sourcePath = std::filesystem::path("renderer/presentConvert"),
    });
    nr::nrAssert(program.valid(), "Present pass failed to compile shader module renderer/presentConvert.");

    auto pipelineDesc = nr::rhi::ComputePipelineDesc{
        .entryPointName = "presentConvertMain",
    };

    auto runtime = std::make_shared<PresentRuntimeCache>();
    runtime->pipeline = std::make_shared<nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>>();
    runtime->pipeline->initialize(device.pipeline().createComputePipeline(program, pipelineDesc));
    nr::nrAssert(runtime->pipeline->valid(), "Present pass failed to create compute pipeline.");

    return runtime;
}

[[nodiscard]] std::uint32_t divideRoundUp(std::uint32_t value, std::uint32_t divisor)
{
    nr::nrAssert(divisor > 0u, "divideRoundUp requires divisor > 0.");
    return (value + divisor - 1u) / divisor;
}

[[nodiscard]] vk::DeviceSize presentReadbackBytesPerPixel(vk::Format format) noexcept
{
    switch (format)
    {
    case vk::Format::eR8G8B8A8Unorm:
    case vk::Format::eR8G8B8A8Srgb:
    case vk::Format::eB8G8R8A8Unorm:
    case vk::Format::eB8G8R8A8Srgb:
    case vk::Format::eA2B10G10R10UnormPack32:
    case vk::Format::eA2R10G10B10UnormPack32:
        return 4u;
    case vk::Format::eR16G16B16A16Sfloat:
        return 8u;
    case vk::Format::eR32G32B32A32Sfloat:
        return 16u;
    default:
        nr::nrAssert(
            false,
            std::format("Present readback unsupported format for size estimation: {}", vk::to_string(format)));
        return 0u;
    }
}

[[nodiscard]] vk::DeviceSize presentReadbackByteSize(vk::Extent2D extent, vk::Format format) noexcept
{
    return static_cast<vk::DeviceSize>(extent.width) *
           static_cast<vk::DeviceSize>(extent.height) *
           presentReadbackBytesPerPixel(format);
}

[[nodiscard]] bool supportsLinearExrScreenshotFormat(vk::Format format) noexcept
{
    switch (format)
    {
    case vk::Format::eR8G8B8A8Unorm:
    case vk::Format::eR8G8B8A8Srgb:
    case vk::Format::eB8G8R8A8Unorm:
    case vk::Format::eB8G8R8A8Srgb:
    case vk::Format::eR16G16B16A16Sfloat:
    case vk::Format::eR32G32B32A32Sfloat:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] std::filesystem::path makeScreenshotPath(
    const PresentScreenshotConfig& config,
    std::uint64_t sequence)
{
    auto outputDirectory = config.outputDirectory.empty()
                               ? std::filesystem::path{"screenshots"}
                               : config.outputDirectory;
    auto filePrefix = config.filePrefix.empty()
                          ? std::string{"screenshot"}
                          : config.filePrefix;
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    return outputDirectory / std::format("{}-{}-{}.exr", filePrefix, timestamp, sequence);
}

[[nodiscard]] nr::renderer::GraphResourceHandle makeTransparentUiFallback(
    nr::renderer::NodeBuildContext& context,
    vk::Extent2D extent)
{
    auto fallback = context.addResource(nr::renderer::GraphTransientImageDesc{
        .debugName = "Present.TransparentUiFallback",
        .extent = vk::Extent3D{extent.width, extent.height, 1u},
        .format = vk::Format::eR8G8B8A8Unorm,
        .usageIntents = {
            nr::renderer::ImageUsageIntent::TransferDst,
            nr::renderer::ImageUsageIntent::Sampled,
        },
    });

    [[maybe_unused]] auto clearPassHandle = nr::renderer::ops::clearColorImage(
        context,
        "Present.ClearTransparentUiFallback",
        nr::renderer::ops::ClearColorImagePassDesc{
            .image = fallback,
            .value = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}},
        });

    return fallback;
}

void ensureConvertedColorImage(
    nr::rhi::Device& device,
    PresentRuntimeCache& runtime,
    vk::Extent2D extent,
    vk::Format format)
{
    if (runtime.allocatedExtent == extent &&
        runtime.allocatedFormat == format &&
        runtime.convertedColorImage.valid())
    {
        return;
    }

    auto imageInfo = nr::rhi::makeImageCreateInfo(
        format,
        extent,
        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc);

    runtime.convertedColorImage = device.resourceFactory.createImage(
        imageInfo,
        nr::rhi::MemoryUsage::GpuOnly,
        "Present.ConvertedColor");
    nr::nrAssert(runtime.convertedColorImage.valid(), "Present failed to allocate convertedColor image.");
    runtime.convertedColorState.reset();

    runtime.allocatedExtent = extent;
    runtime.allocatedFormat = format;
}

void ensureScreenshotReadbackBuffer(
    nr::rhi::Device& device,
    nr::rhi::Buffer& buffer,
    vk::DeviceSize requiredBytes)
{
    if (buffer.valid() && buffer.size() >= requiredBytes)
    {
        return;
    }

    auto bufferInfo = nr::rhi::makeBufferCreateInfo(
        requiredBytes,
        vk::BufferUsageFlagBits::eTransferDst);
    buffer = device.resourceFactory.createBuffer(
        bufferInfo,
        nr::rhi::MemoryUsage::GpuToCpu,
        "Present.ScreenshotReadback");
    nr::nrAssert(buffer.valid(), "Present failed to allocate screenshot readback buffer.");
}

void addPresentReadbackCopyPass(
    nr::renderer::NodeBuildContext& context,
    nr::renderer::GraphResourceHandle sourceImage,
    PresentReadbackTarget readbackTarget,
    vk::Extent2D extent,
    vk::Format format,
    std::string_view bufferDebugName,
    std::string_view passDebugName)
{
    nr::nrAssert(
        context.queue == nr::renderer::QueueDomain::Compute,
        "Present readback copy must be recorded by a Present node running on the compute queue.");

    auto const requiredReadbackBytes = presentReadbackByteSize(extent, format);
    auto const& readbackBuffer = readbackTarget.buffer.get();
    nr::nrAssert(readbackBuffer.valid(), "Present readback target requires a valid buffer.");
    nr::nrAssert(
        (readbackBuffer.usage() & vk::BufferUsageFlagBits::eTransferDst) != vk::BufferUsageFlags{},
        "Present readback target buffer must include eTransferDst usage.");
    nr::nrAssert(
        readbackBuffer.mapped() != nullptr,
        "Present readback target buffer must be host visible and persistently mapped.");
    nr::nrAssert(
        readbackTarget.offset <= readbackBuffer.size() &&
            requiredReadbackBytes <= readbackBuffer.size() - readbackTarget.offset,
        std::format(
            "Present readback target range [{}..{}) exceeds buffer size {}.",
            readbackTarget.offset,
            readbackTarget.offset + requiredReadbackBytes,
            readbackBuffer.size()));

    auto readbackResource = context.importBuffer(
        readbackBuffer,
        bufferDebugName,
        nr::renderer::ResourceLifetime::RendererPersistent,
        {
            nr::renderer::BufferUsageIntent::Readback,
        },
        nr::renderer::ResourceOwnershipDomain::Compute);

    auto copyRegion = vk::BufferImageCopy{};
    copyRegion.bufferOffset = readbackTarget.offset;
    copyRegion.imageSubresource = vk::ImageSubresourceLayers{
        vk::ImageAspectFlagBits::eColor,
        0u,
        0u,
        1u,
    };
    copyRegion.imageExtent = vk::Extent3D{
        extent.width,
        extent.height,
        1u,
    };

    [[maybe_unused]] auto readbackPassHandle = nr::renderer::ops::copyImageToBuffer(
        context,
        passDebugName,
        nr::renderer::CopyImageToBufferPassDesc{
            .sourceImage = sourceImage,
            .destinationBuffer = readbackResource,
            .region = copyRegion,
            .imageAspect = nr::renderer::ImageAspectIntent::Color,
            .destinationIntent = nr::renderer::CopyBufferDestinationIntent::Readback,
            .destinationBufferRangeSize = requiredReadbackBytes,
        });
}

struct ExrHalfRgba
{
    nr::dependency::imath::Half r{};
    nr::dependency::imath::Half g{};
    nr::dependency::imath::Half b{};
    nr::dependency::imath::Half a{};
};

struct ExrFloatRgba
{
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

[[nodiscard]] std::uint8_t readUint8(std::span<const std::byte> bytes, std::size_t offset) noexcept
{
    return std::to_integer<std::uint8_t>(bytes[offset]);
}

[[nodiscard]] std::uint16_t readUint16(std::span<const std::byte> bytes, std::size_t offset) noexcept
{
    auto value = std::uint16_t{};
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

[[nodiscard]] float readFloat32(std::span<const std::byte> bytes, std::size_t offset) noexcept
{
    auto value = float{};
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

[[nodiscard]] nr::dependency::imath::Half halfFromBits(std::uint16_t bits) noexcept
{
    auto value = nr::dependency::imath::Half{};
    value.setBits(bits);
    return value;
}

[[nodiscard]] nr::dependency::imath::Half halfFromFloat(float value) noexcept
{
    return nr::dependency::imath::Half{value};
}

[[nodiscard]] float unorm8ToFloat(std::uint8_t value) noexcept
{
    return static_cast<float>(value) / 255.0f;
}

[[nodiscard]] float srgbToLinear(float value)
{
    if (value <= 0.04045f)
    {
        return value / 12.92f;
    }
    return std::pow((value + 0.055f) / 1.055f, 2.4f);
}

[[nodiscard]] ExrHalfRgba loadHalfRgba16(std::span<const std::byte> bytes, std::size_t offset) noexcept
{
    return ExrHalfRgba{
        .r = halfFromBits(readUint16(bytes, offset + 0u)),
        .g = halfFromBits(readUint16(bytes, offset + 2u)),
        .b = halfFromBits(readUint16(bytes, offset + 4u)),
        .a = halfFromBits(readUint16(bytes, offset + 6u)),
    };
}

[[nodiscard]] ExrFloatRgba loadFloatRgba32(std::span<const std::byte> bytes, std::size_t offset) noexcept
{
    return ExrFloatRgba{
        .r = readFloat32(bytes, offset + 0u),
        .g = readFloat32(bytes, offset + 4u),
        .b = readFloat32(bytes, offset + 8u),
        .a = readFloat32(bytes, offset + 12u),
    };
}

[[nodiscard]] ExrHalfRgba loadUnorm8Rgba(std::span<const std::byte> bytes, std::size_t offset, vk::Format format)
{
    auto r = 0.0f;
    auto g = 0.0f;
    auto b = 0.0f;
    auto a = 0.0f;

    if (format == vk::Format::eB8G8R8A8Unorm || format == vk::Format::eB8G8R8A8Srgb)
    {
        b = unorm8ToFloat(readUint8(bytes, offset + 0u));
        g = unorm8ToFloat(readUint8(bytes, offset + 1u));
        r = unorm8ToFloat(readUint8(bytes, offset + 2u));
        a = unorm8ToFloat(readUint8(bytes, offset + 3u));
    }
    else
    {
        r = unorm8ToFloat(readUint8(bytes, offset + 0u));
        g = unorm8ToFloat(readUint8(bytes, offset + 1u));
        b = unorm8ToFloat(readUint8(bytes, offset + 2u));
        a = unorm8ToFloat(readUint8(bytes, offset + 3u));
    }

    if (format == vk::Format::eR8G8B8A8Srgb || format == vk::Format::eB8G8R8A8Srgb)
    {
        r = srgbToLinear(r);
        g = srgbToLinear(g);
        b = srgbToLinear(b);
    }

    return ExrHalfRgba{
        .r = halfFromFloat(r),
        .g = halfFromFloat(g),
        .b = halfFromFloat(b),
        .a = halfFromFloat(a),
    };
}

template <typename Pixel>
void insertRgbaSlices(
    nr::dependency::openexr::FrameBuffer& frameBuffer,
    nr::dependency::openexr::PixelType pixelType,
    std::vector<Pixel>& pixels,
    std::uint32_t width)
{
    nr::nrAssert(!pixels.empty(), "Present EXR write requires at least one pixel.");

    auto const xStride = sizeof(Pixel);
    auto const yStride = xStride * static_cast<std::size_t>(width);
    auto* base = pixels.data();
    frameBuffer.insert(
        "R",
        nr::dependency::openexr::Slice(
            pixelType,
            reinterpret_cast<char*>(&base->r),
            xStride,
            yStride));
    frameBuffer.insert(
        "G",
        nr::dependency::openexr::Slice(
            pixelType,
            reinterpret_cast<char*>(&base->g),
            xStride,
            yStride));
    frameBuffer.insert(
        "B",
        nr::dependency::openexr::Slice(
            pixelType,
            reinterpret_cast<char*>(&base->b),
            xStride,
            yStride));
    frameBuffer.insert(
        "A",
        nr::dependency::openexr::Slice(
            pixelType,
            reinterpret_cast<char*>(&base->a),
            xStride,
            yStride));
}

template <typename Pixel>
[[nodiscard]] bool writeOpenExrRgba(
    const std::filesystem::path& path,
    vk::Extent2D extent,
    nr::dependency::openexr::PixelType pixelType,
    std::vector<Pixel>& pixels)
{
    auto const pathString = path.string();
    try
    {
        auto header = nr::dependency::openexr::Header{
            static_cast<int>(extent.width),
            static_cast<int>(extent.height)};
        header.channels().insert("R", nr::dependency::openexr::Channel(pixelType));
        header.channels().insert("G", nr::dependency::openexr::Channel(pixelType));
        header.channels().insert("B", nr::dependency::openexr::Channel(pixelType));
        header.channels().insert("A", nr::dependency::openexr::Channel(pixelType));

        auto frameBuffer = nr::dependency::openexr::FrameBuffer{};
        insertRgbaSlices(frameBuffer, pixelType, pixels, extent.width);

        auto file = nr::dependency::openexr::OutputFile(pathString.c_str(), header);
        file.setFrameBuffer(frameBuffer);
        file.writePixels(static_cast<int>(extent.height));
        return true;
    }
    catch (const std::exception& error)
    {
        nr::nrInfo<nr::LogLevel::error>(std::format(
            "Present failed to write EXR screenshot '{}': {}",
            path.generic_string(),
            error.what()));
        return false;
    }
    catch (...)
    {
        nr::nrInfo<nr::LogLevel::error>(std::format(
            "Present failed to write EXR screenshot '{}': unknown OpenEXR error.",
            path.generic_string()));
        return false;
    }
}

template <typename Pixel, typename LoadPixel>
[[nodiscard]] std::vector<Pixel> decodeScreenshotPixels(
    std::span<const std::byte> bytes,
    vk::Extent2D extent,
    vk::DeviceSize bytesPerPixel,
    bool flipY,
    LoadPixel loadPixel)
{
    auto const width = static_cast<std::size_t>(extent.width);
    auto pixels = std::vector<Pixel>(width * static_cast<std::size_t>(extent.height));

    auto rows = std::views::iota(std::uint32_t{0}, extent.height);
    std::ranges::for_each(rows, [&](std::uint32_t y) {
        auto const sourceY = flipY ? extent.height - 1u - y : y;
        auto columns = std::views::iota(std::uint32_t{0}, extent.width);
        std::ranges::for_each(columns, [&](std::uint32_t x) {
            auto const destinationIndex = static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x);
            auto const sourceIndex = static_cast<std::size_t>(sourceY) * width + static_cast<std::size_t>(x);
            auto const sourceOffset = sourceIndex * static_cast<std::size_t>(bytesPerPixel);
            pixels[destinationIndex] = loadPixel(bytes, sourceOffset);
        });
    });

    return pixels;
}

[[nodiscard]] bool writeLinearScreenshotExr(
    const std::filesystem::path& path,
    vk::Extent2D extent,
    vk::Format format,
    std::span<const std::byte> bytes,
    bool flipY)
{
    nr::nrAssert(extent.width > 0u && extent.height > 0u, "Present EXR screenshot requires a non-empty extent.");
    auto const expectedByteSize = presentReadbackByteSize(extent, format);
    nr::nrAssert(
        bytes.size_bytes() >= expectedByteSize,
        "Present EXR screenshot readback payload is smaller than the expected image size.");

    switch (format)
    {
    case vk::Format::eR16G16B16A16Sfloat:
    {
        auto pixels = decodeScreenshotPixels<ExrHalfRgba>(
            bytes,
            extent,
            presentReadbackBytesPerPixel(format),
            flipY,
            [](std::span<const std::byte> payload, std::size_t offset) {
                return loadHalfRgba16(payload, offset);
            });
        return writeOpenExrRgba(path, extent, nr::dependency::openexr::halfPixelType, pixels);
    }
    case vk::Format::eR32G32B32A32Sfloat:
    {
        auto pixels = decodeScreenshotPixels<ExrFloatRgba>(
            bytes,
            extent,
            presentReadbackBytesPerPixel(format),
            flipY,
            [](std::span<const std::byte> payload, std::size_t offset) {
                return loadFloatRgba32(payload, offset);
            });
        return writeOpenExrRgba(path, extent, nr::dependency::openexr::floatPixelType, pixels);
    }
    case vk::Format::eR8G8B8A8Unorm:
    case vk::Format::eR8G8B8A8Srgb:
    case vk::Format::eB8G8R8A8Unorm:
    case vk::Format::eB8G8R8A8Srgb:
    {
        auto pixels = decodeScreenshotPixels<ExrHalfRgba>(
            bytes,
            extent,
            presentReadbackBytesPerPixel(format),
            flipY,
            [format](std::span<const std::byte> payload, std::size_t offset) {
                return loadUnorm8Rgba(payload, offset, format);
            });
        return writeOpenExrRgba(path, extent, nr::dependency::openexr::halfPixelType, pixels);
    }
    default:
        nr::nrInfo<nr::LogLevel::error>(std::format(
            "Present EXR screenshot unsupported source format: {}.",
            vk::to_string(format)));
        return false;
    }
}
} // namespace nr::renderPasses::detail

namespace nr::renderPasses
{
PresentNode::~PresentNode() = default;

NodeDescription PresentNode::describe() const
{
    return NodeDescription{
        .name = "Present",
    };
}

void PresentNode::initialize(NodeInitContext& context)
{
    device_ = context.device;
    runtime_ = detail::ensurePresentRuntime(context.device.get());
    nr::rhi::setPipelineDebugName(
        context.device.get().device,
        runtime_->pipeline->pipeline().raw(),
        describe().name + ".Pipeline");
}

void PresentNode::build(NodeBuildContext& context, const NodeFrameParameters& frameParameters)
{
    nr::nrAssert(static_cast<bool>(runtime_), "Present build stage requires initialized runtime state.");
    nr::nrAssert(device_.has_value(), "Present build stage requires device reference from initialize stage.");
    processCompletedScreenshot(frameParameters.frameIndex);

    auto sourceColor = context.requireFrameResource(nr::renderer::frameResource::presentSourceColor, "Present");

    auto viewportExtent = frameParameters.swapchainExtent;

    auto swapchainFormat = frameParameters.swapchainFormat == vk::Format::eUndefined
                               ? input.format
                               : frameParameters.swapchainFormat;
    auto swapchainColorSpace = frameParameters.swapchainColorSpace;
    auto formatConversion = detail::resolvePresentFormatConversion(swapchainFormat, swapchainColorSpace);
    nr::nrAssert(
        formatConversion.has_value(),
        std::format(
            "Present node only supports SDR RGBA8/BGRA8, HDR10 A2R/A2B10G10B10, or scRGB R16G16B16A16 swapchain output. got format={} colorSpace={}",
            vk::to_string(swapchainFormat),
            vk::to_string(swapchainColorSpace)));

    detail::ensureConvertedColorImage(device_->get(), *runtime_, viewportExtent, formatConversion->convertedFormat);

    auto convertedColor = context.importRetainedStorageColor(
        runtime_->convertedColorImage,
        runtime_->convertedColorState,
        "Present.ConvertedColor",
        viewportExtent,
        formatConversion->convertedFormat);

    auto swapchainFrameParameters = frameParameters;
    swapchainFrameParameters.swapchainExtent = viewportExtent;
    swapchainFrameParameters.swapchainFormat = swapchainFormat;
    auto swapchainImage = context.importSwapchain("Swapchain.Image", swapchainFrameParameters);

    auto conversionExtent = vk::Extent2D{
        std::max(1u, viewportExtent.width),
        std::max(1u, viewportExtent.height),
    };

    auto uiBuffer = context.resolveFrameResource(nr::renderer::frameResource::uiColor);
    auto const hasUiBuffer = uiBuffer.valid();
    if (!hasUiBuffer)
    {
        uiBuffer = detail::makeTransparentUiFallback(context, conversionExtent);
    }

    auto pushConstants = detail::PresentConvertPushConstants{
        .width = conversionExtent.width,
        .height = conversionExtent.height,
        .swizzleBgr = formatConversion->swizzleBgr ? 1u : 0u,
        .outputEncoding = formatConversion->outputEncoding,
        .toneMapping = detail::resolveToneMappingMethod(toneMappingSelection_, swapchainColorSpace),
        .flipY = input.flipY ? 1u : 0u,
        .uiOpacity = hasUiBuffer ? std::clamp(input.uiOpacity, 0.0f, 1.0f) : 0.0f,
    };

    auto const captureScreenshot = screenshotRequestCount_ > 0u && !screenshotPendingSave_.has_value();
    if (captureScreenshot)
    {
        --screenshotRequestCount_;
        auto sourceDesc = context.describeImageResource(sourceColor);
        if (!sourceDesc.has_value() || sourceDesc->aspect != nr::renderer::ImageAspectIntent::Color)
        {
            screenshotStatus_ = "Screenshot failed: source metadata unavailable";
            nr::nrInfo<nr::LogLevel::error>(
                "Present EXR screenshot requires color-image metadata for frameResource::presentSourceColor.");
        }
        else if (!detail::supportsLinearExrScreenshotFormat(sourceDesc->format))
        {
            screenshotStatus_ = "Screenshot failed: unsupported source format";
            nr::nrInfo<nr::LogLevel::error>(std::format(
                "Present EXR screenshot unsupported source format for '{}': {}.",
                sourceDesc->debugName,
                vk::to_string(sourceDesc->format)));
        }
        else
        {
            auto const screenshotExtent = vk::Extent2D{
                std::max(1u, sourceDesc->extent.width),
                std::max(1u, sourceDesc->extent.height),
            };
            auto const screenshotByteSize = detail::presentReadbackByteSize(screenshotExtent, sourceDesc->format);
            detail::ensureScreenshotReadbackBuffer(device_->get(), screenshotReadbackBuffer_, screenshotByteSize);

            detail::addPresentReadbackCopyPass(
                context,
                sourceColor,
                PresentReadbackTarget{
                    .buffer = std::cref(screenshotReadbackBuffer_),
                },
                screenshotExtent,
                sourceDesc->format,
                "Present.ScreenshotReadbackBuffer",
                "Present.CopyScreenshotToReadback");

            ++screenshotSequence_;
            screenshotPendingSave_ = detail::PresentScreenshotPendingSave{
                .extent = screenshotExtent,
                .format = sourceDesc->format,
                .byteSize = screenshotByteSize,
                .path = detail::makeScreenshotPath(input.screenshot, screenshotSequence_),
                .frameSlot = frameParameters.frameIndex,
                .flipY = input.flipY,
            };
            screenshotStatus_ = std::format("Saving {}", screenshotPendingSave_->path.generic_string());
        }
    }

    auto convertPass = nr::renderer::ComputePassBuilder{
        context,
        "Present.Convert",
        runtime_->pipeline};
    convertPass
        .sampledImage("gSourceColor", sourceColor, "Present.SourceColor")
        .sampledImage("gUiColor", uiBuffer, "Present.UiBuffer")
        .storageImage("gConvertedColor", convertedColor, "Present.ConvertedColor")
        .pushConstants("gPresentConvert", pushConstants)
        .record([conversionExtent](const nr::renderer::ComputePassRecordContext& computeContext) {
            constexpr auto kThreadGroupSize = 16u;
            computeContext.commandBuffer.dispatch(
                detail::divideRoundUp(conversionExtent.width, kThreadGroupSize),
                detail::divideRoundUp(conversionExtent.height, kThreadGroupSize),
                1u);
        });

    [[maybe_unused]] auto convertPassHandle = convertPass.build();

    if (input.readback.has_value())
    {
        detail::addPresentReadbackCopyPass(
            context,
            convertedColor,
            *input.readback,
            conversionExtent,
            formatConversion->convertedFormat,
            "Present.ReadbackBuffer",
            "Present.CopyToReadback");
    }

    [[maybe_unused]] auto copyPassHandle = nr::renderer::ops::copyImageToImage(
        context,
        "Present.CopyToSwapchain",
        nr::renderer::CopyImageToImagePassDesc{
            .source = convertedColor,
            .destination = swapchainImage,
            .presentDestination = true,
        });

    context.publishFrameResource(nr::renderer::frameResource::swapchainImage, swapchainImage);
}

void PresentNode::collectUi(NodeUiBuildContext& context, const NodeFrameParameters& frameParameters)
{
    if (pendingScreenshotRequestCount_ > 0u)
    {
        screenshotRequestCount_ += pendingScreenshotRequestCount_;
        pendingScreenshotRequestCount_ = 0u;
        screenshotStatus_ = "Screenshot queued";
    }

    if (pendingUiOpacityValid_)
    {
        input.uiOpacity = std::clamp(pendingUiOpacity_, 0.0f, 1.0f);
        pendingUiOpacityValid_ = false;
    }

    if (pendingToneMappingSelectionValid_)
    {
        toneMappingSelection_ = std::min(
            pendingToneMappingSelection_,
            static_cast<std::uint32_t>(detail::kToneMappingSelectionLabels.size() - 1u));
        pendingToneMappingSelectionValid_ = false;
    }

    auto const autoMethod = detail::defaultToneMappingForColorSpace(frameParameters.swapchainColorSpace);

    uiOpacityDraft_ = std::clamp(input.uiOpacity, 0.0f, 1.0f);
    context.addSection(
        context.runtimeName(),
        [this, autoMethod](NodeUiWriter& ui) {
            auto const selection = toneMappingSelection_;
            auto const autoLabel = detail::kToneMappingSelectionLabels[autoMethod + 1u];
            auto const previewText = selection == 0u
                                         ? std::format("Auto ({})", autoLabel)
                                         : std::string(detail::kToneMappingSelectionLabels[selection]);
            if (ui.beginCombo("Tone Mapping", previewText))
            {
                auto indices = std::views::iota(std::size_t{0}, detail::kToneMappingSelectionLabels.size());
                std::ranges::for_each(indices, [&](std::size_t index) {
                    auto const optionLabel = index == 0u
                                                 ? std::format("Auto ({})", autoLabel)
                                                 : std::string(detail::kToneMappingSelectionLabels[index]);
                    if (ui.selectable(optionLabel, index == static_cast<std::size_t>(selection)))
                    {
                        pendingToneMappingSelection_ = static_cast<std::uint32_t>(index);
                        pendingToneMappingSelectionValid_ = true;
                    }
                });
                ui.endCombo();
            }

            auto value = uiOpacityDraft_;
            if (ui.sliderFloat("UI Opacity", value, 0.0f, 1.0f))
            {
                uiOpacityDraft_ = std::clamp(value, 0.0f, 1.0f);
                pendingUiOpacity_ = uiOpacityDraft_;
                pendingUiOpacityValid_ = true;
            }

            if (ui.button("Screenshot"))
            {
                ++pendingScreenshotRequestCount_;
            }
            if (!screenshotStatus_.empty())
            {
                ui.text(screenshotStatus_);
            }
        },
        true,
        "controls");
}

void PresentNode::processCompletedScreenshot(std::uint32_t frameSlot)
{
    if (!screenshotPendingSave_.has_value() || screenshotPendingSave_->frameSlot != frameSlot)
    {
        return;
    }

    savePendingScreenshot();
}

void PresentNode::savePendingScreenshot()
{
    if (!screenshotPendingSave_.has_value())
    {
        return;
    }

    auto const pending = *screenshotPendingSave_;
    nr::nrAssert(screenshotReadbackBuffer_.valid(), "Present screenshot save requires a valid readback buffer.");
    nr::nrAssert(
        screenshotReadbackBuffer_.mapped() != nullptr,
        "Present screenshot save requires a persistently mapped readback buffer.");
    nr::nrAssert(
        pending.byteSize <= screenshotReadbackBuffer_.size(),
        "Present screenshot pending save exceeds readback buffer size.");

    screenshotReadbackBuffer_.invalidate(0, pending.byteSize);

    auto const parentPath = pending.path.parent_path();
    if (!parentPath.empty())
    {
        auto directoryError = std::error_code{};
        std::filesystem::create_directories(parentPath, directoryError);
        if (directoryError)
        {
            screenshotStatus_ = std::format(
                "Screenshot failed: {}",
                directoryError.message());
            nr::nrInfo<nr::LogLevel::error>(std::format(
                "Present failed to create screenshot directory '{}': {}",
                parentPath.generic_string(),
                directoryError.message()));
            screenshotPendingSave_.reset();
            return;
        }
    }

    nr::nrAssert(
        pending.extent.width <= static_cast<std::uint32_t>(std::numeric_limits<int>::max()) &&
            pending.extent.height <= static_cast<std::uint32_t>(std::numeric_limits<int>::max()),
        "Present EXR screenshot dimensions exceed OpenEXR int limits.");
    nr::nrAssert(
        pending.byteSize <= static_cast<vk::DeviceSize>(std::numeric_limits<std::size_t>::max()),
        "Present EXR screenshot byte size exceeds std::size_t range.");

    auto const* pixels = static_cast<const std::byte*>(screenshotReadbackBuffer_.mapped());
    auto const bytes = std::span<const std::byte>{pixels, static_cast<std::size_t>(pending.byteSize)};
    auto const writeResult = detail::writeLinearScreenshotExr(
        pending.path,
        pending.extent,
        pending.format,
        bytes,
        pending.flipY);
    if (!writeResult)
    {
        screenshotStatus_ = "Screenshot failed";
        nr::nrInfo<nr::LogLevel::error>(std::format(
            "Present failed to write EXR screenshot '{}'.",
            pending.path.generic_string()));
        screenshotPendingSave_.reset();
        return;
    }

    screenshotStatus_ = std::format("Saved {}", pending.path.generic_string());
    nr::nrInfo(std::format("Present saved screenshot '{}'.", pending.path.generic_string()));
    screenshotPendingSave_.reset();
}

void PresentNode::shutdown(NodeShutdownContext&)
{
    savePendingScreenshot();

    if (runtime_ && runtime_->pipeline)
    {
        runtime_->pipeline->clearBindingSets();
    }
    runtime_.reset();
    device_.reset();
    screenshotReadbackBuffer_ = {};
    screenshotPendingSave_.reset();
}
} // namespace nr::renderPasses
