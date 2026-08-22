module nr.rhi;
import :pipelineBinary;
import dependency.vulkan;
import nr.utils;
import std;

namespace nr::rhi
{
namespace
{
inline constexpr auto artifactMagic = std::array<std::uint8_t, 8u>{'N', 'R', 'P', 'S', 'O', 'B', 'I', 'N'};
inline constexpr std::uint32_t artifactVersion = 2u;
inline constexpr std::uint32_t maximumBinaryCount = 64u;
inline constexpr std::uint64_t maximumArtifactBytes = 512ull * 1024ull * 1024ull;

template <typename T> void appendScalar(std::vector<std::uint8_t> &destination, T value)
{
    static_assert(std::is_trivially_copyable_v<T>);
    auto const offset = destination.size();
    destination.resize(offset + sizeof(T));
    std::memcpy(destination.data() + offset, std::addressof(value), sizeof(T));
}

void appendBytes(std::vector<std::uint8_t> &destination, std::span<const std::uint8_t> bytes)
{
    destination.insert(destination.end(), bytes.begin(), bytes.end());
}

class ArtifactReader
{
  public:
    explicit ArtifactReader(std::span<const std::uint8_t> bytes) : bytes_(bytes)
    {
    }

    template <typename T> [[nodiscard]] std::optional<T> scalar() noexcept
    {
        static_assert(std::is_trivially_copyable_v<T>);
        if (remaining() < sizeof(T))
        {
            return std::nullopt;
        }
        auto value = T{};
        std::memcpy(std::addressof(value), bytes_.data() + offset_, sizeof(T));
        offset_ += sizeof(T);
        return value;
    }

    [[nodiscard]] std::optional<std::span<const std::uint8_t>> bytes(std::size_t count) noexcept
    {
        if (count > remaining())
        {
            return std::nullopt;
        }
        auto result = bytes_.subspan(offset_, count);
        offset_ += count;
        return result;
    }

    [[nodiscard]] std::size_t remaining() const noexcept
    {
        return bytes_.size() - offset_;
    }

