import std;
import dependency;
import nr.load;
import nr.rhi;
import nr.scene;

namespace
{
[[nodiscard]] std::filesystem::path projectRoot()
{
    return std::filesystem::path{NR_PROJECT_ROOT_DIR};
}

[[nodiscard]] bool require(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::println("[fail] {}", message);
        return false;
    }
    return true;
}

[[nodiscard]] bool almostEqual(float lhs, float rhs, float eps = 1e-5f)
{
    return std::abs(lhs - rhs) <= eps;
}

[[nodiscard]] bool almostEqualVec3(const glm::vec3 &lhs, const glm::vec3 &rhs, float eps = 1e-5f)
{
    auto delta = glm::abs(lhs - rhs);
    return delta.x <= eps && delta.y <= eps && delta.z <= eps;
}

[[nodiscard]] bool almostEqualTransform(const std::array<float, 16> &lhs, const std::array<float, 16> &rhs, float eps = 1e-5f)
{
    auto indices = std::views::iota(std::size_t{0}, lhs.size());
    return std::ranges::all_of(indices, [&](std::size_t index) { return almostEqual(lhs[index], rhs[index], eps); });
}

[[nodiscard]] auto loadSceneFromRelative(const std::filesystem::path &relativePath) -> std::expected<nr::load::SceneAsset, std::string>
{
    auto absolutePath = projectRoot() / relativePath;
    std::println("[load] source='{}'", absolutePath.generic_string());

    nr::load::SceneLoadRequest request{};
    request.sourcePath = absolutePath;

    auto importResult = nr::load::loadScene(request);
    if (!importResult.has_value())
    {
        auto const &error = importResult.error();
        auto message = std::format("backend='{}' code={} path='{}' message='{}'", error.backend, static_cast<unsigned>(error.code), error.sourcePath.generic_string(), error.message);
        return std::unexpected(message);
    }

    return std::move(importResult.value());
}

[[nodiscard]] std::array<float, 16> identityTransform()
{
    return {
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
    };
}

[[nodiscard]] nr::load::SceneAsset buildDuplicateNameSceneAsset()
{
    auto scene = nr::load::SceneAsset{};
    scene.sourcePath = std::filesystem::path{"manual_phase2_duplicate_names.gltf"};

    auto image = nr::load::Image{};
    image.width = 2;
    image.height = 2;
    image.channels = 4;
    image.pixels = {
        255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255,
    };

    auto texture = nr::load::TextureAsset{};
    texture.key = "manual://textures/baseColor";
    texture.resolvedPath = std::filesystem::path{"manual_textures/baseColor.png"};
    texture.decodedImage = image;
    scene.textures.push_back(std::move(texture));

    auto material = nr::load::MaterialAsset{};
    material.name = "manual_material";
    material.textures.push_back(nr::load::MaterialTextureBinding{
        .textureIndex = 0,
        .uvChannel = 0,
        .textureTypeRaw = 0,
        .semantic = "diffuse",
    });
    scene.materials.push_back(std::move(material));

    auto makeTriangleMesh = [](std::string name, std::uint32_t materialIndex) {
        auto mesh = nr::load::MeshAsset{};
        mesh.name = std::move(name);
        mesh.materialIndex = materialIndex;
        mesh.vertices = {
            nr::load::VertexAsset{.position = {0.0f, 0.0f, 0.0f}},
            nr::load::VertexAsset{.position = {1.0f, 0.0f, 0.0f}},
            nr::load::VertexAsset{.position = {0.0f, 1.0f, 0.0f}},
        };
        mesh.indices = {0, 1, 2};
        return mesh;
    };

    scene.meshes.push_back(makeTriangleMesh("mesh_a", 0));
    scene.meshes.push_back(makeTriangleMesh("mesh_b", 0));

    scene.nodes.resize(4);
    scene.rootNodeIndex = 0;

    scene.nodes[0].name = "Root";
    scene.nodes[0].parentIndex = nr::load::invalidIndex;
    scene.nodes[0].childIndices = {1, 2, 3};
    scene.nodes[0].localTransform = identityTransform();

    scene.nodes[1].name = "Part";
    scene.nodes[1].parentIndex = 0;
    scene.nodes[1].meshIndices = {0};
    scene.nodes[1].localTransform = identityTransform();

    scene.nodes[2].name = "Part";
    scene.nodes[2].parentIndex = 0;
    scene.nodes[2].meshIndices = {1};
    scene.nodes[2].localTransform = identityTransform();

    scene.nodes[3].name = "Part";
    scene.nodes[3].parentIndex = 0;
    scene.nodes[3].localTransform = identityTransform();

    scene.stats.nodeCount = static_cast<std::uint32_t>(scene.nodes.size());
    scene.stats.meshCount = static_cast<std::uint32_t>(scene.meshes.size());
    scene.stats.materialCount = static_cast<std::uint32_t>(scene.materials.size());
    scene.stats.textureCount = static_cast<std::uint32_t>(scene.textures.size());
    scene.stats.vertexCount = 6;
    scene.stats.indexCount = 6;

    return scene;
}

