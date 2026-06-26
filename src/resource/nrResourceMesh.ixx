export module nr.resource:mesh;
import dependency.math;

import std;
import :handle;
import :geometry;
import :math;

export namespace nr::resource
{
struct VertexSkinData
{
    glm::uvec4 joints{};
    glm::vec4 weights{1.0f, 0.0f, 0.0f, 0.0f};

    constexpr VertexSkinData() noexcept = default;
    ~VertexSkinData() = default;

    [[nodiscard]] bool hasInfluence(float eps = 1e-6f) const noexcept;

    void normalizeWeights(float eps = 1e-6f) noexcept;
};

struct Vertex
{
    glm::vec3 position{};
    glm::vec3 normal{0.0f, 0.0f, 1.0f};
    glm::vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
    glm::vec2 texCoord0{};
    glm::vec2 texCoord1{};
    glm::vec4 color0{1.0f};
    VertexSkinData skin{};

    constexpr Vertex() noexcept = default;
    ~Vertex() = default;

    [[nodiscard]] bool hasValidNormal(float eps = 1e-6f) const noexcept;

    [[nodiscard]] bool hasValidTangent(float eps = 1e-6f) const noexcept;

    void normalizeFrame(float eps = 1e-6f) noexcept;
};

struct MeshGeometry
{
    std::string name{};
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t vertexOffset = 0;
    MaterialHandle material{};
    Aabb localBounds{};

    MeshGeometry() = default;
    ~MeshGeometry() = default;

    [[nodiscard]] std::uint32_t triangleCount() const noexcept;

    [[nodiscard]] bool indexed() const noexcept;
};

struct Mesh
{
    std::string name{};
    std::vector<Vertex> vertices{};
    std::vector<std::uint32_t> indices{};
    std::vector<MeshGeometry> geometries{};
    Aabb localBounds{};
    BoundingSphere localSphere{};
    bool clockwiseFrontFace = false;
    bool skinned = false;

    Mesh() = default;
    ~Mesh() = default;

    [[nodiscard]] std::size_t vertexCount() const noexcept;

    [[nodiscard]] std::size_t indexCount() const noexcept;

    [[nodiscard]] std::size_t triangleCount() const noexcept;

    [[nodiscard]] bool indexed() const noexcept;

    [[nodiscard]] Triangle triangle(std::size_t triangleIndex) const;

    void rebuildLocalBounds() noexcept;

    void rebuildLocalSphere() noexcept;

    void rebuildFlatNormals(float eps = 1e-6f) noexcept;

    void rebuildVertexNormals(float eps = 1e-6f) noexcept;

    void normalizeSkinWeights(float eps = 1e-6f) noexcept;

    [[nodiscard]] bool validate() const noexcept;
};

} // namespace nr::resource