  private:
    std::span<const std::uint8_t> bytes_{};
    std::size_t offset_ = 0u;
};

[[nodiscard]] std::span<const std::uint8_t> keyBytes(const vk::PipelineBinaryKeyKHR &key) noexcept
{
    return {key.key.data(), key.keySize};
}

[[nodiscard]] vk::PipelineBinaryKeyKHR makeKey(std::span<const std::uint8_t> bytes)
{
    nrAssert(bytes.size() <= vk::MaxPipelineBinaryKeySizeKHR, "Pipeline binary artifact contains an oversized key.");
    auto key = vk::PipelineBinaryKeyKHR{};
    key.keySize = static_cast<std::uint32_t>(bytes.size());
    std::ranges::copy(bytes, key.key.begin());
    return key;
}

void logArtifactWarning(std::string_view operation, const std::filesystem::path &path, std::string_view detail)
{
    nrLog<LogLevel::warning>("PipelineBinaryStore {} '{}': {}", operation, path.string(), detail);
}
} // namespace

PipelineBinaryStore::PipelineBinaryStore(const vk::raii::Device &device, std::filesystem::path root)
    : device_(device), root_(std::move(root)), globalKey_(device.getPipelineKeyKHR(nullptr))
{
    nrAssert(validKey(globalKey_), "VK_KHR_pipeline_binary returned an invalid global pipeline key.");
}

[[nodiscard]] bool PipelineBinaryStore::validKey(const vk::PipelineBinaryKeyKHR &key) noexcept
{
    return key.keySize > 0u && key.keySize <= vk::MaxPipelineBinaryKeySizeKHR;
}

[[nodiscard]] bool PipelineBinaryStore::equalKeys(const vk::PipelineBinaryKeyKHR &lhs,
                                                  const vk::PipelineBinaryKeyKHR &rhs) noexcept
{
    return lhs.keySize == rhs.keySize && validKey(lhs) && validKey(rhs) &&
           std::ranges::equal(keyBytes(lhs), keyBytes(rhs));
}

[[nodiscard]] std::string PipelineBinaryStore::keyHex(const vk::PipelineBinaryKeyKHR &key)
{
    static constexpr auto digits = std::string_view{"0123456789abcdef"};
    auto text = std::string{};
    text.reserve(static_cast<std::size_t>(key.keySize) * 2u);
    std::ranges::for_each(keyBytes(key), [&](std::uint8_t byte) {
        text.push_back(digits[byte >> 4u]);
        text.push_back(digits[byte & 0x0fu]);
    });
    return text;
}

[[nodiscard]] PipelineBinaryCacheKey PipelineBinaryStore::pipelineKey(const vk::PipelineCreateInfoKHR &createInfo,
                                                                      std::uint64_t contentFingerprint) const
{
    auto key = device_.get().getPipelineKeyKHR(createInfo);
    nrAssert(validKey(key), "VK_KHR_pipeline_binary returned an invalid PSO pipeline key.");
    nrAssert(contentFingerprint != 0u, "Pipeline binary content fingerprint must not be zero.");
    return PipelineBinaryCacheKey{
        .driverKey = key,
        .contentFingerprint = contentFingerprint,
    };
}

[[nodiscard]] std::filesystem::path PipelineBinaryStore::artifactPath(const PipelineBinaryCacheKey &cacheKey) const
{
    return root_ / keyHex(globalKey_) /
           std::format("{}.{}.nrpso", nr::hash::toHexString(cacheKey.contentFingerprint), keyHex(cacheKey.driverKey));
}

[[nodiscard]] std::optional<PipelineBinaryStore::Artifact> PipelineBinaryStore::readArtifact(
    const PipelineBinaryCacheKey &cacheKey) const
{
    auto const path = artifactPath(cacheKey);
    auto lock = std::scoped_lock{fileMutex_};
    auto rejectArtifact = [&](std::string_view detail) -> std::optional<Artifact> {
        logArtifactWarning("rejected", path, detail);
        auto removeError = std::error_code{};
        std::filesystem::remove(path, removeError);
        if (removeError)
        {
            logArtifactWarning("failed to invalidate", path, removeError.message());
        }
        return std::nullopt;
    };
    auto error = std::error_code{};
    if (!std::filesystem::exists(path, error))
    {
        if (error)
        {
            logArtifactWarning("failed to query", path, error.message());
        }
        return std::nullopt;
    }

    auto const fileSize = std::filesystem::file_size(path, error);
    if (error)
    {
        logArtifactWarning("failed to query size of", path, error.message());
        return std::nullopt;
    }
    if (fileSize == 0u || fileSize > maximumArtifactBytes)
    {
        return rejectArtifact(std::format("invalid size {} bytes", fileSize));
    }

    auto stream = std::ifstream{path, std::ios::binary};
    auto bytes = std::vector<std::uint8_t>(static_cast<std::size_t>(fileSize));
    if (!stream || !stream.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
    {
        logArtifactWarning("failed to read", path, "I/O failure");
        return std::nullopt;
    }

    auto reader = ArtifactReader{bytes};
    auto magic = reader.bytes(artifactMagic.size());
    auto version = reader.scalar<std::uint32_t>();
    auto globalKeySize = reader.scalar<std::uint32_t>();
    if (!magic || !std::ranges::equal(*magic, artifactMagic) || version != artifactVersion || !globalKeySize ||
        *globalKeySize == 0u || *globalKeySize > vk::MaxPipelineBinaryKeySizeKHR)
    {
        return rejectArtifact("invalid header");
    }
    auto globalKeyData = reader.bytes(*globalKeySize);
    auto pipelineKeySize = reader.scalar<std::uint32_t>();
    if (!globalKeyData || !pipelineKeySize || *pipelineKeySize == 0u ||
        *pipelineKeySize > vk::MaxPipelineBinaryKeySizeKHR)
    {
        return rejectArtifact("invalid key metadata");
    }
    auto pipelineKeyData = reader.bytes(*pipelineKeySize);
    auto contentFingerprint = reader.scalar<std::uint64_t>();
    auto binaryCount = reader.scalar<std::uint32_t>();
    if (!pipelineKeyData || !binaryCount || *binaryCount == 0u || *binaryCount > maximumBinaryCount ||
        !contentFingerprint || *contentFingerprint != cacheKey.contentFingerprint ||
        !equalKeys(makeKey(*globalKeyData), globalKey_) || !equalKeys(makeKey(*pipelineKeyData), cacheKey.driverKey))
    {
        return rejectArtifact("key, content fingerprint, or binary count mismatch");
    }

    auto artifact = Artifact{};
    artifact.binaries.reserve(*binaryCount);
    auto parseFailed = false;
    std::ranges::for_each(std::views::iota(std::uint32_t{0u}, *binaryCount), [&](std::uint32_t) {
        if (parseFailed)
        {
            return;
        }
        auto keySize = reader.scalar<std::uint32_t>();
        auto dataSize = reader.scalar<std::uint64_t>();
        if (!keySize || !dataSize || *keySize == 0u || *keySize > vk::MaxPipelineBinaryKeySizeKHR ||
            *dataSize > maximumArtifactBytes || *dataSize > reader.remaining())
        {
            parseFailed = true;
            return;
        }
        auto binaryKeyData = reader.bytes(*keySize);
        auto binaryData = reader.bytes(static_cast<std::size_t>(*dataSize));
        if (!binaryKeyData || !binaryData)
        {
            parseFailed = true;
            return;
        }
        artifact.binaries.push_back(BinaryBlob{
            .key = makeKey(*binaryKeyData),
            .data = std::vector<std::uint8_t>{binaryData->begin(), binaryData->end()},
        });
    });
    if (parseFailed || reader.remaining() != 0u)
    {
        return rejectArtifact("truncated or trailing payload");
    }
    return artifact;
}

[[nodiscard]] bool PipelineBinaryStore::writeArtifact(const PipelineBinaryCacheKey &cacheKey,
                                                      const Artifact &artifact) const
{
    if (artifact.binaries.empty() || artifact.binaries.size() > maximumBinaryCount)
    {
        return false;
    }

    auto bytes = std::vector<std::uint8_t>{};
    appendBytes(bytes, artifactMagic);
    appendScalar(bytes, artifactVersion);
    appendScalar(bytes, globalKey_.keySize);
    appendBytes(bytes, keyBytes(globalKey_));
    appendScalar(bytes, cacheKey.driverKey.keySize);
    appendBytes(bytes, keyBytes(cacheKey.driverKey));
    appendScalar(bytes, cacheKey.contentFingerprint);
    appendScalar(bytes, static_cast<std::uint32_t>(artifact.binaries.size()));
    std::ranges::for_each(artifact.binaries, [&](const BinaryBlob &binary) {
        appendScalar(bytes, binary.key.keySize);
        appendScalar(bytes, static_cast<std::uint64_t>(binary.data.size()));
        appendBytes(bytes, keyBytes(binary.key));
        appendBytes(bytes, binary.data);
    });
    if (bytes.size() > maximumArtifactBytes)
    {
        logArtifactWarning("rejected generated artifact for", artifactPath(cacheKey), "artifact is too large");
        return false;
    }

    auto const path = artifactPath(cacheKey);
    static std::atomic_uint64_t temporarySequence = 0u;
    auto const timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto const threadIdentity = std::hash<std::thread::id>{}(std::this_thread::get_id());
    auto temporaryPath = path;
    temporaryPath += std::format(".tmp.{}.{}.{}", timestamp, threadIdentity,
                                 temporarySequence.fetch_add(1u, std::memory_order_relaxed));
    auto lock = std::scoped_lock{fileMutex_};
    auto error = std::error_code{};
    std::filesystem::create_directories(path.parent_path(), error);
    if (error)
    {
        logArtifactWarning("failed to create directory for", path, error.message());
        return false;
    }

    auto stream = std::ofstream{temporaryPath, std::ios::binary | std::ios::trunc};
    if (!stream ||
        !stream.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size())) ||
        !stream.flush())
    {
        logArtifactWarning("failed to write", temporaryPath, "I/O failure");
        std::filesystem::remove(temporaryPath, error);
        return false;
    }
    stream.close();

