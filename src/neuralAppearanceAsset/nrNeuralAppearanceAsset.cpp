module;

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

module nr.neuralAppearanceAsset;

import dependency.crypto;
import std;

namespace nr::neuralAppearance
{
namespace
{
inline constexpr std::array<std::byte, 4u> v3Magic{
    std::byte{static_cast<unsigned char>('N')}, std::byte{static_cast<unsigned char>('A')},
    std::byte{static_cast<unsigned char>('R')}, std::byte{static_cast<unsigned char>('T')},
};

// Fixed eight-affine topology mirroring shader/include/neuralAppearance/layout.slang.
// Every FP16 byte the model defines is covered by exactly one weight or bias
// range below; the remaining bytes are portable padding and must stay zero.
inline constexpr std::array<ArtifactLayerDescriptor, v3AffineLayerCount> layerDescriptors{
    ArtifactLayerDescriptor{.weightOffset = 0u, .rowStrideBytes = 24u, .inputCount = 12u, .outputCount = 32u,
                            .biasOffset = 768u, .biasCount = 32u},
    ArtifactLayerDescriptor{.weightOffset = 832u, .rowStrideBytes = 64u, .inputCount = 32u, .outputCount = 32u,
                            .biasOffset = 2'880u, .biasCount = 32u},
    ArtifactLayerDescriptor{.weightOffset = 2'944u, .rowStrideBytes = 64u, .inputCount = 32u, .outputCount = 8u,
                            .biasOffset = 3'456u, .biasCount = 8u},
    ArtifactLayerDescriptor{.weightOffset = 3'520u, .rowStrideBytes = 16u, .inputCount = 8u, .outputCount = 6u,
                            .biasOffset = 3'648u, .biasCount = 6u},
    ArtifactLayerDescriptor{.weightOffset = 3'712u, .rowStrideBytes = 16u, .inputCount = 8u, .outputCount = 32u,
                            .biasOffset = 4'224u, .biasCount = 32u},
    ArtifactLayerDescriptor{.weightOffset = 4'288u, .rowStrideBytes = 16u, .inputCount = 8u, .outputCount = 32u,
                            .biasOffset = v3NoBiasOffset, .biasCount = 0u},
    ArtifactLayerDescriptor{.weightOffset = 4'800u, .rowStrideBytes = 64u, .inputCount = 32u, .outputCount = 32u,
                            .biasOffset = 6'848u, .biasCount = 32u},
    ArtifactLayerDescriptor{.weightOffset = 6'912u, .rowStrideBytes = 64u, .inputCount = 32u, .outputCount = 6u,
                            .biasOffset = 7'296u, .biasCount = 6u},
};

std::atomic_uint64_t temporaryArtifactNonce = 0u;

enum class HeaderOffset : std::size_t
{
    magic = 0u,
    version = 4u,
    headerBytes = 8u,
    totalBytes = 12u,
    payloadBytes = 16u,
    endianness = 20u,
    modelLogicalBytes = 24u,
    modelStorageBytes = 28u,
    topology = 32u,
    coordinateBasis = 36u,
    activation = 40u,
    outputSemantic = 44u,
    affineLayerCount = 48u,
    trainableScalarCount = 52u,
    materialInputCount = 56u,
    elementFormat = 60u,
    completedSteps = 64u,
    batchSize = 68u,
    sampleCount = 72u,
    trainingProfileDigest = 80u,
    payloadDigest = 112u,
    layerDescriptors = 144u,
    reserved = 336u,
};

inline constexpr std::uint32_t v3Topology = 2u;
inline constexpr std::uint32_t v3CoordinateBasis = 1u;
inline constexpr std::uint32_t v3Activation = 2u;
inline constexpr std::uint32_t v3OutputSemantic = 2u;
inline constexpr std::uint32_t v3ElementFormat = 1u;
inline constexpr std::uint32_t v3Endianness = 0x01020304u;

static_assert(static_cast<std::size_t>(HeaderOffset::layerDescriptors) +
                  v3AffineLayerCount * v3LayerDescriptorBytes ==
              static_cast<std::size_t>(HeaderOffset::reserved));
static_assert(static_cast<std::size_t>(HeaderOffset::reserved) < v3HeaderBytes);

[[nodiscard]] std::uint32_t readLe32(std::span<const std::byte> bytes, std::size_t offset) noexcept
{
    return static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset])) |
           (static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset + 1u])) << 8u) |
           (static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset + 2u])) << 16u) |
           (static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset + 3u])) << 24u);
}

