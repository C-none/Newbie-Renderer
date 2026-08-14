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
import dependency.json;
import nr.load;
import nr.resource;
import std;

namespace nr::neuralAppearance
{
namespace
{
inline constexpr std::array<std::byte, 4u> v2Magic{
    std::byte{static_cast<unsigned char>('N')}, std::byte{static_cast<unsigned char>('A')},
    std::byte{static_cast<unsigned char>('R')}, std::byte{static_cast<unsigned char>('T')},
};
inline constexpr std::size_t maximumSidecarBytes = 64u * 1024u;
inline constexpr std::size_t headerLayerDescriptorOffset = 204u;
inline constexpr std::size_t headerLayerDescriptorBytes = v2LayerDescriptorBytes;
inline constexpr std::size_t headerCompletedStepsOffset = 324u;
inline constexpr std::size_t headerBatchSizeOffset = 328u;
inline constexpr std::size_t headerSampleCountOffset = 332u;
inline constexpr std::size_t headerElementFormatOffset = 340u;
inline constexpr std::size_t headerLatentSamplingOffset = 344u;
inline constexpr std::size_t headerTrainingProfileIdOffset = 348u;
inline constexpr std::size_t headerTrainableScalarCountOffset = 352u;
inline constexpr std::size_t headerReservedOffset = 356u;
inline constexpr std::array<ArtifactLayerDescriptor, 5u> v2LayerDescriptors{
    ArtifactLayerDescriptor{.weightOffset = 0u, .rowStrideBytes = 16u, .inputCount = 8u, .outputCount = 12u,
                            .biasOffset = 192u, .biasCount = 12u},
    ArtifactLayerDescriptor{.weightOffset = 256u, .rowStrideBytes = 16u, .inputCount = 8u, .outputCount = 32u,
                            .biasOffset = 768u, .biasCount = 32u},
    ArtifactLayerDescriptor{.weightOffset = 832u, .rowStrideBytes = 24u, .inputCount = 12u, .outputCount = 32u,
                            .biasOffset = v2NoBiasOffset, .biasCount = 0u},
    ArtifactLayerDescriptor{.weightOffset = 1'600u, .rowStrideBytes = 64u, .inputCount = 32u, .outputCount = 32u,
                            .biasOffset = 3'648u, .biasCount = 32u},
    ArtifactLayerDescriptor{.weightOffset = 3'712u, .rowStrideBytes = 64u, .inputCount = 32u, .outputCount = 3u,
                            .biasOffset = 3'904u, .biasCount = 3u},
};
struct ModelByteRange
{
    std::size_t offset = 0u;
    std::size_t size = 0u;
};
inline constexpr std::array<ModelByteRange, 9u> v2ModelValueRanges{
    ModelByteRange{.offset = 0u, .size = 192u}, ModelByteRange{.offset = 192u, .size = 24u},
    ModelByteRange{.offset = 256u, .size = 512u}, ModelByteRange{.offset = 768u, .size = 64u},
    ModelByteRange{.offset = 832u, .size = 768u},
    ModelByteRange{.offset = 1'600u, .size = 2'048u}, ModelByteRange{.offset = 3'648u, .size = 64u},
    ModelByteRange{.offset = 3'712u, .size = 192u}, ModelByteRange{.offset = 3'904u, .size = 6u},
};
inline constexpr std::array<ModelByteRange, 2u> v2ModelPaddingRanges{
    ModelByteRange{.offset = 216u, .size = 40u},
    ModelByteRange{.offset = 3'910u, .size = 58u},
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
    latent0Offset = 32u,
    latent0Bytes = 36u,
    latent1Offset = 40u,
    latent1Bytes = 44u,
    latentWidth = 48u,
    latentHeight = 52u,
    latentChannels = 56u,
    topology = 60u,
    coordinateBasis = 64u,
    activation = 68u,
    outputSemantic = 72u,
    sourceMaterialIndex = 76u,
    uvSet = 80u,
    uvAffine = 84u,
    sourceSceneDigest = 108u,
    trainingProfileDigest = 140u,
    payloadDigest = 172u,
};

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

void writeLeFloat(std::span<std::byte> bytes, std::size_t offset, float value) noexcept
{
    writeLe32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

[[nodiscard]] float readLeFloat(std::span<const std::byte> bytes, std::size_t offset) noexcept
{
    return std::bit_cast<float>(readLe32(bytes, offset));
}

[[nodiscard]] bool isFiniteFp16(std::byte low, std::byte high) noexcept
{
    auto const bits = static_cast<std::uint16_t>(std::to_integer<unsigned char>(low)) |
                      (static_cast<std::uint16_t>(std::to_integer<unsigned char>(high)) << 8u);
    return (bits & 0x7c00u) != 0x7c00u;
}

[[nodiscard]] bool isNonzeroDigest(const dependency::crypto::Sha256Digest &digest) noexcept
{
    return std::ranges::any_of(digest, [](std::byte value) { return value != std::byte{}; });
}

[[nodiscard]] bool hasValidModelStorage(std::span<const std::byte> model) noexcept
{
    if (model.size() != v2ModelBytes)
    {
        return false;
    }
    if (!std::ranges::all_of(v2ModelPaddingRanges, [&](ModelByteRange range) {
            return std::ranges::all_of(model.subspan(range.offset, range.size),
                                       [](std::byte value) { return value == std::byte{}; });
        }))
    {
        return false;
    }
    return std::ranges::all_of(v2ModelValueRanges, [&](ModelByteRange range) {
        auto values = model.subspan(range.offset, range.size);
        for (auto index = std::size_t{}; index < values.size(); index += 2u)
        {
            if (!isFiniteFp16(values[index], values[index + 1u]))
            {
                return false;
            }
        }
        return true;
    });
}

[[nodiscard]] bool isSafeRelativePath(const std::filesystem::path &path) noexcept
{
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory())
    {
        return false;
    }
    auto const text = path.generic_string();
    if (text.starts_with("//") || text.starts_with(R"(\\)") || text.contains(':') ||
        text.find_first_of("|&;<>'\"`$*?\n\r\t") != std::string::npos)
    {
        return false;
    }
    return std::ranges::none_of(path, [](const std::filesystem::path &component) {
        return component == "." || component == ".." || component.empty();
    });
}

[[nodiscard]] bool hasV2ArtifactExtension(const std::filesystem::path &path) noexcept
{
    return path.extension().generic_string() == ".nart";
}

[[nodiscard]] bool isContainedBy(const std::filesystem::path &child, const std::filesystem::path &root) noexcept
{
    auto relative = child.lexically_relative(root);
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}

[[nodiscard]] std::expected<std::vector<std::byte>, std::string> readRegularFile(const std::filesystem::path &path,
                                                                                   std::size_t maximumBytes)
{
    auto error = std::error_code{};
    if (!std::filesystem::is_regular_file(path, error) || error)
    {
        return std::unexpected(std::format("Path is not a regular file: '{}'.", path.generic_string()));
    }
    auto const bytes = std::filesystem::file_size(path, error);
    if (error || bytes > maximumBytes)
    {
        return std::unexpected(std::format("File '{}' exceeds its allowed size.", path.generic_string()));
    }
    auto input = std::ifstream{path, std::ios::binary};
    if (!input)
    {
        return std::unexpected(std::format("Failed to open '{}'.", path.generic_string()));
    }
    auto output = std::vector<std::byte>(static_cast<std::size_t>(bytes));
    if (!output.empty())
    {
        input.read(reinterpret_cast<char *>(output.data()), static_cast<std::streamsize>(output.size()));
    }
    if (!input.good() && !input.eof())
    {
        return std::unexpected(std::format("Failed to read '{}'.", path.generic_string()));
    }
    return output;
}

[[nodiscard]] std::expected<std::filesystem::path, std::string> canonicalRegularFileContained(
    const std::filesystem::path &candidate, const std::filesystem::path &root)
{
    auto error = std::error_code{};
    auto const canonicalRoot = std::filesystem::canonical(root, error);
    if (error)
    {
        return std::unexpected(std::format("Failed to canonicalize asset root '{}': {}.", root.generic_string(),
                                           error.message()));
    }
    auto const canonicalCandidate = std::filesystem::canonical(candidate, error);
    if (error || !std::filesystem::is_regular_file(canonicalCandidate, error) || error ||
        !isContainedBy(canonicalCandidate, canonicalRoot))
    {
        return std::unexpected(std::format("Artifact path is not a regular file under '{}'.", canonicalRoot.generic_string()));
    }
    return canonicalCandidate;
}

[[nodiscard]] std::expected<const dependency::json::JsonValue::Object *, std::string> asObject(
    const dependency::json::JsonValue &value)
{
    if (auto const *object = std::get_if<dependency::json::JsonValue::Object>(std::addressof(value.storage)); object != nullptr)
    {
        return object;
    }
    return std::unexpected("Neural material sidecar root must be an object.");
}

[[nodiscard]] std::expected<std::string_view, std::string> requiredString(
    const dependency::json::JsonValue::Object &object, std::string_view key)
{
    auto const iterator = object.find(key);
    if (iterator == object.end())
    {
        return std::unexpected(std::format("Neural material sidecar is missing '{}'.", key));
    }
    if (auto const *text = std::get_if<std::string>(std::addressof(iterator->second.storage)); text != nullptr)
    {
        return *text;
    }
    return std::unexpected(std::format("Neural material sidecar field '{}' must be a string.", key));
}

[[nodiscard]] std::expected<std::uint32_t, std::string> requiredMaterial(
    const dependency::json::JsonValue::Object &object)
{
    auto const iterator = object.find("material");
    if (iterator == object.end())
    {
        return std::unexpected("Neural material sidecar is missing 'material'.");
    }
    if (auto const *value = std::get_if<std::uint64_t>(std::addressof(iterator->second.storage));
        value != nullptr && *value <= std::numeric_limits<std::uint32_t>::max())
    {
        return static_cast<std::uint32_t>(*value);
    }
    return std::unexpected("Neural material sidecar field 'material' must be a non-negative uint32.");
}

[[nodiscard]] dependency::crypto::Sha256Digest sceneDigest(std::string_view normalizedScene) noexcept
{
    auto bytes = std::as_bytes(std::span{normalizedScene.data(), normalizedScene.size()});
    return dependency::crypto::sha256(bytes).value_or(dependency::crypto::Sha256Digest{});
}

[[nodiscard]] bool equivalentIdentityUv(const nr::resource::MaterialTextureTransform &transform) noexcept
{
    return transform.linear.x == 1.0f && transform.linear.y == 0.0f && transform.linear.z == 0.0f &&
           transform.linear.w == 1.0f && transform.offset.x == 0.0f && transform.offset.y == 0.0f;
}
} // namespace

Artifact::Artifact(ArtifactBindingContract bindingContract, dependency::crypto::Sha256Digest payloadDigest,
                   std::array<std::byte, v2PayloadBytes> payload) noexcept
    : bindingContract_{std::move(bindingContract)}, payloadDigest_{payloadDigest}, payload_{payload}
{
}

[[nodiscard]] std::span<const std::byte> Artifact::modelBytes() const noexcept
{
    return std::span{payload_}.first(v2ModelBytes);
}

[[nodiscard]] std::span<const std::byte> Artifact::latentPlane(std::size_t index) const noexcept
{
    if (index >= v2LatentPlaneCount)
    {
        return {};
    }
    return std::span{payload_}.subspan(v2ModelBytes + index * v2LatentPlaneBytes, v2LatentPlaneBytes);
}

[[nodiscard]] const ArtifactBindingContract &Artifact::bindingContract() const noexcept
{
    return bindingContract_;
}

[[nodiscard]] const dependency::crypto::Sha256Digest &Artifact::payloadDigest() const noexcept
{
    return payloadDigest_;
}

[[nodiscard]] std::expected<std::shared_ptr<const Artifact>, std::string> loadArtifactV2(
    const std::filesystem::path &artifactPath)
{
    if (!hasV2ArtifactExtension(artifactPath))
    {
        return std::unexpected("Neural artifact V2 paths must use the exact lowercase '.nart' extension.");
    }
    auto file = readRegularFile(artifactPath, v2TotalBytes);
    if (!file)
    {
        return std::unexpected(file.error());
    }
    if (file->size() != v2TotalBytes)
    {
        return std::unexpected(std::format("Neural artifact '{}' must be exactly {} bytes; V1 artifacts require retraining.",
                                           artifactPath.generic_string(), v2TotalBytes));
    }
    auto header = std::span<const std::byte>{file->data(), v2HeaderBytes};
    if (!std::ranges::equal(header.first(v2Magic.size()), v2Magic))
    {
        return std::unexpected("Neural artifact magic is not V2; V1 artifacts require retraining.");
    }
    auto field = [&](HeaderOffset offset) { return readLe32(header, static_cast<std::size_t>(offset)); };
    if (field(HeaderOffset::version) != 2u || field(HeaderOffset::headerBytes) != v2HeaderBytes ||
        field(HeaderOffset::totalBytes) != v2TotalBytes || field(HeaderOffset::payloadBytes) != v2PayloadBytes ||
        field(HeaderOffset::endianness) != 0x01020304u || field(HeaderOffset::modelLogicalBytes) != v2ModelLogicalBytes ||
        field(HeaderOffset::modelStorageBytes) != v2ModelBytes || field(HeaderOffset::latent0Offset) != v2ModelBytes ||
        field(HeaderOffset::latent0Bytes) != v2LatentPlaneBytes ||
        field(HeaderOffset::latent1Offset) != v2ModelBytes + v2LatentPlaneBytes ||
        field(HeaderOffset::latent1Bytes) != v2LatentPlaneBytes || field(HeaderOffset::latentWidth) != 64u ||
        field(HeaderOffset::latentHeight) != 64u || field(HeaderOffset::latentChannels) != 8u ||
        field(HeaderOffset::topology) != 1u || field(HeaderOffset::coordinateBasis) != 1u ||
        field(HeaderOffset::activation) != 1u || field(HeaderOffset::outputSemantic) != 1u ||
        field(HeaderOffset::sourceMaterialIndex) != 0u || field(HeaderOffset::uvSet) != 0u)
    {
        return std::unexpected("Neural artifact V2 layout or material contract does not match this renderer.");
    }
    for (auto layerIndex = std::size_t{}; layerIndex < v2LayerDescriptors.size(); ++layerIndex)
    {
        auto const descriptorOffset = headerLayerDescriptorOffset + layerIndex * headerLayerDescriptorBytes;
        auto const &expected = v2LayerDescriptors[layerIndex];
        if (readLe32(header, descriptorOffset) != expected.weightOffset ||
            readLe32(header, descriptorOffset + 4u) != expected.rowStrideBytes ||
            readLe32(header, descriptorOffset + 8u) != expected.inputCount ||
            readLe32(header, descriptorOffset + 12u) != expected.outputCount ||
            readLe32(header, descriptorOffset + 16u) != expected.biasOffset ||
            readLe32(header, descriptorOffset + 20u) != expected.biasCount)
        {
            return std::unexpected("Neural artifact V2 layer descriptors do not match the fixed five-layer topology.");
        }
    }
    if (readLe32(header, headerCompletedStepsOffset) != 32'768u || readLe32(header, headerBatchSizeOffset) != 64u ||
        readLe64(header, headerSampleCountOffset) != 2'097'152u ||
        readLe32(header, headerElementFormatOffset) != 1u || readLe32(header, headerLatentSamplingOffset) != 1u ||
        readLe32(header, headerTrainingProfileIdOffset) != 1u ||
        readLe32(header, headerTrainableScalarCountOffset) != 1'935u)
    {
        return std::unexpected("Neural artifact V2 training metadata does not match the production profile.");
    }

    auto contract = ArtifactBindingContract{};
    contract.sourceMaterialIndex = field(HeaderOffset::sourceMaterialIndex);
    contract.uvSet = field(HeaderOffset::uvSet);
    for (auto index = std::size_t{}; index < contract.uvAffine.size(); ++index)
    {
        contract.uvAffine[index] = readLeFloat(header, static_cast<std::size_t>(HeaderOffset::uvAffine) + index * 4u);
        if (!std::isfinite(contract.uvAffine[index]))
        {
            return std::unexpected("Neural artifact UV affine contains a non-finite value.");
        }
    }
    if (contract.uvAffine != std::array<float, 6u>{1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f})
    {
        return std::unexpected("Neural artifact V2 only supports identity base-color UV transforms.");
    }
    std::memcpy(contract.sourceSceneDigest.data(), header.data() + static_cast<std::size_t>(HeaderOffset::sourceSceneDigest),
                contract.sourceSceneDigest.size());
    std::memcpy(contract.trainingProfileDigest.data(),
                header.data() + static_cast<std::size_t>(HeaderOffset::trainingProfileDigest),
                contract.trainingProfileDigest.size());
    auto expectedProfileDigest = v2TrainingProfileDigest();
    auto payloadDigest = dependency::crypto::Sha256Digest{};
    std::memcpy(payloadDigest.data(), header.data() + static_cast<std::size_t>(HeaderOffset::payloadDigest),
                payloadDigest.size());
    if (!expectedProfileDigest || !isNonzeroDigest(contract.sourceSceneDigest) ||
        contract.trainingProfileDigest != *expectedProfileDigest ||
        !isNonzeroDigest(payloadDigest))
    {
        return std::unexpected("Neural artifact V2 source, fixed training-profile, or payload digest is invalid.");
    }
    if (!std::ranges::all_of(header.subspan(headerReservedOffset), [](std::byte value) { return value == std::byte{}; }))
    {
        return std::unexpected("Neural artifact V2 reserved header bytes must be zero.");
    }

    auto payload = std::array<std::byte, v2PayloadBytes>{};
    std::memcpy(payload.data(), file->data() + v2HeaderBytes, payload.size());
    if (!hasValidModelStorage(std::span{payload}.first(v2ModelBytes)))
    {
        return std::unexpected("Neural artifact V2 model storage has non-zero padding or a non-finite FP16 value.");
    }
    for (auto index = v2ModelBytes; index < payload.size(); index += 2u)
    {
        if (!isFiniteFp16(payload[index], payload[index + 1u]))
        {
            return std::unexpected("Neural artifact V2 latent payload contains a non-finite FP16 value.");
        }
    }
    auto calculatedDigest = dependency::crypto::sha256(std::span{payload});
    if (!calculatedDigest.has_value() || *calculatedDigest != payloadDigest)
    {
        return std::unexpected("Neural artifact V2 payload SHA-256 verification failed.");
    }
    return std::shared_ptr<const Artifact>{new Artifact{std::move(contract), payloadDigest, payload}};
}

[[nodiscard]] std::optional<dependency::crypto::Sha256Digest> v2TrainingProfileDigest() noexcept
{
    return dependency::crypto::sha256(
        std::as_bytes(std::span{v2TrainingProfileText.data(), v2TrainingProfileText.size()}));
}

[[nodiscard]] std::expected<ArtifactBindingContract, std::string> makeArtifactBindingContract(
    std::string_view normalizedScenePath, std::uint32_t sourceMaterialIndex)
{
    auto path = std::filesystem::path{normalizedScenePath};
    if (!isSafeRelativePath(path) || sourceMaterialIndex != 0u)
    {
        return std::unexpected("Artifact binding contract requires a safe assets-relative scene and material 0.");
    }
    auto sourceDigest = dependency::crypto::sha256(std::as_bytes(std::span{normalizedScenePath.data(), normalizedScenePath.size()}));
    if (!sourceDigest)
    {
        return std::unexpected("Failed to calculate the neural artifact source-scene SHA-256.");
    }
    auto profileDigest = v2TrainingProfileDigest();
    if (!profileDigest)
    {
        return std::unexpected("Failed to calculate the fixed neural artifact training-profile SHA-256.");
    }
    return ArtifactBindingContract{
        .sourceSceneDigest = *sourceDigest,
        .trainingProfileDigest = *profileDigest,
        .sourceMaterialIndex = sourceMaterialIndex,
    };
}

[[nodiscard]] std::expected<void, std::string> writeArtifactV2(const ArtifactWriteRequest &request)
{
    auto profileDigest = v2TrainingProfileDigest();
    if (request.destination.empty() || !hasV2ArtifactExtension(request.destination) || !hasValidModelStorage(request.model) ||
        !isNonzeroDigest(request.bindingContract.sourceSceneDigest) ||
        !profileDigest || request.bindingContract.trainingProfileDigest != *profileDigest ||
        request.bindingContract.sourceMaterialIndex != 0u ||
        request.bindingContract.uvSet != 0u ||
        request.bindingContract.uvAffine != std::array<float, 6u>{1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f} ||
        request.completedSteps != 32'768u || request.batchSize != 64u || request.sampleCount != 2'097'152u)
    {
        return std::unexpected("Neural artifact writer received invalid V2 metadata or payload dimensions.");
    }
    for (auto const plane : request.latentPlanes)
    {
        if (plane.size() != v2LatentPlaneBytes)
        {
            return std::unexpected("Neural artifact writer requires two 32768-byte latent planes.");
        }
    }
    for (auto const bytes : std::array{request.latentPlanes[0u], request.latentPlanes[1u]})
    {
        for (auto index = std::size_t{}; index < bytes.size(); index += 2u)
        {
            if (!isFiniteFp16(bytes[index], bytes[index + 1u]))
            {
                return std::unexpected("Neural artifact writer rejected a non-finite FP16 payload value.");
            }
        }
    }

    auto payload = std::array<std::byte, v2PayloadBytes>{};
    std::memcpy(payload.data(), request.model.data(), request.model.size());
    std::memcpy(payload.data() + v2ModelBytes, request.latentPlanes[0u].data(), v2LatentPlaneBytes);
    std::memcpy(payload.data() + v2ModelBytes + v2LatentPlaneBytes, request.latentPlanes[1u].data(), v2LatentPlaneBytes);
    auto payloadDigest = dependency::crypto::sha256(std::span{payload});
    if (!payloadDigest)
    {
        return std::unexpected("Failed to calculate the neural artifact payload SHA-256.");
    }

    auto header = std::array<std::byte, v2HeaderBytes>{};
    std::ranges::copy(v2Magic, header.begin());
    auto headerBytes = std::span{header};
    writeLe32(headerBytes, static_cast<std::size_t>(HeaderOffset::version), 2u);
    writeLe32(headerBytes, static_cast<std::size_t>(HeaderOffset::headerBytes), v2HeaderBytes);
    writeLe32(headerBytes, static_cast<std::size_t>(HeaderOffset::totalBytes), v2TotalBytes);
    writeLe32(headerBytes, static_cast<std::size_t>(HeaderOffset::payloadBytes), v2PayloadBytes);
    writeLe32(headerBytes, static_cast<std::size_t>(HeaderOffset::endianness), 0x01020304u);
    writeLe32(headerBytes, static_cast<std::size_t>(HeaderOffset::modelLogicalBytes), v2ModelLogicalBytes);
    writeLe32(headerBytes, static_cast<std::size_t>(HeaderOffset::modelStorageBytes), v2ModelBytes);
    writeLe32(headerBytes, static_cast<std::size_t>(HeaderOffset::latent0Offset), v2ModelBytes);
    writeLe32(headerBytes, static_cast<std::size_t>(HeaderOffset::latent0Bytes), v2LatentPlaneBytes);
    writeLe32(headerBytes, static_cast<std::size_t>(HeaderOffset::latent1Offset), v2ModelBytes + v2LatentPlaneBytes);
    writeLe32(headerBytes, static_cast<std::size_t>(HeaderOffset::latent1Bytes), v2LatentPlaneBytes);
    writeLe32(headerBytes, static_cast<std::size_t>(HeaderOffset::latentWidth), 64u);
    writeLe32(headerBytes, static_cast<std::size_t>(HeaderOffset::latentHeight), 64u);
    writeLe32(headerBytes, static_cast<std::size_t>(HeaderOffset::latentChannels), 8u);
    writeLe32(headerBytes, static_cast<std::size_t>(HeaderOffset::topology), 1u);
    writeLe32(headerBytes, static_cast<std::size_t>(HeaderOffset::coordinateBasis), 1u);
    writeLe32(headerBytes, static_cast<std::size_t>(HeaderOffset::activation), 1u);
    writeLe32(headerBytes, static_cast<std::size_t>(HeaderOffset::outputSemantic), 1u);
    writeLe32(headerBytes, static_cast<std::size_t>(HeaderOffset::sourceMaterialIndex), request.bindingContract.sourceMaterialIndex);
    writeLe32(headerBytes, static_cast<std::size_t>(HeaderOffset::uvSet), request.bindingContract.uvSet);
    for (auto index = std::size_t{}; index < request.bindingContract.uvAffine.size(); ++index)
    {
        writeLeFloat(headerBytes, static_cast<std::size_t>(HeaderOffset::uvAffine) + index * 4u,
                     request.bindingContract.uvAffine[index]);
    }
    std::memcpy(header.data() + static_cast<std::size_t>(HeaderOffset::sourceSceneDigest),
                request.bindingContract.sourceSceneDigest.data(), request.bindingContract.sourceSceneDigest.size());
    std::memcpy(header.data() + static_cast<std::size_t>(HeaderOffset::trainingProfileDigest),
                request.bindingContract.trainingProfileDigest.data(), request.bindingContract.trainingProfileDigest.size());
    std::memcpy(header.data() + static_cast<std::size_t>(HeaderOffset::payloadDigest), payloadDigest->data(),
                payloadDigest->size());
    for (auto layerIndex = std::size_t{}; layerIndex < v2LayerDescriptors.size(); ++layerIndex)
    {
        auto const offset = headerLayerDescriptorOffset + layerIndex * headerLayerDescriptorBytes;
        auto const &descriptor = v2LayerDescriptors[layerIndex];
        writeLe32(headerBytes, offset, descriptor.weightOffset);
        writeLe32(headerBytes, offset + 4u, descriptor.rowStrideBytes);
        writeLe32(headerBytes, offset + 8u, descriptor.inputCount);
        writeLe32(headerBytes, offset + 12u, descriptor.outputCount);
        writeLe32(headerBytes, offset + 16u, descriptor.biasOffset);
        writeLe32(headerBytes, offset + 20u, descriptor.biasCount);
    }
    writeLe32(headerBytes, headerCompletedStepsOffset, request.completedSteps);
    writeLe32(headerBytes, headerBatchSizeOffset, request.batchSize);
    writeLe64(headerBytes, headerSampleCountOffset, request.sampleCount);
    writeLe32(headerBytes, headerElementFormatOffset, 1u);
    writeLe32(headerBytes, headerLatentSamplingOffset, 1u);
    writeLe32(headerBytes, headerTrainingProfileIdOffset, 1u);
    writeLe32(headerBytes, headerTrainableScalarCountOffset, 1'935u);

    auto destinationParent = request.destination.parent_path();
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
                     std::format(".{}.{}.{}.tmp", request.destination.filename().string(), GetCurrentProcessId(), nonce);
    auto output = std::ofstream{temporary, std::ios::binary | std::ios::trunc};
    if (!output)
    {
        return std::unexpected(std::format("Failed to open temporary neural artifact '{}'.", temporary.generic_string()));
    }
    output.write(reinterpret_cast<const char *>(header.data()), static_cast<std::streamsize>(header.size()));
    output.write(reinterpret_cast<const char *>(payload.data()), static_cast<std::streamsize>(payload.size()));
    output.flush();
    if (!output.good())
    {
        return std::unexpected(std::format("Failed to flush temporary neural artifact '{}'.", temporary.generic_string()));
    }
    output.close();
    auto fileHandle = CreateFileW(temporary.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
    if (fileHandle == INVALID_HANDLE_VALUE || !FlushFileBuffers(fileHandle))
    {
        if (fileHandle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(fileHandle);
        }
        std::filesystem::remove(temporary, directoryError);
        return std::unexpected(std::format("Failed to flush temporary neural artifact '{}'.", temporary.generic_string()));
    }
    CloseHandle(fileHandle);
    if (!MoveFileExW(temporary.c_str(), request.destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        auto const error = GetLastError();
        std::filesystem::remove(temporary, directoryError);
        return std::unexpected(std::format("Failed to atomically publish neural artifact '{}': Win32 error {}.",
                                           request.destination.generic_string(), error));
    }
    return {};
}

[[nodiscard]] std::expected<std::optional<BindingRequest>, std::string> loadBindingRequest(
    const std::filesystem::path &assetsRoot, const std::filesystem::path &normalizedScenePath)
{
    if (!isSafeRelativePath(normalizedScenePath))
    {
        return std::unexpected("Neural material lookup requires a normalized assets-relative scene path.");
    }
    auto error = std::error_code{};
    auto canonicalAssets = std::filesystem::canonical(assetsRoot, error);
    if (error)
    {
        return std::unexpected(std::format("Failed to canonicalize assets root '{}': {}.", assetsRoot.generic_string(),
                                           error.message()));
    }
    auto sidecarCandidate = canonicalAssets / "neuralAppearance" / "bindings";
    sidecarCandidate /= normalizedScenePath;
    sidecarCandidate += ".neural.json";
    if (!std::filesystem::exists(sidecarCandidate, error) && !error)
    {
        return std::optional<BindingRequest>{};
    }
    auto sidecarPath = canonicalRegularFileContained(sidecarCandidate, canonicalAssets / "neuralAppearance" / "bindings");
    if (!sidecarPath)
    {
        return std::unexpected(sidecarPath.error());
    }
    auto sidecarBytes = readRegularFile(*sidecarPath, maximumSidecarBytes);
    if (!sidecarBytes)
    {
        return std::unexpected(sidecarBytes.error());
    }
    auto text = std::string{reinterpret_cast<const char *>(sidecarBytes->data()), sidecarBytes->size()};
    auto parsed = dependency::json::parseJsonRejectingDuplicateKeys(text, 16u);
    if (!parsed.valid())
    {
        return std::unexpected("Neural material sidecar is not valid strict UTF-8 JSON.");
    }
    auto object = asObject(*parsed.value);
    if (!object)
    {
        return std::unexpected(object.error());
    }
    auto const *fields = *object;
    if (fields->size() != 4u || !fields->contains("schema") || !fields->contains("scene") ||
        !fields->contains("material") || !fields->contains("artifact"))
    {
        return std::unexpected("Neural material sidecar fields must exactly be schema, scene, material, artifact.");
    }
    auto schema = requiredString(*fields, "schema");
    auto scene = requiredString(*fields, "scene");
    auto artifact = requiredString(*fields, "artifact");
    auto material = requiredMaterial(*fields);
    if (!schema || !scene || !artifact || !material)
    {
        return std::unexpected(!schema ? schema.error() : !scene ? scene.error() : !artifact ? artifact.error() : material.error());
    }
    auto const sceneText = normalizedScenePath.generic_string();
    if (*schema != "nr.neural-material-binding/v2" || *scene != sceneText || *material != 0u)
    {
        return std::unexpected("Neural material sidecar does not match the P0 schema, source scene, or material 0.");
    }
    auto artifactRelative = std::filesystem::path{*artifact};
    if (!isSafeRelativePath(artifactRelative) || !hasV2ArtifactExtension(artifactRelative))
    {
        return std::unexpected("Neural material sidecar artifact path must be safe and use the exact lowercase '.nart' extension.");
    }
    auto artifactPath = canonicalRegularFileContained(canonicalAssets / "neuralAppearance" / "artifacts" / "v2" / artifactRelative,
                                                      canonicalAssets / "neuralAppearance" / "artifacts" / "v2");
    if (!artifactPath)
    {
        return std::unexpected(artifactPath.error());
    }
    auto loadedArtifact = loadArtifactV2(*artifactPath);
    if (!loadedArtifact)
    {
        return std::unexpected(loadedArtifact.error());
    }
    auto expectedDigest = sceneDigest(sceneText);
    if (expectedDigest == dependency::crypto::Sha256Digest{} ||
        (*loadedArtifact)->bindingContract().sourceSceneDigest != expectedDigest ||
        (*loadedArtifact)->bindingContract().sourceMaterialIndex != *material)
    {
        return std::unexpected("Neural artifact binding contract does not match its sidecar scene/material identity.");
    }
    return std::optional<BindingRequest>{BindingRequest{
        .sourceMaterialIndex = *material,
        .artifact = std::move(*loadedArtifact),
    }};
}

[[nodiscard]] std::expected<void, std::string> validateBindingForScene(const BindingRequest &request,
                                                                         const nr::load::SceneAsset &sceneAsset)
{
    if (!request.artifact || request.sourceMaterialIndex != 0u ||
        request.sourceMaterialIndex >= sceneAsset.materials.size())
    {
        return std::unexpected("Neural material binding references an unavailable P0 material.");
    }
    auto const &material = sceneAsset.materials[request.sourceMaterialIndex];
    if (material.alphaModeHint != nr::load::MaterialAlphaModeHint::opaque || material.opacity != 1.0f ||
        material.baseColorFactor[3u] != 1.0f || material.doubleSided || material.unlit ||
        material.normalScale.has_value() || material.clearcoatFactor.has_value() || material.clearcoatRoughnessFactor.has_value() ||
        material.sheenColorFactor.has_value() || material.sheenRoughnessFactor.has_value() ||
        material.transmissionFactor.has_value() || material.anisotropyFactor.has_value() || material.thicknessFactor.has_value())
    {
        return std::unexpected("Neural material P0 only supports opaque single-sided base-surface materials.");
    }
    auto baseColor = std::ranges::find_if(material.textures, [](const nr::load::MaterialTextureBinding &binding) {
        return binding.semantic == nr::resource::MaterialTextureSlotSemantic::baseColor;
    });
    if (baseColor == material.textures.end() || baseColor->textureIndex == nr::load::invalidIndex || baseColor->uvChannel != 0u ||
        !equivalentIdentityUv(baseColor->transform))
    {
        return std::unexpected("Neural material P0 requires a base-color texture on identity UV0.");
    }
    auto hasUnsupportedTexture = std::ranges::any_of(material.textures, [](const nr::load::MaterialTextureBinding &binding) {
        using enum nr::resource::MaterialTextureSlotSemantic;
        return binding.semantic == normal || binding.semantic == clearcoat || binding.semantic == clearcoatRoughness ||
               binding.semantic == clearcoatNormal || binding.semantic == sheenColor || binding.semantic == sheenRoughness ||
               binding.semantic == transmission || binding.semantic == anisotropy;
    });
    if (hasUnsupportedTexture)
    {
        return std::unexpected("Neural material P0 cannot bind materials with optional PBR texture layers.");
    }
    return {};
}
} // namespace nr::neuralAppearance
