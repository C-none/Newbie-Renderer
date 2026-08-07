module;
#include <cstddef>

module nr.renderPasses;
import dependency.assets;
import dependency.vulkan;

import :presentNode;
import nr.options;
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
    float uiOpacity = 1.0f;
};

static_assert(std::is_standard_layout_v<PresentConvertPushConstants>);
static_assert(sizeof(PresentConvertPushConstants) == 24u);
static_assert(offsetof(PresentConvertPushConstants, width) == 0u);
static_assert(offsetof(PresentConvertPushConstants, height) == 4u);
static_assert(offsetof(PresentConvertPushConstants, swizzleBgr) == 8u);
static_assert(offsetof(PresentConvertPushConstants, outputEncoding) == 12u);
static_assert(offsetof(PresentConvertPushConstants, toneMapping) == 16u);
static_assert(offsetof(PresentConvertPushConstants, uiOpacity) == 20u);

inline constexpr std::uint32_t kOutputEncodingLinear = 0u;
inline constexpr std::uint32_t kOutputEncodingSrgb = 1u;
inline constexpr std::uint32_t kOutputEncodingHdr10Pq = 2u;
inline constexpr std::uint32_t kOutputEncodingScRgb = 3u;
inline constexpr std::uint32_t kPresentThreadGroupSize = 16u;

inline constexpr std::uint32_t kToneMappingNone = 0u;
inline constexpr std::uint32_t kToneMappingReinhard = 1u;
inline constexpr std::uint32_t kToneMappingAcesFilmic = 2u;
inline constexpr std::uint32_t kToneMappingBt2390 = 3u;

template <typename T>
[[nodiscard]] const T &requiredOption(const nr::options::OptionFrameSnapshot &snapshot, nr::options::OptionKey<T> key)
{
    auto const *value = snapshot.find(key);
    nr::nrAssert(value != nullptr, "Present requires option '{}' in the frame snapshot.", key.id());
    return *value;
}

[[nodiscard]] std::uint32_t toneMappingSelection(const nr::options::OptionFrameSnapshot &snapshot)
{
    auto const &value = requiredOption(snapshot, nr::options::keys::presentToneMapping);
    if (value == "auto")
    {
        return 0u;
    }
    if (value == "none")
    {
        return 1u;
    }
    if (value == "reinhard")
    {
        return 2u;
    }
    if (value == "aces_filmic")
    {
        return 3u;
    }
    nr::nrAssert(value == "bt2390_eetf", "Present snapshot contains an invalid tone-mapping value.");
    return 4u;
}

[[nodiscard]] float uiOpacity(const nr::options::OptionFrameSnapshot &snapshot)
{
    return static_cast<float>(requiredOption(snapshot, nr::options::keys::presentUiOpacity));
}

[[nodiscard]] bool hasCaptureEffect(const nr::options::OptionFrameSnapshot &snapshot)
{
    return snapshot.effect.has_value() &&
           snapshot.effect->id == nr::options::optionId(nr::options::keys::presentCaptureExr);
}

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

struct PresentScreenshotPendingSave
{
    vk::Extent2D extent{1u, 1u};
    vk::Format format = vk::Format::eUndefined;
    vk::DeviceSize byteSize = 0;
    std::filesystem::path path{};
    std::uint32_t frameSlot = 0;
    std::uint64_t frameIndex = 0;
    nr::options::FrameEffect effect{};
};

struct PresentScreenshotPrepared
{
    vk::Extent2D extent{1u, 1u};
    vk::Format format = vk::Format::eUndefined;
    vk::DeviceSize byteSize = 0;
    std::filesystem::path path{};
    std::uint64_t frameIndex = 0;
    nr::options::FrameEffect effect{};
};

struct PresentRuntimeState
{
    explicit PresentRuntimeState(nr::rhi::Device &requiredDevice) noexcept : device(requiredDevice)
    {
    }

