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

using TextureBridgeInput = BridgeInput<struct TextureBridgeInputTag>;
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

    [[nodiscard]] static std::string makeTextureCanonicalKey(const nr::load::TextureAsset &textureAsset);

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
    std::function<std::optional<std::uint32_t>(nr::resource::MeshHandle)> resolveMeshBindless{};
    std::function<std::optional<std::uint32_t>(nr::resource::MaterialHandle)> resolveMaterialBindless{};
    std::function<std::optional<SceneMaterialTextureBindings>(nr::resource::MaterialHandle)> resolveMaterialTextures{};
    std::function<std::optional<SceneBridgeMaterialRasterState>(nr::resource::MaterialHandle)>
        resolveMaterialRasterState{};
    std::function<std::optional<SceneBridgeGeometryBuffers>()> resolveGeometryBuffers{};
    std::function<std::optional<SceneBridgeDrawGeometry>(nr::resource::MeshHandle, std::uint32_t)>
        resolveRasterDrawGeometry{};
};

class SceneRenderBridge
{
  public:
    [[nodiscard]] static SceneBridgeFrame buildFrame(const SceneRenderBridgeBuildInput &input);
};
} // namespace nr::scene