[[nodiscard]] std::uint64_t readLe64(std::span<const std::byte> bytes, std::size_t offset) noexcept
{
    auto value = std::uint64_t{};
    for (auto index = std::size_t{}; index < sizeof(value); ++index)
    {
        value |= static_cast<std::uint64_t>(std::to_integer<unsigned char>(bytes[offset + index])) << (index * 8u);
    }
    return value;
}

void writeLe32(std::span<std::byte> bytes, std::size_t offset, std::uint32_t value) noexcept
{
    for (auto index = std::size_t{}; index < sizeof(value); ++index)
    {
        bytes[offset + index] = std::byte{static_cast<unsigned char>((value >> (index * 8u)) & 0xffu)};
    }
}

void writeLe64(std::span<std::byte> bytes, std::size_t offset, std::uint64_t value) noexcept
{
    for (auto index = std::size_t{}; index < sizeof(value); ++index)
    {
        bytes[offset + index] = std::byte{static_cast<unsigned char>((value >> (index * 8u)) & 0xffu)};
    }
}

[[nodiscard]] bool isFiniteFp16(std::byte low, std::byte high) noexcept
{
    auto const bits = static_cast<std::uint16_t>(std::to_integer<unsigned char>(low)) |
                      (static_cast<std::uint16_t>(std::to_integer<unsigned char>(high)) << 8u);
    return (bits & 0x7c00u) != 0x7c00u;
}

struct ModelByteRange
{
    std::size_t offset = 0u;
    std::size_t size = 0u;
};

[[nodiscard]] bool rangeContains(ModelByteRange range, std::size_t byteOffset) noexcept
{
    return byteOffset >= range.offset && byteOffset - range.offset < range.size;
}

[[nodiscard]] ModelByteRange weightRange(const ArtifactLayerDescriptor &descriptor) noexcept
{
    return ModelByteRange{
        .offset = descriptor.weightOffset,
        .size = static_cast<std::size_t>(descriptor.outputCount) * descriptor.rowStrideBytes,
    };
}

[[nodiscard]] ModelByteRange biasRange(const ArtifactLayerDescriptor &descriptor) noexcept
{
    return descriptor.biasOffset == v3NoBiasOffset
               ? ModelByteRange{}
               : ModelByteRange{.offset = descriptor.biasOffset, .size = descriptor.biasCount * 2u};
}

[[nodiscard]] bool isDefinedModelByte(std::size_t byteOffset) noexcept
{
    return std::ranges::any_of(layerDescriptors, [byteOffset](const ArtifactLayerDescriptor &descriptor) {
        return rangeContains(weightRange(descriptor), byteOffset) ||
               rangeContains(biasRange(descriptor), byteOffset);
    });
}

[[nodiscard]] bool hasValidModelStorage(std::span<const std::byte> model) noexcept
{
    if (model.size() != v3ModelBytes)
    {
        return false;
    }
    auto definedBytes = std::size_t{};
    for (auto byteOffset = std::size_t{}; byteOffset < model.size(); byteOffset += 2u)
    {
        if (!isDefinedModelByte(byteOffset))
        {
            if (model[byteOffset] != std::byte{} || model[byteOffset + 1u] != std::byte{})
            {
                return false;
            }
            continue;
        }
        if (!isFiniteFp16(model[byteOffset], model[byteOffset + 1u]))
        {
            return false;
        }
        definedBytes += 2u;
    }
    return definedBytes == v3ModelLogicalBytes;
}

[[nodiscard]] bool hasArtifactExtension(const std::filesystem::path &path) noexcept
{
    return path.extension().generic_string() == ".nart";
}