[[nodiscard]] std::size_t countDescendants(const flecs::world &world, flecs::entity_t parentId)
{
    auto collectDirectChildren = [&](flecs::entity_t nodeId) {
        auto children = std::vector<flecs::entity_t>{};
        auto iterator = ecs_children(world.c_ptr(), nodeId);
        while (ecs_children_next(&iterator))
        {
            auto indices = std::views::iota(0, iterator.count);
            std::ranges::for_each(indices, [&](int index) { children.push_back(iterator.entities[index]); });
        }
        return children;
    };

    auto recurse = [&](auto &&self, flecs::entity_t parentId) -> std::size_t {
        auto const directChildren = collectDirectChildren(parentId);
        auto total = directChildren.size();
        std::ranges::for_each(directChildren, [&](flecs::entity_t childId) { total += self(self, childId); });
        return total;
    };

    return recurse(recurse, parentId);
}

[[nodiscard]] bool checkStaticBridgeAndValidation()
{
    std::println("\n=== Case: checkStaticBridgeAndValidation ===");
    auto importedScene = loadSceneFromRelative(std::filesystem::path{"assets/glTF-Sample-Assets/Models/DamagedHelmet/glTF/DamagedHelmet.gltf"});
    if (!importedScene.has_value())
    {
        std::println("[fail] loadScene failed: {}", importedScene.error());
        return false;
    }

    auto const &sceneAsset = importedScene.value();
    nr::rhi::Device device{};
    nr::scene::Scene scene(nr::scene::SceneCreateInfo{.device = device});

    auto templateHandle = scene.registerTemplate(sceneAsset);

    if (!require(templateHandle.valid(), "Template handle must be valid for DamagedHelmet."))
    {
        return false;
    }

    auto plan = nr::scene::SceneBridge::buildPlan(sceneAsset);

    auto textureChecksOk = true;
    std::ranges::for_each(plan.textures, [&](const nr::scene::TextureBridgeInput &entry) {
        auto textureHandle = scene.findTextureHandleByStableKey(entry.canonicalKey);
        if (!textureHandle.has_value())
        {
            textureChecksOk = false;
            std::println("[fail] missing texture handle for key='{}'", entry.canonicalKey);
            return;
        }

        auto textureRecord = scene.tryGetTextureAsset(*textureHandle);
        if (!textureRecord.has_value())
        {
            textureChecksOk = false;
            std::println("[fail] missing texture record for key='{}'", entry.canonicalKey);
            return;
        }

        if (!textureRecord->get().cpuReady || !textureRecord->get().cpu.valid())
        {
            textureChecksOk = false;
            std::println("[fail] invalid canonical texture for key='{}'", entry.canonicalKey);
        }
    });
    if (!require(textureChecksOk, "All bridged textures should exist and pass resource::Texture::valid()."))
    {
        return false;
    }

    auto materialChecksOk = true;
    std::ranges::for_each(plan.materials, [&](const nr::scene::MaterialBridgeInput &entry) {
        auto materialHandle = scene.findMaterialHandleByStableKey(entry.canonicalKey);
        if (!materialHandle.has_value())
        {
            materialChecksOk = false;
            std::println("[fail] missing material handle for key='{}'", entry.canonicalKey);
            return;
        }

        auto materialRecord = scene.tryGetMaterialAsset(*materialHandle);
        if (!materialRecord.has_value())
        {
            materialChecksOk = false;
            std::println("[fail] missing material record for key='{}'", entry.canonicalKey);
            return;
        }

        if (!materialRecord->get().cpuReady)
        {
            materialChecksOk = false;
            std::println("[fail] canonical material not ready for key='{}'", entry.canonicalKey);
            return;
        }

        auto const &material = materialRecord->get().cpu;
        auto slotHandles = std::array{
            material.baseColor.texture, material.normal.texture, material.metallicRoughness.texture, material.occlusion.texture, material.emissive.texture,
        };
        std::ranges::for_each(slotHandles, [&](auto slotHandle) {
            if (!slotHandle.valid())
            {
                return;
            }

            if (!scene.tryGetTextureAsset(slotHandle).has_value())
            {
                materialChecksOk = false;
                std::println("[fail] material key='{}' references unknown texture handle (slot={}, gen={})", entry.canonicalKey, slotHandle.slot, slotHandle.generation);
            }
        });
    });
    if (!require(materialChecksOk, "All canonical materials should reference valid texture handles."))
    {
        return false;
    }

    auto meshChecksOk = true;
    std::ranges::for_each(plan.meshes, [&](const nr::scene::MeshBridgeInput &entry) {
        auto meshHandle = scene.findMeshHandleByStableKey(entry.canonicalKey);
        if (!meshHandle.has_value())
        {
            meshChecksOk = false;
            std::println("[fail] missing mesh handle for key='{}'", entry.canonicalKey);
            return;
        }

        auto meshRecord = scene.tryGetMeshAsset(*meshHandle);
        if (!meshRecord.has_value())
        {
            meshChecksOk = false;
            std::println("[fail] missing mesh record for key='{}'", entry.canonicalKey);
            return;
        }

        auto const &mesh = meshRecord->get().cpu;
        if (!meshRecord->get().cpuReady || !mesh.validate())
        {
            meshChecksOk = false;
            std::println("[fail] canonical mesh invalid for key='{}'", entry.canonicalKey);
            return;
        }

        if (mesh.submeshes.size() != 1)
        {
            meshChecksOk = false;
            std::println("[fail] mesh key='{}' expected one submesh but got {}", entry.canonicalKey, mesh.submeshes.size());
            return;
        }

        auto const expectedIndexCount = static_cast<std::uint32_t>(mesh.indices.empty() ? mesh.vertices.size() : mesh.indices.size());
        if (mesh.submeshes.front().indexCount != expectedIndexCount)
        {
            meshChecksOk = false;
            std::println("[fail] mesh key='{}' expected submesh indexCount={} got {}", entry.canonicalKey, expectedIndexCount, mesh.submeshes.front().indexCount);
        }
    });
    if (!require(meshChecksOk, "All canonical meshes should pass validation and one-submesh rule."))
    {
        return false;
    }

    auto templateRecord = scene.tryGetTemplate(templateHandle);
    if (!require(templateRecord.has_value(), "Template record should exist after registration."))
    {
        return false;
    }
    if (!require(templateRecord->get().templateNodeCount > 0, "Template node count should be populated."))
    {
        return false;
    }

    return true;
}

