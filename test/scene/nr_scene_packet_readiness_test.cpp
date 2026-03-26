import std;
import dependency;
import nr.load;
import nr.rhi;
import nr.scene;

namespace
{
static_assert(requires(const nr::scene::Scene &scene,
                       nr::scene::SceneExtractProfileHandle profile,
                       const nr::scene::SceneExtractInput &input) {
    { scene.extractPackets(profile, input) } -> std::same_as<nr::scene::ScenePacketSet>;
});

[[nodiscard]] bool require(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::println("[fail] {}", message);
        return false;
    }

    return true;
}

[[nodiscard]] std::array<float, 16> identityTransform()
{
    return {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
}

[[nodiscard]] nr::load::SceneAsset buildReadinessSceneAsset()
{
    auto scene = nr::load::SceneAsset{};
    scene.sourcePath = std::filesystem::path{"manual_packet_readiness_scene.gltf"};

    auto image = nr::load::Image{};
    image.width = 1;
    image.height = 1;
    image.channels = 4;
    image.pixels = {255, 255, 255, 255};

    auto texture = nr::load::TextureAsset{};
    texture.key = "manual://textures/packet_readiness/baseColor";
    texture.resolvedPath = std::filesystem::path{"manual_textures/packet_readiness_baseColor.png"};
    texture.decodedImage = image;
    scene.textures.push_back(std::move(texture));

    auto material = nr::load::MaterialAsset{};
    material.name = "packet_readiness_material";
    material.textures.push_back(nr::load::MaterialTextureBinding{
        .textureIndex = 0,
        .uvChannel = 0,
        .textureTypeRaw = 0,
        .semantic = "diffuse",
    });
    scene.materials.push_back(std::move(material));

    auto mesh = nr::load::MeshAsset{};
    mesh.name = "packet_readiness_mesh";
    mesh.materialIndex = 0;
    mesh.vertices = {
        nr::load::VertexAsset{.position = {-0.5f, -0.5f, 0.0f}},
        nr::load::VertexAsset{.position = {0.5f, -0.5f, 0.0f}},
        nr::load::VertexAsset{.position = {0.0f, 0.5f, 0.0f}},
    };
    mesh.indices = {0, 1, 2};
    scene.meshes.push_back(std::move(mesh));

    scene.nodes.resize(2);
    scene.rootNodeIndex = 0;

    scene.nodes[0].name = "Root";
    scene.nodes[0].parentIndex = nr::load::invalidIndex;
    scene.nodes[0].childIndices = {1};
    scene.nodes[0].localTransform = identityTransform();

    scene.nodes[1].name = "MeshNode";
    scene.nodes[1].parentIndex = 0;
    scene.nodes[1].meshIndices = {0};
    scene.nodes[1].localTransform = identityTransform();

    scene.stats.nodeCount = static_cast<std::uint32_t>(scene.nodes.size());
    scene.stats.meshCount = static_cast<std::uint32_t>(scene.meshes.size());
    scene.stats.materialCount = static_cast<std::uint32_t>(scene.materials.size());
    scene.stats.textureCount = static_cast<std::uint32_t>(scene.textures.size());
    scene.stats.vertexCount = 3;
    scene.stats.indexCount = 3;

    return scene;
}

struct ReadinessHandles
{
    nr::resource::MeshHandle mesh{};
    nr::resource::MaterialHandle material{};
    nr::resource::TextureHandle texture{};
};

[[nodiscard]] std::optional<ReadinessHandles> resolveReadinessHandles(const nr::scene::Scene &scene,
                                                                      const nr::load::SceneAsset &sceneAsset)
{
    auto meshHandle = scene.findMeshHandleByStableKey(nr::scene::SceneBridge::makeMeshCanonicalKey(sceneAsset, 0));
    auto materialHandle = scene.findMaterialHandleByStableKey(nr::scene::SceneBridge::makeMaterialCanonicalKey(sceneAsset, 0));
    auto textureHandle = scene.findTextureHandleByStableKey(nr::scene::SceneBridge::makeTextureCanonicalKey(sceneAsset.textures[0]));

    if (!meshHandle.has_value() || !materialHandle.has_value() || !textureHandle.has_value())
    {
        return std::nullopt;
    }

    return ReadinessHandles{
        .mesh = *meshHandle,
        .material = *materialHandle,
        .texture = *textureHandle,
    };
}

void setMeshResidentForTest(nr::scene::Scene &scene,
                            nr::resource::MeshHandle handle,
                            bool resident)
{
    auto meshRecord = scene.tryGetMeshAsset(handle);
    if (!meshRecord.has_value())
    {
        return;
    }

    auto &mutableRecord = const_cast<nr::scene::MeshAssetRecord &>(meshRecord->get());
    mutableRecord.uploadQueued = false;
    if (resident)
    {
        mutableRecord.gpuState = nr::scene::GpuResidencyState::resident;
        mutableRecord.gpuVersion = mutableRecord.cpuVersion;
    }
    else
    {
        mutableRecord.gpuState = nr::scene::GpuResidencyState::none;
        mutableRecord.gpuVersion = 0;
    }
}

void setMaterialResidentForTest(nr::scene::Scene &scene,
                                nr::resource::MaterialHandle handle,
                                bool resident)
{
    auto materialRecord = scene.tryGetMaterialAsset(handle);
    if (!materialRecord.has_value())
    {
        return;
    }

    auto &mutableRecord = const_cast<nr::scene::MaterialAssetRecord &>(materialRecord->get());
    mutableRecord.uploadQueued = false;
    if (resident)
    {
        mutableRecord.gpuState = nr::scene::GpuResidencyState::resident;
        mutableRecord.gpuVersion = mutableRecord.cpuVersion;
    }
    else
    {
        mutableRecord.gpuState = nr::scene::GpuResidencyState::none;
        mutableRecord.gpuVersion = 0;
    }
}

void setTextureResidentForTest(nr::scene::Scene &scene,
                               nr::resource::TextureHandle handle,
                               bool resident)
{
    auto textureRecord = scene.tryGetTextureAsset(handle);
    if (!textureRecord.has_value())
    {
        return;
    }

    auto &mutableRecord = const_cast<nr::scene::TextureAssetRecord &>(textureRecord->get());
    mutableRecord.uploadQueued = false;
    if (resident)
    {
        mutableRecord.gpuState = nr::scene::GpuResidencyState::resident;
        mutableRecord.gpuVersion = mutableRecord.cpuVersion;
    }
    else
    {
        mutableRecord.gpuState = nr::scene::GpuResidencyState::none;
        mutableRecord.gpuVersion = 0;
    }
}

[[nodiscard]] nr::scene::SceneExtractProfileHandle registerProfile(nr::scene::Scene &scene,
                                                                   nr::scene::ScenePacketDomain domain,
                                                                   bool requireReadyForDomain)
{
    auto requiredSelection = nr::scene::sceneSelectionMask(nr::scene::SceneSelectionBit::rtMain);
    if (domain == nr::scene::ScenePacketDomain::rasterDraw)
    {
        requiredSelection = nr::scene::sceneSelectionMask(nr::scene::SceneSelectionBit::rasterOpaque);
    }

    return scene.registerExtractProfile(nr::scene::SceneExtractProfileCreateInfo{
        .debugName = std::format("packet_readiness_domain_{}", static_cast<std::uint32_t>(domain)),
        .domain = domain,
        .selection = nr::scene::SceneSelectionMask{.requireAll = requiredSelection},
        .requireReadyForDomain = requireReadyForDomain,
        .requireActiveInstances = true,
    });
}

[[nodiscard]] bool checkDomainSpecificReadinessFiltering()
{
    std::println("\n=== Case: checkDomainSpecificReadinessFiltering ===");

    auto sceneAsset = buildReadinessSceneAsset();

    nr::rhi::Device device{};
    nr::scene::Scene scene(nr::scene::SceneCreateInfo{.device = device});

    auto templateHandle = scene.registerTemplate(sceneAsset);
    auto instanceHandle = scene.instantiate(templateHandle);
    if (!require(templateHandle.valid() && instanceHandle.valid(), "Readiness scene should register and instantiate."))
    {
        return false;
    }

    scene.updateSimulation({.deltaSeconds = 1.0f / 60.0f});

    auto handles = resolveReadinessHandles(scene, sceneAsset);
    if (!require(handles.has_value(), "Readiness test should resolve mesh/material/texture handles."))
    {
        return false;
    }

    auto rasterProfile = registerProfile(scene, nr::scene::ScenePacketDomain::rasterDraw, true);
    auto rtProfile = registerProfile(scene, nr::scene::ScenePacketDomain::rayTracingInstance, true);
    auto tlasProfile = registerProfile(scene, nr::scene::ScenePacketDomain::tlasBuildInput, true);
    if (!require(rasterProfile.valid() && rtProfile.valid() && tlasProfile.valid(),
                 "All readiness profiles should be valid."))
    {
        return false;
    }

    setMeshResidentForTest(scene, handles->mesh, false);
    setMaterialResidentForTest(scene, handles->material, false);
    setTextureResidentForTest(scene, handles->texture, false);

    auto initialRaster = scene.extractPackets(rasterProfile);
    auto initialRt = scene.extractPackets(rtProfile);
    auto initialTlas = scene.extractPackets(tlasProfile);

    if (!require(initialRaster.rasterDraws.empty(), "Raster packets should be empty when nothing is resident."))
    {
        return false;
    }
    if (!require(initialRt.rtInstances.empty(), "RT packets should be empty when mesh is not resident."))
    {
        return false;
    }
    if (!require(initialTlas.tlasBuildInputs.empty(), "TLAS packets should be empty when mesh is not resident."))
    {
        return false;
    }

    setMeshResidentForTest(scene, handles->mesh, true);

    auto meshOnlyRaster = scene.extractPackets(rasterProfile);
    auto meshOnlyRt = scene.extractPackets(rtProfile);
    auto meshOnlyTlas = scene.extractPackets(tlasProfile);

    if (!require(meshOnlyRaster.rasterDraws.empty(),
                 "Raster packets should remain empty when material/texture are not ready."))
    {
        return false;
    }
    if (!require(!meshOnlyRt.rtInstances.empty(),
                 "RT packets should be available when mesh is resident."))
    {
        return false;
    }
    if (!require(!meshOnlyTlas.tlasBuildInputs.empty(),
                 "TLAS packets should be available when mesh is resident."))
    {
        return false;
    }

    setMaterialResidentForTest(scene, handles->material, true);

    auto meshMaterialRaster = scene.extractPackets(rasterProfile);
    if (!require(meshMaterialRaster.rasterDraws.empty(),
                 "Raster packets should stay empty when required texture is not resident."))
    {
        return false;
    }

    setTextureResidentForTest(scene, handles->texture, true);

    auto allReadyRaster = scene.extractPackets(rasterProfile);
    auto allReadyRt = scene.extractPackets(rtProfile);
    auto allReadyTlas = scene.extractPackets(tlasProfile);

    if (!require(!allReadyRaster.rasterDraws.empty(),
                 "Raster packets should be produced once mesh/material/texture are all ready."))
    {
        return false;
    }
    if (!require(!allReadyRt.rtInstances.empty(),
                 "RT packets should remain available after full readiness."))
    {
        return false;
    }
    if (!require(!allReadyTlas.tlasBuildInputs.empty(),
                 "TLAS packets should remain available after full readiness."))
    {
        return false;
    }

    return true;
}

[[nodiscard]] bool checkRequireResidentGeometryFalseBypassesReadiness()
{
    std::println("\n=== Case: checkRequireResidentGeometryFalseBypassesReadiness ===");

    auto sceneAsset = buildReadinessSceneAsset();

    nr::rhi::Device device{};
    nr::scene::Scene scene(nr::scene::SceneCreateInfo{.device = device});

    auto templateHandle = scene.registerTemplate(sceneAsset);
    auto instanceHandle = scene.instantiate(templateHandle);
    if (!require(templateHandle.valid() && instanceHandle.valid(), "Bypass scene should register and instantiate."))
    {
        return false;
    }

    scene.updateSimulation({.deltaSeconds = 1.0f / 60.0f});

    auto rasterProfile = registerProfile(scene, nr::scene::ScenePacketDomain::rasterDraw, false);
    if (!require(rasterProfile.valid(), "Bypass profile should be valid."))
    {
        return false;
    }

    auto packets = scene.extractPackets(rasterProfile);
    if (!require(!packets.rasterDraws.empty(),
                 "When requireReadyForDomain=false, raster extraction should not be blocked by readiness."))
    {
        return false;
    }

    return true;
}

} // namespace

int main()
{
    auto const cases = std::array{
        std::pair{"checkDomainSpecificReadinessFiltering", &checkDomainSpecificReadinessFiltering},
        std::pair{"checkRequireResidentGeometryFalseBypassesReadiness", &checkRequireResidentGeometryFalseBypassesReadiness},
    };

    std::size_t passedCount = 0;
    for (auto const &[name, fn] : cases)
    {
        std::println("\n[run] {}", name);
        auto const ok = fn();
        std::println("[result] {} => {}", name, ok ? "PASS" : "FAIL");
        if (ok)
        {
            ++passedCount;
        }
    }

    std::println("\n[summary] passed={} failed={}", passedCount, cases.size() - passedCount);
    if (passedCount != cases.size())
    {
        return 1;
    }

    return 0;
}
