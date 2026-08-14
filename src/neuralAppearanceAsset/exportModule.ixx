export module nr.neuralAppearanceAsset;

import dependency.crypto;
import nr.load;
import std;

export namespace nr::neuralAppearance
{
inline constexpr std::size_t v2HeaderBytes = 512u;
inline constexpr std::size_t v2PayloadBytes = 69'504u;
inline constexpr std::size_t v2TotalBytes = v2HeaderBytes + v2PayloadBytes;
inline constexpr std::size_t v2ModelLogicalBytes = 3'870u;
inline constexpr std::size_t v2ModelBytes = 3'968u;
inline constexpr std::size_t v2LatentPlaneBytes = 32'768u;
inline constexpr std::size_t v2LatentPlaneCount = 2u;
inline constexpr std::size_t v2LayerDescriptorBytes = 24u;
inline constexpr std::uint32_t v2NoBiasOffset = std::numeric_limits<std::uint32_t>::max();
inline constexpr std::string_view v2TrainingProfileText =
    "nr.neural-appearance/v2;topology=F8x12,S8x32,D12x32,H32x32,O32x3;basis=y-up;activation=relu;"
    "output=capped-exp-projected-specular;parameters=fp16-row-major;latent=64x64x8;uv=base-color:0:identity;"
    "training=coopvec-fp16-qat;steps=32768;batch=64;samples=2097152";

struct ArtifactBindingContract
{
    dependency::crypto::Sha256Digest sourceSceneDigest{};
    dependency::crypto::Sha256Digest trainingProfileDigest{};
    std::uint32_t sourceMaterialIndex = 0u;
    std::uint32_t uvSet = 0u;
    std::array<float, 6u> uvAffine{1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
};

struct ArtifactLayerDescriptor
{
    std::uint32_t weightOffset = 0u;
    std::uint32_t rowStrideBytes = 0u;
    std::uint32_t inputCount = 0u;
    std::uint32_t outputCount = 0u;
    std::uint32_t biasOffset = v2NoBiasOffset;
    std::uint32_t biasCount = 0u;
};
static_assert(sizeof(ArtifactLayerDescriptor) == v2LayerDescriptorBytes);

struct ArtifactWriteRequest
{
    std::filesystem::path destination{};
    ArtifactBindingContract bindingContract{};
    std::span<const std::byte> model{};
    std::array<std::span<const std::byte>, v2LatentPlaneCount> latentPlanes{};
    std::uint32_t completedSteps = 32'768u;
    std::uint32_t batchSize = 64u;
    std::uint64_t sampleCount = 2'097'152u;
};

class Artifact
{
  public:
    Artifact() = delete;

    [[nodiscard]] std::span<const std::byte> modelBytes() const noexcept;
    [[nodiscard]] std::span<const std::byte> latentPlane(std::size_t index) const noexcept;
    [[nodiscard]] const ArtifactBindingContract &bindingContract() const noexcept;
    [[nodiscard]] const dependency::crypto::Sha256Digest &payloadDigest() const noexcept;

  private:
    friend std::expected<std::shared_ptr<const Artifact>, std::string> loadArtifactV2(const std::filesystem::path &);

    Artifact(ArtifactBindingContract bindingContract, dependency::crypto::Sha256Digest payloadDigest,
             std::array<std::byte, v2PayloadBytes> payload) noexcept;

    ArtifactBindingContract bindingContract_{};
    dependency::crypto::Sha256Digest payloadDigest_{};
    std::array<std::byte, v2PayloadBytes> payload_{};
};

struct BindingRequest
{
    std::uint32_t sourceMaterialIndex = 0u;
    std::shared_ptr<const Artifact> artifact{};
};

[[nodiscard]] std::expected<std::shared_ptr<const Artifact>, std::string> loadArtifactV2(
    const std::filesystem::path &artifactPath);

[[nodiscard]] std::expected<void, std::string> writeArtifactV2(const ArtifactWriteRequest &request);

[[nodiscard]] std::optional<dependency::crypto::Sha256Digest> v2TrainingProfileDigest() noexcept;

[[nodiscard]] std::expected<ArtifactBindingContract, std::string> makeArtifactBindingContract(
    std::string_view normalizedScenePath, std::uint32_t sourceMaterialIndex = 0u);

[[nodiscard]] std::expected<std::optional<BindingRequest>, std::string> loadBindingRequest(
    const std::filesystem::path &assetsRoot, const std::filesystem::path &normalizedScenePath);

[[nodiscard]] std::expected<void, std::string> validateBindingForScene(const BindingRequest &request,
                                                                         const nr::load::SceneAsset &sceneAsset);
} // namespace nr::neuralAppearance
