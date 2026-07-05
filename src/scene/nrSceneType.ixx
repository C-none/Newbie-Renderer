// #include <flecs.h>
export module nr.scene:type;
import dependency.math;
import dependency.ecs;
import dependency.vulkan;

import nr.load;
import nr.resource;
import nr.rhi;
import std;

export namespace nr::scene
{
using SceneTemplateHandle = nr::resource::Handle<struct SceneTemplateTag>;
using SceneInstanceHandle = nr::resource::Handle<struct SceneInstanceTag>;
using SceneExtractProfileHandle = nr::resource::Handle<struct SceneExtractProfileTag>;

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
    evictQueued,
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
    glm::mat4 rootTransform{1.0f};
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
    std::array<glm::vec4, 6> planes{};
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
    std::optional<glm::uvec2> viewportExtent{};
    std::optional<std::uint32_t> partitionOverride{};
};

struct RasterDrawPacket
{
    flecs::entity renderable{};
    nr::resource::MeshHandle mesh{};
    nr::resource::MaterialHandle material{};
    std::uint32_t geometryIndex = 0;
    glm::mat4 world{1.0f};
    nr::resource::Aabb worldBounds{};
    std::uint64_t sortKey = 0;
};

struct RayTracingInstancePacket
{
    flecs::entity renderable{};
    nr::resource::MeshHandle mesh{};
    glm::mat4 world{1.0f};
    std::uint32_t instanceMask = 0xFF;
    std::uint16_t tlasBucket = 0;
};

struct TlasBuildInputPacket
{
    flecs::entity renderable{};
    nr::resource::MeshHandle mesh{};
    glm::mat4 world{1.0f};
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
    glm::mat4 world{1.0f};
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    SceneFrustum frustum{};
    bool fallback = false;
};

struct SceneLightPacket
{
    flecs::entity entity{};
    nr::resource::LightAssetHandle light{};
    glm::mat4 world{1.0f};
    glm::vec3 position{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
    std::uint32_t stableInstanceId = 0;
};

struct ScenePacketSet
{
    ScenePacketDomain domain = ScenePacketDomain::rasterDraw;
    std::vector<RasterDrawPacket> rasterDraws{};
    std::vector<RayTracingInstancePacket> rtInstances{};
    std::vector<TlasBuildInputPacket> tlasBuildInputs{};
    std::vector<SceneLightPacket> lights{};
};

struct SceneBridgeFrameConstants
{
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::mat4 viewProjection{1.0f};
    glm::vec3 cameraWorld{0.0f};
    float drawCount = 0.0f;
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
};

struct SceneBridgeDrawGeometry
{
    SceneBridgeBufferBinding vertexBuffer{};
    SceneBridgeBufferBinding indexBuffer{};
    std::uint32_t firstVertex = 0;
    std::uint32_t vertexCount = 0;
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    std::int32_t vertexOffset = 0;
    vk::IndexType indexType = vk::IndexType::eUint32;
    vk::FrontFace frontFace = vk::FrontFace::eCounterClockwise;

    [[nodiscard]] bool hasVertexBuffer() const noexcept;

