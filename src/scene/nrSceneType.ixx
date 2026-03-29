module;
// #include <flecs.h>
export module nr.scene:type;

import dependency;
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
    waitingAcquire,
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
    std::uint32_t submeshIndex = 0;
    glm::mat4 world{1.0f};
    nr::resource::Aabb worldBounds{};
    std::uint64_t sortKey = 0;
};

struct RayTracingInstancePacket
{
    flecs::entity renderable{};
    nr::resource::MeshHandle mesh{};
    std::uint32_t submeshIndex = 0;
    glm::mat4 world{1.0f};
    std::uint32_t instanceMask = 0xFF;
    std::uint16_t tlasBucket = 0;
};

struct TlasBuildInputPacket
{
    flecs::entity renderable{};
    nr::resource::MeshHandle mesh{};
    std::uint32_t submeshIndex = 0;
    glm::mat4 world{1.0f};
    std::uint32_t instanceMask = 0xFF;
    std::uint16_t tlasBucket = 0;
};

struct SceneCameraBinding
{
    nr::resource::CameraAssetHandle camera{};
    bool synthetic = false;
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

struct ScenePacketSet
{
    ScenePacketDomain domain = ScenePacketDomain::rasterDraw;
    std::vector<RasterDrawPacket> rasterDraws{};
    std::vector<RayTracingInstancePacket> rtInstances{};
    std::vector<TlasBuildInputPacket> tlasBuildInputs{};
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

    [[nodiscard]] bool hasVertexBuffer() const noexcept
    {
        return vertexBuffer.buffer.has_value();
    }

    [[nodiscard]] bool hasIndexBuffer() const noexcept
    {
        return indexBuffer.buffer.has_value() && indexCount > 0;
    }
};

struct SceneBridgeDrawPacket
{
    flecs::entity renderable{};
    nr::resource::MeshHandle mesh{};
    nr::resource::MaterialHandle material{};
    std::uint32_t submeshIndex = 0;
    glm::mat4 world{1.0f};
    nr::resource::Aabb worldBounds{};
    std::uint64_t sortKey = 0;
    std::uint32_t meshBindless = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t materialBindless = std::numeric_limits<std::uint32_t>::max();
    SceneBridgeDrawGeometry geometry{};
};

struct SceneBridgeDrawGroup
{
    nr::resource::MaterialHandle material{};
    std::uint32_t materialBindless = std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint32_t> drawIndices{};
};

struct SceneBridgeFrame
{
    ScenePacketDomain domain = ScenePacketDomain::rasterDraw;
    std::vector<SceneBridgeDrawPacket> rasterDraws{};
    std::vector<SceneBridgeDrawGroup> materialGroups{};
    SceneBridgeFrameConstants frameConstants{};
    bool hasPrimaryCamera = false;
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
    std::uint32_t submeshCount = 0;
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
    // RGB: base color, A: opacity
    glm::vec4 baseColorFactor{1.0f};
    
    // RGB: emissive, F: metallic factor
    glm::vec4 emissiveAndMetallic{0.0f, 0.0f, 0.0f, 0.0f};
    
    // R: roughness, G: normal scale, B: occlusion strength, A: alpha cutoff
    glm::vec4 roughnessNormalOcclusionAlpha{1.0f, 1.0f, 1.0f, 0.5f};
    
    // R: alpha mode, G: flags (double-sided), B: unused, A: unused
    glm::uvec4 alphaAndFlags{0u};
    
    // Specular/Glossiness workflow
    // RGB: specular factor, A: glossiness factor
    glm::vec4 specularAndGlossiness{0.0f};
    
    // Anisotropy and workflow flags
    // R: anisotropy factor, G: metallic-roughness flag, B: specular-glossiness flag, A: anisotropy flag
    glm::uvec4 anisotropyAndWorkflow{0u};
    
    // Texture handles: baseColor, normal, metallicRoughness, occlusion, emissive
    std::array<std::uint64_t, 5> textureHandles{};
    
    // UV sets for each texture
    std::array<std::uint32_t, 5> uvSets{};
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

struct MeshGpuPayload
{
    nr::rhi::Buffer vertexBuffer{};
    nr::rhi::Buffer indexBuffer{};
    std::uint32_t vertexCount = 0;
    std::uint32_t indexCount = 0;
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

enum class MaterialSemanticSlot : std::uint8_t
{
    baseColor,
    normal,
    metallicRoughness,
    occlusion,
    emissive,
    unsupported,
};

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
