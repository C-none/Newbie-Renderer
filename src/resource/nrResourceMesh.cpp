module nr.resource;
import :mesh;
import dependency.math;
import std;
import :handle;
import :geometry;
import :math;

namespace nr::resource
{
[[nodiscard]] bool VertexSkinData::hasInfluence(float eps) const noexcept
{
    return weights.x > eps || weights.y > eps || weights.z > eps || weights.w > eps;
}

void VertexSkinData::normalizeWeights(float eps) noexcept
{
    weights.x = std::max(weights.x, 0.0f);
    weights.y = std::max(weights.y, 0.0f);
    weights.z = std::max(weights.z, 0.0f);
    weights.w = std::max(weights.w, 0.0f);

    auto sum = weights.x + weights.y + weights.z + weights.w;
    if (sum <= eps)
    {
        weights = math::float4(1.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    weights = math::scale(weights, 1.0f / sum);
}

[[nodiscard]] bool Vertex::hasValidNormal(float eps) const noexcept
{
    auto length = math::length(normal);
    return math::finiteFloat(length) && length > eps;
}

[[nodiscard]] bool Vertex::hasValidTangent(float eps) const noexcept
{
    auto tangentVec = math::float3(tangent.x, tangent.y, tangent.z);
    auto length = math::length(tangentVec);
    return math::finiteFloat(length) && math::finiteFloat(tangent.w) && length > eps;
}

void Vertex::normalizeFrame(float eps) noexcept
{
    if (hasValidNormal(eps))
    {
        normal = math::normalize(normal);
    }
    else
    {
        normal = math::float3(0.0f, 0.0f, 1.0f);
    }

    auto tangentVec = math::float3(tangent.x, tangent.y, tangent.z);
    auto tangentLength = math::length(tangentVec);
    if (math::finiteFloat(tangentLength) && tangentLength > eps)
    {
        tangentVec = math::scale(tangentVec, 1.0f / tangentLength);
    }
    else
    {
        tangentVec = math::float3(1.0f, 0.0f, 0.0f);
    }

    auto handedness =
        math::finiteFloat(tangent.w) && std::abs(tangent.w) > eps ? (tangent.w > 0.0f ? 1.0f : -1.0f) : 1.0f;
    tangent = math::float4(tangentVec.x, tangentVec.y, tangentVec.z, handedness);
}

[[nodiscard]] std::uint32_t MeshGeometry::triangleCount() const noexcept
{
    return indexCount / 3u;
}

[[nodiscard]] bool MeshGeometry::indexed() const noexcept
{
    return indexCount > 0;
}

[[nodiscard]] std::size_t Mesh::vertexCount() const noexcept
{
    return vertices.size();
}

[[nodiscard]] std::size_t Mesh::indexCount() const noexcept
{
    return indices.size();
}

[[nodiscard]] std::size_t Mesh::triangleCount() const noexcept
{
    return indexed() ? indices.size() / 3u : vertices.size() / 3u;
}

[[nodiscard]] bool Mesh::indexed() const noexcept
{
    return !indices.empty();
}

[[nodiscard]] Triangle Mesh::triangle(std::size_t triangleIndex) const
{
    auto triBase = triangleIndex * 3u;
    if (indexed())
    {
        if (triBase + 2u >= indices.size())
        {
            throw std::out_of_range{"Mesh::triangle indexed triangleIndex out of range."};
        }

        auto i0 = static_cast<std::size_t>(indices[triBase + 0u]);
        auto i1 = static_cast<std::size_t>(indices[triBase + 1u]);
        auto i2 = static_cast<std::size_t>(indices[triBase + 2u]);
        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
        {
            throw std::out_of_range{"Mesh::triangle index references missing vertex."};
        }

        return Triangle{vertices[i0].position, vertices[i1].position, vertices[i2].position};
    }

    if (triBase + 2u >= vertices.size())
    {
        throw std::out_of_range{"Mesh::triangle triangleIndex out of range."};
    }

    return Triangle{vertices[triBase + 0u].position, vertices[triBase + 1u].position, vertices[triBase + 2u].position};
}

void Mesh::rebuildLocalBounds() noexcept
{
    localBounds = Aabb{};
    std::ranges::for_each(vertices, [&](const Vertex &vertex) { localBounds.expand(vertex.position); });
}

void Mesh::rebuildLocalSphere() noexcept
{
    if (vertices.empty())
    {
        localSphere = BoundingSphere{};
        return;
    }

    if (!localBounds.valid())
    {
        rebuildLocalBounds();
    }

    auto center = localBounds.center();
    auto maxDistanceSquared = std::ranges::fold_left(vertices, 0.0f, [&](float current, const Vertex &vertex) {
        auto delta = math::subtract(vertex.position, center);
        return std::max(current, math::dot(delta, delta));
    });

    localSphere.center = center;
    localSphere.radius = std::sqrt(std::max(maxDistanceSquared, 0.0f));
}

void Mesh::rebuildFlatNormals(float eps) noexcept
{
    if (vertices.empty())
    {
        return;
    }

    if (indexed())
    {
        auto triIndices = std::views::iota(std::size_t{0}, indices.size() / 3u);
        std::ranges::for_each(triIndices, [&](std::size_t tri) {
            auto base = tri * 3u;
            auto i0 = static_cast<std::size_t>(indices[base + 0u]);
            auto i1 = static_cast<std::size_t>(indices[base + 1u]);
            auto i2 = static_cast<std::size_t>(indices[base + 2u]);
            if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
            {
                return;
            }

            auto normal =
                Triangle{vertices[i0].position, vertices[i1].position, vertices[i2].position}.computeFaceNormal();
            if (math::length(normal) <= eps)
            {
                return;
            }

            vertices[i0].normal = normal;
            vertices[i1].normal = normal;
            vertices[i2].normal = normal;
        });
        return;
    }

    auto triIndices = std::views::iota(std::size_t{0}, vertices.size() / 3u);
    std::ranges::for_each(triIndices, [&](std::size_t tri) {
        auto base = tri * 3u;
        auto normal = Triangle{vertices[base + 0u].position, vertices[base + 1u].position, vertices[base + 2u].position}
                          .computeFaceNormal();
        if (math::length(normal) <= eps)
        {
            return;
        }

        vertices[base + 0u].normal = normal;
        vertices[base + 1u].normal = normal;
        vertices[base + 2u].normal = normal;
    });
}

void Mesh::rebuildVertexNormals(float eps) noexcept
{
    std::ranges::for_each(vertices, [](Vertex &vertex) { vertex.normal = math::float3(); });

    if (indexed())
    {
        auto triIndices = std::views::iota(std::size_t{0}, indices.size() / 3u);
        std::ranges::for_each(triIndices, [&](std::size_t tri) {
            auto base = tri * 3u;
            auto i0 = static_cast<std::size_t>(indices[base + 0u]);
            auto i1 = static_cast<std::size_t>(indices[base + 1u]);
            auto i2 = static_cast<std::size_t>(indices[base + 2u]);
            if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
            {
                return;
            }

            auto face = math::cross(math::subtract(vertices[i1].position, vertices[i0].position),
                                    math::subtract(vertices[i2].position, vertices[i0].position));

            vertices[i0].normal = math::add(vertices[i0].normal, face);
            vertices[i1].normal = math::add(vertices[i1].normal, face);
            vertices[i2].normal = math::add(vertices[i2].normal, face);
        });
    }
    else
    {
        auto triIndices = std::views::iota(std::size_t{0}, vertices.size() / 3u);
        std::ranges::for_each(triIndices, [&](std::size_t tri) {
            auto base = tri * 3u;
            auto face = math::cross(math::subtract(vertices[base + 1u].position, vertices[base + 0u].position),
                                    math::subtract(vertices[base + 2u].position, vertices[base + 0u].position));

            vertices[base + 0u].normal = math::add(vertices[base + 0u].normal, face);
            vertices[base + 1u].normal = math::add(vertices[base + 1u].normal, face);
            vertices[base + 2u].normal = math::add(vertices[base + 2u].normal, face);
        });
    }

    std::ranges::for_each(vertices, [&](Vertex &vertex) {
        auto length = math::length(vertex.normal);
        if (length > eps)
        {
            vertex.normal = math::scale(vertex.normal, 1.0f / length);
        }
        else
        {
            vertex.normal = math::float3(0.0f, 0.0f, 1.0f);
        }
    });
}

void Mesh::normalizeSkinWeights(float eps) noexcept
{
    std::ranges::for_each(vertices, [&](Vertex &vertex) { vertex.skin.normalizeWeights(eps); });
}

[[nodiscard]] bool Mesh::validate() const noexcept
{
    constexpr float eps = 1e-6f;

    if (vertices.empty())
    {
        return false;
    }

    auto verticesValid = std::ranges::all_of(vertices, [&](const Vertex &vertex) {
        auto weightSum = vertex.skin.weights.x + vertex.skin.weights.y + vertex.skin.weights.z + vertex.skin.weights.w;
        auto nonNegativeWeights = vertex.skin.weights.x >= 0.0f && vertex.skin.weights.y >= 0.0f &&
                                  vertex.skin.weights.z >= 0.0f && vertex.skin.weights.w >= 0.0f;

        return math::finiteVec(vertex.position) && math::finiteVec(vertex.normal) && math::finiteVec(vertex.tangent) &&
               math::finiteVec(vertex.texCoord0) && math::finiteVec(vertex.texCoord1) &&
               math::finiteVec(vertex.color0) && vertex.hasValidNormal(eps) && math::finiteFloat(weightSum) &&
               nonNegativeWeights && weightSum > eps;
    });
    if (!verticesValid)
    {
        return false;
    }

    if (indexed())
    {
        if (indices.empty())
        {
            return false;
        }

        if (indices.size() % 3u != 0u)
        {
            return false;
        }

        auto indicesValid = std::ranges::all_of(
            indices, [&](std::uint32_t index) { return static_cast<std::size_t>(index) < vertices.size(); });
        if (!indicesValid)
        {
            return false;
        }
    }
    else if (vertices.size() % 3u != 0u)
    {
        return false;
    }

    if (!localBounds.valid())
    {
        return false;
    }

    if (!math::finiteVec(localSphere.center) || !math::finiteFloat(localSphere.radius) || localSphere.radius < 0.0f)
    {
        return false;
    }

    if (geometries.empty())
    {
        return false;
    }

    auto geometriesValid = std::ranges::all_of(geometries, [&](const MeshGeometry &geometry) {
        auto begin = static_cast<std::uint64_t>(geometry.firstIndex);
        auto count = static_cast<std::uint64_t>(geometry.indexCount);
        auto end = begin + count;

        if (!geometry.material.valid())
        {
            return false;
        }

        if (!geometry.localBounds.valid())
        {
            return false;
        }

        if (indexed())
        {
            if ((count % 3u != 0u) || (end > static_cast<std::uint64_t>(indices.size())))
            {
                return false;
            }

            if ((count > 0u) && (static_cast<std::size_t>(geometry.vertexOffset) >= vertices.size()))
            {
                return false;
            }

            auto beginIndex = static_cast<std::size_t>(geometry.firstIndex);
            auto countIndex = static_cast<std::size_t>(geometry.indexCount);
            auto referencedVerticesValid = std::ranges::all_of(
                std::ranges::subrange(indices.begin() + static_cast<std::ptrdiff_t>(beginIndex),
                                      indices.begin() + static_cast<std::ptrdiff_t>(beginIndex + countIndex)),
                [&](std::uint32_t localIndex) {
                    auto resolved =
                        static_cast<std::uint64_t>(localIndex) + static_cast<std::uint64_t>(geometry.vertexOffset);
                    return resolved < static_cast<std::uint64_t>(vertices.size());
                });

            return referencedVerticesValid;
        }

        return (count % 3u == 0u) && (end <= static_cast<std::uint64_t>(vertices.size()));
    });

    return geometriesValid;
}
} // namespace nr::resource
