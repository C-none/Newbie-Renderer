import std;
import nr.load;
import nr.resource;
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
    nr::test::require(
        std::ranges::any_of(scene.meshes, [](const nr::load::MeshAsset &mesh) { return !mesh.indices.empty(); }),
        std::format("{} should import indexed geometry", label));
    nr::test::require(
        std::ranges::all_of(scene.meshes, [](const nr::load::MeshAsset &mesh) { return !mesh.clockwiseFrontFace; }),
        std::format("{} glTF meshes should import as counter-clockwise front-face geometry", label));

    auto referencedTextures = referencedTextureIndices(scene);
    auto allReferencedDecoded = std::ranges::all_of(referencedTextures, [&](std::uint32_t textureIndex) {
        return scene.textures[textureIndex].decodedImage.has_value();
    });
    nr::test::require(allReferencedDecoded, std::format("{} referenced textures should be decoded", label));
}

[[nodiscard]] nr::load::SceneAsset loadSceneCase(std::string_view label,
                                                 const std::filesystem::path &relativeSourcePath)
{
    auto request = nr::load::SceneLoadRequest{};
    request.sourcePath = projectRoot() / relativeSourcePath;

    auto importResult = nr::load::loadScene(request);
    nr::test::require(importResult.has_value(), std::format("{} should import successfully", label));
    requireSceneImportValid(label, importResult.value());
    return std::move(importResult).value();
}

void runLoadCase(std::string_view label, const std::filesystem::path &relativeSourcePath)
{
    static_cast<void>(loadSceneCase(label, relativeSourcePath));
}

[[nodiscard]] const nr::load::MaterialAsset &requireMaterial(const nr::load::SceneAsset &scene, std::string_view name)
{
    auto material = std::ranges::find(scene.materials, name, &nr::load::MaterialAsset::name);
    nr::test::require(material != scene.materials.end(), std::format("scene should contain material '{}'", name));
    return *material;
}

[[nodiscard]] const nr::load::MaterialTextureBinding &requireTextureBinding(
    const nr::load::MaterialAsset &material, nr::resource::MaterialTextureSlotSemantic semantic)
{
    auto binding = std::ranges::find(material.textures, semantic, &nr::load::MaterialTextureBinding::semantic);
    nr::test::require(binding != material.textures.end(),
                      std::format("material '{}' should contain {} texture metadata", material.name,
                                  nr::resource::materialTextureSlotSemanticName(semantic)));
    return *binding;
}

[[nodiscard]] const nr::load::MeshAsset &requireMesh(const nr::load::SceneAsset &scene, std::string_view name)
{
    auto mesh = std::ranges::find(scene.meshes, name, &nr::load::MeshAsset::name);
    nr::test::require(mesh != scene.meshes.end(), std::format("scene should contain mesh '{}'", name));
    return *mesh;
}

[[nodiscard]] bool almostEqual(float lhs, float rhs, float tolerance = 1e-5f) noexcept
{
    return std::abs(lhs - rhs) <= tolerance;
}

[[nodiscard]] bool tangentNear(const nr::load::VertexAsset &vertex, const std::array<float, 3> &expectedDirection,
                               float expectedSign, float tolerance = 1e-4f) noexcept
{
    return almostEqual(vertex.tangent[0], expectedDirection[0], tolerance) &&
           almostEqual(vertex.tangent[1], expectedDirection[1], tolerance) &&
           almostEqual(vertex.tangent[2], expectedDirection[2], tolerance) &&
           almostEqual(vertex.tangent[3], expectedSign, tolerance);
}