    std::filesystem::rename(temporaryPath, path, error);
    if (!error)
    {
        return true;
    }

    auto targetError = std::error_code{};
    auto const targetExists = std::filesystem::exists(path, targetError) && !targetError;
    auto removeError = std::error_code{};
    std::filesystem::remove(temporaryPath, removeError);
    if (!targetExists)
    {
        logArtifactWarning("failed to publish", path, error.message());
        return false;
    }
    return true;
}

[[nodiscard]] std::optional<PipelineBinaryStore::LoadedBinaries> PipelineBinaryStore::load(
    const PipelineBinaryCacheKey &cacheKey)
{
    auto artifact = readArtifact(cacheKey);
    if (!artifact)
    {
        return std::nullopt;
    }

    auto keys = artifact->binaries | std::views::transform([](const BinaryBlob &binary) { return binary.key; }) |
                std::ranges::to<std::vector>();
    auto data = artifact->binaries | std::views::transform([](BinaryBlob &binary) {
                    auto dataInfo = vk::PipelineBinaryDataKHR{};
                    dataInfo.dataSize = binary.data.size();
                    dataInfo.pData = binary.data.empty() ? nullptr : binary.data.data();
                    return dataInfo;
                }) |
                std::ranges::to<std::vector>();
    auto keysAndData = vk::PipelineBinaryKeysAndDataKHR{};
    keysAndData.binaryCount = static_cast<std::uint32_t>(keys.size());
    keysAndData.pPipelineBinaryKeys = keys.data();
    keysAndData.pPipelineBinaryData = data.data();
    auto createInfo = vk::PipelineBinaryCreateInfoKHR{};
    createInfo.pKeysAndDataInfo = &keysAndData;

    try
    {
        auto loaded = LoadedBinaries{};
        loaded.owned = device_.get().createPipelineBinariesKHR(createInfo);
        if (loaded.owned.size() != keys.size() ||
            std::ranges::any_of(loaded.owned, [](const auto &binary) { return *binary == vk::PipelineBinaryKHR{}; }))
        {
            invalidate(cacheKey);
            return std::nullopt;
        }
        loaded.handles = loaded.owned | std::views::transform([](const auto &binary) { return *binary; }) |
                         std::ranges::to<std::vector>();
        return loaded;
    }
    catch (const vk::SystemError &error)
    {
        nrLog<LogLevel::warning>("PipelineBinaryStore rejected persisted PSO binaries: {}", error.what());
        invalidate(cacheKey);
        return std::nullopt;
    }
}

