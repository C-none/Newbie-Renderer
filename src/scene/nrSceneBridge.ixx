module;
export module nr.scene:bridge;

import nr.load;
import nr.utils;
import std;
import :type;

export namespace nr::scene
{
template <typename TagT>
struct BridgeInput
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

        auto appendBridgeInputs = [&]<typename InputT>(std::vector<InputT> &inputs,
                                                       std::uint32_t count,
                                                       auto &&makeCanonicalKey,
                                                       auto &&formatErrorMessage) {
            auto const sourceIndices = std::views::iota(std::uint32_t{0}, count);
            std::ranges::for_each(sourceIndices, [&](std::uint32_t sourceIndex) {
                auto canonicalKey = makeCanonicalKey(sourceIndex);
                if (canonicalKey.empty())
                {
                    reportPlanError(formatErrorMessage(sourceIndex));
                    return;
                }

                inputs.push_back(InputT{
                    .sourceIndex = sourceIndex,
                    .canonicalKey = std::move(canonicalKey),
                });
            });
        };

        appendBridgeInputs(plan.textures,
                           static_cast<std::uint32_t>(sceneAsset.textures.size()),
                           [&](std::uint32_t sourceIndex) {
                               return makeTextureCanonicalKey(sceneAsset.textures[sourceIndex]);
                           },
                           [](std::uint32_t sourceIndex) {
                               return std::format("Texture asset {} is missing a canonical key.", sourceIndex);
                           });

        appendBridgeInputs(plan.materials,
                           static_cast<std::uint32_t>(sceneAsset.materials.size()),
                           [&](std::uint32_t sourceIndex) {
                               return makeMaterialCanonicalKey(sceneAsset, sourceIndex);
                           },
                           [](std::uint32_t sourceIndex) {
                               return std::format("SceneAsset.sourcePath must not be empty when deriving material key {}.", sourceIndex);
                           });

        appendBridgeInputs(plan.meshes,
                           static_cast<std::uint32_t>(sceneAsset.meshes.size()),
                           [&](std::uint32_t sourceIndex) {
                               return makeMeshCanonicalKey(sceneAsset, sourceIndex);
                           },
                           [](std::uint32_t sourceIndex) {
                               return std::format("SceneAsset.sourcePath must not be empty when deriving mesh key {}.", sourceIndex);
                           });

        appendBridgeInputs(plan.cameras,
                           static_cast<std::uint32_t>(sceneAsset.cameras.size()),
                           [&](std::uint32_t sourceIndex) {
                               return makeCameraCanonicalKey(sceneAsset, sourceIndex);
                           },
                           [](std::uint32_t sourceIndex) {
                               return std::format("SceneAsset.sourcePath must not be empty when deriving camera key {}.", sourceIndex);
                           });

        appendBridgeInputs(plan.lights,
                           static_cast<std::uint32_t>(sceneAsset.lights.size()),
                           [&](std::uint32_t sourceIndex) {
                               return makeLightCanonicalKey(sceneAsset, sourceIndex);
                           },
                           [](std::uint32_t sourceIndex) {
                               return std::format("SceneAsset.sourcePath must not be empty when deriving light key {}.", sourceIndex);
                           });

        return plan;
    }
};

struct SceneRenderBridgeBuildInput
{
    std::reference_wrapper<const ScenePacketSet> packetSet;
    std::optional<std::reference_wrapper<const SceneResolvedCamera>> primaryCamera{};
    std::function<std::optional<std::uint32_t>(nr::resource::MeshHandle)> resolveMeshBindless{};
    std::function<std::optional<std::uint32_t>(nr::resource::MaterialHandle)> resolveMaterialBindless{};
};

class SceneRenderBridge
{
  public:
    [[nodiscard]] static SceneBridgeFrame buildFrame(const SceneRenderBridgeBuildInput &input)
    {
        auto frame = SceneBridgeFrame{};
        auto const &packetSet = input.packetSet.get();
        frame.domain = packetSet.domain;

        if (input.primaryCamera.has_value())
        {
            auto const &camera = input.primaryCamera->get();
            frame.hasPrimaryCamera = true;
            frame.frameConstants.view = camera.view;
            frame.frameConstants.projection = camera.projection;
            frame.frameConstants.viewProjection = camera.projection * camera.view;
            frame.frameConstants.cameraWorld = glm::vec3{camera.world[3]};
        }

        if (packetSet.domain != ScenePacketDomain::rasterDraw)
        {
            return frame;
        }

        auto resolveMeshBindless = [&](nr::resource::MeshHandle meshHandle) {
            if (input.resolveMeshBindless)
            {
                auto resolved = input.resolveMeshBindless(meshHandle);
                if (resolved.has_value())
                {
                    return *resolved;
                }
            }

            return meshHandle.valid() ? meshHandle.slot : std::numeric_limits<std::uint32_t>::max();
        };

        auto resolveMaterialBindless = [&](nr::resource::MaterialHandle materialHandle) {
            if (input.resolveMaterialBindless)
            {
                auto resolved = input.resolveMaterialBindless(materialHandle);
                if (resolved.has_value())
                {
                    return *resolved;
                }
            }

            return materialHandle.valid() ? materialHandle.slot : std::numeric_limits<std::uint32_t>::max();
        };

        frame.rasterDraws.reserve(packetSet.rasterDraws.size());
        std::ranges::for_each(packetSet.rasterDraws, [&](const RasterDrawPacket &packet) {
            frame.rasterDraws.push_back(SceneBridgeDrawPacket{
                .renderable = packet.renderable,
                .mesh = packet.mesh,
                .material = packet.material,
                .submeshIndex = packet.submeshIndex,
                .world = packet.world,
                .worldBounds = packet.worldBounds,
                .sortKey = packet.sortKey,
                .meshBindless = resolveMeshBindless(packet.mesh),
                .materialBindless = resolveMaterialBindless(packet.material),
            });
        });

        frame.frameConstants.drawCount = static_cast<float>(frame.rasterDraws.size());

        auto materialGroupByHandle = std::map<nr::resource::MaterialHandle, std::size_t>{};
        auto drawIndices = std::views::iota(std::size_t{0}, frame.rasterDraws.size());
        std::ranges::for_each(drawIndices, [&](std::size_t drawIndex) {
            auto const &draw = frame.rasterDraws[drawIndex];
            auto [groupIt, inserted] = materialGroupByHandle.try_emplace(draw.material, frame.materialGroups.size());
            if (inserted)
            {
                frame.materialGroups.push_back(SceneBridgeDrawGroup{
                    .material = draw.material,
                    .materialBindless = draw.materialBindless,
                    .drawIndices = {},
                });
            }

            frame.materialGroups[groupIt->second].drawIndices.push_back(static_cast<std::uint32_t>(drawIndex));
        });

        return frame;
    }
};
} // namespace nr::scene
