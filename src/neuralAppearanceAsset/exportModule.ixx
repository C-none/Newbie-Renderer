export module nr.neuralAppearanceAsset;

import dependency.crypto;
import std;

export namespace nr::neuralAppearance
{
// V3 artifacts describe one universal material-parameter-driven model. There is
// no latent plane and no per-scene binding contract: the network consumes base
// surface parameters directly, so a published artifact is scene independent.
inline constexpr std::size_t v3HeaderBytes = 512u;
inline constexpr std::size_t v3ModelBytes = 7'360u;
inline constexpr std::size_t v3ModelLogicalBytes = 7'176u;
inline constexpr std::size_t v3PayloadBytes = v3ModelBytes;
inline constexpr std::size_t v3TotalBytes = v3HeaderBytes + v3PayloadBytes;
inline constexpr std::size_t v3LayerDescriptorBytes = 24u;
inline constexpr std::size_t v3AffineLayerCount = 8u;
inline constexpr std::uint32_t v3TrainableScalarCount = 3'588u;
inline constexpr std::uint32_t v3MaterialInputCount = 12u;
inline constexpr std::uint32_t v3NoBiasOffset = std::numeric_limits<std::uint32_t>::max();
inline constexpr std::uint32_t v3CompletedSteps = 16'384u;
inline constexpr std::uint32_t v3BatchSize = 64u;
inline constexpr std::uint64_t v3SampleCount =
    static_cast<std::uint64_t>(v3CompletedSteps) * v3BatchSize;
inline constexpr std::string_view v3TrainingProfileText =
    "nr.neural-appearance/v3;topology=E12x32,E32x32,E32x8,F8x6,S8x32,D8x32,H32x32,O32x6;basis=y-up-geometry;"
    "activation=leaky-relu;output=capped-sigmoid-projected-diffuse-specular;parameters=fp16-row-major;"
    "input=base-color:metallic:roughness:shading-normal:anisotropy-tangent:anisotropy-strength;"
    "training=coopvec-fp16-qat;steps=16384;batch=64;samples=1048576";

struct ArtifactLayerDescriptor
{
    std::uint32_t weightOffset = 0u;
    std::uint32_t rowStrideBytes = 0u;
    std::uint32_t inputCount = 0u;
    std::uint32_t outputCount = 0u;
    std::uint32_t biasOffset = v3NoBiasOffset;
    std::uint32_t biasCount = 0u;
};
static_assert(sizeof(ArtifactLayerDescriptor) == v3LayerDescriptorBytes);

struct ArtifactWriteRequest
{
    std::filesystem::path destination{};
    std::span<const std::byte> model{};
    std::uint32_t completedSteps = v3CompletedSteps;
    std::uint32_t batchSize = v3BatchSize;
    std::uint64_t sampleCount = v3SampleCount;
};

class Artifact
{
  public:
    Artifact() = delete;

    [[nodiscard]] std::span<const std::byte> modelBytes() const noexcept;
    [[nodiscard]] const dependency::crypto::Sha256Digest &payloadDigest() const noexcept;
    [[nodiscard]] std::uint32_t completedSteps() const noexcept;

  private:
    friend std::expected<std::shared_ptr<const Artifact>, std::string> loadArtifactV3(const std::filesystem::path &);

    Artifact(dependency::crypto::Sha256Digest payloadDigest, std::uint32_t completedSteps,
             std::array<std::byte, v3PayloadBytes> payload) noexcept;

    dependency::crypto::Sha256Digest payloadDigest_{};
    std::uint32_t completedSteps_ = 0u;
    std::array<std::byte, v3PayloadBytes> payload_{};
};

[[nodiscard]] std::expected<std::shared_ptr<const Artifact>, std::string> loadArtifactV3(
    const std::filesystem::path &artifactPath);

[[nodiscard]] std::expected<void, std::string> writeArtifactV3(const ArtifactWriteRequest &request);

[[nodiscard]] std::optional<dependency::crypto::Sha256Digest> v3TrainingProfileDigest() noexcept;

[[nodiscard]] std::span<const ArtifactLayerDescriptor, v3AffineLayerCount> v3LayerDescriptors() noexcept;
} // namespace nr::neuralAppearance