[[nodiscard]] bool headerDescribesThisRenderer(std::span<const std::byte> header) noexcept
{
    auto const matchesLayerDescriptors =
        std::ranges::all_of(std::views::iota(std::size_t{}, layerDescriptors.size()), [&](std::size_t layerIndex) {
            auto const offset = static_cast<std::size_t>(HeaderOffset::layerDescriptors) +
                                layerIndex * v3LayerDescriptorBytes;
            auto const &descriptor = layerDescriptors[layerIndex];
            return readLe32(header, offset) == descriptor.weightOffset &&
                   readLe32(header, offset + 4u) == descriptor.rowStrideBytes &&
                   readLe32(header, offset + 8u) == descriptor.inputCount &&
                   readLe32(header, offset + 12u) == descriptor.outputCount &&
                   readLe32(header, offset + 16u) == descriptor.biasOffset &&
                   readLe32(header, offset + 20u) == descriptor.biasCount;
        });
    return readLe32(header, static_cast<std::size_t>(HeaderOffset::headerBytes)) == v3HeaderBytes &&
           readLe32(header, static_cast<std::size_t>(HeaderOffset::totalBytes)) == v3TotalBytes &&
           readLe32(header, static_cast<std::size_t>(HeaderOffset::payloadBytes)) == v3PayloadBytes &&
           readLe32(header, static_cast<std::size_t>(HeaderOffset::endianness)) == v3Endianness &&
           readLe32(header, static_cast<std::size_t>(HeaderOffset::modelLogicalBytes)) == v3ModelLogicalBytes &&
           readLe32(header, static_cast<std::size_t>(HeaderOffset::modelStorageBytes)) == v3ModelBytes &&
           readLe32(header, static_cast<std::size_t>(HeaderOffset::topology)) == v3Topology &&
           readLe32(header, static_cast<std::size_t>(HeaderOffset::coordinateBasis)) == v3CoordinateBasis &&
           readLe32(header, static_cast<std::size_t>(HeaderOffset::activation)) == v3Activation &&
           readLe32(header, static_cast<std::size_t>(HeaderOffset::outputSemantic)) == v3OutputSemantic &&
           readLe32(header, static_cast<std::size_t>(HeaderOffset::affineLayerCount)) == v3AffineLayerCount &&
           readLe32(header, static_cast<std::size_t>(HeaderOffset::trainableScalarCount)) == v3TrainableScalarCount &&
           readLe32(header, static_cast<std::size_t>(HeaderOffset::materialInputCount)) == v3MaterialInputCount &&
           readLe32(header, static_cast<std::size_t>(HeaderOffset::elementFormat)) == v3ElementFormat &&
           matchesLayerDescriptors;
}

[[nodiscard]] std::expected<void, std::string> publishAtomically(const std::filesystem::path &destination,
                                                                 std::span<const std::byte> header,
                                                                 std::span<const std::byte> payload)
{
    auto destinationParent = destination.parent_path();
    if (destinationParent.empty())
    {
        return std::unexpected("Neural artifact destination must have a parent directory.");
    }
    auto directoryError = std::error_code{};
    std::filesystem::create_directories(destinationParent, directoryError);
    if (directoryError)
    {
        return std::unexpected(std::format("Failed to create neural artifact directory '{}': {}.",
                                           destinationParent.generic_string(), directoryError.message()));
    }

    auto const nonce = temporaryArtifactNonce.fetch_add(1u, std::memory_order_relaxed);
    auto temporary = destinationParent /
                     std::format(".{}.{}.{}.tmp", destination.filename().string(), GetCurrentProcessId(), nonce);
    auto output = std::ofstream{temporary, std::ios::binary | std::ios::trunc};
    if (!output)
    {
        return std::unexpected(
            std::format("Failed to open temporary neural artifact '{}'.", temporary.generic_string()));
    }
    output.write(reinterpret_cast<const char *>(header.data()), static_cast<std::streamsize>(header.size()));
    output.write(reinterpret_cast<const char *>(payload.data()), static_cast<std::streamsize>(payload.size()));
    output.flush();
    if (!output.good())
    {
        return std::unexpected(
            std::format("Failed to flush temporary neural artifact '{}'.", temporary.generic_string()));
    }
    output.close();

    auto fileHandle = CreateFileW(temporary.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (fileHandle == INVALID_HANDLE_VALUE || !FlushFileBuffers(fileHandle))
    {
        if (fileHandle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(fileHandle);
        }
        std::filesystem::remove(temporary, directoryError);
        return std::unexpected(
            std::format("Failed to flush temporary neural artifact '{}'.", temporary.generic_string()));
    }
    CloseHandle(fileHandle);
    if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        auto const error = GetLastError();
        std::filesystem::remove(temporary, directoryError);
        return std::unexpected(std::format("Failed to atomically publish neural artifact '{}': Win32 error {}.",
                                           destination.generic_string(), error));
    }
    return {};
}
} // namespace

Artifact::Artifact(dependency::crypto::Sha256Digest payloadDigest, std::uint32_t completedSteps,
                   std::array<std::byte, v3PayloadBytes> payload) noexcept
    : payloadDigest_(payloadDigest), completedSteps_(completedSteps), payload_(payload)
{
}

std::span<const std::byte> Artifact::modelBytes() const noexcept
{
    return std::span{payload_}.subspan(0u, v3ModelBytes);
}

const dependency::crypto::Sha256Digest &Artifact::payloadDigest() const noexcept
{
    return payloadDigest_;
}

std::uint32_t Artifact::completedSteps() const noexcept
{
    return completedSteps_;
}