    nr::rhi::Device &device;
    std::shared_ptr<nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>> pipeline{};
    nr::rhi::Image convertedColorImage{};
    nr::renderer::RetainedImageState convertedColorState{};
    vk::Extent2D allocatedExtent{0, 0};
    vk::Format allocatedFormat = vk::Format::eUndefined;
    nr::rhi::Buffer screenshotReadbackBuffer{};
    std::optional<PresentScreenshotPrepared> screenshotPrepared{};
    std::optional<PresentScreenshotPendingSave> screenshotPendingSave{};
};

[[nodiscard]] std::optional<PresentFormatConversion> resolvePresentFormatConversion(vk::Format format,
                                                                                    vk::ColorSpaceKHR colorSpace)
{
    switch (format)
    {
    case vk::Format::eB8G8R8A8Srgb:
    case vk::Format::eB8G8R8A8Unorm:
    case vk::Format::eR8G8B8A8Srgb:
    case vk::Format::eR8G8B8A8Unorm:
        if (colorSpace != vk::ColorSpaceKHR::eSrgbNonlinear)
        {
            return std::nullopt;
        }
        return PresentFormatConversion{
            .swizzleBgr = format == vk::Format::eB8G8R8A8Srgb || format == vk::Format::eB8G8R8A8Unorm,
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

[[nodiscard]] std::unique_ptr<PresentRuntimeState> makePresentRuntime(nr::rhi::Device &device,
                                                                      const nr::rhi::SlangProgram &program,
                                                                      std::string debugName)
{
    auto runtime = std::make_unique<PresentRuntimeState>(device);
    runtime->pipeline = std::make_shared<nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>>();
    runtime->pipeline->initialize(device.pipeline().createComputePipeline(program, {}, 64u, {}, std::move(debugName)));

    return runtime;
}

[[nodiscard]] std::uint32_t divideRoundUp(std::uint32_t value, std::uint32_t divisor)
{
    nr::nrAssert(divisor > 0u, "divideRoundUp requires divisor > 0.");
    return value / divisor + static_cast<std::uint32_t>(value % divisor != 0u);
}

[[nodiscard]] vk::DeviceSize presentReadbackTexelBlockByteSize(vk::Format format) noexcept
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
        nr::nrAssert(false, "Present readback unsupported format for size estimation: {}", vk::to_string(format));
        return 0u;
    }
}

[[nodiscard]] vk::DeviceSize checkedPresentReadbackByteSize(vk::Extent2D extent,
                                                            vk::DeviceSize texelBlockByteSize) noexcept
{
    auto const width = static_cast<vk::DeviceSize>(extent.width);
    auto const height = static_cast<vk::DeviceSize>(extent.height);
    constexpr auto maximum = std::numeric_limits<vk::DeviceSize>::max();
    nr::nrAssert(width == 0u || height <= maximum / width,
                 "Present readback extent multiplication overflows vk::DeviceSize.");
    auto const texelCount = width * height;
    nr::nrAssert(texelCount == 0u || texelBlockByteSize <= maximum / texelCount,
                 "Present readback byte-size multiplication overflows vk::DeviceSize.");
    return texelCount * texelBlockByteSize;
}

[[nodiscard]] vk::DeviceSize presentReadbackByteSize(vk::Extent2D extent, vk::Format format) noexcept
{
    return checkedPresentReadbackByteSize(extent, presentReadbackTexelBlockByteSize(format));
}

[[nodiscard]] vk::DeviceSize validatePresentReadbackTarget(const PresentReadbackTarget &target, vk::Extent2D extent,
                                                           vk::Format format)
{
    auto const texelBlockByteSize = presentReadbackTexelBlockByteSize(format);
    auto const requiredBytes = checkedPresentReadbackByteSize(extent, texelBlockByteSize);
    auto const &buffer = target.buffer.get();
    nr::nrAssert(buffer.valid(), "Present readback target requires a valid buffer.");
    nr::nrAssert((buffer.usage() & vk::BufferUsageFlagBits::eTransferDst) != vk::BufferUsageFlags{},
                 "Present readback target buffer must include eTransferDst usage.");
    nr::nrAssert(buffer.mapped() != nullptr,
                 "Present readback target buffer must be host visible and persistently mapped.");
    nr::nrAssert(target.offset % texelBlockByteSize == 0u, "Present readback target offset {} is not aligned to the format's {}-byte texel block.",
                             target.offset, texelBlockByteSize);
    constexpr auto maximum = std::numeric_limits<vk::DeviceSize>::max();
    nr::nrAssert(requiredBytes <= maximum - target.offset,
                 "Present readback target range end overflows vk::DeviceSize.");
    auto const rangeEnd = target.offset + requiredBytes;
    nr::nrAssert(target.offset <= buffer.size() && rangeEnd <= buffer.size(), "Present readback target range [{}..{}) exceeds buffer size {}.", target.offset, rangeEnd,
                             buffer.size());
    return requiredBytes;
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

[[nodiscard]] std::filesystem::path makeScreenshotPath(const PresentScreenshotConfig &config, std::uint64_t sequence,
                                                       std::uint64_t frameIndex)
{
    auto outputDirectory =
        config.outputDirectory.empty() ? std::filesystem::path{"screenshots"} : config.outputDirectory;
    auto const sessionId = config.sessionId.empty() ? std::string{"session"} : config.sessionId;
    return outputDirectory / sessionId / std::format("capture_{}_frame_{}.exr", sequence, frameIndex);
}

void ensureConvertedColorImage(nr::rhi::Device &device, PresentRuntimeState &runtime, vk::Extent2D extent,
                               vk::Format format)
{
    if (runtime.allocatedExtent == extent && runtime.allocatedFormat == format && runtime.convertedColorImage.valid())
    {
        return;
    }

    auto imageInfo = nr::rhi::makeImageCreateInfo(
        format, extent, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc);

    runtime.convertedColorImage =
        device.resourceFactory.createImage(imageInfo, nr::rhi::MemoryUsage::GpuOnly, "Present.ConvertedColor");
    nr::nrAssert(runtime.convertedColorImage.valid(), "Present failed to allocate convertedColor image.");
    runtime.convertedColorState.reset();

    runtime.allocatedExtent = extent;
    runtime.allocatedFormat = format;
}

void ensureScreenshotReadbackBuffer(nr::rhi::Device &device, nr::rhi::Buffer &buffer, vk::DeviceSize requiredBytes)
{
    if (buffer.valid() && buffer.size() >= requiredBytes)
    {
        return;
    }

    auto bufferInfo = nr::rhi::makeBufferCreateInfo(requiredBytes, vk::BufferUsageFlagBits::eTransferDst);
    buffer =
        device.resourceFactory.createBuffer(bufferInfo, nr::rhi::MemoryUsage::GpuToCpu, "Present.ScreenshotReadback");
    nr::nrAssert(buffer.valid(), "Present failed to allocate screenshot readback buffer.");
}

[[nodiscard]] nr::renderer::GraphPassHandle addPresentReadbackCopyPass(nr::renderer::NodeBuildContext &context,
                                                                       nr::renderer::GraphResourceHandle sourceImage,
                                                                       PresentReadbackTarget readbackTarget,
                                                                       vk::Extent2D extent, vk::Format format,
                                                                       std::string_view bufferDebugName,
                                                                       std::string_view passDebugName)
{
    nr::nrAssert(context.queue == nr::renderer::QueueDomain::Compute,
                 "Present readback copy must be recorded by a Present node running on the compute queue.");

    auto const requiredBytes = validatePresentReadbackTarget(readbackTarget, extent, format);
    auto const &readbackBuffer = readbackTarget.buffer.get();

    auto readbackResource =
        context.importBuffer(readbackBuffer, bufferDebugName, nr::renderer::ResourceLifetime::RendererPersistent,
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

    return nr::renderer::ops::copyImageToBuffer(
        context, passDebugName,
        nr::renderer::CopyImageToBufferPassDesc{
            .sourceImage = sourceImage,
            .destinationBuffer = readbackResource,
            .region = copyRegion,
            .imageAspect = nr::renderer::ImageAspectIntent::Color,
            .destinationIntent = nr::renderer::CopyBufferDestinationIntent::Readback,
            .destinationBufferRangeSize = requiredBytes,
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
void insertRgbaSlices(nr::dependency::openexr::FrameBuffer &frameBuffer, nr::dependency::openexr::PixelType pixelType,
                      std::vector<Pixel> &pixels, std::uint32_t width)
{
    nr::nrAssert(!pixels.empty(), "Present EXR write requires at least one pixel.");

    auto const xStride = sizeof(Pixel);
    auto const yStride = xStride * static_cast<std::size_t>(width);
    auto *base = pixels.data();
    frameBuffer.insert("R",
                       nr::dependency::openexr::Slice(pixelType, reinterpret_cast<char *>(&base->r), xStride, yStride));
    frameBuffer.insert("G",
                       nr::dependency::openexr::Slice(pixelType, reinterpret_cast<char *>(&base->g), xStride, yStride));
    frameBuffer.insert("B",
                       nr::dependency::openexr::Slice(pixelType, reinterpret_cast<char *>(&base->b), xStride, yStride));
    frameBuffer.insert("A",
                       nr::dependency::openexr::Slice(pixelType, reinterpret_cast<char *>(&base->a), xStride, yStride));
}

template <typename Pixel>
[[nodiscard]] bool writeOpenExrRgba(const std::filesystem::path &path, vk::Extent2D extent,
                                    nr::dependency::openexr::PixelType pixelType, std::vector<Pixel> &pixels)
{
    auto const pathString = path.string();
    try
    {
        auto header = nr::dependency::openexr::Header{static_cast<int>(extent.width), static_cast<int>(extent.height)};
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
    catch (const std::exception &error)
    {
        nr::nrLog<nr::LogLevel::warning, "LOG">("Present failed to write EXR screenshot '{}': {}", path.generic_string(), error.what());
        return false;
    }
    catch (...)
    {
        nr::nrLog<nr::LogLevel::warning, "LOG">("Present failed to write EXR screenshot '{}': unknown OpenEXR error.", path.generic_string());
        return false;
    }
}

template <typename Pixel, typename LoadPixel>
[[nodiscard]] std::vector<Pixel> decodeScreenshotPixels(std::span<const std::byte> bytes, vk::Extent2D extent,
                                                        vk::DeviceSize bytesPerPixel, LoadPixel loadPixel)
{
    auto const width = static_cast<std::size_t>(extent.width);
    auto pixels = std::vector<Pixel>(width * static_cast<std::size_t>(extent.height));

    auto rows = std::views::iota(std::uint32_t{0}, extent.height);
    std::ranges::for_each(rows, [&](std::uint32_t y) {
        auto columns = std::views::iota(std::uint32_t{0}, extent.width);
        std::ranges::for_each(columns, [&](std::uint32_t x) {
            auto const index = static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x);
            auto const sourceOffset = index * static_cast<std::size_t>(bytesPerPixel);
            pixels[index] = loadPixel(bytes, sourceOffset);
        });
    });

    return pixels;
}

[[nodiscard]] bool writeLinearScreenshotExr(const std::filesystem::path &path, vk::Extent2D extent, vk::Format format,
                                            std::span<const std::byte> bytes)
{
    nr::nrAssert(extent.width > 0u && extent.height > 0u, "Present EXR screenshot requires a non-empty extent.");
    auto const expectedByteSize = presentReadbackByteSize(extent, format);
    nr::nrAssert(bytes.size_bytes() >= expectedByteSize,
                 "Present EXR screenshot readback payload is smaller than the expected image size.");

    switch (format)
    {
    case vk::Format::eR16G16B16A16Sfloat: {
        auto pixels = decodeScreenshotPixels<ExrHalfRgba>(
            bytes, extent, presentReadbackTexelBlockByteSize(format),
            [](std::span<const std::byte> payload, std::size_t offset) { return loadHalfRgba16(payload, offset); });
        return writeOpenExrRgba(path, extent, nr::dependency::openexr::halfPixelType, pixels);
    }
    case vk::Format::eR32G32B32A32Sfloat: {
        auto pixels = decodeScreenshotPixels<ExrFloatRgba>(
            bytes, extent, presentReadbackTexelBlockByteSize(format),
            [](std::span<const std::byte> payload, std::size_t offset) { return loadFloatRgba32(payload, offset); });
        return writeOpenExrRgba(path, extent, nr::dependency::openexr::floatPixelType, pixels);
    }
    case vk::Format::eR8G8B8A8Unorm:
    case vk::Format::eR8G8B8A8Srgb:
    case vk::Format::eB8G8R8A8Unorm:
    case vk::Format::eB8G8R8A8Srgb: {
        auto pixels =
            decodeScreenshotPixels<ExrHalfRgba>(bytes, extent, presentReadbackTexelBlockByteSize(format),
                                                [format](std::span<const std::byte> payload, std::size_t offset) {
                                                    return loadUnorm8Rgba(payload, offset, format);
                                                });
        return writeOpenExrRgba(path, extent, nr::dependency::openexr::halfPixelType, pixels);
    }
    default:
        nr::nrLog<nr::LogLevel::warning, "LOG">("Present EXR screenshot unsupported source format: {}.", vk::to_string(format));
        return false;
    }
}
} // namespace nr::renderPasses::detail

namespace nr::renderPasses
{
PresentNode::PresentNode() = default;
PresentNode::~PresentNode() = default;

void PresentNode::declareOptions(nr::options::OptionCatalogBuilder &builder) const
{
    std::ranges::for_each(nr::options::makePresentDefinitions(), [&](nr::options::OptionDefinition definition) {
        static_cast<void>(builder.add(std::move(definition)));
    });
}

void PresentNode::collectOptionAvailability(const nr::options::OptionFrameSnapshot &,
                                            nr::options::OptionAvailabilityMap &availability) const
{
    auto const scalarIds = std::array{
        nr::options::optionId(nr::options::keys::presentToneMapping),
        nr::options::optionId(nr::options::keys::presentUiOpacity),
    };
    std::ranges::for_each(scalarIds, [&](const nr::options::OptionId &id) {
        availability.insert_or_assign(id, nr::options::OptionAvailability{.available = true, .reason = {}});
    });
    auto const captureAvailable =
        runtime_ && !runtime_->screenshotPrepared.has_value() && !runtime_->screenshotPendingSave.has_value();
    availability.insert_or_assign(nr::options::optionId(nr::options::keys::presentCaptureExr),
                                  captureAvailable
                                      ? nr::options::OptionAvailability{.available = true, .reason = {}}
                                      : nr::options::OptionAvailability{.available = false, .reason = "capture_busy"});
}

[[nodiscard]] std::vector<nr::rhi::SlangProgramCompileFileRequest> PresentNode::shaderRequests() const
{
    return {
        nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/presentConvert"},
        },
    };
}

void PresentNode::initialize(NodeInitContext &context)
{
    nr::nrAssert(context.shaderPrograms.size() == 1u && context.shaderPrograms.front().entryPoint() != nullptr &&
                     context.shaderPrograms.front().entryPoint()->stage == SLANG_STAGE_COMPUTE,
                 "Present initialization requires one compiled compute shader.");
    nr::nrAssert(!runtime_, "Present initialization must create runtime state exactly once.");
    runtime_ = detail::makePresentRuntime(context.device.get(), context.shaderPrograms.front(),
                                          context.runtimeName + ".Pipeline");
}

void PresentNode::finalizeInitialization()
{
    nr::nrAssert(runtime_ && runtime_->pipeline && runtime_->pipeline->valid(),
                 "Present async compute PSO construction failed.");
}

void PresentNode::build(NodeBuildContext &context, const NodeFrameParameters &frameParameters)
{
    materializeCurrentFrame(context, frameParameters);
}

void PresentNode::materializeCurrentFrame(NodeBuildContext &context, const NodeFrameParameters &frameParameters)
{
    nr::nrAssert(static_cast<bool>(runtime_), "Present build stage requires initialized runtime state.");
    auto &runtime = *runtime_;

    auto sourceColor = context.requireFrameResource(nr::renderer::frameResource::presentSourceColor, "Present");

    auto viewportExtent = frameParameters.swapchainExtent;

    auto swapchainFormat =
        frameParameters.swapchainFormat == vk::Format::eUndefined ? input.format : frameParameters.swapchainFormat;
    auto swapchainColorSpace = frameParameters.swapchainColorSpace;
    auto formatConversion = detail::resolvePresentFormatConversion(swapchainFormat, swapchainColorSpace);
    nr::nrAssert(formatConversion.has_value(), "Present node only supports SDR RGBA8/BGRA8, HDR10 A2R/A2B10G10B10, or scRGB R16G16B16A16 "
                             "swapchain output. got format={} colorSpace={}",
                             vk::to_string(swapchainFormat), vk::to_string(swapchainColorSpace));

    detail::ensureConvertedColorImage(runtime.device, runtime, viewportExtent, formatConversion->convertedFormat);

    auto convertedColor =
        context.importRetainedStorageColor(runtime.convertedColorImage, runtime.convertedColorState,
                                           "Present.ConvertedColor", viewportExtent, formatConversion->convertedFormat);

    auto swapchainFrameParameters = frameParameters;
    swapchainFrameParameters.swapchainExtent = viewportExtent;
    swapchainFrameParameters.swapchainFormat = swapchainFormat;
    auto swapchainImage = context.importSwapchain("Swapchain.Image", swapchainFrameParameters);

    auto conversionExtent = vk::Extent2D{
        std::max(1u, viewportExtent.width),
        std::max(1u, viewportExtent.height),
    };

    auto const resolvedUiBuffer = context.resolveFrameResource(nr::renderer::frameResource::uiColor);
    auto const hasUiBuffer = resolvedUiBuffer.valid();
    auto const uiBuffer = hasUiBuffer ? resolvedUiBuffer : sourceColor;

    auto const toneMapping = detail::toneMappingSelection(frameParameters.optionSnapshot.get());
    auto const opacity = detail::uiOpacity(frameParameters.optionSnapshot.get());
    auto pushConstants = detail::PresentConvertPushConstants{
        .width = conversionExtent.width,
        .height = conversionExtent.height,
        .swizzleBgr = formatConversion->swizzleBgr ? 1u : 0u,
        .outputEncoding = formatConversion->outputEncoding,
        .toneMapping = detail::resolveToneMappingMethod(toneMapping, swapchainColorSpace),
        .uiOpacity = hasUiBuffer ? opacity : 0.0f,
    };

    nr::nrAssert(!runtime.screenshotPrepared.has_value(),
                 "Present must finalize a provisional screenshot before building another frame.");
    if (detail::hasCaptureEffect(frameParameters.optionSnapshot.get()))
    {
        auto sourceDesc = context.describeImageResource(sourceColor);
        if (!sourceDesc.has_value() || sourceDesc->aspect != nr::renderer::ImageAspectIntent::Color)
        {
            nr::nrLog<nr::LogLevel::warning, "LOG">(
                "Present EXR screenshot requires color-image metadata for frameResource::presentSourceColor.");
        }
        else if (!detail::supportsLinearExrScreenshotFormat(sourceDesc->format))
        {
            nr::nrLog<nr::LogLevel::warning, "LOG">("Present EXR screenshot unsupported source format for '{}': {}.", sourceDesc->debugName,
                            vk::to_string(sourceDesc->format));
        }
        else
        {
            auto const screenshotExtent = vk::Extent2D{
                std::max(1u, sourceDesc->extent.width),
                std::max(1u, sourceDesc->extent.height),
            };
            auto const screenshotByteSize = detail::presentReadbackByteSize(screenshotExtent, sourceDesc->format);
            detail::ensureScreenshotReadbackBuffer(runtime.device, runtime.screenshotReadbackBuffer,
                                                   screenshotByteSize);

            auto const readbackPass = detail::addPresentReadbackCopyPass(
                context, sourceColor,
                PresentReadbackTarget{
                    .buffer = std::cref(runtime.screenshotReadbackBuffer),
                },
                screenshotExtent, sourceDesc->format, "Present.ScreenshotReadbackBuffer",
                "Present.CopyScreenshotToReadback");
            auto const &effect = *frameParameters.optionSnapshot.get().effect;
            runtime.screenshotPrepared = detail::PresentScreenshotPrepared{
                .extent = screenshotExtent,
                .format = sourceDesc->format,
                .byteSize = screenshotByteSize,
                .path = detail::makeScreenshotPath(input.screenshot, effect.sequence,
                                                   frameParameters.optionSnapshot.get().frameIndex),
                .frameIndex = frameParameters.optionSnapshot.get().frameIndex,
                .effect = effect,
            };
            nr::nrAssert(frameParameters.frameEffectSink.has_value() &&
                             frameParameters.frameEffectSink->get().claim(*this, readbackPass),
                         "Present capture effect must claim its image-to-readback copy pass exactly once.");
        }
    }

    auto convertPass = nr::renderer::ComputePassBuilder{context, "Present.Convert", runtime.pipeline};
    convertPass.sampledImage("gSourceColor", sourceColor, "Present.SourceColor")
        .sampledImage("gUiColor", uiBuffer, "Present.UiBuffer")
        .storageImage("gConvertedColor", convertedColor, "Present.ConvertedColor")
        .pushConstants("gPresentConvert", pushConstants)
        .record([conversionExtent](const nr::renderer::ComputePassRecordContext &computeContext) {
            computeContext.commandBuffer.dispatch(
                detail::divideRoundUp(conversionExtent.width, detail::kPresentThreadGroupSize),
                detail::divideRoundUp(conversionExtent.height, detail::kPresentThreadGroupSize), 1u);
        });

    [[maybe_unused]] auto convertPassHandle = convertPass.build();

    if (input.readback.has_value())
    {
        static_cast<void>(detail::addPresentReadbackCopyPass(context, convertedColor, *input.readback, conversionExtent,
                                                             formatConversion->convertedFormat,
                                                             "Present.ReadbackBuffer", "Present.CopyToReadback"));
    }

    [[maybe_unused]] auto acquireNodeHandle = context.addSwapchainAcquireNode("Present.AcquireSwapchainImage");

    [[maybe_unused]] auto copyPassHandle = nr::renderer::ops::copyImageToImage(context, "Present.CopyToSwapchain",
                                                                               nr::renderer::CopyImageToImagePassDesc{
                                                                                   .source = convertedColor,
                                                                                   .destination = swapchainImage,
                                                                                   .presentDestination = true,
                                                                               });

    context.publishFrameResource(nr::renderer::frameResource::swapchainImage, swapchainImage);
}

void PresentNode::advanceContinuations(std::uint32_t frameSlot)
{
    processCompletedScreenshot(frameSlot);
}

void PresentNode::flushContinuations()
{
    savePendingScreenshot();
}

[[nodiscard]] nr::renderer::FrameEffectFinalizeDisposition PresentNode::finalizeFrameEffect(
    const nr::options::FrameEffect &effect, bool targetBatchSubmitted, std::uint32_t frameSlot)
{
    nr::nrAssert(static_cast<bool>(runtime_), "Present frame-effect finalization requires initialized runtime state.");
    auto &runtime = *runtime_;
    if (effect.id != nr::options::optionId(nr::options::keys::presentCaptureExr) ||
        !runtime.screenshotPrepared.has_value() || runtime.screenshotPrepared->effect.sequence != effect.sequence)
    {
        runtime.screenshotPrepared.reset();
        return nr::renderer::FrameEffectFinalizeDisposition::terminalFailed;
    }
    if (!targetBatchSubmitted || runtime.screenshotPendingSave.has_value())
    {
        runtime.screenshotPrepared.reset();
        return nr::renderer::FrameEffectFinalizeDisposition::terminalFailed;
    }

    auto prepared = std::move(*runtime.screenshotPrepared);
    runtime.screenshotPrepared.reset();
    runtime.screenshotPendingSave = detail::PresentScreenshotPendingSave{
        .extent = prepared.extent,
        .format = prepared.format,
        .byteSize = prepared.byteSize,
        .path = std::move(prepared.path),
        .frameSlot = frameSlot,
        .frameIndex = prepared.frameIndex,
        .effect = std::move(prepared.effect),
    };
    return nr::renderer::FrameEffectFinalizeDisposition::continuationArmed;
}

void PresentNode::processCompletedScreenshot(std::uint32_t frameSlot)
{
    if (!runtime_ || !runtime_->screenshotPendingSave.has_value() ||
        runtime_->screenshotPendingSave->frameSlot != frameSlot)
    {
        return;
    }

    savePendingScreenshot();
}

void PresentNode::savePendingScreenshot()
{
    if (!runtime_ || !runtime_->screenshotPendingSave.has_value())
    {
        return;
    }

    auto &runtime = *runtime_;
    auto const pending = *runtime.screenshotPendingSave;
    auto emitTerminal = [&](nr::options::OptionLogStatus status, std::optional<std::string> reason = {}) {
        nr::options::emitMachineRecord(nr::options::OptionMachineRecord{
            .sequence = pending.effect.sequence,
            .id = pending.effect.id,
            .phase = nr::options::OptionLogPhase::terminal,
            .status = status,
            .frameIndex = pending.frameIndex,
            .origin = pending.effect.origin,
            .requestId = pending.effect.requestId,
            .reason = std::move(reason),
        });
    };
    nr::nrAssert(runtime.screenshotReadbackBuffer.valid(), "Present screenshot save requires a valid readback buffer.");
    nr::nrAssert(runtime.screenshotReadbackBuffer.mapped() != nullptr,
                 "Present screenshot save requires a persistently mapped readback buffer.");
    nr::nrAssert(pending.byteSize <= runtime.screenshotReadbackBuffer.size(),
                 "Present screenshot pending save exceeds readback buffer size.");

    runtime.screenshotReadbackBuffer.invalidate(0, pending.byteSize);

    auto const parentPath = pending.path.parent_path();
    if (!parentPath.empty())
    {
        auto directoryError = std::error_code{};
        std::filesystem::create_directories(parentPath, directoryError);
        if (directoryError)
        {
            nr::nrLog<nr::LogLevel::warning, "LOG">("Present failed to create screenshot directory '{}': {}",
                                                               parentPath.generic_string(), directoryError.message());
            emitTerminal(nr::options::OptionLogStatus::failed,
                         std::format("create_directory_failed:{}", directoryError.message()));
            runtime.screenshotPendingSave.reset();
            return;
        }
    }

    nr::nrAssert(pending.extent.width <= static_cast<std::uint32_t>(std::numeric_limits<int>::max()) &&
                     pending.extent.height <= static_cast<std::uint32_t>(std::numeric_limits<int>::max()),
                 "Present EXR screenshot dimensions exceed OpenEXR int limits.");
    nr::nrAssert(pending.byteSize <= static_cast<vk::DeviceSize>(std::numeric_limits<std::size_t>::max()),
                 "Present EXR screenshot byte size exceeds std::size_t range.");

    auto const *pixels = static_cast<const std::byte *>(runtime.screenshotReadbackBuffer.mapped());
    auto const bytes = std::span<const std::byte>{pixels, static_cast<std::size_t>(pending.byteSize)};
    auto const writeResult = detail::writeLinearScreenshotExr(pending.path, pending.extent, pending.format, bytes);
    if (!writeResult)
    {
        nr::nrLog<nr::LogLevel::warning, "LOG">("Present failed to write EXR screenshot '{}'.", pending.path.generic_string());
        emitTerminal(nr::options::OptionLogStatus::failed, "exr_write_failed");
        runtime.screenshotPendingSave.reset();
        return;
    }

    nr::nrLog<nr::LogLevel::info>("Present saved screenshot '{}'.", pending.path.generic_string());
    emitTerminal(nr::options::OptionLogStatus::succeeded);
    runtime.screenshotPendingSave.reset();
}

void PresentNode::shutdown(NodeShutdownContext &)
{
    if (runtime_ && runtime_->pipeline)
    {
        runtime_->pipeline->clearBindingSets();
    }
    runtime_.reset();
}
} // namespace nr::renderPasses
