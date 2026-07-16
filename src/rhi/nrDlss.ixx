export module nr.rhi:dlss;

import dependency.dlss;
import dependency.vulkan;
import std;

export namespace nr::rhi
{
using DlssStatus = nr::dependency::dlss::Status;
using DlssStatusCode = nr::dependency::dlss::StatusCode;
using DlssQuality = nr::dependency::dlss::Quality;
using DlssRayReconstructionPreset = nr::dependency::dlss::Preset;
using DlssRoughnessMode = nr::dependency::dlss::RoughnessMode;
using DlssDepthType = nr::dependency::dlss::DepthType;
using DlssToneMapper = nr::dependency::dlss::ToneMapper;
using DlssDimensions = nr::dependency::dlss::Dimensions;
using DlssCoordinates = nr::dependency::dlss::Coordinates;
using DlssRayReconstructionCreateFlags = nr::dependency::dlss::RayReconstructionCreateFlags;
using DlssRayReconstructionCreateDesc = nr::dependency::dlss::RayReconstructionCreateDesc;
using DlssOptimalSettings = nr::dependency::dlss::OptimalSettings;
using DlssImage = nr::dependency::dlss::VulkanImage;
using DlssRayReconstructionResourceSlot = nr::dependency::dlss::RayReconstructionResourceSlot;
using DlssRayReconstructionSubrectSlot = nr::dependency::dlss::RayReconstructionSubrectSlot;
using DlssRayReconstructionEvalDesc = nr::dependency::dlss::RayReconstructionEvalDesc;

inline constexpr auto kDlssRayReconstructionResourceSlotCount =
    nr::dependency::dlss::rayReconstructionResourceSlotCount;
inline constexpr auto kDlssRayReconstructionSubrectSlotCount =
    nr::dependency::dlss::rayReconstructionSubrectSlotCount;

class DlssContext final
{
  public:
    DlssContext(
        vk::Instance instance,
        vk::PhysicalDevice physicalDevice,
        vk::Device device,
        std::filesystem::path applicationDataPath);

    DlssContext(const DlssContext&) = delete;
    DlssContext& operator=(const DlssContext&) = delete;
    DlssContext(DlssContext&&) = delete;
    DlssContext& operator=(DlssContext&&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] const DlssStatus& status() const noexcept;
    [[nodiscard]] DlssOptimalSettings optimalSettings(DlssDimensions targetSize, DlssQuality quality);

  private:
    friend class DlssRayReconstructionFeature;
    nr::dependency::dlss::Context context_;
};

class DlssRayReconstructionFeature final
{
  public:
    DlssRayReconstructionFeature(
        std::shared_ptr<DlssContext> context,
        const vk::raii::CommandBuffer& commandBuffer,
        const DlssRayReconstructionCreateDesc& desc);
    ~DlssRayReconstructionFeature();

    DlssRayReconstructionFeature(const DlssRayReconstructionFeature&) = delete;
    DlssRayReconstructionFeature& operator=(const DlssRayReconstructionFeature&) = delete;
    DlssRayReconstructionFeature(DlssRayReconstructionFeature&&) noexcept;
    DlssRayReconstructionFeature& operator=(DlssRayReconstructionFeature&&) noexcept;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] const DlssStatus& status() const noexcept;
    [[nodiscard]] DlssStatus evaluate(
        const vk::raii::CommandBuffer& commandBuffer,
        const DlssRayReconstructionEvalDesc& desc);

  private:
    // Declaration order guarantees the feature and its parameter map are
    // released before the shared NGX context.
    std::shared_ptr<DlssContext> context_{};
    std::unique_ptr<nr::dependency::dlss::RayReconstructionFeature> feature_{};
};

[[nodiscard]] bool dlssSdkCompiled() noexcept;
[[nodiscard]] std::string_view dlssQualityName(DlssQuality quality) noexcept;
[[nodiscard]] std::string_view dlssPresetName(DlssRayReconstructionPreset preset) noexcept;
[[nodiscard]] std::string_view dlssResourceSlotName(DlssRayReconstructionResourceSlot slot) noexcept;
[[nodiscard]] std::string_view dlssSubrectSlotName(DlssRayReconstructionSubrectSlot slot) noexcept;
} // namespace nr::rhi