std::optional<dependency::crypto::Sha256Digest> v3TrainingProfileDigest() noexcept
{
    return dependency::crypto::sha256(std::as_bytes(std::span{v3TrainingProfileText}));
}

std::span<const ArtifactLayerDescriptor, v3AffineLayerCount> v3LayerDescriptors() noexcept
{
    return std::span<const ArtifactLayerDescriptor, v3AffineLayerCount>{layerDescriptors};
}

std::expected<std::shared_ptr<const Artifact>, std::string> loadArtifactV3(const std::filesystem::path &artifactPath)
{
    if (!hasArtifactExtension(artifactPath))
    {
        return std::unexpected("Neural artifact V3 paths must use the exact lowercase '.nart' extension.");
    }

    auto error = std::error_code{};
    if (!std::filesystem::is_regular_file(artifactPath, error) || error)
    {
        return std::unexpected(std::format("Neural artifact '{}' is not a regular file.", artifactPath.generic_string()));
    }
    auto const fileBytes = std::filesystem::file_size(artifactPath, error);
    if (error || fileBytes != v3TotalBytes)
    {
        return std::unexpected(std::format("Neural artifact '{}' must be exactly {} bytes; earlier artifacts require retraining.",
                                           artifactPath.generic_string(), v3TotalBytes));
    }

    auto input = std::ifstream{artifactPath, std::ios::binary};
    if (!input)
    {
        return std::unexpected(std::format("Failed to open neural artifact '{}'.", artifactPath.generic_string()));
    }
    auto header = std::array<std::byte, v3HeaderBytes>{};
    auto payload = std::array<std::byte, v3PayloadBytes>{};
    input.read(reinterpret_cast<char *>(header.data()), static_cast<std::streamsize>(header.size()));
    input.read(reinterpret_cast<char *>(payload.data()), static_cast<std::streamsize>(payload.size()));
    if (!input.good() && !input.eof())
    {
        return std::unexpected(std::format("Failed to read neural artifact '{}'.", artifactPath.generic_string()));
    }

    auto const headerBytes = std::span<const std::byte>{header};
    if (!std::ranges::equal(headerBytes.subspan(0u, v3Magic.size()), v3Magic) ||
        readLe32(headerBytes, static_cast<std::size_t>(HeaderOffset::version)) != 3u)
    {
        return std::unexpected("Neural artifact magic is not V3; earlier artifacts require retraining.");
    }
    if (!headerDescribesThisRenderer(headerBytes))
    {
        return std::unexpected("Neural artifact V3 layout does not match this renderer.");
    }
    if (!std::ranges::all_of(headerBytes.subspan(static_cast<std::size_t>(HeaderOffset::reserved)),
                             [](std::byte value) { return value == std::byte{}; }))
    {
        return std::unexpected("Neural artifact V3 reserved header bytes must be zero.");
    }

    auto profileDigest = v3TrainingProfileDigest();
    auto storedProfileDigest = dependency::crypto::Sha256Digest{};
    auto storedPayloadDigest = dependency::crypto::Sha256Digest{};
    std::memcpy(storedProfileDigest.data(),
                header.data() + static_cast<std::size_t>(HeaderOffset::trainingProfileDigest),
                storedProfileDigest.size());
    std::memcpy(storedPayloadDigest.data(), header.data() + static_cast<std::size_t>(HeaderOffset::payloadDigest),
                storedPayloadDigest.size());
    if (!profileDigest || storedProfileDigest != *profileDigest)
    {
        return std::unexpected("Neural artifact V3 was not produced by this renderer's training profile.");
    }

    auto const completedSteps = readLe32(headerBytes, static_cast<std::size_t>(HeaderOffset::completedSteps));
    if (completedSteps != v3CompletedSteps ||
        readLe32(headerBytes, static_cast<std::size_t>(HeaderOffset::batchSize)) != v3BatchSize ||
        readLe64(headerBytes, static_cast<std::size_t>(HeaderOffset::sampleCount)) != v3SampleCount)
    {
        return std::unexpected("Neural artifact V3 training metadata does not match the production profile.");
    }
    if (!hasValidModelStorage(std::span<const std::byte>{payload}.subspan(0u, v3ModelBytes)))
    {
        return std::unexpected("Neural artifact V3 model storage has non-zero padding or a non-finite FP16 value.");
    }

    auto computedPayloadDigest = dependency::crypto::sha256(std::span<const std::byte>{payload});
    if (!computedPayloadDigest || *computedPayloadDigest != storedPayloadDigest)
    {
        return std::unexpected("Neural artifact V3 payload SHA-256 verification failed.");
    }
    return std::shared_ptr<const Artifact>{new Artifact{storedPayloadDigest, completedSteps, payload}};
}

