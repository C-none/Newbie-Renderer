import std;
import dependency;
import nr.load;
import nr.resource;
import nr.rhi;
import nr.scene;

namespace
{
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

[[nodiscard]] std::array<float, 16> translatedTransform(float tx, float ty, float tz)
{
    return {
        1.0f, 0.0f, 0.0f, tx,
        0.0f, 1.0f, 0.0f, ty,
        0.0f, 0.0f, 1.0f, tz,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
}

[[nodiscard]] nr::load::SceneAsset buildRtIntegrationSceneAsset()
{
    auto scene = nr::load::SceneAsset{};
    scene.sourcePath = std::filesystem::path{"profile_scene_rt_pipeline.gltf"};

    auto material = nr::load::MaterialAsset{};
    material.name = "profile_rt_material";
    scene.materials.push_back(std::move(material));

    auto mesh = nr::load::MeshAsset{};
    mesh.name = "profile_rt_mesh";
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
    scene.nodes[1].localTransform = translatedTransform(0.0f, 0.0f, -2.0f);

    scene.stats.nodeCount = static_cast<std::uint32_t>(scene.nodes.size());
    scene.stats.meshCount = static_cast<std::uint32_t>(scene.meshes.size());
    scene.stats.materialCount = static_cast<std::uint32_t>(scene.materials.size());
    scene.stats.vertexCount = 3;
    scene.stats.indexCount = 3;

    return scene;
}

struct RtConsumerInstance
{
    nr::resource::MeshHandle mesh{};
    std::uint32_t submeshIndex = 0;
    glm::mat4 world{1.0f};
    std::uint32_t mask = 0;
    std::uint16_t bucket = 0;
};

struct TlasConsumerBuildItem
{
    nr::resource::MeshHandle mesh{};
    std::uint32_t submeshIndex = 0;
    glm::mat4 world{1.0f};
    std::uint32_t mask = 0;
    std::uint16_t bucket = 0;
};

[[nodiscard]] std::vector<RtConsumerInstance> buildRtConsumerInstances(std::span<const nr::scene::RayTracingInstancePacket> packets)
{
    auto instances = std::vector<RtConsumerInstance>{};
    instances.reserve(packets.size());

    std::ranges::for_each(packets, [&](const nr::scene::RayTracingInstancePacket &packet) {
        instances.push_back(RtConsumerInstance{
            .mesh = packet.mesh,
            .submeshIndex = packet.submeshIndex,
            .world = packet.world,
            .mask = packet.instanceMask,
            .bucket = packet.tlasBucket,
        });
    });

    return instances;
}

[[nodiscard]] std::vector<TlasConsumerBuildItem> buildTlasConsumerItems(std::span<const nr::scene::TlasBuildInputPacket> packets)
{
    auto builds = std::vector<TlasConsumerBuildItem>{};
    builds.reserve(packets.size());

    std::ranges::for_each(packets, [&](const nr::scene::TlasBuildInputPacket &packet) {
        builds.push_back(TlasConsumerBuildItem{
            .mesh = packet.mesh,
            .submeshIndex = packet.submeshIndex,
            .world = packet.world,
            .mask = packet.instanceMask,
            .bucket = packet.tlasBucket,
        });
    });

    return builds;
}

[[nodiscard]] bool checkSceneDrivenRtAndTlasConsumers()
{
    std::println("\n=== Case: checkSceneDrivenRtAndTlasConsumers ===");

    auto sceneAsset = buildRtIntegrationSceneAsset();

    nr::rhi::Device device{};
    nr::scene::Scene scene(nr::scene::SceneCreateInfo{.device = device});

    auto templateHandle = scene.registerTemplate(sceneAsset);
    if (!require(templateHandle.valid(), "Template registration should succeed for RT integration scene."))
    {
        return false;
    }

    auto instanceHandle = scene.instantiate(templateHandle);
    if (!require(instanceHandle.valid(), "Instance should be valid for RT integration scene."))
    {
        return false;
    }

    scene.updateSimulation({.deltaSeconds = 1.0f / 60.0f});

    auto rtProfile = scene.registerExtractProfile(nr::scene::SceneExtractProfileCreateInfo{
        .debugName = "profile_scene_rt_instances",
        .domain = nr::scene::ScenePacketDomain::rayTracingInstance,
        .selection = nr::scene::SceneSelectionMask{
            .requireAll = nr::scene::sceneSelectionMask(nr::scene::SceneSelectionBit::rtMain),
        },
        .requireReadyForDomain = false,
        .requireActiveInstances = true,
    });

    auto tlasProfile = scene.registerExtractProfile(nr::scene::SceneExtractProfileCreateInfo{
        .debugName = "profile_scene_tlas_build",
        .domain = nr::scene::ScenePacketDomain::tlasBuildInput,
        .selection = nr::scene::SceneSelectionMask{
            .requireAll = nr::scene::sceneSelectionMask(nr::scene::SceneSelectionBit::rtMain),
        },
        .requireReadyForDomain = false,
        .requireActiveInstances = true,
    });

    if (!require(rtProfile.valid() && tlasProfile.valid(), "RT and TLAS extraction profiles should both be valid."))
    {
        return false;
    }

    auto rtPacketSet = scene.extractPackets(rtProfile);
    auto tlasPacketSet = scene.extractPackets(tlasProfile);

    if (!require(rtPacketSet.domain == nr::scene::ScenePacketDomain::rayTracingInstance,
                 "RT profile should return rayTracingInstance packet domain."))
    {
        return false;
    }
    if (!require(tlasPacketSet.domain == nr::scene::ScenePacketDomain::tlasBuildInput,
                 "TLAS profile should return tlasBuildInput packet domain."))
    {
        return false;
    }

    if (!require(!rtPacketSet.rtInstances.empty(), "RT packet extraction should produce ray-tracing instances."))
    {
        return false;
    }
    if (!require(!tlasPacketSet.tlasBuildInputs.empty(), "TLAS packet extraction should produce dedicated build inputs."))
    {
        return false;
    }

    if (!require(rtPacketSet.tlasBuildInputs.empty(), "RT domain should not emit TLAS-build packet array."))
    {
        return false;
    }
    if (!require(tlasPacketSet.rtInstances.empty(), "TLAS-build domain should not emit RT instance packet array."))
    {
        return false;
    }

    auto rtInstances = buildRtConsumerInstances(std::span{rtPacketSet.rtInstances});
    auto tlasItems = buildTlasConsumerItems(std::span{tlasPacketSet.tlasBuildInputs});

    if (!require(rtInstances.size() == rtPacketSet.rtInstances.size(),
                 "RT consumer should consume all ray-tracing instance packets."))
    {
        return false;
    }
    if (!require(tlasItems.size() == tlasPacketSet.tlasBuildInputs.size(),
                 "TLAS consumer should consume all dedicated TLAS build input packets."))
    {
        return false;
    }

    if (!require(std::ranges::all_of(rtInstances, [](const RtConsumerInstance &instance) {
                      return instance.mesh.valid() && instance.mask != 0u;
                  }),
                 "All RT consumer instances should have valid mesh handles and non-zero masks."))
    {
        return false;
    }

    if (!require(std::ranges::all_of(tlasItems, [](const TlasConsumerBuildItem &item) {
                      return item.mesh.valid() && item.mask != 0u;
                  }),
                 "All TLAS consumer build items should have valid mesh handles and non-zero masks."))
    {
        return false;
    }

    return true;
}

} // namespace

int main()
{
    auto const cases = std::array{
        std::pair{"checkSceneDrivenRtAndTlasConsumers", &checkSceneDrivenRtAndTlasConsumers},
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
