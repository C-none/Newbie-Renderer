import std;
import dependency;
import nr.load;
import nr.rhi;
import nr.scene;

namespace
{
static_assert(requires(nr::scene::Scene &scene, const nr::scene::SceneUpdateInput &input) {
    scene.updateSimulation(input);
});

static_assert(requires {
    nr::scene::LocalTransform{};
    nr::scene::WorldTransform{};
    nr::scene::LocalBounds{};
    nr::scene::WorldBounds{};
    nr::scene::RenderableBinding{};
    nr::scene::SceneSelectionBits{};
    nr::scene::ScenePartitionId{};
    nr::scene::TlasBucketId{};
    nr::scene::StaticObject{};
    nr::scene::DynamicObject{};
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

[[nodiscard]] bool almostEqual(float lhs, float rhs, float epsilon = 1e-5f)
{
    return std::abs(lhs - rhs) <= epsilon;
}

[[nodiscard]] bool almostEqualVec3(const glm::vec3 &lhs, const glm::vec3 &rhs, float epsilon = 1e-4f)
{
    auto delta = glm::abs(lhs - rhs);
    return delta.x <= epsilon && delta.y <= epsilon && delta.z <= epsilon;
}

[[nodiscard]] std::array<float, 16> matrixWithTranslation(float x, float y, float z)
{
    return {
        1.0f, 0.0f, 0.0f, x,
        0.0f, 1.0f, 0.0f, y,
        0.0f, 0.0f, 1.0f, z,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
}

[[nodiscard]] nr::load::SceneAsset buildPhase3SceneAsset()
{
    auto scene = nr::load::SceneAsset{};
    scene.sourcePath = std::filesystem::path{"manual_phase3_scene.gltf"};

    auto mesh = nr::load::MeshAsset{};
    mesh.name = "phase3_mesh";
    mesh.materialIndex = nr::load::invalidIndex;
    mesh.vertices = {
        nr::load::VertexAsset{.position = {0.0f, 0.0f, 0.0f}},
        nr::load::VertexAsset{.position = {1.0f, 0.0f, 0.0f}},
        nr::load::VertexAsset{.position = {1.0f, 1.0f, 0.0f}},
    };
    mesh.indices = {0, 1, 2};
    scene.meshes.push_back(std::move(mesh));

    scene.nodes.resize(2);
    scene.rootNodeIndex = 0;

    scene.nodes[0].name = "ImportedRoot";
    scene.nodes[0].parentIndex = nr::load::invalidIndex;
    scene.nodes[0].childIndices = {1};
    scene.nodes[0].localTransform = matrixWithTranslation(1.0f, 0.0f, 0.0f);

    scene.nodes[1].name = "MeshNode";
    scene.nodes[1].parentIndex = 0;
    scene.nodes[1].childIndices = {};
    scene.nodes[1].meshIndices = {0};
    scene.nodes[1].localTransform = matrixWithTranslation(0.0f, 2.0f, 0.0f);

    scene.stats.nodeCount = static_cast<std::uint32_t>(scene.nodes.size());
    scene.stats.meshCount = static_cast<std::uint32_t>(scene.meshes.size());
    scene.stats.vertexCount = 3;
    scene.stats.indexCount = 3;

    return scene;
}

[[nodiscard]] glm::vec3 translationOf(const glm::mat4 &matrix)
{
    return glm::vec3{matrix[3][0], matrix[3][1], matrix[3][2]};
}

[[nodiscard]] std::vector<flecs::entity> collectDescendants(const flecs::world &world, flecs::entity_t parentId)
{
    auto descendants = std::vector<flecs::entity>{};

    auto recurse = [&](auto &&self, flecs::entity_t currentParent) -> void {
        auto iterator = ecs_children(world.c_ptr(), currentParent);
        while (ecs_children_next(&iterator))
        {
            auto const indices = std::views::iota(0, iterator.count);
            std::ranges::for_each(indices, [&](int index) {
                auto child = flecs::entity{world.c_ptr(), iterator.entities[index]};
                descendants.push_back(child);
                self(self, child.id());
            });
        }
    };

    recurse(recurse, parentId);
    return descendants;
}

[[nodiscard]] std::optional<flecs::entity> findNodeBySourceIndex(const flecs::world &world,
                                                                  flecs::entity_t rootId,
                                                                  std::uint32_t sourceNodeIndex)
{
    auto descendants = collectDescendants(world, rootId);
    auto found = std::ranges::find_if(descendants, [&](flecs::entity entity) {
        auto nodeRef = entity.try_get<nr::scene::SceneTemplateNodeRef>();
        return nodeRef != nullptr && nodeRef->sourceNodeIndex == sourceNodeIndex;
    });

    if (found == descendants.end())
    {
        return std::nullopt;
    }

    return *found;
}

[[nodiscard]] std::optional<flecs::entity> findFirstMeshBinding(const flecs::world &world, flecs::entity_t rootId)
{
    auto descendants = collectDescendants(world, rootId);
    auto found = std::ranges::find_if(descendants, [](flecs::entity entity) {
        return entity.try_get<nr::scene::RenderableBinding>() != nullptr;
    });

    if (found == descendants.end())
    {
        return std::nullopt;
    }

    return *found;
}

[[nodiscard]] bool checkStaticCompileContracts()
{
    std::println("\n=== Case: checkStaticCompileContracts ===");
    return true;
}

[[nodiscard]] bool checkHierarchyTransformAndBounds()
{
    std::println("\n=== Case: checkHierarchyTransformAndBounds ===");

    auto sceneAsset = buildPhase3SceneAsset();

    nr::rhi::Device device{};
    nr::scene::Scene scene(nr::scene::SceneCreateInfo{.device = device});

    auto templateHandle = scene.registerTemplate(sceneAsset);
    if (!require(templateHandle.valid(), "Template handle should be valid for phase3 scene asset."))
    {
        return false;
    }

    auto rootTransform = glm::mat4{1.0f};
    rootTransform[3][0] = 10.0f;

    auto instanceHandle = scene.instantiate(templateHandle, nr::scene::SceneInstantiateInfo{
                                                                .rootTransform = rootTransform,
                                                                .activate = true,
                                                            });

    if (!require(instanceHandle.valid(), "Instance handle should be valid for phase3 scene asset."))
    {
        return false;
    }

    auto instanceRecord = scene.tryGetInstance(instanceHandle);
    if (!require(instanceRecord.has_value(), "Instance record should exist after instantiate."))
    {
        return false;
    }

    scene.updateSimulation({.deltaSeconds = 1.0f / 60.0f});

    auto runtimeRoot = instanceRecord->get().root;
    auto runtimeRootWorld = runtimeRoot.try_get<nr::scene::WorldTransform>();
    if (!require(runtimeRootWorld != nullptr, "Runtime root should have WorldTransform after updateSimulation."))
    {
        return false;
    }

    if (!require(almostEqualVec3(translationOf(runtimeRootWorld->value), glm::vec3{10.0f, 0.0f, 0.0f}),
                 "Runtime root world translation mismatch."))
    {
        return false;
    }

    auto importedRootEntity = findNodeBySourceIndex(scene.ecs(), runtimeRoot.id(), 0);
    if (!require(importedRootEntity.has_value(), "Imported root node entity should be found in instance hierarchy."))
    {
        return false;
    }

    auto importedRootWorld = importedRootEntity->try_get<nr::scene::WorldTransform>();
    if (!require(importedRootWorld != nullptr, "Imported root node should have WorldTransform after updateSimulation."))
    {
        return false;
    }

    if (!require(almostEqualVec3(translationOf(importedRootWorld->value), glm::vec3{11.0f, 0.0f, 0.0f}),
                 "Imported root world translation mismatch."))
    {
        return false;
    }

    auto meshBindingEntity = findFirstMeshBinding(scene.ecs(), runtimeRoot.id());
    if (!require(meshBindingEntity.has_value(), "Mesh-binding entity should be found in instance hierarchy."))
    {
        return false;
    }

    auto renderableBinding = meshBindingEntity->try_get<nr::scene::RenderableBinding>();
    if (!require(renderableBinding != nullptr, "Mesh-binding entity should expose RenderableBinding."))
    {
        return false;
    }
    if (!require(renderableBinding->mesh.valid(), "RenderableBinding.mesh should be valid."))
    {
        return false;
    }

    auto meshWorld = meshBindingEntity->try_get<nr::scene::WorldTransform>();
    if (!require(meshWorld != nullptr, "Mesh-binding entity should have WorldTransform."))
    {
        return false;
    }

    if (!require(almostEqualVec3(translationOf(meshWorld->value), glm::vec3{11.0f, 2.0f, 0.0f}),
                 "Mesh-binding world translation mismatch."))
    {
        return false;
    }

    auto meshWorldBounds = meshBindingEntity->try_get<nr::scene::WorldBounds>();
    if (!require(meshWorldBounds != nullptr, "Mesh-binding entity should have WorldBounds."))
    {
        return false;
    }

    if (!require(meshWorldBounds->value.valid(), "Mesh-binding WorldBounds should be valid after updateSimulation."))
    {
        return false;
    }

    auto const expectedMin = glm::vec3{11.0f, 2.0f, 0.0f};
    auto const expectedMax = glm::vec3{12.0f, 3.0f, 0.0f};

    if (!require(almostEqualVec3(meshWorldBounds->value.min, expectedMin), "Mesh-binding world bounds min mismatch."))
    {
        return false;
    }

    if (!require(almostEqualVec3(meshWorldBounds->value.max, expectedMax), "Mesh-binding world bounds max mismatch."))
    {
        return false;
    }

    auto rootWorldBounds = runtimeRoot.try_get<nr::scene::WorldBounds>();
    if (!require(rootWorldBounds != nullptr, "Runtime root should have WorldBounds."))
    {
        return false;
    }

    if (!require(rootWorldBounds->value.valid(), "Runtime root WorldBounds should be valid."))
    {
        return false;
    }

    if (!require(rootWorldBounds->value.min.x <= meshWorldBounds->value.min.x &&
                     rootWorldBounds->value.min.y <= meshWorldBounds->value.min.y &&
                     rootWorldBounds->value.min.z <= meshWorldBounds->value.min.z,
                 "Runtime root world bounds min should contain mesh bounds."))
    {
        return false;
    }

    if (!require(rootWorldBounds->value.max.x >= meshWorldBounds->value.max.x &&
                     rootWorldBounds->value.max.y >= meshWorldBounds->value.max.y &&
                     rootWorldBounds->value.max.z >= meshWorldBounds->value.max.z,
                 "Runtime root world bounds max should contain mesh bounds."))
    {
        return false;
    }

    auto staticTagPresent = meshBindingEntity->has<nr::scene::StaticObject>();
    if (!require(staticTagPresent, "Mesh-binding entity should be tagged as StaticObject."))
    {
        return false;
    }

    return true;
}

} // namespace

int main()
{
    auto const cases = std::array{
        std::pair{"checkStaticCompileContracts", &checkStaticCompileContracts},
        std::pair{"checkHierarchyTransformAndBounds", &checkHierarchyTransformAndBounds},
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