[[nodiscard]] bool checkDeterministicSiblingNamingAndTemplateTree()
{
    std::println("\n=== Case: checkDeterministicSiblingNamingAndTemplateTree ===");
    auto sceneAsset = buildDuplicateNameSceneAsset();

    nr::rhi::Device device{};
    nr::scene::Scene scene(nr::scene::SceneCreateInfo{.device = device});
    auto templateHandle = scene.registerTemplate(sceneAsset);

    if (!require(templateHandle.valid(), "Template should register for manual duplicate-name scene asset."))
    {
        return false;
    }

    auto templateRecord = scene.tryGetTemplate(templateHandle);
    if (!require(templateRecord.has_value(), "Template record should exist for duplicate-name scene."))
    {
        return false;
    }

    if (!require(templateRecord->get().templateNodeCount == sceneAsset.nodes.size(), "Template node count mismatch for duplicate-name scene."))
    {
        return false;
    }
    if (!require(templateRecord->get().templateMeshBindingCount == 2, "Template mesh binding count should match node mesh references."))
    {
        return false;
    }

    auto sceneRootEntity = flecs::entity{};
    auto rootChildrenIterator = ecs_children(scene.ecs().c_ptr(), templateRecord->get().prefabRoot.id());
    while (ecs_children_next(&rootChildrenIterator))
    {
        auto indices = std::views::iota(0, rootChildrenIterator.count);
        std::ranges::for_each(indices, [&](int index) {
            auto child = flecs::entity{scene.ecs().c_ptr(), rootChildrenIterator.entities[index]};
            auto nodeRef = child.try_get<nr::scene::SceneTemplateNodeRef>();
            if (nodeRef != nullptr && nodeRef->sourceNodeIndex == sceneAsset.rootNodeIndex)
            {
                sceneRootEntity = child;
            }
        });
    }

    if (!require(sceneRootEntity.is_alive(), "Template tree should contain imported root node entity."))
    {
        return false;
    }

    auto resolvedNames = std::vector<std::string>{};
    auto sceneRootChildrenIterator = ecs_children(scene.ecs().c_ptr(), sceneRootEntity.id());
    while (ecs_children_next(&sceneRootChildrenIterator))
    {
        auto indices = std::views::iota(0, sceneRootChildrenIterator.count);
        std::ranges::for_each(indices, [&](int index) {
            auto child = flecs::entity{scene.ecs().c_ptr(), sceneRootChildrenIterator.entities[index]};
            auto nodeRef = child.try_get<nr::scene::SceneTemplateNodeRef>();
            if (nodeRef != nullptr)
            {
                resolvedNames.push_back(nodeRef->resolvedName);
            }
        });
    }

    std::ranges::sort(resolvedNames);
    auto expectedNames = std::vector<std::string>{"Part", "Part_1", "Part_2"};
    std::ranges::sort(expectedNames);

    if (!require(resolvedNames == expectedNames, "Duplicate sibling names should be deterministically suffixed."))
    {
        std::println("[debug] resolved names:");
        std::ranges::for_each(resolvedNames, [](const std::string &name) { std::println("  - {}", name); });
        return false;
    }

    return true;
}

