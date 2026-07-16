export module dependency.dlss;

import dependency.vulkan;
import std;

export namespace nr::dependency::dlss
{
inline constexpr std::string_view projectId = "eb1ed2e1-c610-47ef-a126-3e97625116d6";
inline constexpr std::string_view engineName = "NewbieRenderer";
inline constexpr std::string_view engineVersion = "0.1.0";

enum class StatusCode : std::uint8_t
{
    Success,
    SdkNotCompiled,
    Unavailable,
    InvalidArgument,
    ApiFailure,
};

struct Status
{
    StatusCode code = StatusCode::Success;
    std::uint32_t nativeCode = 0u;
    std::string message{};

    [[nodiscard]] bool success() const noexcept
    {
        return code == StatusCode::Success;
    }
};

struct ExtensionQueryResult
{
    Status status{};
    std::vector<std::string> names{};
};

enum class Quality : std::uint8_t
{
    Performance,
    Balanced,
    Quality,
    UltraPerformance,
    Dlaa,
    Count,
};

enum class Preset : std::uint8_t
{
    Default,
    D,
    E,
};

enum class RoughnessMode : std::uint8_t
{
    Unpacked,
    Packed,
};

enum class DepthType : std::uint8_t
{
    Linear,
    Hardware,
};

enum class ToneMapper : std::uint8_t
{
    String,
    Reinhard,
    OneOverLuma,
    Aces,
};

struct Dimensions
{
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;

    [[nodiscard]] bool valid() const noexcept
    {
        return width > 0u && height > 0u;
    }

    auto operator<=>(const Dimensions&) const = default;
};

struct Coordinates
{
    std::uint32_t x = 0u;
    std::uint32_t y = 0u;

    auto operator<=>(const Coordinates&) const = default;
};

struct RayReconstructionCreateFlags
{
    bool hdr = true;
    bool motionVectorsLowResolution = true;
    bool motionVectorsJittered = false;
    bool depthInverted = false;
    bool autoExposure = false;
    bool alphaUpscaling = false;

    auto operator<=>(const RayReconstructionCreateFlags&) const = default;
};

struct RayReconstructionCreateDesc
{
    Dimensions renderSize{};
    Dimensions targetSize{};
    Quality quality = Quality::Quality;
    std::array<Preset, static_cast<std::size_t>(Quality::Count)> presets{};
    RoughnessMode roughnessMode = RoughnessMode::Packed;
    DepthType depthType = DepthType::Linear;
    RayReconstructionCreateFlags flags{};
    bool enableOutputSubrects = false;

    auto operator<=>(const RayReconstructionCreateDesc&) const = default;
};

struct OptimalSettings
{
    Dimensions optimalRenderSize{};
    Dimensions minimumRenderSize{};
    Dimensions maximumRenderSize{};
    Status status{};
};

struct VulkanImage
{
    vk::Image image{};
    vk::ImageView view{};
    vk::ImageSubresourceRange subresourceRange{
        vk::ImageAspectFlagBits::eColor,
        0u,
        1u,
        0u,
        1u,
    };
    vk::Format format = vk::Format::eUndefined;
    Dimensions extent{};
    bool readWrite = false;

