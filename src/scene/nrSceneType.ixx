export module nr.scene:type;
import dependency.math;
import dependency.ecs;
import dependency.shaderShare;
import dependency.vulkan;

import nr.load;
import nr.resource;
import nr.rhi;
import nr.utils;
import std;

export namespace nr::scene
{
inline constexpr DirectX::XMFLOAT4X4 kIdentityMatrix{
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f,
};

using SceneTemplateHandle = nr::resource::Handle<struct SceneTemplateTag>;
using SceneInstanceHandle = nr::resource::Handle<struct SceneInstanceTag>;
using SceneExtractProfileHandle = nr::resource::Handle<struct SceneExtractProfileTag>;

enum class SceneRtRevisionDomain : std::uint8_t
{
    topology,
    transform,
    visibility,
    traceMask,
    meshBinding,
    meshContent,
    meshLayout,
    materialBinding,
    materialPayload,
    textureBinding,
    textureContent,
    textureResidency,
    count,
};

using SceneRtStructuralRevisionProjection =
    nr::revision::RevisionProjection<SceneRtRevisionDomain::topology, SceneRtRevisionDomain::visibility,
                                     SceneRtRevisionDomain::meshBinding, SceneRtRevisionDomain::meshContent,
                                     SceneRtRevisionDomain::meshLayout, SceneRtRevisionDomain::materialBinding,
                                     SceneRtRevisionDomain::materialPayload, SceneRtRevisionDomain::textureBinding,
                                     SceneRtRevisionDomain::textureContent, SceneRtRevisionDomain::textureResidency>;

enum class SceneRevisionMutation : std::uint8_t
{
    templateRegistered,
    templateDestroyed,
    instanceAdded,
    instanceRemoved,
    simulationUpdated,
    meshResident,
    textureResident,
};

struct SceneRevisionMutationPolicy
{
    using Mask = nr::revision::RevisionMask<SceneRtRevisionDomain>;

    [[nodiscard]] static constexpr Mask mask(SceneRevisionMutation mutation) noexcept
    {
        using enum SceneRtRevisionDomain;
        switch (mutation)
        {
        case SceneRevisionMutation::templateRegistered:
        case SceneRevisionMutation::templateDestroyed:
            return Mask::of<topology, meshBinding, meshContent, meshLayout, materialBinding, materialPayload,
                            textureBinding, textureContent>();
        case SceneRevisionMutation::instanceAdded:
        case SceneRevisionMutation::instanceRemoved:
            return Mask::of<topology, transform, visibility, traceMask, meshBinding>();
        case SceneRevisionMutation::simulationUpdated:
            return Mask::of<transform>();
        case SceneRevisionMutation::meshResident:
            return Mask::of<topology, meshContent>();
        case SceneRevisionMutation::textureResident:
            return Mask::of<topology, textureResidency>();
        default:
            return {};
        }
    }
};

struct SceneRevisionSnapshot
{
    std::uint64_t sceneIdentity = 0u;
    nr::revision::RevisionSnapshot<SceneRtRevisionDomain> rt{};

    [[nodiscard]] bool valid() const noexcept
    {
        return sceneIdentity != 0u;
    }