void PipelineBinaryStore::releaseCapturedData(vk::Pipeline pipeline) const noexcept
{
    try
    {
        auto releaseInfo = vk::ReleaseCapturedPipelineDataInfoKHR{};
        releaseInfo.pipeline = pipeline;
        device_.get().releaseCapturedPipelineDataKHR(releaseInfo);
    }
    catch (const vk::SystemError &error)
    {
        nrLog<LogLevel::warning>("PipelineBinaryStore failed to release captured PSO data: {}", error.what());
    }
}

void PipelineBinaryStore::capture(const PipelineBinaryCacheKey &cacheKey, vk::Pipeline pipeline)
{
    nrAssert(pipeline != vk::Pipeline{}, "PipelineBinaryStore::capture requires a valid pipeline.");
    auto artifact = Artifact{};
    try
    {
        auto createInfo = vk::PipelineBinaryCreateInfoKHR{};
        createInfo.pipeline = pipeline;
        auto binaries = device_.get().createPipelineBinariesKHR(createInfo);
        if (binaries.empty() || binaries.size() > maximumBinaryCount ||
            std::ranges::any_of(binaries, [](const auto &binary) { return *binary == vk::PipelineBinaryKHR{}; }))
        {
            nrLog<LogLevel::warning>("PipelineBinaryStore received an invalid captured binary sequence.");
        }
        else
        {
            artifact.binaries.reserve(binaries.size());
            std::ranges::for_each(binaries, [&](const vk::raii::PipelineBinaryKHR &binary) {
                auto dataInfo = vk::PipelineBinaryDataInfoKHR{};
                dataInfo.pipelineBinary = *binary;
                auto [binaryKey, binaryData] = device_.get().getPipelineBinaryDataKHR(dataInfo);
                nrAssert(validKey(binaryKey), "VK_KHR_pipeline_binary returned an invalid binary key.");
                artifact.binaries.push_back(BinaryBlob{.key = binaryKey, .data = std::move(binaryData)});
            });
        }
    }
    catch (const vk::SystemError &error)
    {
        nrLog<LogLevel::warning>("PipelineBinaryStore failed to capture PSO binaries: {}", error.what());
        releaseCapturedData(pipeline);
        return;
    }

    if (artifact.binaries.empty())
    {
        nrLog<LogLevel::warning>("PipelineBinaryStore captured no binaries for a valid PSO.");
    }
    else
    {
        if (writeArtifact(cacheKey, artifact))
        {
            persistedCaptureCount_.fetch_add(1u, std::memory_order_relaxed);
        }
    }
    releaseCapturedData(pipeline);
}

void PipelineBinaryStore::markLoadAccepted() noexcept
{
    acceptedLoadCount_.fetch_add(1u, std::memory_order_relaxed);
}

[[nodiscard]] std::uint64_t PipelineBinaryStore::acceptedLoadCount() const noexcept
{
    return acceptedLoadCount_.load(std::memory_order_relaxed);
}

[[nodiscard]] std::uint64_t PipelineBinaryStore::persistedCaptureCount() const noexcept
{
    return persistedCaptureCount_.load(std::memory_order_relaxed);
}

void PipelineBinaryStore::invalidate(const PipelineBinaryCacheKey &cacheKey) noexcept
{
    auto const path = artifactPath(cacheKey);
    auto lock = std::scoped_lock{fileMutex_};
    auto error = std::error_code{};
    std::filesystem::remove(path, error);
    if (error)
    {
        logArtifactWarning("failed to invalidate", path, error.message());
    }
}
} // namespace nr::rhi