void requireGeneratedTransformedTangentMesh(const nr::load::MeshAsset &mesh, std::string_view label)
{
    nr::test::requireEqual(mesh.indices.size(), std::size_t{6},
                           std::format("{} should retain two indexed triangles", label));
    nr::test::requireEqual(
        mesh.vertices.size(), std::size_t{6},
        std::format("{} should split the two shared source vertices whose MikkTSpace corner frames differ", label));
    nr::test::require(mesh.indices[0] != mesh.indices[3] && mesh.indices[2] != mesh.indices[4],
                      std::format("{} should not merge incompatible indexed-corner tangent frames", label));

    auto firstFaceCorners = std::views::iota(std::size_t{0}, std::size_t{3});
    nr::test::require(
        std::ranges::all_of(firstFaceCorners,
                            [&](std::size_t corner) {
                                return tangentNear(mesh.vertices[mesh.indices[corner]],
                                                   std::array<float, 3>{0.0f, 1.0f, 0.0f}, -1.0f);
                            }),
        std::format("{} should generate +Y/-1 tangents from UV1 after the swap/reflection KHR transform", label));

    constexpr auto secondFaceDirection = std::array<float, 3>{0.89442719f, 0.44721360f, 0.0f};
    auto secondFaceCorners = std::views::iota(std::size_t{3}, std::size_t{6});
    nr::test::require(std::ranges::all_of(secondFaceCorners,
                                          [&](std::size_t corner) {
                                              return tangentNear(mesh.vertices[mesh.indices[corner]],
                                                                 secondFaceDirection, 1.0f);
                                          }),
                      std::format("{} should retain the second face's distinct +1 MikkTSpace handedness", label));
}

void requireNormalTextureTangentCase()
{
    auto scene = loadSceneCase("normal texture effective-UV tangent generation",
                               std::filesystem::path{"test/fixtures/load/gltf_normal_uv1_transform_tangents.gltf"});

    auto const &baseAndClearcoat = requireMaterial(scene, "BaseAndClearcoatNormals");
    auto const &baseNormal = requireTextureBinding(baseAndClearcoat, nr::resource::MaterialTextureSlotSemantic::normal);
    auto const &competingClearcoatNormal =
        requireTextureBinding(baseAndClearcoat, nr::resource::MaterialTextureSlotSemantic::clearcoatNormal);
    nr::test::requireEqual(baseNormal.uvChannel, std::uint32_t{1}, "base normal transform should select TEXCOORD_1");
    nr::test::require(
        almostEqual(baseNormal.transform.linear.x, 0.0f) && almostEqual(baseNormal.transform.linear.y, 1.0f) &&
            almostEqual(baseNormal.transform.linear.z, 1.0f) && almostEqual(baseNormal.transform.linear.w, 0.0f),
        "base normal should retain the swap/reflection KHR texture transform");
    nr::test::requireEqual(competingClearcoatNormal.uvChannel, std::uint32_t{0},
                           "the competing clearcoat normal should retain its distinct UV0 mapping");
    requireGeneratedTransformedTangentMesh(requireMesh(scene, "GeneratedBaseNormalMesh"), "base-normal-priority mesh");

    auto const &clearcoatOnly = requireMaterial(scene, "ClearcoatNormalOnly");
    auto const &clearcoatNormal =
        requireTextureBinding(clearcoatOnly, nr::resource::MaterialTextureSlotSemantic::clearcoatNormal);
    nr::test::requireEqual(clearcoatNormal.uvChannel, std::uint32_t{1},
                           "clearcoat-only normal transform should select TEXCOORD_1");
    requireGeneratedTransformedTangentMesh(requireMesh(scene, "GeneratedClearcoatNormalMesh"),
                                           "clearcoat-normal fallback mesh");

    auto const &authored = requireMesh(scene, "AuthoredTangentMesh");
    nr::test::requireEqual(authored.vertices.size(), std::size_t{4},
                           "authored glTF tangents should not trigger MikkTSpace vertex splitting");
    nr::test::require(authored.indices[0] == authored.indices[3] && authored.indices[2] == authored.indices[4],
                      "authored glTF tangent topology should remain indexed and shared");
    nr::test::require(std::ranges::all_of(authored.vertices,
                                          [](const nr::load::VertexAsset &vertex) {
                                              return tangentNear(vertex, std::array<float, 3>{1.0f, 0.0f, 0.0f}, 1.0f);
                                          }),
                      "authored glTF tangent direction and handedness should be preserved instead of regenerated");
}

