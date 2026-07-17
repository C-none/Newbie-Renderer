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
inline constexpr std::uint32_t scanlineBlockRowCount = 32u;

[[nodiscard]] LoadError makeExrError(
    LoadErrorCode code,
    const std::filesystem::path& sourcePath,
    std::string message)
{
    return LoadError{
        .code = code,
        .backend = "openexr",
        .sourcePath = sourcePath,
        .message = std::move(message),
    };
}

[[nodiscard]] LoadError makeReportedExrError(
    LoadErrorCode code,
    const std::filesystem::path& sourcePath,
    std::string message)
{
    auto error = makeExrError(code, sourcePath, std::move(message));
    nrInfo<LogLevel::error>(std::format(
        "OpenEXR environment '{}' failed: {}",
        sourcePath.generic_string(),
        error.message));
    return error;
}

[[nodiscard]] std::string utf8Path(const std::filesystem::path& path)
{
    auto const encoded = path.u8string();
    return std::string{
        reinterpret_cast<const char*>(encoded.data()),
        encoded.size(),
    };
}

[[nodiscard]] std::expected<void, LoadError> validateExrContainer(
    const std::filesystem::path& sourcePath,
    const std::string& fileName)
{
    auto tiled = false;
    auto deep = false;
    auto multiPart = false;
    auto isOpenExr = false;
    try
    {
        isOpenExr = nr::dependency::openexr::inspectFile(
            fileName.c_str(),
            tiled,
            deep,
            multiPart);
    }
    catch (const std::exception& error)
    {
        return std::unexpected(makeReportedExrError(
            LoadErrorCode::importFailed,
            sourcePath,
            std::format("Failed to inspect OpenEXR container: {}", error.what())));
    }
    catch (...)
    {
        return std::unexpected(makeReportedExrError(
            LoadErrorCode::importFailed,
            sourcePath,
            "Failed to inspect OpenEXR container: unknown OpenEXR error."));
    }

    if (!isOpenExr)
    {
        return std::unexpected(makeExrError(
            LoadErrorCode::unsupportedFormat,
            sourcePath,
            "The source is not an OpenEXR file."));
    }
    if (multiPart || deep || tiled)
    {
        return std::unexpected(makeExrError(
            LoadErrorCode::unsupportedFormat,
            sourcePath,
            std::format(
                "Environment loading supports only single-part scanline OpenEXR files "
                "(multiPart={}, deep={}, tiled={}).",
                multiPart,
                deep,
                tiled)));
    }
    return {};
}

[[nodiscard]] std::expected<std::unique_ptr<nr::dependency::openexr::InputFile>, LoadError>
openExrFile(const std::filesystem::path& sourcePath, const std::string& fileName)
{
    try
    {
        return std::make_unique<nr::dependency::openexr::InputFile>(fileName.c_str());
    }
    catch (const std::exception& error)
    {
        return std::unexpected(makeReportedExrError(
            LoadErrorCode::importFailed,
            sourcePath,
            std::format("Failed to open OpenEXR image: {}", error.what())));
    }
    catch (...)
    {
        return std::unexpected(makeReportedExrError(
            LoadErrorCode::importFailed,
            sourcePath,
            "Failed to open OpenEXR image: unknown OpenEXR error."));
    }
}

[[nodiscard]] std::optional<LoadError> validateChannel(
    const nr::dependency::openexr::Header& header,
    const std::filesystem::path& sourcePath,
    std::string_view channelName,
    bool required)
{
    auto const* channel = header.channels().findChannel(channelName.data());
    if (channel == nullptr)
    {
        if (!required)
        {
            return {};
        }
        return makeExrError(
            LoadErrorCode::textureDataUnsupported,
            sourcePath,
            std::format("OpenEXR environment is missing required '{}' channel.", channelName));
    }

    if (channel->type != nr::dependency::openexr::halfPixelType &&
        channel->type != nr::dependency::openexr::floatPixelType)
    {
        return makeExrError(
            LoadErrorCode::textureDataUnsupported,
            sourcePath,
            std::format("OpenEXR environment channel '{}' must use HALF or FLOAT samples.", channelName));
    }
    if (channel->xSampling != 1 || channel->ySampling != 1)
    {
        return makeExrError(
            LoadErrorCode::textureDataUnsupported,
            sourcePath,
            std::format("OpenEXR environment channel '{}' must not be subsampled.", channelName));
    }
    return {};
}

