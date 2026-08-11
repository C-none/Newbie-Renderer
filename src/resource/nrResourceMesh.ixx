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
    DirectX::XMUINT4 joints{};
    DirectX::XMFLOAT4 weights{1.0f, 0.0f, 0.0f, 0.0f};

    [[nodiscard]] bool hasInfluence(float eps = 1e-6f) const noexcept;

    void normalizeWeights(float eps = 1e-6f) noexcept;
};

struct Vertex
{
    DirectX::XMFLOAT3 position{};
    DirectX::XMFLOAT3 normal{0.0f, 0.0f, 1.0f};
    DirectX::XMFLOAT4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
    DirectX::XMFLOAT2 texCoord0{};
    DirectX::XMFLOAT2 texCoord1{};
    DirectX::XMFLOAT4 color0{1.0f, 1.0f, 1.0f, 1.0f};
    VertexSkinData skin{};

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