    [[nodiscard]] bool hasIndexBuffer() const noexcept;
};

struct SceneBridgeMaterialRasterState
{
    vk::CullModeFlags cullMode = vk::CullModeFlagBits::eBack;
    bool doubleSided = false;
};

enum class SceneMaterialTextureSlot : std::uint32_t
{
    baseColor = 0,
    normal = 1,
    metallicRoughness = 2,
    occlusion = 3,
    emissive = 4,
    clearcoat = 5,
    clearcoatRoughness = 6,
    clearcoatNormal = 7,
    sheenColor = 8,
    sheenRoughness = 9,
    transmission = 10,
    anisotropy = 11,
    count,
};

inline constexpr auto sceneMaterialTextureSlotCount = static_cast<std::size_t>(SceneMaterialTextureSlot::count);
using SceneTextureId = std::uint16_t;
inline constexpr SceneTextureId kDefaultSceneTextureId = 0u;
inline constexpr std::uint32_t kMaxSceneTextureId = static_cast<std::uint32_t>(std::numeric_limits<SceneTextureId>::max());

static_assert(sceneMaterialTextureSlotCount == nr::resource::materialTextureSlotCount);
static_assert(static_cast<std::uint32_t>(SceneMaterialTextureSlot::baseColor) == static_cast<std::uint32_t>(nr::resource::MaterialTextureSlotSemantic::baseColor));
static_assert(static_cast<std::uint32_t>(SceneMaterialTextureSlot::normal) == static_cast<std::uint32_t>(nr::resource::MaterialTextureSlotSemantic::normal));
static_assert(static_cast<std::uint32_t>(SceneMaterialTextureSlot::metallicRoughness) == static_cast<std::uint32_t>(nr::resource::MaterialTextureSlotSemantic::metallicRoughness));
static_assert(static_cast<std::uint32_t>(SceneMaterialTextureSlot::occlusion) == static_cast<std::uint32_t>(nr::resource::MaterialTextureSlotSemantic::occlusion));
static_assert(static_cast<std::uint32_t>(SceneMaterialTextureSlot::emissive) == static_cast<std::uint32_t>(nr::resource::MaterialTextureSlotSemantic::emissive));
static_assert(static_cast<std::uint32_t>(SceneMaterialTextureSlot::clearcoat) == static_cast<std::uint32_t>(nr::resource::MaterialTextureSlotSemantic::clearcoat));
static_assert(static_cast<std::uint32_t>(SceneMaterialTextureSlot::clearcoatRoughness) == static_cast<std::uint32_t>(nr::resource::MaterialTextureSlotSemantic::clearcoatRoughness));
static_assert(static_cast<std::uint32_t>(SceneMaterialTextureSlot::clearcoatNormal) == static_cast<std::uint32_t>(nr::resource::MaterialTextureSlotSemantic::clearcoatNormal));
static_assert(static_cast<std::uint32_t>(SceneMaterialTextureSlot::sheenColor) == static_cast<std::uint32_t>(nr::resource::MaterialTextureSlotSemantic::sheenColor));
static_assert(static_cast<std::uint32_t>(SceneMaterialTextureSlot::sheenRoughness) == static_cast<std::uint32_t>(nr::resource::MaterialTextureSlotSemantic::sheenRoughness));
static_assert(static_cast<std::uint32_t>(SceneMaterialTextureSlot::transmission) == static_cast<std::uint32_t>(nr::resource::MaterialTextureSlotSemantic::transmission));
static_assert(static_cast<std::uint32_t>(SceneMaterialTextureSlot::anisotropy) == static_cast<std::uint32_t>(nr::resource::MaterialTextureSlotSemantic::anisotropy));

using SceneMaterialTextureIds = std::array<SceneTextureId, sceneMaterialTextureSlotCount>;

[[nodiscard]] constexpr std::uint32_t sceneTextureId(nr::resource::TextureHandle texture) noexcept
{
    return texture.valid() ? texture.slot + 1u : kDefaultSceneTextureId;
}

struct SceneBridgeDrawPacket
{
    flecs::entity renderable{};
    nr::resource::MeshHandle mesh{};
    nr::resource::MaterialHandle material{};
    std::uint32_t geometryIndex = 0;
    glm::mat4 world{1.0f};
    nr::resource::Aabb worldBounds{};
    std::uint64_t sortKey = 0;
    std::uint32_t meshBindless = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t materialBindless = std::numeric_limits<std::uint32_t>::max();
    SceneMaterialTextureIds materialTextureIds{};
    SceneBridgeMaterialRasterState materialRaster{};
    SceneBridgeDrawGeometry geometry{};
};

struct SceneBridgeDrawGroup
{
    nr::resource::MaterialHandle material{};
    std::uint32_t materialBindless = std::numeric_limits<std::uint32_t>::max();
    SceneBridgeMaterialRasterState materialRaster{};
    std::vector<std::uint32_t> drawIndices{};
};

struct SceneBridgeFrame
{
    ScenePacketDomain domain = ScenePacketDomain::rasterDraw;
    SceneBridgeGeometryBuffers geometryBuffers{};
    std::vector<SceneBridgeDrawPacket> rasterDraws{};
    std::vector<SceneBridgeDrawGroup> materialGroups{};
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

struct SceneAccelerationStructureMeshSemanticKey
{
    std::uint64_t meshGpuVersion = 0;
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
    glm::mat4 value{1.0f};
};

struct WorldTransform
{
    glm::mat4 value{1.0f};
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
struct MaterialGpuData
{
    std::uint32_t abiVersion = 2;
    std::uint32_t featureFlags = 0;
    std::uint32_t alphaMode = 0;
    std::uint32_t textureSlotCount = static_cast<std::uint32_t>(nr::resource::materialTextureSlotCount);

    glm::vec4 baseColorFactor{1.0f};

    glm::vec4 emissiveAndMetallic{0.0f, 0.0f, 0.0f, 1.0f};

    glm::vec4 roughnessNormalOcclusionAlpha{1.0f, 1.0f, 1.0f, 0.5f};

    glm::vec4 clearcoatFactorRoughness{0.0f, 0.0f, 0.0f, 0.0f};

    glm::vec4 sheenColorRoughness{0.0f, 0.0f, 0.0f, 0.0f};

    glm::vec4 transmissionAnisotropy{0.0f, 0.0f, 0.0f, 0.0f};

    std::array<std::uint64_t, nr::resource::materialTextureSlotCount> textureHandles{};