[[nodiscard]] std::expected<void, LoadError> readDecodedBlock(
    nr::dependency::openexr::InputFile& file,
    const std::filesystem::path& sourcePath,
    int originX,
    int firstY,
    std::uint32_t width,
    std::uint32_t rowCount,
    std::span<float> decoded)
{
    auto const expectedSampleCount =
        static_cast<std::size_t>(width) * rowCount * decodedChannelCount;
    nrAssert(
        decoded.size() == expectedSampleCount,
        "readDecodedBlock requires an exactly sized RGB destination span.");

    auto const xStride = decodedChannelCount * sizeof(float);
    auto const yStride = static_cast<std::size_t>(width) * xStride;

    try
    {
        auto frameBuffer = nr::dependency::openexr::FrameBuffer{};
        auto insertSlice = [&](std::string_view name, std::size_t channelIndex) {
            frameBuffer.insert(
                name.data(),
                nr::dependency::openexr::makeSlice(
                    nr::dependency::openexr::floatPixelType,
                    decoded.data() + channelIndex,
                    originX,
                    firstY,
                    width,
                    rowCount,
                    xStride,
                    yStride));
        };
        insertSlice("R", 0u);
        insertSlice("G", 1u);
        insertSlice("B", 2u);
        file.setFrameBuffer(frameBuffer);
        file.readPixels(firstY, firstY + static_cast<int>(rowCount) - 1);
    }
    catch (const std::exception& error)
    {
        return std::unexpected(makeReportedExrError(
            LoadErrorCode::importFailed,
            sourcePath,
            std::format("Failed to decode OpenEXR scanlines: {}", error.what())));
    }
    catch (...)
    {
        return std::unexpected(makeReportedExrError(
            LoadErrorCode::importFailed,
            sourcePath,
            "Failed to decode OpenEXR scanlines: unknown OpenEXR error."));
    }
    return {};
}

