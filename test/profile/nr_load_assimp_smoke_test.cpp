import std;
import nr.load;

namespace
{
[[nodiscard]] std::filesystem::path projectRoot()
{
    return std::filesystem::path{NR_PROJECT_ROOT_DIR};
}

[[nodiscard]] bool validateScene(std::string_view label, const nr::load::SceneAsset &scene)
{
    if (scene.nodes.empty())
    {
        std::println("[error] {}: imported scene has no nodes.", label);
        return false;
    }
    if (scene.meshes.empty())
    {
        std::println("[error] {}: imported scene has no meshes.", label);
        return false;
    }

    auto hasIndices = std::ranges::any_of(scene.meshes, [](const nr::load::MeshAsset &mesh) {
        return !mesh.indices.empty();
    });
    if (!hasIndices)
    {
        std::println("[error] {}: all imported meshes are missing indices.", label);
        return false;
    }

    auto referencedTextureIndices = std::set<std::uint32_t>{};
    std::ranges::for_each(scene.materials, [&](const nr::load::MaterialAsset &material) {
        std::ranges::for_each(material.textures, [&](const nr::load::MaterialTextureBinding &binding) {
            if (binding.textureIndex < scene.textures.size())
            {
                referencedTextureIndices.emplace(binding.textureIndex);
            }
        });
    });

    auto missingDecodedTextureKeys = std::vector<std::string>{};
    std::ranges::for_each(referencedTextureIndices, [&](std::uint32_t textureIndex) {
        auto const &texture = scene.textures[textureIndex];
        if (!texture.decodedImage.has_value())
        {
            missingDecodedTextureKeys.push_back(texture.key);
        }
    });

    if (!missingDecodedTextureKeys.empty())
    {
        auto previewCount = std::min<size_t>(missingDecodedTextureKeys.size(), 8);
        auto missingPreview = std::vector<std::string>{};
        missingPreview.reserve(previewCount);
        auto previewKeys = missingDecodedTextureKeys | std::views::take(previewCount);
        std::ranges::copy(previewKeys, std::back_inserter(missingPreview));

        auto missingPreviewText = std::string{};
        if (!missingPreview.empty())
        {
            missingPreviewText = missingPreview.front();
            auto remainder = missingPreview | std::views::drop(size_t{1});
            std::ranges::for_each(remainder, [&](const std::string &key) {
                missingPreviewText.append(", ");
                missingPreviewText.append(key);
            });
        }

        std::println(
            "[error] {}: {} referenced texture(s) are missing decoded image data. Examples: {}",
            label,
            missingDecodedTextureKeys.size(),
            missingPreviewText);
        return false;
    }

    std::println(
        "[ok] {}: nodes={}, meshes={}, materials={}, textures={}, referencedTextures={}, vertices={}, indices={}",
        label,
        scene.stats.nodeCount,
        scene.stats.meshCount,
        scene.stats.materialCount,
        scene.stats.textureCount,
        referencedTextureIndices.size(),
        scene.stats.vertexCount,
        scene.stats.indexCount);

    return true;
}

[[nodiscard]] bool runLoadCase(std::string_view label, const std::filesystem::path &relativeSourcePath)
{
    nr::load::SceneLoadRequest request{};
    request.sourcePath = projectRoot() / relativeSourcePath;

    auto importResult = nr::load::loadScene(request);
    if (!importResult.has_value())
    {
        auto const &error = importResult.error();
        std::println(
            "[error] {}: backend='{}', code={}, path='{}', message='{}'",
            label,
            error.backend,
            static_cast<unsigned>(error.code),
            error.sourcePath.generic_string(),
            error.message);
        return false;
    }

    return validateScene(label, importResult.value());
}

} // namespace

int main()
{
    auto gltfOk = runLoadCase(
        "Triangle.gltf",
        std::filesystem::path{"assets/glTF-Sample-Assets/Models/Triangle/glTF/Triangle.gltf"});

    auto glbOk = runLoadCase(
        "Duck.glb",
        std::filesystem::path{"assets/glTF-Sample-Assets/Models/Duck/glTF-Binary/Duck.glb"});

    if (!gltfOk || !glbOk)
    {
        std::println("[FAIL] nr_load_assimp_smoke_test failed.");
        return 1;
    }

    std::println("[OK] nr_load_assimp_smoke_test passed.");
    return 0;
}