void requireTextureTransformCase()
{
    auto scene =
        loadSceneCase("KHR_texture_transform texCoord override",
                      std::filesystem::path{"test/fixtures/load/khr_texture_transform_texcoord_override.gltf"});
    auto const &material = requireMaterial(scene, "ExtensionTexCoordOverride");

    auto const &baseColor = requireTextureBinding(material, nr::resource::MaterialTextureSlotSemantic::baseColor);
    nr::test::requireEqual(baseColor.uvChannel, std::uint32_t{1},
                           "KHR_texture_transform texCoord should override the textureInfo texCoord");
    nr::test::require(
        almostEqual(baseColor.transform.linear.x, 0.0f) && almostEqual(baseColor.transform.linear.y, -0.5f) &&
            almostEqual(baseColor.transform.linear.z, 2.0f) && almostEqual(baseColor.transform.linear.w, 0.0f) &&
            almostEqual(baseColor.transform.offset.x, 0.25f) && almostEqual(baseColor.transform.offset.y, 0.5f),
        "base-color KHR_texture_transform should retain canonical glTF image-space affine values");

    auto const &occlusion = requireTextureBinding(material, nr::resource::MaterialTextureSlotSemantic::occlusion);
    nr::test::requireEqual(occlusion.uvChannel, std::uint32_t{1},
                           "occlusion metadata should retain the KHR_texture_transform UV set override");
    nr::test::require(
        almostEqual(occlusion.transform.linear.x, 1.0f) && almostEqual(occlusion.transform.linear.y, 0.0f) &&
            almostEqual(occlusion.transform.linear.z, 0.0f) && almostEqual(occlusion.transform.linear.w, 1.0f) &&
            almostEqual(occlusion.transform.offset.x, 0.125f) && almostEqual(occlusion.transform.offset.y, 0.25f),
        "occlusion metadata should transport its UV transform without implying shader behavior");

    nr::test::require(
        std::ranges::any_of(scene.meshes,
                            [](const nr::load::MeshAsset &mesh) {
                                return std::ranges::any_of(mesh.vertices, [](const nr::load::VertexAsset &vertex) {
                                    return almostEqual(vertex.texCoord1[0], 0.25f) &&
                                           almostEqual(vertex.texCoord1[1], 0.75f);
                                });
                            }),
        "glTF TEXCOORD_1 should be ingested with the same restored image-space V orientation as TEXCOORD_0");
}