    [[nodiscard]] friend bool operator==(const SceneRevisionSnapshot &,
                                         const SceneRevisionSnapshot &) noexcept = default;
};

enum class DestroyTemplateResult : std::uint8_t
{
    destroyed,
    notFound,
    instancesAlive,
};

enum class CpuRetentionPolicy : std::uint8_t
{
    keepAll,
    discardUploadSourceAfterResident,
};

enum class TemplateHierarchyPolicy : std::uint8_t
{
    autoSelect,
    preferParent,
    preferChildOf,
};

struct SceneFrameStamp
{
    std::uint32_t frameSlot = 0;
    std::uint64_t frameSerial = 0;
};

enum class GpuResidencyState : std::uint8_t
{
    none,
    uploadQueued,
    waitingGraphicsSync,
    resident,
};

struct SceneCreateInfo
{
    nr::rhi::Device &device;
    std::size_t uploadBudgetBytesPerFrame = 128ull * 1024ull * 1024ull;
    CpuRetentionPolicy cpuRetention = CpuRetentionPolicy::keepAll;
};

struct SceneTemplateCreateInfo
{
    std::string debugName{};
    std::string stableKey{};
    TemplateHierarchyPolicy hierarchyPolicy = TemplateHierarchyPolicy::autoSelect;
};

struct SceneInstantiateInfo
{
    std::optional<std::reference_wrapper<const flecs::entity>> runtimeParent{};
    DirectX::XMFLOAT4X4 rootTransform = kIdentityMatrix;
    bool activate = true;
};

struct SceneUpdateInput
{
    float deltaSeconds = 0.0f;
};

enum class ScenePacketDomain : std::uint8_t
{
    rasterDraw,
    rayTracingInstance,
    tlasBuildInput,
};

enum class SceneVisibilityMode : std::uint8_t
{
    none,
    primaryCameraFrustum,
    customFrustum,
};

struct SceneSelectionMask
{
    std::uint64_t requireAll = 0;
    std::uint64_t requireAny = 0;
    std::uint64_t rejectAny = 0;
};

struct SceneFrustum
{
    std::array<DirectX::XMFLOAT4, 6> planes{};
};

struct SceneExtractProfileCreateInfo
{
    std::string debugName{};
    ScenePacketDomain domain = ScenePacketDomain::rasterDraw;
    SceneSelectionMask selection{};
    // When enabled, extraction only emits packets whose dependencies are ready
    // for the selected domain (for example raster also requires material/texture readiness).
    bool requireReadyForDomain = true;
    bool requireActiveInstances = true;
    bool enableCoarseGrouping = true;
};

struct SceneExtractInput
{
    SceneVisibilityMode visibility = SceneVisibilityMode::none;
    std::optional<SceneFrustum> customFrustum{};
    std::optional<DirectX::XMUINT2> viewportExtent{};
    std::optional<std::uint32_t> partitionOverride{};
};

struct SceneBridgeBufferBinding
{
    std::optional<std::reference_wrapper<const nr::rhi::Buffer>> buffer{};
    vk::DeviceSize offset = 0;
};

struct SceneBridgeGeometryBuffers
{
    SceneBridgeBufferBinding vertexBuffer{};
    SceneBridgeBufferBinding indexBuffer{};
    vk::IndexType indexType = vk::IndexType::eUint32;

    [[nodiscard]] bool hasVertexBuffer() const noexcept;

    [[nodiscard]] bool hasIndexBuffer() const noexcept;

    [[nodiscard]] bool valid() const noexcept;
};

struct SceneBridgeDrawGeometry
{
    std::uint32_t firstVertex = 0;
    std::uint32_t vertexCount = 0;
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    std::int32_t vertexOffset = 0;
    vk::FrontFace frontFace = vk::FrontFace::eCounterClockwise;

    [[nodiscard]] bool indexed() const noexcept;