template <typename Consumer>
[[nodiscard]] std::expected<void, LoadError> visitDecodedBlocks(
    nr::dependency::openexr::InputFile& file,
    const std::filesystem::path& sourcePath,
    int originX,
    int originY,
    std::uint32_t width,
    std::uint32_t height,
    Consumer&& consumer)
{
    auto const maximumBlockRows = std::min(scanlineBlockRowCount, height);
    auto decoded = std::vector<float>(
        static_cast<std::size_t>(width) * maximumBlockRows * decodedChannelCount);
    auto const blockCount = (height + scanlineBlockRowCount - 1u) / scanlineBlockRowCount;
    auto failure = std::optional<LoadError>{};

    auto const blockIndices = std::views::iota(std::uint32_t{0u}, blockCount);
    std::ranges::for_each(blockIndices, [&](std::uint32_t blockIndex) {
        if (failure.has_value())
        {
            return;
        }

        auto const firstRow = blockIndex * scanlineBlockRowCount;
        auto const rowCount = std::min(scanlineBlockRowCount, height - firstRow);
        auto const sampleCount =
            static_cast<std::size_t>(width) * rowCount * decodedChannelCount;
        auto block = std::span<float>{decoded}.first(sampleCount);
        auto readResult = readDecodedBlock(
            file,
            sourcePath,
            originX,
            originY + static_cast<int>(firstRow),
            width,
            rowCount,
            block);
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

void storeHalfPixel(
    std::span<std::byte> destination,
    std::size_t pixelIndex,
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

[[nodiscard]] EnvironmentMapLoadResult loadExrEnvironmentMap(
    const ExrEnvironmentLoadRequest& request)
{
    if (request.sourcePath.empty())
    {
        return std::unexpected(makeExrError(
            LoadErrorCode::invalidArgument,
            request.sourcePath,
            "OpenEXR environment source path is empty."));
    }
    if (!std::isfinite(request.halfSafeMaximum) ||
        request.halfSafeMaximum <= 0.0f ||
        request.halfSafeMaximum > static_cast<float>(std::numeric_limits<nr::dependency::imath::Half>::max()))
    {
        return std::unexpected(makeExrError(
            LoadErrorCode::invalidArgument,
            request.sourcePath,
            "OpenEXR environment HALF safe maximum must be finite and within (0, 65504]."));
    }

    auto fileStatusError = std::error_code{};
    auto const sourceExists = std::filesystem::is_regular_file(request.sourcePath, fileStatusError);
    if (fileStatusError || !sourceExists)
    {
        return std::unexpected(makeExrError(
            LoadErrorCode::fileNotFound,
            request.sourcePath,
            std::format("OpenEXR environment file was not found: '{}'.", request.sourcePath.generic_string())));
    }

    auto const fileName = utf8Path(request.sourcePath);
    auto containerResult = validateExrContainer(request.sourcePath, fileName);
    if (!containerResult)
    {
        return std::unexpected(std::move(containerResult.error()));
    }

    auto openResult = openExrFile(request.sourcePath, fileName);
    if (!openResult)
    {
        return std::unexpected(std::move(openResult.error()));
    }
    auto file = std::move(*openResult);

    try
    {
        if (!file->isComplete())
        {
            return std::unexpected(makeExrError(
                LoadErrorCode::importFailed,
                request.sourcePath,
                "OpenEXR environment file is incomplete."));
        }
    }
    catch (const std::exception& error)
    {
        return std::unexpected(makeReportedExrError(
            LoadErrorCode::importFailed,
            request.sourcePath,
            std::format("Failed to validate OpenEXR completeness: {}", error.what())));
    }
    catch (...)
    {
        return std::unexpected(makeReportedExrError(
            LoadErrorCode::importFailed,
            request.sourcePath,
            "Failed to validate OpenEXR completeness: unknown OpenEXR error."));
    }

    auto const& header = file->header();
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
            channelError = validateChannel(
                header,
                request.sourcePath,
                requirement.first,
                requirement.second);
        }
    });
    if (channelError.has_value())
    {
        return std::unexpected(std::move(*channelError));
    }

    auto const dataWindow = header.dataWindow();
    auto const width64 =
        static_cast<std::int64_t>(dataWindow.max.x) - static_cast<std::int64_t>(dataWindow.min.x) + 1;
    auto const height64 =
        static_cast<std::int64_t>(dataWindow.max.y) - static_cast<std::int64_t>(dataWindow.min.y) + 1;
    if (width64 <= 0 || height64 <= 0 ||
        width64 > std::numeric_limits<std::uint32_t>::max() ||
        height64 > std::numeric_limits<std::uint32_t>::max())
    {
        return std::unexpected(makeExrError(
            LoadErrorCode::textureDataUnsupported,
            request.sourcePath,
            "OpenEXR environment data window has unsupported dimensions."));
    }
    auto const width = static_cast<std::uint32_t>(width64);
    auto const height = static_cast<std::uint32_t>(height64);
    auto const texelCount = static_cast<std::uint64_t>(width) * height;
    auto const outputByteCount = texelCount * encodedChannelCount * sizeof(nr::dependency::imath::Half);
    if (outputByteCount > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
    {
        return std::unexpected(makeExrError(
            LoadErrorCode::textureDataUnsupported,
            request.sourcePath,
            "OpenEXR environment exceeds host addressable memory."));
    }

    auto peakRadiance = 0.0f;
    auto minimumRadiance = 0.0f;
    auto negativeSampleCount = std::uint64_t{0u};
    auto nonFiniteSampleCount = std::uint64_t{0u};
    auto scanResult = visitDecodedBlocks(
        *file,
        request.sourcePath,
        dataWindow.min.x,
        dataWindow.min.y,
        width,
        height,
        [&](std::span<const float> decoded, std::uint32_t, std::uint32_t) {
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
        });
    if (!scanResult)
    {
        return std::unexpected(std::move(scanResult.error()));
    }
    if (nonFiniteSampleCount > 0u)
    {
        return std::unexpected(makeExrError(
            LoadErrorCode::textureDataUnsupported,
            request.sourcePath,
            std::format(
                "OpenEXR environment contains {} non-finite RGB samples.",
                nonFiniteSampleCount)));
    }
    if (negativeSampleCount > 0u && minimumRadiance < -1.0e-6f)
    {
        nrInfo<LogLevel::warning>(std::format(
            "OpenEXR environment '{}' clamps {} negative RGB samples (minimum {}).",
            request.sourcePath.generic_string(),
            negativeSampleCount,
            minimumRadiance));
    }

    auto const radianceDecodeScale = std::max(1.0f, peakRadiance / request.halfSafeMaximum);
    auto bytes = std::vector<std::byte>(static_cast<std::size_t>(outputByteCount));
    auto encodeResult = visitDecodedBlocks(
        *file,
        request.sourcePath,
        dataWindow.min.x,
        dataWindow.min.y,
        width,
        height,
        [&](std::span<const float> decoded, std::uint32_t firstRow, std::uint32_t rowCount) {
            auto const blockTexelCount = static_cast<std::size_t>(width) * rowCount;
            auto const pixelIndices = std::views::iota(std::size_t{0u}, blockTexelCount);
            std::ranges::for_each(pixelIndices, [&](std::size_t blockPixelIndex) {
                auto const sourceOffset = blockPixelIndex * decodedChannelCount;
                auto encode = [&](std::size_t channelIndex) {
                    return std::clamp(
                        decoded[sourceOffset + channelIndex] / radianceDecodeScale,
                        0.0f,
                        request.halfSafeMaximum);
                };
                auto const destinationPixelIndex =
                    static_cast<std::size_t>(firstRow) * width + blockPixelIndex;
                storeHalfPixel(
                    bytes,
                    destinationPixelIndex,
                    std::array{encode(0u), encode(1u), encode(2u), 1.0f});
            });
        });
    if (!encodeResult)
    {
        return std::unexpected(std::move(encodeResult.error()));
    }

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
    return environment;
}
} // namespace nr::load
