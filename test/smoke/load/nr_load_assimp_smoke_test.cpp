import std;
import nr.load;
import nr.test;
import nr.utils;

namespace
{
[[nodiscard]] std::filesystem::path projectRoot()
{
    return std::filesystem::path{std::string{nr::projectRoot}};
}

[[nodiscard]] std::set<std::uint32_t> referencedTextureIndices(const nr::load::SceneAsset &scene)
{
    auto indices = std::set<std::uint32_t>{};
    std::ranges::for_each(scene.materials, [&](const nr::load::MaterialAsset &material) {
        std::ranges::for_each(material.textures, [&](const nr::load::MaterialTextureBinding &binding) {
            if (binding.textureIndex < scene.textures.size())
            {
                indices.emplace(binding.textureIndex);
            }
        });
    });
    return indices;
}

void requireSceneImportValid(std::string_view label, const nr::load::SceneAsset &scene)
{
    nr::test::require(!scene.nodes.empty(), std::format("{} should import nodes", label));
    nr::test::require(!scene.meshes.empty(), std::format("{} should import meshes", label));
    nr::test::require(std::ranges::any_of(scene.meshes, [](const nr::load::MeshAsset &mesh) {
                          return !mesh.indices.empty();
                      }),
                      std::format("{} should import indexed geometry", label));

    auto referencedTextures = referencedTextureIndices(scene);
    auto allReferencedDecoded = std::ranges::all_of(referencedTextures, [&](std::uint32_t textureIndex) {
        return scene.textures[textureIndex].decodedImage.has_value();
    });
    nr::test::require(allReferencedDecoded, std::format("{} referenced textures should be decoded", label));
}

void runLoadCase(std::string_view label, const std::filesystem::path &relativeSourcePath)
{
    auto request = nr::load::SceneLoadRequest{};
    request.sourcePath = projectRoot() / relativeSourcePath;

    auto importResult = nr::load::loadScene(request);
    nr::test::require(importResult.has_value(), std::format("{} should import successfully", label));
    requireSceneImportValid(label, importResult.value());
}

const nr::test::CaseRegistrar gltfTriangleCase{
    "load smoke imports Triangle.gltf",
    [] {
        runLoadCase(
            "Triangle.gltf",
            std::filesystem::path{"assets/glTF-Sample-Assets/Models/Triangle/glTF/Triangle.gltf"});
    }};

const nr::test::CaseRegistrar glbDuckCase{
    "load smoke imports Duck.glb",
    [] {
        runLoadCase(
            "Duck.glb",
            std::filesystem::path{"assets/glTF-Sample-Assets/Models/Duck/glTF-Binary/Duck.glb"});
    }};
} // namespace