    [[nodiscard]] bool valid() const noexcept
    {
        return static_cast<bool>(image) && static_cast<bool>(view) &&
               format != vk::Format::eUndefined && extent.valid();
    }
};

// This order is the stable project ABI for every image pointer accepted by
// NVSDK_NGX_VK_DLSSD_Eval_Params in the bundled DLSS 310.7.0 SDK.
enum class RayReconstructionResourceSlot : std::uint8_t
{
    DiffuseAlbedo,
    SpecularAlbedo,
    Normals,
    Roughness,
    Color,
    Alpha,
    Output,
    OutputAlpha,
    Depth,
    MotionVectors,
    TransparencyMask,
    ExposureTexture,
    BiasCurrentColorMask,
    ReflectedAlbedo,
    ColorBeforeParticles,
    ColorAfterParticles,
    ColorBeforeTransparency,
    ColorAfterTransparency,
    ColorBeforeFog,
    ColorAfterFog,
    ScreenSpaceSubsurfaceScatteringGuide,
    ColorBeforeScreenSpaceSubsurfaceScattering,
    ColorAfterScreenSpaceSubsurfaceScattering,
    ScreenSpaceRefractionGuide,
    ColorBeforeScreenSpaceRefraction,
    ColorAfterScreenSpaceRefraction,
    DepthOfFieldGuide,
    ColorBeforeDepthOfField,
    ColorAfterDepthOfField,
    DiffuseHitDistance,
    SpecularHitDistance,
    DiffuseRayDirection,
    SpecularRayDirection,
    DiffuseRayDirectionHitDistance,
    SpecularRayDirectionHitDistance,
    GBufferAlbedo,
    GBufferRoughness,
    GBufferMetallic,
    GBufferSpecular,
    GBufferSubsurface,
    GBufferNormals,
    GBufferShadingModelId,
    GBufferMaterialId,
    GBufferSpecularAlbedo,
    GBufferIndirectAlbedo,
    GBufferSpecularMotionVectors,
    GBufferDisocclusionMask,
    GBufferEmissive,
    GBufferResponsivityMask,
    GBufferReserved14,
    GBufferReserved15,
    GBufferReserved16,
    MotionVectors3D,
    ParticleMask,
    AnimatedTextureMask,
    DepthHighResolution,
    PositionViewSpace,
    RayTracingHitDistance,
    ReflectionMotionVectors,
    TransparencyLayer,
    TransparencyLayerOpacity,
    TransparencyLayerMotionVectors,
    DisocclusionMask,
    ResponsivityMask,
    Count,
};

inline constexpr std::size_t rayReconstructionResourceSlotCount =
    static_cast<std::size_t>(RayReconstructionResourceSlot::Count);
static_assert(rayReconstructionResourceSlotCount == 64u);

enum class RayReconstructionSubrectSlot : std::uint8_t
{
    Alpha,
    OutputAlpha,
    DiffuseAlbedo,
    SpecularAlbedo,
    Normals,
    Roughness,
    Color,
    Depth,
    MotionVectors,
    Translucency,
    BiasCurrentColor,
    Output,
    ReflectedAlbedo,
    ColorBeforeParticles,
    ColorAfterParticles,
    ColorBeforeTransparency,
    ColorAfterTransparency,
    ColorBeforeFog,
    ColorAfterFog,
    ScreenSpaceSubsurfaceScatteringGuide,
    ColorBeforeScreenSpaceSubsurfaceScattering,
    ColorAfterScreenSpaceSubsurfaceScattering,
    ScreenSpaceRefractionGuide,
    ColorBeforeScreenSpaceRefraction,
    ColorAfterScreenSpaceRefraction,
    DepthOfFieldGuide,
    ColorBeforeDepthOfField,
    ColorAfterDepthOfField,
    DiffuseHitDistance,
    SpecularHitDistance,
    DiffuseRayDirection,
    SpecularRayDirection,
    DiffuseRayDirectionHitDistance,
    SpecularRayDirectionHitDistance,
    TransparencyLayer,
    TransparencyLayerOpacity,
    TransparencyLayerMotionVectors,
    DisocclusionMask,
    ResponsivityMask,
    Count,
};

inline constexpr std::size_t rayReconstructionSubrectSlotCount =
    static_cast<std::size_t>(RayReconstructionSubrectSlot::Count);
static_assert(rayReconstructionSubrectSlotCount == 39u);

struct RayReconstructionEvalDesc
{
    std::array<std::optional<VulkanImage>, rayReconstructionResourceSlotCount> resources{};
    std::array<Coordinates, rayReconstructionSubrectSlotCount> subrectBases{};
    std::array<float, 2u> jitterOffset{};
    Dimensions renderSubrectDimensions{};
    bool reset = false;
    std::array<float, 2u> motionVectorScale{1.0f, 1.0f};
    float preExposure = 1.0f;
    float exposureScale = 1.0f;
    bool indicatorInvertXAxis = false;
    bool indicatorInvertYAxis = false;
    std::optional<std::array<float, 16u>> worldToViewRowMajor{};
    std::optional<std::array<float, 16u>> viewToClipRowMajor{};
    ToneMapper toneMapper = ToneMapper::Aces;
    float frameTimeDeltaMilliseconds = 1000.0f / 60.0f;
};

struct VulkanContextDesc
{
    vk::Instance instance{};
    vk::PhysicalDevice physicalDevice{};
    vk::Device device{};
    std::filesystem::path applicationDataPath{};
};

class RayReconstructionFeature;

class Context
{
  public:
    explicit Context(const VulkanContextDesc& desc);
    ~Context();

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    Context(Context&&) noexcept;
    Context& operator=(Context&&) noexcept;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] const Status& status() const noexcept;
    [[nodiscard]] bool rayReconstructionAvailable() const noexcept;
    [[nodiscard]] OptimalSettings optimalSettings(Dimensions targetSize, Quality quality);
    [[nodiscard]] std::unique_ptr<RayReconstructionFeature> createRayReconstruction(
        vk::CommandBuffer commandBuffer,
        const RayReconstructionCreateDesc& desc);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_{};
};

class RayReconstructionFeature
{
  public:
    ~RayReconstructionFeature();

    RayReconstructionFeature(const RayReconstructionFeature&) = delete;
    RayReconstructionFeature& operator=(const RayReconstructionFeature&) = delete;
    RayReconstructionFeature(RayReconstructionFeature&&) noexcept;
    RayReconstructionFeature& operator=(RayReconstructionFeature&&) noexcept;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] const Status& status() const noexcept;
    [[nodiscard]] Status evaluate(vk::CommandBuffer commandBuffer, const RayReconstructionEvalDesc& desc);

  private:
    friend class Context;
    struct Impl;
    explicit RayReconstructionFeature(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_{};
};

[[nodiscard]] bool sdkCompiled() noexcept;
[[nodiscard]] ExtensionQueryResult rayReconstructionInstanceExtensions();
[[nodiscard]] ExtensionQueryResult rayReconstructionDeviceExtensions(
    vk::Instance instance,
    vk::PhysicalDevice physicalDevice);
[[nodiscard]] std::string_view qualityName(Quality quality) noexcept;
[[nodiscard]] std::string_view presetName(Preset preset) noexcept;
[[nodiscard]] std::string_view resourceSlotName(RayReconstructionResourceSlot slot) noexcept;
[[nodiscard]] std::string_view subrectSlotName(RayReconstructionSubrectSlot slot) noexcept;
} // namespace nr::dependency::dlss