[[nodiscard]] bool checkInstantiateRuntimeRootAndHierarchy()
{
    std::println("\n=== Case: checkInstantiateRuntimeRootAndHierarchy ===");
    auto sceneAsset = buildDuplicateNameSceneAsset();

    nr::rhi::Device device{};
    nr::scene::Scene scene(nr::scene::SceneCreateInfo{.device = device});
    auto templateHandle = scene.registerTemplate(sceneAsset);
    if (!require(templateHandle.valid(), "Template should register before runtime instantiate."))
    {
        return false;
    }

    auto templateRecord = scene.tryGetTemplate(templateHandle);
    if (!require(templateRecord.has_value(), "Template record should exist before instantiate."))
    {
        return false;
    }

    auto runtimeParent = scene.ecs().entity("stage2_runtime_parent");
    auto instanceHandle = scene.instantiate(templateHandle, nr::scene::SceneInstantiateInfo{
                                                                .runtimeParent = std::cref(runtimeParent),
                                                                .activate = true,
                                                            });

    if (!require(instanceHandle.valid(), "Instance handle should be valid."))
    {
        return false;
    }

    auto instanceRecord = scene.tryGetInstance(instanceHandle);
    if (!require(instanceRecord.has_value(), "Instance record should exist after instantiate."))
    {
        return false;
    }

    if (!require(instanceRecord->get().root.is_alive(), "Instance root should be alive."))
    {
        return false;
    }
    if (!require(instanceRecord->get().root.has(EcsChildOf, runtimeParent.id()), "Instance root should be attached to runtime parent with ChildOf."))
    {
        return false;
    }

    auto const descendantCount = countDescendants(scene.ecs(), instanceRecord->get().root.id());
    auto const expectedDescendants = templateRecord->get().templateNodeCount + templateRecord->get().templateMeshBindingCount;
    std::println("[instance] descendants={} expectedDescendants={} expectedEntityCount={}", descendantCount, expectedDescendants, instanceRecord->get().expectedEntityCount);

    if (!require(descendantCount == expectedDescendants, "Instance hierarchy size should match template node + mesh-binding entities."))
    {
        return false;
    }
    if (!require(instanceRecord->get().expectedEntityCount == descendantCount + 1, "Instance expectedEntityCount should include runtime root."))
    {
        return false;
    }

    auto updatedTemplateRecord = scene.tryGetTemplate(templateHandle);
    if (!require(updatedTemplateRecord.has_value(), "Template record should still exist after instantiate."))
    {
        return false;
    }
    if (!require(updatedTemplateRecord->get().liveInstanceCount == 1, "Template liveInstanceCount should increment to 1."))
    {
        return false;
    }

    return true;
}