    [[nodiscard]] bool valid() const noexcept;
};

struct SceneBridgeMaterialRasterState
{
    vk::CullModeFlags cullMode = vk::CullModeFlagBits::eBack;
};

using SceneMaterialTextureSlot = nr::shader::share::MaterialTextureSlot;
inline constexpr auto sceneMaterialTextureSlotCount = static_cast<std::size_t>(SceneMaterialTextureSlot::count);
using SceneTextureId = std::uint16_t;
inline constexpr SceneTextureId kDefaultSceneTextureId = 0u;
inline constexpr std::uint32_t kMaxSceneTextureId =
    static_cast<std::uint32_t>(std::numeric_limits<SceneTextureId>::max());

static_assert(sceneMaterialTextureSlotCount == nr::resource::materialTextureSlotCount);
static_assert(static_cast<std::uint32_t>(SceneMaterialTextureSlot::baseColor) ==
              static_cast<std::uint32_t>(nr::resource::MaterialTextureSlotSemantic::baseColor));
static_assert(static_cast<std::uint32_t>(SceneMaterialTextureSlot::normal) ==
              static_cast<std::uint32_t>(nr::resource::MaterialTextureSlotSemantic::normal));
static_assert(static_cast<std::uint32_t>(SceneMaterialTextureSlot::metallicRoughness) ==
              static_cast<std::uint32_t>(nr::resource::MaterialTextureSlotSemantic::metallicRoughness));
static_assert(static_cast<std::uint32_t>(SceneMaterialTextureSlot::occlusion) ==
              static_cast<std::uint32_t>(nr::resource::MaterialTextureSlotSemantic::occlusion));
static_assert(static_cast<std::uint32_t>(SceneMaterialTextureSlot::emissive) ==
              static_cast<std::uint32_t>(nr::resource::MaterialTextureSlotSemantic::emissive));
static_assert(static_cast<std::uint32_t>(SceneMaterialTextureSlot::clearcoat) ==
              static_cast<std::uint32_t>(nr::resource::MaterialTextureSlotSemantic::clearcoat));
static_assert(static_cast<std::uint32_t>(SceneMaterialTextureSlot::clearcoatRoughness) ==
              static_cast<std::uint32_t>(nr::resource::MaterialTextureSlotSemantic::clearcoatRoughness));
static_assert(static_cast<std::uint32_t>(SceneMaterialTextureSlot::clearcoatNormal) ==
              static_cast<std::uint32_t>(nr::resource::MaterialTextureSlotSemantic::clearcoatNormal));
static_assert(static_cast<std::uint32_t>(SceneMaterialTextureSlot::sheenColor) ==
              static_cast<std::uint32_t>(nr::resource::MaterialTextureSlotSemantic::sheenColor));
static_assert(static_cast<std::uint32_t>(SceneMaterialTextureSlot::sheenRoughness) ==
              static_cast<std::uint32_t>(nr::resource::MaterialTextureSlotSemantic::sheenRoughness));
static_assert(static_cast<std::uint32_t>(SceneMaterialTextureSlot::transmission) ==
              static_cast<std::uint32_t>(nr::resource::MaterialTextureSlotSemantic::transmission));
static_assert(static_cast<std::uint32_t>(SceneMaterialTextureSlot::anisotropy) ==
              static_cast<std::uint32_t>(nr::resource::MaterialTextureSlotSemantic::anisotropy));
static_assert(static_cast<std::uint32_t>(SceneMaterialTextureSlot::specular) ==
              static_cast<std::uint32_t>(nr::resource::MaterialTextureSlotSemantic::specular));
static_assert(static_cast<std::uint32_t>(SceneMaterialTextureSlot::specularColor) ==
              static_cast<std::uint32_t>(nr::resource::MaterialTextureSlotSemantic::specularColor));

using SceneMaterialTextureIds = std::array<SceneTextureId, sceneMaterialTextureSlotCount>;

[[nodiscard]] constexpr std::uint32_t sceneTextureId(nr::resource::TextureHandle texture) noexcept
{
    return texture.valid() ? texture.slot + 1u : kDefaultSceneTextureId;
}

struct SceneMaterialNormalTextureBinding
{
    SceneTextureId textureId = kDefaultSceneTextureId;
    std::uint32_t uvSet = 0u;
    DirectX::XMFLOAT4 uvLinear{1.0f, 0.0f, 0.0f, 1.0f};
    DirectX::XMFLOAT2 uvOffset{};
    float normalScale = 1.0f;
};

struct SceneMaterialTextureBindings
{
    SceneMaterialTextureIds ids{};
    SceneMaterialNormalTextureBinding normal{};
};

using SceneTextureHandleTable = std::map<std::uint32_t, nr::resource::TextureHandle>;

struct RasterDrawPacket
{
    flecs::entity renderable{};
    nr::resource::MeshHandle mesh{};
    nr::resource::MaterialHandle material{};
    std::uint32_t geometryIndex = 0;
    DirectX::XMFLOAT4X4 world = kIdentityMatrix;
    nr::resource::Aabb worldBounds{};
    std::uint64_t sortKey = 0;
    SceneMaterialTextureBindings materialTextures{};
    SceneBridgeMaterialRasterState materialRaster{};
    SceneBridgeDrawGeometry geometry{};
};

struct RayTracingInstancePacket
{
    flecs::entity renderable{};
    nr::resource::MeshHandle mesh{};
    DirectX::XMFLOAT4X4 world = kIdentityMatrix;
    std::uint32_t instanceMask = 0xFF;
    std::uint16_t tlasBucket = 0;
};

struct TlasBuildInputPacket
{
    flecs::entity renderable{};
    nr::resource::MeshHandle mesh{};
    DirectX::XMFLOAT4X4 world = kIdentityMatrix;
    std::uint32_t instanceMask = 0xFF;
    std::uint16_t tlasBucket = 0;
};

struct SceneCameraBinding
{
    nr::resource::CameraAssetHandle camera{};
    bool synthetic = false;
};

struct SceneLightBinding
{
    nr::resource::LightAssetHandle light{};
};

struct SceneResolvedCamera
{
    flecs::entity entity{};
    nr::resource::CameraAssetHandle camera{};
    DirectX::XMFLOAT4X4 world = kIdentityMatrix;
    DirectX::XMFLOAT4X4 view = kIdentityMatrix;
    DirectX::XMFLOAT4X4 projection = kIdentityMatrix;
    SceneFrustum frustum{};
    bool fallback = false;
};

struct SceneLightPacket
{
    flecs::entity entity{};
    nr::resource::LightAssetHandle light{};
    DirectX::XMFLOAT4X4 world = kIdentityMatrix;
    DirectX::XMFLOAT3 position{};
    DirectX::XMFLOAT3 direction{0.0f, 0.0f, -1.0f};
    std::uint32_t stableInstanceId = 0;
};

struct ScenePacketSet
{
    SceneRevisionSnapshot revisions{};
    ScenePacketDomain domain = ScenePacketDomain::rasterDraw;
    SceneBridgeGeometryBuffers rasterGeometryBuffers{};
    SceneTextureHandleTable rasterTextureHandlesById{};
    std::vector<RasterDrawPacket> rasterDraws{};
    std::vector<RayTracingInstancePacket> rtInstances{};
    std::vector<TlasBuildInputPacket> tlasBuildInputs{};
    std::vector<SceneLightPacket> lights{};
};

struct SceneBridgeFrameConstants
{
    DirectX::XMFLOAT4X4 view = kIdentityMatrix;
    DirectX::XMFLOAT4X4 projection = kIdentityMatrix;
    DirectX::XMFLOAT4X4 viewProjection = kIdentityMatrix;
    DirectX::XMFLOAT3 cameraWorld{};
    float drawCount = 0.0f;
};

struct SceneBridgeFrame
{
    ScenePacketDomain domain = ScenePacketDomain::rasterDraw;
    SceneBridgeGeometryBuffers geometryBuffers{};
    SceneTextureHandleTable rasterTextureHandlesById{};
    std::vector<RasterDrawPacket> rasterDraws{};
    SceneBridgeFrameConstants frameConstants{};
    bool hasPrimaryCamera = false;
};

struct SceneAccelerationStructureGeometry
{
    std::uint32_t geometryIndex = 0;
    bool indexed = false;
    vk::DeviceSize primitiveOffset = 0;
    std::uint32_t firstVertex = 0;
    std::uint32_t primitiveCount = 0;
    vk::GeometryFlagsKHR geometryFlags{};
};

struct SceneAccelerationStructureGeometrySemanticKey
{
    std::uint32_t geometryIndex = 0;
    vk::GeometryFlagsKHR geometryFlags{};

