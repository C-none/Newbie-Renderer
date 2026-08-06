export module nr.scene:bridge;

import nr.load;
import nr.utils;
import std;
import :type;

export namespace nr::scene
{
template <typename TagT> struct BridgeInput
{
    std::uint32_t sourceIndex = nr::load::invalidIndex;
    std::string canonicalKey{};
};

enum class TextureSamplingColorIntent : std::uint8_t
{
    srgb,
    linear,
};

struct TextureBridgeInput
{
    std::uint32_t sourceIndex = nr::load::invalidIndex;
    std::string canonicalKey{};
    TextureSamplingColorIntent samplingColorIntent = TextureSamplingColorIntent::linear;
    bool mixedColorAndLinearReferences = false;
};

using MaterialBridgeInput = BridgeInput<struct MaterialBridgeInputTag>;
using MeshBridgeInput = BridgeInput<struct MeshBridgeInputTag>;
using CameraBridgeInput = BridgeInput<struct CameraBridgeInputTag>;
using LightBridgeInput = BridgeInput<struct LightBridgeInputTag>;

struct SceneBridgePlan
{
    std::filesystem::path sourcePath{};
    std::vector<TextureBridgeInput> textures{};
    std::vector<MaterialBridgeInput> materials{};
    std::vector<MeshBridgeInput> meshes{};
    std::vector<CameraBridgeInput> cameras{};
    std::vector<LightBridgeInput> lights{};
    std::size_t reachableNodeCount = 0;
    std::size_t unreachableNodeCount = 0;
    std::size_t maximumReachableTemplateDepth = 0;
    bool hasErrors = false;

    [[nodiscard]] bool valid() const noexcept;
};

class SceneBridge
{
  public:
    enum class IndexedCanonicalKeyKind : std::uint8_t
    {
        material,
        mesh,
        camera,
        light,
    };

    template <IndexedCanonicalKeyKind Kind>
    [[nodiscard]] static constexpr std::string_view indexedCanonicalKeyLabel() noexcept
    {
        if constexpr (Kind == IndexedCanonicalKeyKind::material)
        {
            return "material";
        }
        else if constexpr (Kind == IndexedCanonicalKeyKind::mesh)
        {
            return "mesh";
        }
        else if constexpr (Kind == IndexedCanonicalKeyKind::camera)
        {
            return "camera";
        }
        else
        {
            return "light";
        }
    }

    template <IndexedCanonicalKeyKind Kind>
    [[nodiscard]] static std::string makeIndexedCanonicalKey(const nr::load::SceneAsset &sceneAsset,
                                                             std::uint32_t sourceIndex)
    {
        if (sceneAsset.sourcePath.empty())
        {
            return {};
        }

        return std::format("{}::{}[{}]", sceneAsset.sourcePath.generic_string(), indexedCanonicalKeyLabel<Kind>(),
                           sourceIndex);
    }

    [[nodiscard]] static std::string makeTextureCanonicalKey(const nr::load::SceneAsset &sceneAsset,
                                                             std::uint32_t textureIndex,
                                                             TextureSamplingColorIntent samplingColorIntent);

    [[nodiscard]] static std::string makeMaterialCanonicalKey(const nr::load::SceneAsset &sceneAsset,
                                                              std::uint32_t materialIndex);

    [[nodiscard]] static std::string makeMeshCanonicalKey(const nr::load::SceneAsset &sceneAsset,
                                                          std::uint32_t meshIndex);

    [[nodiscard]] static std::string makeCameraCanonicalKey(const nr::load::SceneAsset &sceneAsset,
                                                            std::uint32_t cameraIndex);

    [[nodiscard]] static std::string makeLightCanonicalKey(const nr::load::SceneAsset &sceneAsset,
                                                           std::uint32_t lightIndex);

    [[nodiscard]] static SceneBridgePlan buildPlan(const nr::load::SceneAsset &sceneAsset);
};

struct SceneRenderBridgeBuildInput
{
    std::reference_wrapper<const ScenePacketSet> packetSet;
    std::optional<std::reference_wrapper<const SceneResolvedCamera>> primaryCamera{};
    std::optional<SceneBridgeFrameConstants> frameConstantsOverride{};
};

[[nodiscard]] bool rasterDrawPacketResolved(const RasterDrawPacket &packet,
                                            const SceneBridgeGeometryBuffers &geometryBuffers,
                                            const SceneTextureHandleTable &textureHandlesById) noexcept;

class SceneRenderBridge
{
  public:
    [[nodiscard]] static SceneBridgeFrame buildFrame(const SceneRenderBridgeBuildInput &input);
};
} // namespace nr::scene