[[nodiscard]] bool checkTriangleExtractionAndExpectation()
{
    std::println("\n=== Case: checkTriangleExtractionAndExpectation ===");
    auto importedScene = loadSceneFromRelative(std::filesystem::path{"assets/glTF-Sample-Assets/Models/Triangle/glTF/Triangle.gltf"});
    if (!importedScene.has_value())
    {
        std::println("[fail] loadScene failed: {}", importedScene.error());
        return false;
    }

    auto const &sceneAsset = importedScene.value();
    auto const expectedNodeCount = std::size_t{1};
    auto const expectedMeshCount = std::size_t{1};
    auto const expectedVertexCount = std::size_t{3};
    auto const expectedIndexCount = std::size_t{3};
    auto const expectedTemplateNodeCount = std::size_t{1};
    auto const expectedTemplateMeshBindingCount = std::size_t{1};

    std::println("[compare] scene nodes expected={} actual={}", expectedNodeCount, sceneAsset.nodes.size());
    std::println("[compare] scene meshes expected={} actual={}", expectedMeshCount, sceneAsset.meshes.size());
    std::println("[compare] scene vertices expected={} actual={}", expectedVertexCount, sceneAsset.meshes.empty() ? 0 : sceneAsset.meshes.front().vertices.size());
    std::println("[compare] scene indices expected={} actual={}", expectedIndexCount, sceneAsset.meshes.empty() ? 0 : sceneAsset.meshes.front().indices.size());

    if (!require(sceneAsset.nodes.size() == expectedNodeCount, "Triangle should import exactly one node."))
    {
        return false;
    }
    if (!require(sceneAsset.meshes.size() == expectedMeshCount, "Triangle should import exactly one mesh."))
    {
        return false;
    }
    if (!require(sceneAsset.rootNodeIndex == 0, "Triangle root node index should be 0."))
    {
        return false;
    }

    auto const &sourceMesh = sceneAsset.meshes.front();
    if (!require(sourceMesh.vertices.size() == expectedVertexCount, "Triangle source mesh should have 3 vertices."))
    {
        return false;
    }
    if (!require(sourceMesh.indices.size() == expectedIndexCount, "Triangle source mesh should have 3 indices."))
    {
        return false;
    }

    auto const expectedPositions = std::array{
        glm::vec3{0.0f, 0.0f, 0.0f},
        glm::vec3{1.0f, 0.0f, 0.0f},
        glm::vec3{0.0f, 1.0f, 0.0f},
    };

    auto sourcePositionsOk = std::ranges::all_of(expectedPositions, [&](const glm::vec3 &expectedPosition) {
        return std::ranges::any_of(sourceMesh.vertices, [&](const nr::load::VertexAsset &vertex) {
            auto actual = glm::vec3{vertex.position[0], vertex.position[1], vertex.position[2]};
            return almostEqualVec3(actual, expectedPosition);
        });
    });
    if (!require(sourcePositionsOk, "Triangle source mesh positions differ from expected triangle vertices."))
    {
        return false;
    }

    auto bridgePlan = nr::scene::SceneBridge::buildPlan(sceneAsset);
    if (!require(bridgePlan.valid(), "Triangle bridge plan should be valid."))
    {
        return false;
    }
    if (!require(bridgePlan.meshes.size() == 1, "Triangle bridge plan should produce one mesh entry."))
    {
        return false;
    }
    if (!require(bridgePlan.textures.empty(), "Triangle bridge plan should not produce textures."))
    {
        return false;
    }
    if (!require(bridgePlan.materials.size() == sceneAsset.materials.size(), "Triangle material bridge count mismatch."))
    {
        return false;
    }

    nr::rhi::Device device{};
    nr::scene::Scene scene(nr::scene::SceneCreateInfo{.device = device});
    auto templateHandle = scene.registerTemplate(sceneAsset);
    if (!require(templateHandle.valid(), "Triangle template registration should succeed."))
    {
        return false;
    }

    auto statistics = scene.statistics();
    std::println("[compare] stats.templateCount expected=1 actual={}", statistics.templateCount);
    std::println("[compare] stats.meshAssetCount expected=1 actual={}", statistics.meshAssetCount);
    std::println("[compare] stats.materialAssetCount expected={} actual={}", sceneAsset.materials.size(), statistics.materialAssetCount);
    std::println("[compare] stats.textureAssetCount expected=0 actual={}", statistics.textureAssetCount);
    std::println("[compare] stats.templateNodeCount expected={} actual={}", expectedTemplateNodeCount, statistics.templateNodeCount);
    std::println("[compare] stats.templateMeshBindingCount expected={} actual={}", expectedTemplateMeshBindingCount, statistics.templateMeshBindingCount);

    if (!require(statistics.templateCount == 1, "Triangle registration should create one template."))
    {
        return false;
    }
    if (!require(statistics.meshAssetCount == 1, "Triangle registration should create one mesh asset."))
    {
        return false;
    }
    if (!require(statistics.materialAssetCount == sceneAsset.materials.size(), "Triangle material asset count mismatch."))
    {
        return false;
    }
    if (!require(statistics.textureAssetCount == 0, "Triangle should not create texture assets."))
    {
        return false;
    }
    if (!require(statistics.templateNodeCount == expectedTemplateNodeCount, "Triangle template node count mismatch."))
    {
        return false;
    }
    if (!require(statistics.templateMeshBindingCount == expectedTemplateMeshBindingCount, "Triangle template mesh-binding count mismatch."))
    {
        return false;
    }

    auto meshHandle = scene.findMeshHandleByStableKey(bridgePlan.meshes.front().canonicalKey);
    if (!require(meshHandle.has_value(), "Triangle mesh handle lookup should succeed."))
    {
        return false;
    }

    auto meshRecord = scene.tryGetMeshAsset(*meshHandle);
    if (!require(meshRecord.has_value(), "Triangle mesh record lookup should succeed."))
    {
        return false;
    }

    auto const &mesh = meshRecord->get().cpu;
    if (!require(meshRecord->get().cpuReady, "Triangle mesh record should be cpuReady."))
    {
        return false;
    }
    if (!require(mesh.validate(), "Triangle canonical mesh should pass validate()."))
    {
        return false;
    }
    if (!require(mesh.vertices.size() == expectedVertexCount, "Triangle canonical mesh vertex count mismatch."))
    {
        return false;
    }
    if (!require(mesh.indices.size() == expectedIndexCount, "Triangle canonical mesh index count mismatch."))
    {
        return false;
    }
    if (!require(mesh.submeshes.size() == 1, "Triangle canonical mesh should contain one submesh."))
    {
        return false;
    }

    auto canonicalPositionsOk = std::ranges::all_of(expectedPositions, [&](const glm::vec3 &expectedPosition) { return std::ranges::any_of(mesh.vertices, [&](const nr::resource::Vertex &vertex) { return almostEqualVec3(vertex.position, expectedPosition); }); });
    if (!require(canonicalPositionsOk, "Triangle canonical mesh positions differ from expected."))
    {
        return false;
    }

    auto indicesInRange = std::ranges::all_of(mesh.indices, [&](std::uint32_t index) { return static_cast<std::size_t>(index) < mesh.vertices.size(); });
    if (!require(indicesInRange, "Triangle canonical mesh indices should be in range."))
    {
        return false;
    }

    auto const expectedMin = glm::vec3{0.0f, 0.0f, 0.0f};
    auto const expectedMax = glm::vec3{1.0f, 1.0f, 0.0f};
    auto const expectedCenter = glm::vec3{0.5f, 0.5f, 0.0f};
    auto const expectedRadius = std::sqrt(0.5f);

    std::println("[compare] bounds.min expected=({}, {}, {}) actual=({}, {}, {})", expectedMin.x, expectedMin.y, expectedMin.z, mesh.localBounds.min.x, mesh.localBounds.min.y, mesh.localBounds.min.z);
    std::println("[compare] bounds.max expected=({}, {}, {}) actual=({}, {}, {})", expectedMax.x, expectedMax.y, expectedMax.z, mesh.localBounds.max.x, mesh.localBounds.max.y, mesh.localBounds.max.z);
    std::println("[compare] sphere.center expected=({}, {}, {}) actual=({}, {}, {})", expectedCenter.x, expectedCenter.y, expectedCenter.z, mesh.localSphere.center.x, mesh.localSphere.center.y, mesh.localSphere.center.z);
    std::println("[compare] sphere.radius expected={} actual={}", expectedRadius, mesh.localSphere.radius);

    if (!require(almostEqualVec3(mesh.localBounds.min, expectedMin), "Triangle canonical mesh bounds min mismatch."))
    {
        return false;
    }
    if (!require(almostEqualVec3(mesh.localBounds.max, expectedMax), "Triangle canonical mesh bounds max mismatch."))
    {
        return false;
    }
    if (!require(almostEqualVec3(mesh.localSphere.center, expectedCenter), "Triangle canonical mesh sphere center mismatch."))
    {
        return false;
    }
    if (!require(almostEqual(mesh.localSphere.radius, expectedRadius), "Triangle canonical mesh sphere radius mismatch."))
    {
        return false;
    }

    auto const &submesh = mesh.submeshes.front();
    if (!require(submesh.firstIndex == 0, "Triangle submesh.firstIndex should be 0."))
    {
        return false;
    }
    if (!require(submesh.vertexOffset == 0, "Triangle submesh.vertexOffset should be 0."))
    {
        return false;
    }
    if (!require(submesh.indexCount == 3, "Triangle submesh.indexCount should be 3."))
    {
        return false;
    }

    if (sceneAsset.materials.empty())
    {
        if (!require(!submesh.material.valid(), "Triangle submesh should not reference material when scene has none."))
        {
            return false;
        }
    }
    else
    {
        if (!require(submesh.material.valid(), "Triangle submesh should reference material when importer provides one."))
        {
            return false;
        }
        if (!require(scene.tryGetMaterialAsset(submesh.material).has_value(), "Triangle submesh material handle should resolve to material record."))
        {
            return false;
        }
    }

    auto templateRecord = scene.tryGetTemplate(templateHandle);
    if (!require(templateRecord.has_value(), "Triangle template record lookup should succeed."))
    {
        return false;
    }
    if (!require(templateRecord->get().templateNodeCount == expectedTemplateNodeCount, "Triangle template record node count mismatch."))
    {
        return false;
    }
    if (!require(templateRecord->get().templateMeshBindingCount == expectedTemplateMeshBindingCount, "Triangle template record mesh-binding count mismatch."))
    {
        return false;
    }

    auto templateNodeEntity = flecs::entity{};
    auto rootChildrenIterator = ecs_children(scene.ecs().c_ptr(), templateRecord->get().prefabRoot.id());
    while (ecs_children_next(&rootChildrenIterator))
    {
        auto indices = std::views::iota(0, rootChildrenIterator.count);
        std::ranges::for_each(indices, [&](int index) {
            auto child = flecs::entity{scene.ecs().c_ptr(), rootChildrenIterator.entities[index]};
            auto nodeRef = child.try_get<nr::scene::SceneTemplateNodeRef>();
            if (nodeRef != nullptr && nodeRef->sourceNodeIndex == sceneAsset.rootNodeIndex)
            {
                templateNodeEntity = child;
            }
        });
    }

    if (!require(templateNodeEntity.is_alive(), "Triangle template root node entity should exist."))
    {
        return false;
    }

    auto templateNodeRef = templateNodeEntity.try_get<nr::scene::SceneTemplateNodeRef>();
    if (!require(templateNodeRef != nullptr, "Triangle template node should expose SceneTemplateNodeRef."))
    {
        return false;
    }
    if (!require(templateNodeRef->sourceNodeIndex == sceneAsset.rootNodeIndex, "Triangle template node source index mismatch."))
    {
        return false;
    }

    auto templateNodeTransform = templateNodeEntity.try_get<nr::scene::SceneTemplateNodeTransform>();
    if (!require(templateNodeTransform != nullptr, "Triangle template node should expose SceneTemplateNodeTransform."))
    {
        return false;
    }
    if (!require(almostEqualTransform(templateNodeTransform->localTransform, sceneAsset.nodes[sceneAsset.rootNodeIndex].localTransform), "Triangle template node transform should match source local transform."))
    {
        return false;
    }

    auto meshBindingEntities = std::vector<flecs::entity>{};
    auto nodeChildrenIterator = ecs_children(scene.ecs().c_ptr(), templateNodeEntity.id());
    while (ecs_children_next(&nodeChildrenIterator))
    {
        auto indices = std::views::iota(0, nodeChildrenIterator.count);
        std::ranges::for_each(indices, [&](int index) {
            auto child = flecs::entity{scene.ecs().c_ptr(), nodeChildrenIterator.entities[index]};
            if (child.try_get<nr::scene::SceneTemplateMeshBindingRef>() != nullptr)
            {
                meshBindingEntities.push_back(child);
            }
        });
    }

    if (!require(meshBindingEntities.size() == 1, "Triangle template node should own one mesh-binding entity."))
    {
        return false;
    }

    auto meshBindingRef = meshBindingEntities.front().try_get<nr::scene::SceneTemplateMeshBindingRef>();
    if (!require(meshBindingRef != nullptr, "Triangle mesh-binding entity should expose SceneTemplateMeshBindingRef."))
    {
        return false;
    }
    if (!require(meshBindingRef->sourceMeshIndex == 0, "Triangle mesh-binding sourceMeshIndex should be 0."))
    {
        return false;
    }
    if (!require(meshBindingRef->mesh == *meshHandle, "Triangle mesh-binding should reference bridged mesh handle."))
    {
        return false;
    }

    auto instanceHandle = scene.instantiate(templateHandle);
    if (!require(instanceHandle.valid(), "Triangle instantiate should produce a valid instance handle."))
    {
        return false;
    }

    auto instanceRecord = scene.tryGetInstance(instanceHandle);
    if (!require(instanceRecord.has_value(), "Triangle instance record lookup should succeed."))
    {
        return false;
    }
    if (!require(instanceRecord->get().root.is_alive(), "Triangle instance root should be alive."))
    {
        return false;
    }

    auto const descendantCount = countDescendants(scene.ecs(), instanceRecord->get().root.id());
    auto const expectedDescendantCount = expectedTemplateNodeCount + expectedTemplateMeshBindingCount;
    std::println("[compare] instance descendants expected={} actual={}", expectedDescendantCount, descendantCount);
    std::println("[compare] instance expectedEntityCount expected={} actual={}", expectedDescendantCount + 1, instanceRecord->get().expectedEntityCount);

    if (!require(descendantCount == expectedDescendantCount, "Triangle instance descendant count mismatch."))
    {
        return false;
    }
    if (!require(instanceRecord->get().expectedEntityCount == expectedDescendantCount + 1, "Triangle instance expectedEntityCount mismatch."))
    {
        return false;
    }

    return true;
}

} // namespace

int main()
{
    auto const cases = std::array{
        std::pair{"checkStaticBridgeAndValidation", &checkStaticBridgeAndValidation},
        std::pair{"checkDeterministicSiblingNamingAndTemplateTree", &checkDeterministicSiblingNamingAndTemplateTree},
        std::pair{"checkInstantiateRuntimeRootAndHierarchy", &checkInstantiateRuntimeRootAndHierarchy},
        std::pair{"checkTriangleExtractionAndExpectation", &checkTriangleExtractionAndExpectation},
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