    std::array<std::uint32_t, nr::resource::materialTextureSlotCount> uvSets{};
};

struct CameraGpuData
{
    std::uint32_t projection = 0;
    float verticalFovRadians = 0.0f;
    float orthoHeight = 0.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
};

struct LightGpuData
{
    std::uint32_t type = 0;
    float intensity = 1.0f;
    glm::vec3 color{1.0f};
    float range = 0.0f;
    float innerConeRadians = 0.0f;
    float outerConeRadians = 0.0f;
    std::uint32_t castShadow = 0;
};

struct MeshGeometryAtlasAllocation
{
    vk::DeviceSize vertexByteOffset = 0;
    vk::DeviceSize indexByteOffset = 0;
    std::uint32_t vertexBase = 0;
    std::uint32_t indexBase = 0;
    std::uint32_t vertexCount = 0;
    std::uint32_t indexCount = 0;
};

struct MeshGpuPayload
{
    MeshGeometryAtlasAllocation atlas{};
};

struct MaterialGpuPayload
{
    nr::rhi::Buffer buffer{};
    std::size_t byteSize = 0;
};

struct TextureGpuPayload
{
    nr::rhi::Image image{};
    vk::ImageLayout layout = vk::ImageLayout::eUndefined;
};

struct CameraGpuPayload
{
    nr::rhi::Buffer buffer{};
};

struct LightGpuPayload
{
    nr::rhi::Buffer buffer{};
};

struct RetiredMeshGpuPayload
{
    MeshGpuPayload payload{};
    std::uint64_t retireAfterSerial = 0;
};

struct RetiredMaterialGpuPayload
{
    MaterialGpuPayload payload{};
    std::uint64_t retireAfterSerial = 0;
};

struct RetiredTextureGpuPayload
{
    TextureGpuPayload payload{};
    std::uint64_t retireAfterSerial = 0;
};

struct RetiredCameraGpuPayload
{
    CameraGpuPayload payload{};
    std::uint64_t retireAfterSerial = 0;
};

struct RetiredLightGpuPayload
{
    LightGpuPayload payload{};
    std::uint64_t retireAfterSerial = 0;
};

struct SceneGeometryAtlas
{
    nr::rhi::Buffer vertexBuffer{};
    nr::rhi::Buffer indexBuffer{};
    vk::DeviceSize vertexCapacityBytes = 0;
    vk::DeviceSize indexCapacityBytes = 0;
    vk::DeviceSize vertexUsedBytes = 0;
    vk::DeviceSize indexUsedBytes = 0;
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
    std::uint32_t liveExplicitPins = 0;
    std::uint64_t gpuVersion = 0;
    std::uint64_t lastUploadFrameSerial = 0;
    GpuResidencyState gpuState = GpuResidencyState::none;
    std::optional<detail::MeshGpuPayload> gpu{};
    std::vector<detail::RetiredMeshGpuPayload> retiredGpu{};
    bool cpuReady = false;
    bool uploadQueued = false;
};

struct MaterialAssetRecord
{
    nr::resource::MaterialHandle handle{};
    std::string stableKey{};
    nr::resource::Material cpu{};
    std::uint64_t cpuVersion = 1;
    std::uint32_t liveTemplatePins = 0;
    std::uint32_t liveExplicitPins = 0;
    std::uint64_t gpuVersion = 0;
    std::uint64_t lastUploadFrameSerial = 0;
    GpuResidencyState gpuState = GpuResidencyState::none;
    std::optional<detail::MaterialGpuPayload> gpu{};
    std::vector<detail::RetiredMaterialGpuPayload> retiredGpu{};
    bool cpuReady = false;
    bool uploadQueued = false;
};

struct TextureAssetRecord
{
    nr::resource::TextureHandle handle{};
    std::string stableKey{};
    nr::resource::Texture cpu{};
    std::uint64_t cpuVersion = 1;
    std::uint32_t liveTemplatePins = 0;
    std::uint32_t liveExplicitPins = 0;
    std::uint64_t gpuVersion = 0;
    std::uint64_t lastUploadFrameSerial = 0;
    GpuResidencyState gpuState = GpuResidencyState::none;
    std::optional<detail::TextureGpuPayload> gpu{};
    std::vector<detail::RetiredTextureGpuPayload> retiredGpu{};
    bool cpuReady = false;
    bool uploadQueued = false;
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
    std::uint64_t cpuVersion = 1;
    std::uint32_t liveTemplatePins = 0;
    std::uint32_t liveExplicitPins = 0;
    std::uint64_t gpuVersion = 0;
    std::uint64_t lastUploadFrameSerial = 0;
    GpuResidencyState gpuState = GpuResidencyState::none;
    std::optional<detail::CameraGpuPayload> gpu{};
    std::vector<detail::RetiredCameraGpuPayload> retiredGpu{};
    bool cpuReady = false;
    bool uploadQueued = false;
};

struct LightAssetRecord
{
    nr::resource::LightAssetHandle handle{};
    std::string stableKey{};
    nr::resource::LightAsset cpu{};
    std::uint64_t cpuVersion = 1;
    std::uint32_t liveTemplatePins = 0;
    std::uint32_t liveExplicitPins = 0;
    std::uint64_t gpuVersion = 0;
    std::uint64_t lastUploadFrameSerial = 0;
    GpuResidencyState gpuState = GpuResidencyState::none;
    std::optional<detail::LightGpuPayload> gpu{};
    std::vector<detail::RetiredLightGpuPayload> retiredGpu{};
    bool cpuReady = false;
    bool uploadQueued = false;
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
    bool preferSrgb = true;
};

struct PreparedImageLevel
{
    nr::resource::ImageLevel level{};
    std::uint32_t channelCount = 4;
};
} // namespace nr::scene::detail
