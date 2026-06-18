import std;
import dependency;
import nr.resource;
import nr.test;

namespace
{
[[nodiscard]] bool near(float lhs, float rhs, float eps = 1e-4f) noexcept
{
    return std::abs(lhs - rhs) <= eps;
}

[[nodiscard]] bool vec3Near(const glm::vec3 &lhs, const glm::vec3 &rhs, float eps = 1e-4f) noexcept
{
    return near(lhs.x, rhs.x, eps) && near(lhs.y, rhs.y, eps) && near(lhs.z, rhs.z, eps);
}

[[nodiscard]] nr::resource::Vertex vertex(glm::vec3 position)
{
    auto result = nr::resource::Vertex{};
    result.position = position;
    return result;
}

[[nodiscard]] nr::resource::Mesh triangleMesh()
{
    auto mesh = nr::resource::Mesh{};
    mesh.vertices = {
        vertex(glm::vec3{-1.0f, -1.0f, 0.0f}),
        vertex(glm::vec3{1.0f, -1.0f, 0.0f}),
        vertex(glm::vec3{0.0f, 1.0f, 0.0f}),
    };
    mesh.indices = {0u, 1u, 2u};
    mesh.rebuildLocalBounds();
    mesh.rebuildLocalSphere();
    mesh.rebuildVertexNormals();
    auto submesh = nr::resource::Submesh{};
    submesh.name = "triangle";
    submesh.indexCount = 3;
    submesh.localBounds = mesh.localBounds;
    mesh.submeshes.push_back(std::move(submesh));
    return mesh;
}

const nr::test::CaseRegistrar handleIdentityCase{
    "resource handles pack slot and generation",
    [] {
        auto empty = nr::resource::MeshHandle{};
        nr::test::require(!empty.valid(), "default handle should be invalid");

        auto handle = nr::resource::MeshHandle{7u, 3u};
        nr::test::require(handle.valid(), "explicit handle should be valid");
        nr::test::requireEqual(handle.packed(), (std::uint64_t{3u} << 32u) | std::uint64_t{7u});

        auto otherTag = nr::resource::MaterialHandle{7u, 3u};
        nr::test::requireEqual(otherTag.packed(), handle.packed(), "typed handles should share the packed identity contract");
    }};

const nr::test::CaseRegistrar geometryCase{
    "geometry helpers validate bounds and triangles",
    [] {
        auto bounds = nr::resource::Aabb{};
        nr::test::require(!bounds.valid(), "default AABB should be invalid until expanded");
        bounds.expand(glm::vec3{-1.0f, 2.0f, 0.0f});
        bounds.expand(glm::vec3{3.0f, 4.0f, 2.0f});
        nr::test::require(bounds.valid(), "expanded AABB should be valid");
        nr::test::require(vec3Near(bounds.center(), glm::vec3{1.0f, 3.0f, 1.0f}), "AABB center mismatch");
        nr::test::require(vec3Near(bounds.extent(), glm::vec3{4.0f, 2.0f, 2.0f}), "AABB extent mismatch");

        auto rhs = nr::resource::Aabb{glm::vec3{-2.0f, 0.0f, -1.0f}, glm::vec3{2.0f, 1.0f, 1.0f}};
        bounds.merge(rhs);
        nr::test::require(vec3Near(bounds.min, glm::vec3{-2.0f, 0.0f, -1.0f}), "AABB merge min mismatch");
        nr::test::require(vec3Near(bounds.max, glm::vec3{3.0f, 4.0f, 2.0f}), "AABB merge max mismatch");

        auto tri = nr::resource::Triangle{
            glm::vec3{0.0f, 0.0f, 0.0f},
            glm::vec3{1.0f, 0.0f, 0.0f},
            glm::vec3{0.0f, 1.0f, 0.0f},
        };
        nr::test::require(near(tri.computeArea(), 0.5f), "triangle area mismatch");
        nr::test::require(vec3Near(tri.computeFaceNormal(), glm::vec3{0.0f, 0.0f, 1.0f}), "triangle normal mismatch");
        nr::test::require(!tri.isDegenerate(), "non-zero triangle should not be degenerate");
    }};

const nr::test::CaseRegistrar meshCase{
    "mesh rebuild and validation contracts",
    [] {
        auto mesh = triangleMesh();
        nr::test::require(mesh.indexed(), "triangle mesh should be indexed");
        nr::test::requireEqual(mesh.vertexCount(), std::size_t{3});
        nr::test::requireEqual(mesh.indexCount(), std::size_t{3});
        nr::test::requireEqual(mesh.triangleCount(), std::size_t{1});
        nr::test::require(mesh.validate(), "rebuilt triangle mesh should validate");
        nr::test::require(vec3Near(mesh.localBounds.min, glm::vec3{-1.0f, -1.0f, 0.0f}), "mesh bounds min mismatch");
        nr::test::require(vec3Near(mesh.localBounds.max, glm::vec3{1.0f, 1.0f, 0.0f}), "mesh bounds max mismatch");
        nr::test::require(mesh.localSphere.valid(), "mesh sphere should be valid");
        nr::test::require(vec3Near(mesh.triangle(0).centroid(), glm::vec3{0.0f, -1.0f / 3.0f, 0.0f}), "mesh triangle centroid mismatch");

        mesh.vertices.front().skin.weights = glm::vec4{-1.0f, 0.0f, 3.0f, 0.0f};
        mesh.normalizeSkinWeights();
        nr::test::require(near(mesh.vertices.front().skin.weights.z, 1.0f), "skin weights should clamp and normalize");
    }};

const nr::test::CaseRegistrar textureMaterialCase{
    "texture and material value helpers stay local",
    [] {
        auto texture = nr::resource::Texture{};
        texture.width = 8;
        texture.height = 4;
        texture.depth = 1;
        texture.mipCount = 4;
        texture.format = vk::Format::eR8G8B8A8Unorm;
        auto level = nr::resource::ImageLevel{};
        level.width = 8;
        level.height = 4;
        level.bytes = std::vector<std::byte>(8u * 4u * 4u);
        texture.levels.push_back(std::move(level));

        nr::test::require(texture.valid(), "texture metadata should validate");
        nr::test::require(texture.hasCpuPixels(), "texture should report CPU pixels");
        nr::test::requireEqual(texture.byteSize(), std::size_t{128});
        nr::test::require(texture.mipExtent(0) == glm::uvec3{8u, 4u, 1u}, "mip 0 extent mismatch");
        nr::test::require(texture.mipExtent(3) == glm::uvec3{1u, 1u, 1u}, "mip 3 extent should clamp to one texel");
        nr::test::require(texture.mipExtent(4) == glm::uvec3{0u, 0u, 0u}, "out-of-range mip should be zero extent");

        auto material = nr::resource::Material{};
        nr::test::require(material.isOpaque(), "default material should be opaque");
        nr::test::require(!material.usesMetallicRoughnessWorkflow(), "default material should not need metallic-roughness workflow");
        material.alphaMode = nr::resource::AlphaMode::blend;
        material.metallicFactor = 0.5f;
        material.specularFactor = glm::vec3{0.1f, 0.0f, 0.0f};
        material.anisotropyFactor = 0.25f;
        nr::test::require(material.isAlphaBlended(), "blend material should report alpha blending");
        nr::test::require(material.usesMetallicRoughnessWorkflow(), "metallic factor should enable metallic-roughness workflow");
        nr::test::require(material.usesSpecularGlossinessWorkflow(), "specular factor should enable specular-glossiness workflow");
        nr::test::require(material.usesAnisotropy(), "anisotropy factor should be reported");
    }};
} // namespace