void requireMeshGpuInstancingCase(std::string_view label, const std::filesystem::path &relativeSourcePath)
{
    constexpr std::size_t expectedInstanceCount = 125;
    constexpr float transformTolerance = 0.0001f;

    auto scene = loadSceneCase(label, relativeSourcePath);
    auto instanceNodes =
        scene.nodes | std::views::filter([](const nr::load::NodeAsset &node) { return !node.meshIndices.empty(); });

    nr::test::requireEqual(scene.meshes.size(), std::size_t{1},
                           std::format("{} should retain one shared source mesh", label));
    nr::test::requireEqual(static_cast<std::size_t>(std::ranges::distance(instanceNodes)), expectedInstanceCount,
                           std::format("{} should expand all EXT_mesh_gpu_instancing instances", label));

    const auto &firstInstance = *std::ranges::begin(instanceNodes);
    nr::test::require(firstInstance.parentIndex != nr::load::invalidIndex &&
                          firstInstance.parentIndex < scene.nodes.size(),
                      std::format("{} instances should have a valid meshless anchor", label));

    const auto anchorIndex = firstInstance.parentIndex;
    const auto sharedMeshIndex = firstInstance.meshIndices.front();
    nr::test::requireEqual(sharedMeshIndex, std::uint32_t{0},
                           std::format("{} instances should reference the sole imported mesh", label));
    nr::test::require(std::ranges::all_of(instanceNodes,
                                          [&](const nr::load::NodeAsset &node) {
                                              return node.parentIndex == anchorIndex && node.meshIndices.size() == 1 &&
                                                     node.meshIndices.front() == sharedMeshIndex;
                                          }),
                      std::format("{} should preserve one shared mesh across all instance nodes", label));

    const auto &anchor = scene.nodes[anchorIndex];
    nr::test::require(anchor.meshIndices.empty(),
                      std::format("{} anchor should not emit an extra base-mesh instance", label));
    nr::test::requireEqual(anchor.childIndices.size(), expectedInstanceCount,
                           std::format("{} anchor should own every expanded instance", label));

    const auto hasTranslation = [&](float expected) {
        return std::ranges::any_of(instanceNodes, [&](const nr::load::NodeAsset &node) {
            return std::abs(node.localTransform[3] - expected) <= transformTolerance &&
                   std::abs(node.localTransform[7] - expected) <= transformTolerance &&
                   std::abs(node.localTransform[11] - expected) <= transformTolerance;
        });
    };
    nr::test::require(hasTranslation(0.0f), std::format("{} should decode the first instance translation", label));
    nr::test::require(hasTranslation(10.0f), std::format("{} should decode the last instance translation", label));

    nr::test::require(std::ranges::any_of(instanceNodes,
                                          [&](const nr::load::NodeAsset &node) {
                                              constexpr std::array<std::size_t, 6> offDiagonalIndices{1, 2, 4, 6, 8, 9};
                                              return std::ranges::any_of(offDiagonalIndices, [&](std::size_t index) {
                                                  return std::abs(node.localTransform[index]) > transformTolerance;
                                              });
                                          }),
                      std::format("{} should decode instance rotations", label));

    const auto basisLength = [](const nr::load::NodeAsset &node, std::size_t column) {
        return std::sqrt(node.localTransform[column] * node.localTransform[column] +
                         node.localTransform[4 + column] * node.localTransform[4 + column] +
                         node.localTransform[8 + column] * node.localTransform[8 + column]);
    };
    nr::test::require(std::ranges::any_of(instanceNodes,
                                          [&](const nr::load::NodeAsset &node) {
                                              return std::abs(basisLength(node, 0) - 2.0f) <= transformTolerance &&
                                                     std::abs(basisLength(node, 1) - 2.0f) <= transformTolerance &&
                                                     std::abs(basisLength(node, 2) - 2.0f) <= transformTolerance;
                                          }),
                      std::format("{} should decode instance scales", label));
}

const nr::test::CaseRegistrar gltfTriangleCase{
    "load smoke imports Triangle.gltf", [] {
        runLoadCase("Triangle.gltf",
                    std::filesystem::path{"assets/glTF-Sample-Assets/Models/Triangle/glTF/Triangle.gltf"});
    }};

const nr::test::CaseRegistrar glbDuckCase{
    "load smoke imports Duck.glb", [] {
        runLoadCase("Duck.glb", std::filesystem::path{"assets/glTF-Sample-Assets/Models/Duck/glTF-Binary/Duck.glb"});
    }};

const nr::test::CaseRegistrar gltfTextureTransformCase{"load smoke imports UV1 and KHR_texture_transform metadata",
                                                       [] { requireTextureTransformCase(); }};

const nr::test::CaseRegistrar gltfNormalTextureTangentCase{
    "load smoke generates MikkTSpace tangents from normal texture effective UVs",
    [] { requireNormalTextureTangentCase(); }};

const nr::test::CaseRegistrar gltfMeshGpuInstancingCase{
    "load smoke imports EXT_mesh_gpu_instancing from glTF", [] {
        requireMeshGpuInstancingCase(
            "SimpleInstancing.gltf",
            std::filesystem::path{"assets/glTF-Sample-Assets/Models/SimpleInstancing/glTF/SimpleInstancing.gltf"});
    }};

const nr::test::CaseRegistrar glbMeshGpuInstancingCase{
    "load smoke imports EXT_mesh_gpu_instancing from GLB", [] {
        requireMeshGpuInstancingCase(
            "SimpleInstancing.glb",
            std::filesystem::path{
                "assets/glTF-Sample-Assets/Models/SimpleInstancing/glTF-Binary/SimpleInstancing.glb"});
    }};
} // namespace