    [[nodiscard]] bool operator==(const SceneAccelerationStructureGeometrySemanticKey &) const = default;
};

struct SceneGeometryAtlasBackingGeneration
{
    std::uint64_t vertex = 0;
    std::uint64_t index = 0;

    [[nodiscard]] bool operator==(const SceneGeometryAtlasBackingGeneration &) const = default;
};

struct SceneGeometryAtlasDomainStats
{
    vk::DeviceSize capacityBytes = 0;
    vk::DeviceSize highWaterBytes = 0;
    vk::DeviceSize reusableBytes = 0;

    [[nodiscard]] bool operator==(const SceneGeometryAtlasDomainStats &) const = default;
};

struct SceneGeometryAtlasStats
{
    SceneGeometryAtlasDomainStats vertex{};
    SceneGeometryAtlasDomainStats index{};

    [[nodiscard]] bool operator==(const SceneGeometryAtlasStats &) const = default;
};

struct SceneAccelerationStructureMeshSemanticKey
{
    std::uint64_t meshGpuVersion = 0;
    SceneGeometryAtlasBackingGeneration atlasBackingGeneration{};
    vk::GeometryInstanceFlagsKHR instanceFlags{};
    std::vector<SceneAccelerationStructureGeometrySemanticKey> geometries{};

