module nr.load;

import dependency.assets;
import dependency.vulkan;
import nr.resource;
import nr.utils;
import std;
import :exr;
import :type;

namespace nr::load
{
namespace
{
inline constexpr std::size_t decodedChannelCount = 3u;
inline constexpr std::size_t encodedChannelCount = 4u;
inline constexpr std::uint32_t scanlineRowsPerWorker = 32u;

using Clock = std::chrono::steady_clock;
using Duration = std::chrono::duration<double, std::milli>;

struct OpenExrDecodeConfig
{
    std::uint32_t workerCount = 1u;
    std::uint32_t blockRowCount = scanlineRowsPerWorker;
};

[[nodiscard]] double elapsedMilliseconds(Clock::time_point begin, Clock::time_point end) noexcept
{
    return Duration{end - begin}.count();
}

[[nodiscard]] const OpenExrDecodeConfig &openExrDecodeConfig()
{
    static const auto config = [] {
        auto const hardwareThreads = std::max(1u, std::thread::hardware_concurrency());
        auto const workerCount = std::min(hardwareThreads, nr::maxThreads);
        nr::dependency::openexr::setGlobalThreadCount(static_cast<int>(workerCount));
        return OpenExrDecodeConfig{
            .workerCount = workerCount,
            .blockRowCount = std::max(scanlineRowsPerWorker, workerCount * scanlineRowsPerWorker),
        };
    }();
    return config;
}

[[nodiscard]] LoadError makeExrError(LoadErrorCode code, const std::filesystem::path &sourcePath, std::string message)
{
    return LoadError{
        .code = code,
        .backend = "openexr",
        .sourcePath = sourcePath,
        .message = std::move(message),
    };
}

[[nodiscard]] LoadError makeReportedExrError(LoadErrorCode code, const std::filesystem::path &sourcePath,
                                             std::string message)
{
    auto error = makeExrError(code, sourcePath, std::move(message));
    nrLog<LogLevel::error>("OpenEXR environment '{}' failed: {}", sourcePath.generic_string(), error.message);
    return error;
}

[[nodiscard]] std::string utf8Path(const std::filesystem::path &path)
{
    auto const encoded = path.u8string();
    return std::string{
        reinterpret_cast<const char *>(encoded.data()),
        encoded.size(),
    };
}

[[nodiscard]] std::expected<void, LoadError> validateExrContainer(const std::filesystem::path &sourcePath,
                                                                  const std::string &fileName)
{
    auto tiled = false;
    auto deep = false;
    auto multiPart = false;
    auto isOpenExr = false;
    try
    {
        isOpenExr = nr::dependency::openexr::inspectFile(fileName.c_str(), tiled, deep, multiPart);
    }
    catch (const std::exception &error)
    {
        return std::unexpected(
            makeReportedExrError(LoadErrorCode::importFailed, sourcePath,
                                 std::format("Failed to inspect OpenEXR container: {}", error.what())));
    }
    catch (...)
    {
        return std::unexpected(makeReportedExrError(LoadErrorCode::importFailed, sourcePath,
                                                    "Failed to inspect OpenEXR container: unknown OpenEXR error."));
    }

    if (!isOpenExr)
    {
        return std::unexpected(
            makeExrError(LoadErrorCode::unsupportedFormat, sourcePath, "The source is not an OpenEXR file."));
    }
    if (multiPart || deep || tiled)
    {
        return std::unexpected(
            makeExrError(LoadErrorCode::unsupportedFormat, sourcePath,
                         std::format("Environment loading supports only single-part scanline OpenEXR files "
                                     "(multiPart={}, deep={}, tiled={}).",
                                     multiPart, deep, tiled)));
    }
    return {};
}

[[nodiscard]] std::expected<std::unique_ptr<nr::dependency::openexr::InputFile>, LoadError> openExrFile(
    const std::filesystem::path &sourcePath, const std::string &fileName)
{
    try
    {
        return std::make_unique<nr::dependency::openexr::InputFile>(fileName.c_str());
    }
    catch (const std::exception &error)
    {
        return std::unexpected(makeReportedExrError(LoadErrorCode::importFailed, sourcePath,
                                                    std::format("Failed to open OpenEXR image: {}", error.what())));
    }
    catch (...)
    {
        return std::unexpected(makeReportedExrError(LoadErrorCode::importFailed, sourcePath,
                                                    "Failed to open OpenEXR image: unknown OpenEXR error."));
    }
}

[[nodiscard]] std::optional<LoadError> validateChannel(const nr::dependency::openexr::Header &header,
                                                       const std::filesystem::path &sourcePath,
                                                       std::string_view channelName, bool required)
{
    auto const *channel = header.channels().findChannel(channelName.data());
    if (channel == nullptr)
    {
        if (!required)
        {
            return {};
        }
        return makeExrError(LoadErrorCode::textureDataUnsupported, sourcePath,
                            std::format("OpenEXR environment is missing required '{}' channel.", channelName));
    }

    if (channel->type != nr::dependency::openexr::halfPixelType &&
        channel->type != nr::dependency::openexr::floatPixelType)
    {
        return makeExrError(
            LoadErrorCode::textureDataUnsupported, sourcePath,
            std::format("OpenEXR environment channel '{}' must use HALF or FLOAT samples.", channelName));
    }
    if (channel->xSampling != 1 || channel->ySampling != 1)
    {
        return makeExrError(LoadErrorCode::textureDataUnsupported, sourcePath,
                            std::format("OpenEXR environment channel '{}' must not be subsampled.", channelName));
    }
    return {};
}

[[nodiscard]] std::expected<void, LoadError> readDecodedBlock(nr::dependency::openexr::InputFile &file,
                                                              const std::filesystem::path &sourcePath, int originX,
                                                              int firstY, std::uint32_t width, std::uint32_t rowCount,
                                                              std::span<float> decoded)
{
    auto const expectedSampleCount = static_cast<std::size_t>(width) * rowCount * decodedChannelCount;
    nrAssert(decoded.size() == expectedSampleCount, "readDecodedBlock requires an exactly sized RGB destination span.");

    auto const xStride = decodedChannelCount * sizeof(float);
    auto const yStride = static_cast<std::size_t>(width) * xStride;

    try
    {
        auto frameBuffer = nr::dependency::openexr::FrameBuffer{};
        auto insertSlice = [&](std::string_view name, std::size_t channelIndex) {
            frameBuffer.insert(name.data(), nr::dependency::openexr::makeSlice(
                                                nr::dependency::openexr::floatPixelType, decoded.data() + channelIndex,
                                                originX, firstY, width, rowCount, xStride, yStride));
        };
        insertSlice("R", 0u);
        insertSlice("G", 1u);
        insertSlice("B", 2u);
        file.setFrameBuffer(frameBuffer);
        file.readPixels(firstY, firstY + static_cast<int>(rowCount) - 1);
    }
    catch (const std::exception &error)
    {
        return std::unexpected(
            makeReportedExrError(LoadErrorCode::importFailed, sourcePath,
                                 std::format("Failed to decode OpenEXR scanlines: {}", error.what())));
    }
    catch (...)
    {
        return std::unexpected(makeReportedExrError(LoadErrorCode::importFailed, sourcePath,
                                                    "Failed to decode OpenEXR scanlines: unknown OpenEXR error."));
    }
    return {};
}

template <typename Consumer>
[[nodiscard]] std::expected<void, LoadError> decodeImageBlocks(nr::dependency::openexr::InputFile &file,
                                                               const std::filesystem::path &sourcePath, int originX,
                                                               int originY, std::uint32_t width, std::uint32_t height,
                                                               std::uint32_t blockRowCount,
                                                               std::span<float> decodedImage, Consumer &&consumer)
{
    nrAssert(blockRowCount > 0u, "decodeImageBlocks requires a non-zero block row count.");
    auto const expectedSampleCount = static_cast<std::size_t>(width) * height * decodedChannelCount;
    nrAssert(decodedImage.size() == expectedSampleCount,
             "decodeImageBlocks requires an exactly sized RGB float image destination.");
    auto const blockCount = (height + blockRowCount - 1u) / blockRowCount;
    auto failure = std::optional<LoadError>{};

    auto const blockIndices = std::views::iota(std::uint32_t{0u}, blockCount);
    std::ranges::for_each(blockIndices, [&](std::uint32_t blockIndex) {
        if (failure.has_value())
        {
            return;
        }

        auto const firstRow = blockIndex * blockRowCount;
        auto const rowCount = std::min(blockRowCount, height - firstRow);
        auto const sampleCount = static_cast<std::size_t>(width) * rowCount * decodedChannelCount;
        auto const sampleOffset = static_cast<std::size_t>(firstRow) * width * decodedChannelCount;
        auto block = decodedImage.subspan(sampleOffset, sampleCount);
        auto readResult =
            readDecodedBlock(file, sourcePath, originX, originY + static_cast<int>(firstRow), width, rowCount, block);
        if (!readResult)
        {
            failure = std::move(readResult.error());
            return;
        }
        std::invoke(consumer, std::span<const float>{block}, firstRow, rowCount);
    });

    if (failure.has_value())
    {
        return std::unexpected(std::move(*failure));
    }
    return {};
}

void storeHalfPixel(std::span<std::byte> destination, std::size_t pixelIndex,
                    std::array<float, encodedChannelCount> values)
{
    auto encoded = std::array<nr::dependency::imath::Half, encodedChannelCount>{
        nr::dependency::imath::Half{values[0]},
        nr::dependency::imath::Half{values[1]},
        nr::dependency::imath::Half{values[2]},
        nr::dependency::imath::Half{values[3]},
    };
    auto const byteOffset = pixelIndex * sizeof(encoded);
    std::memcpy(destination.data() + byteOffset, encoded.data(), sizeof(encoded));
}
} // namespace

[[nodiscard]] EnvironmentMapLoadResult loadExrEnvironmentMap(const ExrEnvironmentLoadRequest &request)
{
    auto const totalStart = Clock::now();
    if (request.sourcePath.empty())
    {
        return std::unexpected(makeExrError(LoadErrorCode::invalidArgument, request.sourcePath,
                                            "OpenEXR environment source path is empty."));
    }
    if (!std::isfinite(request.halfSafeMaximum) || request.halfSafeMaximum <= 0.0f ||
        request.halfSafeMaximum > static_cast<float>(std::numeric_limits<nr::dependency::imath::Half>::max()))
    {
        return std::unexpected(
            makeExrError(LoadErrorCode::invalidArgument, request.sourcePath,
                         "OpenEXR environment HALF safe maximum must be finite and within (0, 65504]."));
    }

    auto const &decodeConfig = openExrDecodeConfig();
    auto const validationStart = Clock::now();
    auto fileStatusError = std::error_code{};
    auto const sourceExists = std::filesystem::is_regular_file(request.sourcePath, fileStatusError);
    if (fileStatusError || !sourceExists)
    {
        return std::unexpected(makeExrError(
            LoadErrorCode::fileNotFound, request.sourcePath,
            std::format("OpenEXR environment file was not found: '{}'.", request.sourcePath.generic_string())));
    }

    auto const fileName = utf8Path(request.sourcePath);
    auto containerResult = validateExrContainer(request.sourcePath, fileName);
    if (!containerResult)
    {
        return std::unexpected(std::move(containerResult.error()));
    }
    auto const validationFinished = Clock::now();

    auto openResult = openExrFile(request.sourcePath, fileName);
    if (!openResult)
    {
        return std::unexpected(std::move(openResult.error()));
    }
    auto file = std::move(*openResult);
    auto const openFinished = Clock::now();

    try
    {
        if (!file->isComplete())
        {
            return std::unexpected(makeExrError(LoadErrorCode::importFailed, request.sourcePath,
                                                "OpenEXR environment file is incomplete."));
        }
    }
    catch (const std::exception &error)
    {
        return std::unexpected(
            makeReportedExrError(LoadErrorCode::importFailed, request.sourcePath,
                                 std::format("Failed to validate OpenEXR completeness: {}", error.what())));
    }
    catch (...)
    {
        return std::unexpected(makeReportedExrError(LoadErrorCode::importFailed, request.sourcePath,
                                                    "Failed to validate OpenEXR completeness: unknown OpenEXR error."));
    }

    auto const &header = file->header();
    auto const channelRequirements = std::array{
        std::pair{std::string_view{"R"}, true},
        std::pair{std::string_view{"G"}, true},
        std::pair{std::string_view{"B"}, true},
        std::pair{std::string_view{"A"}, false},
    };
    auto channelError = std::optional<LoadError>{};
    std::ranges::for_each(channelRequirements, [&](auto requirement) {
        if (!channelError.has_value())
        {
            channelError = validateChannel(header, request.sourcePath, requirement.first, requirement.second);
        }
    });
    if (channelError.has_value())
    {
        return std::unexpected(std::move(*channelError));
    }

    auto const dataWindow = header.dataWindow();
    auto const width64 = static_cast<std::int64_t>(dataWindow.max.x) - static_cast<std::int64_t>(dataWindow.min.x) + 1;
    auto const height64 = static_cast<std::int64_t>(dataWindow.max.y) - static_cast<std::int64_t>(dataWindow.min.y) + 1;
    if (width64 <= 0 || height64 <= 0 || width64 > std::numeric_limits<std::uint32_t>::max() ||
        height64 > std::numeric_limits<std::uint32_t>::max())
    {
        return std::unexpected(makeExrError(LoadErrorCode::textureDataUnsupported, request.sourcePath,
                                            "OpenEXR environment data window has unsupported dimensions."));
    }
    auto const width = static_cast<std::uint32_t>(width64);
    auto const height = static_cast<std::uint32_t>(height64);
    auto const texelCount = static_cast<std::uint64_t>(width) * height;
    auto const outputBytesPerTexel = encodedChannelCount * sizeof(nr::dependency::imath::Half);
    auto const decodedBytesPerTexel = decodedChannelCount * sizeof(float);
    if (texelCount > std::numeric_limits<std::size_t>::max() / outputBytesPerTexel ||
        texelCount > std::numeric_limits<std::size_t>::max() / decodedBytesPerTexel)
    {
        return std::unexpected(makeExrError(LoadErrorCode::textureDataUnsupported, request.sourcePath,
                                            "OpenEXR environment exceeds host addressable memory."));
    }
    auto const outputByteCount = static_cast<std::size_t>(texelCount) * outputBytesPerTexel;
    auto const decodedByteCount = static_cast<std::size_t>(texelCount) * decodedBytesPerTexel;
    auto const metadataFinished = Clock::now();

    auto const scratchAllocationStart = Clock::now();
    auto decodedImage = std::vector<float>(static_cast<std::size_t>(texelCount) * decodedChannelCount);
    auto const scratchAllocationFinished = Clock::now();
    auto peakRadiance = 0.0f;
    auto minimumRadiance = 0.0f;
    auto negativeSampleCount = std::uint64_t{0u};
    auto nonFiniteSampleCount = std::uint64_t{0u};
    auto scanConsumerMilliseconds = 0.0;
    auto const decodeStart = Clock::now();
    auto decodeResult = decodeImageBlocks(*file, request.sourcePath, dataWindow.min.x, dataWindow.min.y, width, height,
                                          decodeConfig.blockRowCount, decodedImage,
                                          [&](std::span<const float> decoded, std::uint32_t, std::uint32_t) {
                                              auto const consumerStart = Clock::now();
                                              std::ranges::for_each(decoded, [&](float sample) {
                                                 if (!std::isfinite(sample))
                                                 {
                                                     ++nonFiniteSampleCount;
                                                     return;
                                                 }
                                                 if (sample < 0.0f)
                                                 {
                                                     ++negativeSampleCount;
                                                     minimumRadiance = std::min(minimumRadiance, sample);
                                                     return;
                                                 }
                                                 peakRadiance = std::max(peakRadiance, sample);
                                             });
                                              scanConsumerMilliseconds +=
                                                  elapsedMilliseconds(consumerStart, Clock::now());
                                          });
    auto const decodeFinished = Clock::now();
    if (!decodeResult)
    {
        return std::unexpected(std::move(decodeResult.error()));
    }
    if (nonFiniteSampleCount > 0u)
    {
        return std::unexpected(
            makeExrError(LoadErrorCode::textureDataUnsupported, request.sourcePath,
                         std::format("OpenEXR environment contains {} non-finite RGB samples.", nonFiniteSampleCount)));
    }
    if (negativeSampleCount > 0u && minimumRadiance < -1.0e-6f)
    {
        nrLog<LogLevel::warning>("OpenEXR environment '{}' clamps {} negative RGB samples (minimum {}).",
                                 request.sourcePath.generic_string(), negativeSampleCount, minimumRadiance);
    }

    auto const radianceDecodeScale = std::max(1.0f, peakRadiance / request.halfSafeMaximum);
    auto const allocationStart = Clock::now();
    auto bytes = std::vector<std::byte>(outputByteCount);
    auto const allocationFinished = Clock::now();
    auto const encodeStart = Clock::now();
    auto const pixelIndices = std::views::iota(std::size_t{0u}, static_cast<std::size_t>(texelCount));
    std::ranges::for_each(pixelIndices, [&](std::size_t pixelIndex) {
        auto const sourceOffset = pixelIndex * decodedChannelCount;
        auto encode = [&](std::size_t channelIndex) {
            return std::clamp(decodedImage[sourceOffset + channelIndex] / radianceDecodeScale, 0.0f,
                              request.halfSafeMaximum);
        };
        storeHalfPixel(bytes, pixelIndex, std::array{encode(0u), encode(1u), encode(2u), 1.0f});
    });
    auto const encodeFinished = Clock::now();
    auto const encodeConsumerMilliseconds = elapsedMilliseconds(encodeStart, encodeFinished);

    auto texture = nr::resource::Texture{};
    texture.name = request.sourcePath.filename().string();
    texture.sourcePath = request.sourcePath;
    texture.format = vk::Format::eR16G16B16A16Sfloat;
    texture.width = width;
    texture.height = height;
    texture.srgb = false;
    auto level = nr::resource::ImageLevel{};
    level.width = width;
    level.height = height;
    level.bytes = std::move(bytes);
    texture.levels.push_back(std::move(level));

    auto environment = nr::resource::EnvironmentMap{};
    environment.radiance = std::move(texture);
    environment.radianceDecodeScale = radianceDecodeScale;
    nrAssert(environment.valid(), "loadExrEnvironmentMap produced an invalid environment resource.");
    auto const totalFinished = Clock::now();
    auto const decodeMilliseconds = elapsedMilliseconds(decodeStart, decodeFinished);
    auto const encodeMilliseconds = elapsedMilliseconds(encodeStart, encodeFinished);
    nrLog<LogLevel::info>(
        "[EnvironmentMap::loadExr] source='{}', extent={}x{}, outputMiB={:.3f}, decodedScratchMiB={:.3f}, "
        "transientMiB={:.3f}, openExrWorkers={}, decodeBlockRows={}, sourceDecodePasses=1, "
        "phaseMs={{validate={:.3f}, open={:.3f}, metadata={:.3f}, scratchAllocate={:.3f}, "
        "decodePass={:.3f}, scanConsumer={:.3f}, openExrDecodeApprox={:.3f}, outputAllocate={:.3f}, "
        "encodePass={:.3f}, encodeConsumer={:.3f}, total={:.3f}}}",
        request.sourcePath.generic_string(), width, height,
        static_cast<double>(outputByteCount) / (1024.0 * 1024.0),
        static_cast<double>(decodedByteCount) / (1024.0 * 1024.0),
        static_cast<double>(outputByteCount + decodedByteCount) / (1024.0 * 1024.0), decodeConfig.workerCount,
        decodeConfig.blockRowCount, elapsedMilliseconds(validationStart, validationFinished),
        elapsedMilliseconds(validationFinished, openFinished), elapsedMilliseconds(openFinished, metadataFinished),
        elapsedMilliseconds(scratchAllocationStart, scratchAllocationFinished), decodeMilliseconds,
        scanConsumerMilliseconds, std::max(0.0, decodeMilliseconds - scanConsumerMilliseconds),
        elapsedMilliseconds(allocationStart, allocationFinished), encodeMilliseconds, encodeConsumerMilliseconds,
        elapsedMilliseconds(totalStart, totalFinished));
    return environment;
}
} // namespace nr::load