std::expected<void, std::string> writeArtifactV3(const ArtifactWriteRequest &request)
{
    auto profileDigest = v3TrainingProfileDigest();
    if (request.destination.empty() || !hasArtifactExtension(request.destination) ||
        !hasValidModelStorage(request.model) || !profileDigest || request.completedSteps != v3CompletedSteps ||
        request.batchSize != v3BatchSize || request.sampleCount != v3SampleCount)
    {
        return std::unexpected("Neural artifact writer received invalid V3 metadata or payload dimensions.");
    }

    auto payload = std::array<std::byte, v3PayloadBytes>{};
    std::memcpy(payload.data(), request.model.data(), request.model.size());
    auto payloadDigest = dependency::crypto::sha256(std::span<const std::byte>{payload});
    if (!payloadDigest)
    {
        return std::unexpected("Failed to calculate the neural artifact payload SHA-256.");
    }

    auto header = std::array<std::byte, v3HeaderBytes>{};
    std::ranges::copy(v3Magic, header.begin());
    auto headerBytes = std::span{header};
    writeLe32(headerBytes, static_cast<std::size_t>(HeaderOffset::version), 3u);
    writeLe32(headerBytes, static_cast<std::size_t>(HeaderOffset::headerBytes), v3HeaderBytes);
    writeLe32(headerBytes, static_cast<std::size_t>(HeaderOffset::totalBytes), v3TotalBytes);
    writeLe32(headerBytes, static_cast<std::size_t>(HeaderOffset::payloadBytes), v3PayloadBytes);
    writeLe32(headerBytes, static_cast<std::size_t>(HeaderOffset::endianness), v3Endianness);
    writeLe32(headerBytes, static_cast<std::size_t>(HeaderOffset::modelLogicalBytes), v3ModelLogicalBytes);
    writeLe32(headerBytes, static_cast<std::size_t>(HeaderOffset::modelStorageBytes), v3ModelBytes);
    writeLe32(headerBytes, static_cast<std::size_t>(HeaderOffset::topology), v3Topology);
    writeLe32(headerBytes, static_cast<std::size_t>(HeaderOffset::coordinateBasis), v3CoordinateBasis);
    writeLe32(headerBytes, static_cast<std::size_t>(HeaderOffset::activation), v3Activation);
    writeLe32(headerBytes, static_cast<std::size_t>(HeaderOffset::outputSemantic), v3OutputSemantic);
    writeLe32(headerBytes, static_cast<std::size_t>(HeaderOffset::affineLayerCount), v3AffineLayerCount);
    writeLe32(headerBytes, static_cast<std::size_t>(HeaderOffset::trainableScalarCount), v3TrainableScalarCount);
    writeLe32(headerBytes, static_cast<std::size_t>(HeaderOffset::materialInputCount), v3MaterialInputCount);
    writeLe32(headerBytes, static_cast<std::size_t>(HeaderOffset::elementFormat), v3ElementFormat);
    writeLe32(headerBytes, static_cast<std::size_t>(HeaderOffset::completedSteps), request.completedSteps);
    writeLe32(headerBytes, static_cast<std::size_t>(HeaderOffset::batchSize), request.batchSize);
    writeLe64(headerBytes, static_cast<std::size_t>(HeaderOffset::sampleCount), request.sampleCount);
    std::memcpy(header.data() + static_cast<std::size_t>(HeaderOffset::trainingProfileDigest), profileDigest->data(),
                profileDigest->size());
    std::memcpy(header.data() + static_cast<std::size_t>(HeaderOffset::payloadDigest), payloadDigest->data(),
                payloadDigest->size());
    for (auto layerIndex = std::size_t{}; layerIndex < layerDescriptors.size(); ++layerIndex)
    {
        auto const offset =
            static_cast<std::size_t>(HeaderOffset::layerDescriptors) + layerIndex * v3LayerDescriptorBytes;
        auto const &descriptor = layerDescriptors[layerIndex];
        writeLe32(headerBytes, offset, descriptor.weightOffset);
        writeLe32(headerBytes, offset + 4u, descriptor.rowStrideBytes);
        writeLe32(headerBytes, offset + 8u, descriptor.inputCount);
        writeLe32(headerBytes, offset + 12u, descriptor.outputCount);
        writeLe32(headerBytes, offset + 16u, descriptor.biasOffset);
        writeLe32(headerBytes, offset + 20u, descriptor.biasCount);
    }

    return publishAtomically(request.destination, header, payload);
}
} // namespace nr::neuralAppearance