    [[nodiscard]] bool operator==(const SceneAccelerationStructureMeshSemanticKey &) const = default;
};

struct SceneAccelerationStructureMesh
{
    nr::resource::MeshHandle mesh{};
    std::uint64_t gpuVersion = 0;
    vk::GeometryInstanceFlagsKHR instanceFlags{};
    SceneAccelerationStructureMeshSemanticKey semanticKey{};
    SceneBridgeBufferBinding vertexBuffer{};
    SceneBridgeBufferBinding indexBuffer{};
    vk::DeviceSize vertexByteOffset = 0;
    vk::DeviceSize indexByteOffset = 0;
    std::uint32_t maxVertex = 0;
    vk::DeviceSize vertexStride = sizeof(nr::resource::Vertex);
    vk::IndexType indexType = vk::IndexType::eUint32;
    std::vector<SceneAccelerationStructureGeometry> geometries{};

    [[nodiscard]] bool hasVertexBuffer() const noexcept;

    [[nodiscard]] bool hasIndexBuffer() const noexcept;
};

enum class SceneSelectionBit : std::uint8_t
{
    rasterOpaque = 0,
    rasterTransparent = 1,
    rtMain = 2,
    rtTransparent = 3,
    shadowCaster = 4,
    alphaTest = 5,
};

[[nodiscard]] constexpr std::uint64_t sceneSelectionMask(SceneSelectionBit bit) noexcept
{
    return 1ull << static_cast<std::uint8_t>(bit);
}

struct LocalTransform
{
    DirectX::XMFLOAT4X4 value = kIdentityMatrix;
};

struct WorldTransform
{
    DirectX::XMFLOAT4X4 value = kIdentityMatrix;
};

struct LocalBounds
{
    nr::resource::Aabb value{};
};

struct WorldBounds
{
    nr::resource::Aabb value{};
};

struct RenderableBinding
{
    nr::resource::MeshHandle mesh{};
    nr::resource::MaterialHandle material{};
    std::uint32_t geometryCount = 0;
};

struct SceneSelectionBits
{
    std::uint64_t value = 0;
};

struct ScenePartitionId
{
    std::uint32_t value = 0;
};

struct TlasBucketId
{
    std::uint16_t value = 0;
};

struct StaticObject
{
};

struct DynamicObject
{
};

// Tag applied to every entity that belongs to an active scene instance.
// Added/removed when an instance's runtime state is (re)initialized so that
// active-instance membership is an O(1) archetype-level fact instead of an
// O(hierarchy-depth) parent-chain walk during per-frame extraction.
struct ActiveInstanceTag
{
};

struct SceneTemplateRef
{
    SceneTemplateHandle handle{};
};

struct SceneTemplateNodeRef
{
    SceneTemplateHandle templateHandle{};
    std::uint32_t sourceNodeIndex = nr::load::invalidIndex;
    std::string sourceName{};
    std::string resolvedName{};
};

struct SceneTemplateNodeTransform
{
    std::array<float, 16> localTransform{
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
    };
};

struct SceneTemplateMeshBindingRef
{
    SceneTemplateHandle templateHandle{};
    std::uint32_t sourceNodeIndex = nr::load::invalidIndex;
    std::uint32_t sourceMeshIndex = nr::load::invalidIndex;
    nr::resource::MeshHandle mesh{};
    nr::resource::MaterialHandle material{};
};

struct SceneTemplateCameraBindingRef
{
    SceneTemplateHandle templateHandle{};
    std::uint32_t sourceNodeIndex = nr::load::invalidIndex;
    std::uint32_t sourceCameraIndex = nr::load::invalidIndex;
    nr::resource::CameraAssetHandle camera{};
};

struct SceneTemplateLightBindingRef
{
    SceneTemplateHandle templateHandle{};
    std::uint32_t sourceNodeIndex = nr::load::invalidIndex;
    std::uint32_t sourceLightIndex = nr::load::invalidIndex;
    nr::resource::LightAssetHandle light{};
};

struct SceneInstanceRef
{
    SceneInstanceHandle handle{};
    SceneTemplateHandle templateHandle{};
};

struct TemplateResourcePinSet
{
    std::vector<nr::resource::MeshHandle> meshes{};
    std::vector<nr::resource::MaterialHandle> materials{};
    std::vector<nr::resource::TextureHandle> textures{};
    std::vector<nr::resource::CameraAssetHandle> cameras{};
    std::vector<nr::resource::LightAssetHandle> lights{};
};

namespace detail
{
struct MeshGeometryAtlasAllocation
{
    vk::DeviceSize vertexByteOffset = 0;
    vk::DeviceSize indexByteOffset = 0;
    std::uint32_t vertexBase = 0;
    std::uint32_t indexBase = 0;
    std::uint32_t vertexCount = 0;
    std::uint32_t indexCount = 0;
    SceneGeometryAtlasBackingGeneration backingGenerationAtAllocation{};
};

struct MeshGpuPayload
{
    MeshGeometryAtlasAllocation atlas{};
};

struct TextureGpuPayload
{
    nr::rhi::Image image{};
    vk::ImageLayout layout = vk::ImageLayout::eUndefined;
};

struct RetiredMeshGpuPayload
{
    MeshGpuPayload payload{};
    std::uint64_t retireAfterSerial = 0;
};

struct RetiredTextureGpuPayload
{
    TextureGpuPayload payload{};
    std::uint64_t retireAfterSerial = 0;
};

struct SceneGeometryAtlasReusableRange
{
    vk::DeviceSize byteOffset = 0;
    vk::DeviceSize byteSize = 0;
};

struct SceneGeometryAtlas
{
    nr::rhi::Buffer vertexBuffer{};
    nr::rhi::Buffer indexBuffer{};
    vk::DeviceSize vertexCapacityBytes = 0;
    vk::DeviceSize indexCapacityBytes = 0;
    vk::DeviceSize vertexHighWaterBytes = 0;
    vk::DeviceSize indexHighWaterBytes = 0;
    std::vector<SceneGeometryAtlasReusableRange> vertexReusableRanges{};
    std::vector<SceneGeometryAtlasReusableRange> indexReusableRanges{};
    SceneGeometryAtlasBackingGeneration backingGeneration{};
};

struct RetiredSceneGeometryAtlasBuffers
{
    nr::rhi::Buffer vertexBuffer{};
    nr::rhi::Buffer indexBuffer{};
    std::uint64_t retireAfterSerial = 0;
};
} // namespace detail

struct MeshAssetRecord
{
    nr::resource::MeshHandle handle{};
    std::string stableKey{};
    nr::resource::Mesh cpu{};
    std::uint64_t cpuVersion = 1;
    std::uint32_t liveTemplatePins = 0;
    std::uint64_t gpuVersion = 0;
    GpuResidencyState gpuState = GpuResidencyState::none;
    std::optional<detail::MeshGpuPayload> gpu{};
    std::vector<detail::RetiredMeshGpuPayload> retiredGpu{};
    bool cpuReady = false;
};

struct MaterialAssetRecord
{
    nr::resource::MaterialHandle handle{};
    std::string stableKey{};
    nr::resource::Material cpu{};
    std::uint64_t cpuVersion = 1;
    std::uint32_t liveTemplatePins = 0;
    bool cpuReady = false;
};

struct TextureAssetRecord
{
    nr::resource::TextureHandle handle{};
    std::string stableKey{};
    nr::resource::Texture cpu{};
    std::uint64_t cpuVersion = 1;
    std::uint32_t liveTemplatePins = 0;
    std::uint64_t gpuVersion = 0;
    GpuResidencyState gpuState = GpuResidencyState::none;
    std::optional<detail::TextureGpuPayload> gpu{};
    std::vector<detail::RetiredTextureGpuPayload> retiredGpu{};
    bool cpuReady = false;
};

struct SceneSampledTextureBinding
{
    nr::resource::TextureHandle texture{};
    std::uint32_t descriptorIndex = kDefaultSceneTextureId;
    std::reference_wrapper<const nr::rhi::Image> image;
    vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal;
    vk::Format format = vk::Format::eUndefined;
    vk::Extent3D extent{1, 1, 1};
    std::uint64_t gpuVersion = 0;
};

struct CameraAssetRecord
{
    nr::resource::CameraAssetHandle handle{};
    std::string stableKey{};
    nr::resource::CameraAsset cpu{};
    std::uint32_t liveTemplatePins = 0;
    bool cpuReady = false;
};

struct LightAssetRecord
{
    nr::resource::LightAssetHandle handle{};
    std::string stableKey{};
    nr::resource::LightAsset cpu{};
    std::uint32_t liveTemplatePins = 0;
    bool cpuReady = false;
};

struct SceneTemplateRecord
{
    SceneTemplateHandle handle{};
    std::string stableKey{};
    flecs::entity prefabRoot{};
    std::vector<flecs::entity_t> prefabEntities{};
    TemplateResourcePinSet pins{};
    std::uint32_t liveInstanceCount = 0;
    std::size_t templateNodeCount = 0;
    std::size_t templateMeshBindingCount = 0;
    std::size_t templateCameraBindingCount = 0;
    std::size_t templateLightBindingCount = 0;
    TemplateHierarchyPolicy hierarchyPolicy = TemplateHierarchyPolicy::autoSelect;
};

struct SceneInstanceRecord
{
    SceneInstanceHandle handle{};
    SceneTemplateHandle templateHandle{};
    flecs::entity root{};
    bool active = true;
    std::size_t expectedEntityCount = 1;
};

struct SceneExtractProfileRecord
{
    SceneExtractProfileHandle handle{};
    std::string debugName{};
    ScenePacketDomain domain = ScenePacketDomain::rasterDraw;
    SceneSelectionMask selection{};
    bool requireReadyForDomain = true;
    bool requireActiveInstances = true;
    bool enableCoarseGrouping = true;
};

struct SceneStatistics
{
    std::size_t templateCount = 0;
    std::size_t instanceCount = 0;
    std::size_t extractProfileCount = 0;
    std::size_t meshAssetCount = 0;
    std::size_t materialAssetCount = 0;
    std::size_t textureAssetCount = 0;
    std::size_t cameraAssetCount = 0;
    std::size_t lightAssetCount = 0;
    std::size_t templateNodeCount = 0;
    std::size_t templateMeshBindingCount = 0;
    std::size_t templateCameraBindingCount = 0;
    std::size_t templateLightBindingCount = 0;
};
} // namespace nr::scene

export namespace nr::scene::detail
{
using SiblingNameTable = std::map<flecs::entity_t, std::map<std::string, std::uint32_t>>;

struct TextureColorSpaceHint
{
    bool hasColor = false;
    bool hasLinear = false;
    bool preferSrgb = false;
};

struct PreparedImageLevel
{
    nr::resource::ImageLevel level{};
    std::uint32_t channelCount = 4;
};
} // namespace nr::scene::detail
