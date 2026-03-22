module;
export module nr.scene:bridge;

import nr.load;
import nr.utils;
import std;
import :type;

export namespace nr::scene
{
struct TextureBridgeInput
{
    std::uint32_t sourceIndex = nr::load::invalidIndex;
    std::string canonicalKey{};
};

struct MaterialBridgeInput
{
    std::uint32_t sourceIndex = nr::load::invalidIndex;
    std::string canonicalKey{};
};

struct MeshBridgeInput
{
    std::uint32_t sourceIndex = nr::load::invalidIndex;
    std::string canonicalKey{};
};

struct CameraBridgeInput
{
    std::uint32_t sourceIndex = nr::load::invalidIndex;
    std::string canonicalKey{};
};

struct LightBridgeInput
{
    std::uint32_t sourceIndex = nr::load::invalidIndex;
    std::string canonicalKey{};
};

struct SceneBridgePlan
{
    std::filesystem::path sourcePath{};
    std::vector<TextureBridgeInput> textures{};
    std::vector<MaterialBridgeInput> materials{};
    std::vector<MeshBridgeInput> meshes{};
    std::vector<CameraBridgeInput> cameras{};
    std::vector<LightBridgeInput> lights{};
    bool hasErrors = false;

    [[nodiscard]] bool valid() const noexcept
    {
        return !hasErrors;
    }
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
            return "material";
        if constexpr (Kind == IndexedCanonicalKeyKind::mesh)
            return "mesh";
        if constexpr (Kind == IndexedCanonicalKeyKind::camera)
            return "camera";
        return "light";
    }

    template <IndexedCanonicalKeyKind Kind>
    [[nodiscard]] static std::string makeIndexedCanonicalKey(const nr::load::SceneAsset &sceneAsset, std::uint32_t sourceIndex)
    {
        if (sceneAsset.sourcePath.empty())
        {
            return {};
        }

        return std::format("{}::{}[{}]",
                           sceneAsset.sourcePath.generic_string(),
                           indexedCanonicalKeyLabel<Kind>(),
                           sourceIndex);
    }

    [[nodiscard]] static std::string makeTextureCanonicalKey(const nr::load::TextureAsset &textureAsset)
    {
        return textureAsset.key;
    }

    [[nodiscard]] static std::string makeMaterialCanonicalKey(const nr::load::SceneAsset &sceneAsset, std::uint32_t materialIndex)
    {
        return makeIndexedCanonicalKey<IndexedCanonicalKeyKind::material>(sceneAsset, materialIndex);
    }

    [[nodiscard]] static std::string makeMeshCanonicalKey(const nr::load::SceneAsset &sceneAsset, std::uint32_t meshIndex)
    {
        return makeIndexedCanonicalKey<IndexedCanonicalKeyKind::mesh>(sceneAsset, meshIndex);
    }

    [[nodiscard]] static std::string makeCameraCanonicalKey(const nr::load::SceneAsset &sceneAsset, std::uint32_t cameraIndex)
    {
        return makeIndexedCanonicalKey<IndexedCanonicalKeyKind::camera>(sceneAsset, cameraIndex);
    }

    [[nodiscard]] static std::string makeLightCanonicalKey(const nr::load::SceneAsset &sceneAsset, std::uint32_t lightIndex)
    {
        return makeIndexedCanonicalKey<IndexedCanonicalKeyKind::light>(sceneAsset, lightIndex);
    }

    [[nodiscard]] static SceneBridgePlan buildPlan(const nr::load::SceneAsset &sceneAsset)
    {
        auto plan = SceneBridgePlan{.sourcePath = sceneAsset.sourcePath};

        auto reportPlanError = [&](std::string message) {
            plan.hasErrors = true;
            nr::nrLog(nr::LogLevel::error,
                      "SCENE",
                      std::format("SceneBridgePlan error: {}", message),
                      std::source_location::current(),
                      false);
        };

        auto const textureIndices = std::views::iota(std::uint32_t{0}, static_cast<std::uint32_t>(sceneAsset.textures.size()));
        std::ranges::for_each(textureIndices, [&](std::uint32_t textureIndex) {
            auto canonicalKey = makeTextureCanonicalKey(sceneAsset.textures[textureIndex]);
            if (canonicalKey.empty())
            {
                reportPlanError(std::format("Texture asset {} is missing a canonical key.", textureIndex));
                return;
            }

            plan.textures.push_back(TextureBridgeInput{
                .sourceIndex = textureIndex,
                .canonicalKey = std::move(canonicalKey),
            });
        });

        auto const materialIndices = std::views::iota(std::uint32_t{0}, static_cast<std::uint32_t>(sceneAsset.materials.size()));
        std::ranges::for_each(materialIndices, [&](std::uint32_t materialIndex) {
            auto canonicalKey = makeMaterialCanonicalKey(sceneAsset, materialIndex);
            if (canonicalKey.empty())
            {
                reportPlanError(std::format("SceneAsset.sourcePath must not be empty when deriving material key {}.", materialIndex));
                return;
            }

            plan.materials.push_back(MaterialBridgeInput{
                .sourceIndex = materialIndex,
                .canonicalKey = std::move(canonicalKey),
            });
        });

        auto const meshIndices = std::views::iota(std::uint32_t{0}, static_cast<std::uint32_t>(sceneAsset.meshes.size()));
        std::ranges::for_each(meshIndices, [&](std::uint32_t meshIndex) {
            auto canonicalKey = makeMeshCanonicalKey(sceneAsset, meshIndex);
            if (canonicalKey.empty())
            {
                reportPlanError(std::format("SceneAsset.sourcePath must not be empty when deriving mesh key {}.", meshIndex));
                return;
            }

            plan.meshes.push_back(MeshBridgeInput{
                .sourceIndex = meshIndex,
                .canonicalKey = std::move(canonicalKey),
            });
        });

        auto const cameraIndices = std::views::iota(std::uint32_t{0}, static_cast<std::uint32_t>(sceneAsset.cameras.size()));
        std::ranges::for_each(cameraIndices, [&](std::uint32_t cameraIndex) {
            auto canonicalKey = makeCameraCanonicalKey(sceneAsset, cameraIndex);
            if (canonicalKey.empty())
            {
                reportPlanError(std::format("SceneAsset.sourcePath must not be empty when deriving camera key {}.", cameraIndex));
                return;
            }

            plan.cameras.push_back(CameraBridgeInput{
                .sourceIndex = cameraIndex,
                .canonicalKey = std::move(canonicalKey),
            });
        });

        auto const lightIndices = std::views::iota(std::uint32_t{0}, static_cast<std::uint32_t>(sceneAsset.lights.size()));
        std::ranges::for_each(lightIndices, [&](std::uint32_t lightIndex) {
            auto canonicalKey = makeLightCanonicalKey(sceneAsset, lightIndex);
            if (canonicalKey.empty())
            {
                reportPlanError(std::format("SceneAsset.sourcePath must not be empty when deriving light key {}.", lightIndex));
                return;
            }

            plan.lights.push_back(LightBridgeInput{
                .sourceIndex = lightIndex,
                .canonicalKey = std::move(canonicalKey),
            });
        });

        return plan;
    }
};
} // namespace nr::scene
