import std;
import nr.load;

namespace
{
[[nodiscard]] std::filesystem::path projectRoot()
{
    return std::filesystem::path{NR_PROJECT_ROOT_DIR};
}

[[nodiscard]] std::vector<std::uint32_t> collectReferencedTextureIndices(const nr::load::SceneAsset &scene)
{
    auto referenced = std::set<std::uint32_t>{};

    std::ranges::for_each(scene.materials, [&](const nr::load::MaterialAsset &material) {
        std::ranges::for_each(material.textures, [&](const nr::load::MaterialTextureBinding &binding) {
            if (binding.textureIndex < scene.textures.size())
            {
                referenced.emplace(binding.textureIndex);
            }
        });
    });

    auto ordered = std::vector<std::uint32_t>{referenced.begin(), referenced.end()};
    return ordered;
}

[[nodiscard]] bool validateNodeHierarchy(std::string_view label, const nr::load::SceneAsset &scene)
{
    if (scene.nodes.empty())
    {
        std::println("[error] {}: scene has no nodes.", label);
        return false;
    }

    if (scene.rootNodeIndex >= scene.nodes.size())
    {
        std::println(
            "[error] {}: invalid root node index {} (nodeCount={}).",
            label,
            scene.rootNodeIndex,
            scene.nodes.size());
        return false;
    }

    if (scene.nodes[scene.rootNodeIndex].parentIndex != nr::load::invalidIndex)
    {
        std::println("[error] {}: root node parent index must be invalidIndex.", label);
        return false;
    }

    auto hierarchyValid = true;
    auto nodeIndices = std::views::iota(std::size_t{0}, scene.nodes.size());
    std::ranges::for_each(nodeIndices, [&](std::size_t nodeIndex) {
        auto const &node = scene.nodes[nodeIndex];

        auto uniqueChildren = std::set<std::uint32_t>{};

        std::ranges::for_each(node.childIndices, [&](std::uint32_t childIndex) {
            if (childIndex >= scene.nodes.size())
            {
                std::println(
                    "[error] {}: node {} references invalid child index {}.",
                    label,
                    nodeIndex,
                    childIndex);
                hierarchyValid = false;
                return;
            }

            if (!uniqueChildren.emplace(childIndex).second)
            {
                std::println(
                    "[error] {}: node {} has duplicated child index {}.",
                    label,
                    nodeIndex,
                    childIndex);
                hierarchyValid = false;
            }

            if (scene.nodes[childIndex].parentIndex != static_cast<std::uint32_t>(nodeIndex))
            {
                std::println(
                    "[error] {}: child node {} parent mismatch (expected {}, actual {}).",
                    label,
                    childIndex,
                    nodeIndex,
                    scene.nodes[childIndex].parentIndex);
                hierarchyValid = false;
            }
        });

        std::ranges::for_each(node.meshIndices, [&](std::uint32_t meshIndex) {
            if (meshIndex >= scene.meshes.size())
            {
                std::println(
                    "[error] {}: node {} references invalid mesh index {}.",
                    label,
                    nodeIndex,
                    meshIndex);
                hierarchyValid = false;
            }
        });
    });

    if (!hierarchyValid)
    {
        return false;
    }

    auto visited = std::vector<bool>(scene.nodes.size(), false);
    auto stack = std::vector<std::uint32_t>{scene.rootNodeIndex};

    while (!stack.empty())
    {
        auto current = stack.back();
        stack.pop_back();

        if (current >= scene.nodes.size() || visited[current])
        {
            continue;
        }

        visited[current] = true;
        auto const &children = scene.nodes[current].childIndices;
        std::ranges::copy(children, std::back_inserter(stack));
    }

    auto reachableCount = std::ranges::count(visited, true);
    if (reachableCount != static_cast<std::ptrdiff_t>(scene.nodes.size()))
    {
        std::println(
            "[error] {}: scene graph disconnected (reachableNodes={}, totalNodes={}).",
            label,
            reachableCount,
            scene.nodes.size());
        return false;
    }

    return true;
}

[[nodiscard]] bool validateMeshes(std::string_view label, const nr::load::SceneAsset &scene)
{
    if (scene.meshes.empty())
    {
        std::println("[error] {}: scene has no meshes.", label);
        return false;
    }

    auto meshValid = true;
    auto hasIndexedMesh = false;

    auto meshIndices = std::views::iota(std::size_t{0}, scene.meshes.size());
    std::ranges::for_each(meshIndices, [&](std::size_t meshIndex) {
        auto const &mesh = scene.meshes[meshIndex];

        if (mesh.vertices.empty())
        {
            std::println("[error] {}: mesh {} has no vertices.", label, meshIndex);
            meshValid = false;
        }

        if (mesh.indices.empty())
        {
            std::println("[error] {}: mesh {} has no indices.", label, meshIndex);
            meshValid = false;
        }
        else
        {
            hasIndexedMesh = true;
        }

        if (mesh.materialIndex != nr::load::invalidIndex && mesh.materialIndex >= scene.materials.size())
        {
            std::println(
                "[error] {}: mesh {} has invalid material index {}.",
                label,
                meshIndex,
                mesh.materialIndex);
            meshValid = false;
        }

        auto outOfRangeIndex = std::ranges::find_if(mesh.indices, [&](std::uint32_t vertexIndex) {
            return vertexIndex >= mesh.vertices.size();
        });
        if (outOfRangeIndex != mesh.indices.end())
        {
            std::println(
                "[error] {}: mesh {} index {} out of range for vertex count {}.",
                label,
                meshIndex,
                *outOfRangeIndex,
                mesh.vertices.size());
            meshValid = false;
        }
    });

    if (!hasIndexedMesh)
    {
        std::println("[error] {}: all meshes are non-indexed.", label);
        return false;
    }

    return meshValid;
}

[[nodiscard]] bool validateMaterialsAndTextures(std::string_view label,
                                                const nr::load::SceneAsset &scene,
                                                bool expectReferencedTextures)
{
    auto materialValid = true;

    auto materialIndices = std::views::iota(std::size_t{0}, scene.materials.size());
    std::ranges::for_each(materialIndices, [&](std::size_t materialIndex) {
        auto const &material = scene.materials[materialIndex];
        std::ranges::for_each(material.textures, [&](const nr::load::MaterialTextureBinding &binding) {
            if (binding.textureIndex >= scene.textures.size())
            {
                std::println(
                    "[error] {}: material {} ('{}') references invalid texture index {}.",
                    label,
                    materialIndex,
                    material.name,
                    binding.textureIndex);
                materialValid = false;
            }
        });
    });

    if (!materialValid)
    {
        return false;
    }

    auto referencedTextureIndices = collectReferencedTextureIndices(scene);
    if (expectReferencedTextures && referencedTextureIndices.empty())
    {
        std::println("[error] {}: expected at least one referenced texture.", label);
        return false;
    }

    auto missingDecodedCount = std::ranges::count_if(referencedTextureIndices, [&](std::uint32_t textureIndex) {
        return !scene.textures[textureIndex].decodedImage.has_value();
    });
    if (missingDecodedCount != 0)
    {
        std::println(
            "[error] {}: {} referenced texture(s) are missing decoded image data.",
            label,
            missingDecodedCount);
        return false;
    }

    return true;
}

[[nodiscard]] bool validateStatistics(std::string_view label, const nr::load::SceneAsset &scene)
{
    auto vertexCount = std::transform_reduce(
        scene.meshes.begin(),
        scene.meshes.end(),
        std::uint64_t{0},
        std::plus<>{},
        [](const nr::load::MeshAsset &mesh) {
            return static_cast<std::uint64_t>(mesh.vertices.size());
        });

    auto indexCount = std::transform_reduce(
        scene.meshes.begin(),
        scene.meshes.end(),
        std::uint64_t{0},
        std::plus<>{},
        [](const nr::load::MeshAsset &mesh) {
            return static_cast<std::uint64_t>(mesh.indices.size());
        });

    auto countsMatch = true;

    if (scene.stats.nodeCount != scene.nodes.size())
    {
        std::println(
            "[error] {}: stats.nodeCount={} but nodes.size()={}",
            label,
            scene.stats.nodeCount,
            scene.nodes.size());
        countsMatch = false;
    }

    if (scene.stats.meshCount != scene.meshes.size())
    {
        std::println(
            "[error] {}: stats.meshCount={} but meshes.size()={}",
            label,
            scene.stats.meshCount,
            scene.meshes.size());
        countsMatch = false;
    }

    if (scene.stats.materialCount != scene.materials.size())
    {
        std::println(
            "[error] {}: stats.materialCount={} but materials.size()={}",
            label,
            scene.stats.materialCount,
            scene.materials.size());
        countsMatch = false;
    }

    if (scene.stats.textureCount != scene.textures.size())
    {
        std::println(
            "[error] {}: stats.textureCount={} but textures.size()={}",
            label,
            scene.stats.textureCount,
            scene.textures.size());
        countsMatch = false;
    }

    if (scene.stats.vertexCount != vertexCount)
    {
        std::println(
            "[error] {}: stats.vertexCount={} but accumulated vertex count={}",
            label,
            scene.stats.vertexCount,
            vertexCount);
        countsMatch = false;
    }

    if (scene.stats.indexCount != indexCount)
    {
        std::println(
            "[error] {}: stats.indexCount={} but accumulated index count={}",
            label,
            scene.stats.indexCount,
            indexCount);
        countsMatch = false;
    }

    return countsMatch;
}

[[nodiscard]] bool runModelLoadCase()
{
    auto const sourcePath = projectRoot() /
                            std::filesystem::path{"assets/glTF-Sample-Assets/Models/DamagedHelmet/glTF/DamagedHelmet.gltf"};

    nr::load::SceneLoadRequest request{};
    request.sourcePath = sourcePath;

    auto importResult = nr::load::loadScene(request);
    if (!importResult.has_value())
    {
        auto const &error = importResult.error();
        std::println(
            "[error] DamagedHelmet import failed: backend='{}', code={}, path='{}', message='{}'",
            error.backend,
            static_cast<unsigned>(error.code),
            error.sourcePath.generic_string(),
            error.message);
        return false;
    }

    auto const &scene = importResult.value();

    auto structureOk = validateNodeHierarchy("DamagedHelmet", scene);
    auto meshOk = validateMeshes("DamagedHelmet", scene);
    auto materialOk = validateMaterialsAndTextures("DamagedHelmet", scene, true);
    auto statsOk = validateStatistics("DamagedHelmet", scene);

    if (!(structureOk && meshOk && materialOk && statsOk))
    {
        return false;
    }

    std::println(
        "[ok] DamagedHelmet loaded: nodes={}, meshes={}, materials={}, textures={}, vertices={}, indices={}",
        scene.stats.nodeCount,
        scene.stats.meshCount,
        scene.stats.materialCount,
        scene.stats.textureCount,
        scene.stats.vertexCount,
        scene.stats.indexCount);
    return true;
}

} // namespace

int main()
{
    if (!runModelLoadCase())
    {
        std::println("[FAIL] nr_load_model_asset_test failed.");
        return 1;
    }

    std::println("[OK] nr_load_model_asset_test passed.");
    return 0;
}
