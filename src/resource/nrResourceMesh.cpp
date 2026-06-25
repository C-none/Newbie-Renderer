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
            weights = glm::vec4{1.0f, 0.0f, 0.0f, 0.0f};
            return;
        }

        weights /= sum;
    }

[[nodiscard]] bool Vertex::hasValidNormal(float eps) const noexcept
{
        auto length = glm::length(normal);
        return math::finiteFloat(length) && length > eps;
    }

[[nodiscard]] bool Vertex::hasValidTangent(float eps) const noexcept
{
        auto tangentVec = glm::vec3{tangent.x, tangent.y, tangent.z};
        auto length = glm::length(tangentVec);
        return math::finiteFloat(length) && math::finiteFloat(tangent.w) && length > eps;
    }

void Vertex::normalizeFrame(float eps) noexcept
{
        if (hasValidNormal(eps))
        {
            normal = glm::normalize(normal);
        }
        else
        {
            normal = glm::vec3{0.0f, 0.0f, 1.0f};
        }

        auto tangentVec = glm::vec3{tangent.x, tangent.y, tangent.z};
        auto tangentLength = glm::length(tangentVec);
        if (math::finiteFloat(tangentLength) && tangentLength > eps)
        {
            tangentVec /= tangentLength;
        }
        else
        {
            tangentVec = glm::vec3{1.0f, 0.0f, 0.0f};
        }

        auto handedness = math::finiteFloat(tangent.w) && std::abs(tangent.w) > eps ? (tangent.w > 0.0f ? 1.0f : -1.0f) : 1.0f;
        tangent = glm::vec4{tangentVec, handedness};
    }

[[nodiscard]] std::uint32_t Submesh::triangleCount() const noexcept
{
        return indexCount / 3u;
    }

[[nodiscard]] bool Submesh::indexed() const noexcept
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
        std::ranges::for_each(vertices, [&](const Vertex &vertex) {
            localBounds.expand(vertex.position);
        });
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
        auto maxDistanceSquared = std::ranges::fold_left(
            vertices,
            0.0f,
            [&](float current, const Vertex &vertex) {
                auto delta = vertex.position - center;
                return std::max(current, glm::dot(delta, delta));
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

                auto normal = Triangle{vertices[i0].position, vertices[i1].position, vertices[i2].position}.computeFaceNormal();
                if (glm::length(normal) <= eps)
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
            auto normal = Triangle{vertices[base + 0u].position, vertices[base + 1u].position, vertices[base + 2u].position}.computeFaceNormal();
            if (glm::length(normal) <= eps)
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
        std::ranges::for_each(vertices, [](Vertex &vertex) {
            vertex.normal = glm::vec3{0.0f};
        });

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

                auto face = glm::cross(
                    vertices[i1].position - vertices[i0].position,
                    vertices[i2].position - vertices[i0].position);

                vertices[i0].normal += face;
                vertices[i1].normal += face;
                vertices[i2].normal += face;
            });
        }
        else
        {
            auto triIndices = std::views::iota(std::size_t{0}, vertices.size() / 3u);
            std::ranges::for_each(triIndices, [&](std::size_t tri) {
                auto base = tri * 3u;
                auto face = glm::cross(
                    vertices[base + 1u].position - vertices[base + 0u].position,
                    vertices[base + 2u].position - vertices[base + 0u].position);

                vertices[base + 0u].normal += face;
                vertices[base + 1u].normal += face;
                vertices[base + 2u].normal += face;
            });
        }

        std::ranges::for_each(vertices, [&](Vertex &vertex) {
            auto length = glm::length(vertex.normal);
            if (length > eps)
            {
                vertex.normal /= length;
            }
            else
            {
                vertex.normal = glm::vec3{0.0f, 0.0f, 1.0f};
            }
        });
    }

void Mesh::normalizeSkinWeights(float eps) noexcept
{
        std::ranges::for_each(vertices, [&](Vertex &vertex) {
            vertex.skin.normalizeWeights(eps);
        });
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
            auto nonNegativeWeights = vertex.skin.weights.x >= 0.0f &&
                                      vertex.skin.weights.y >= 0.0f &&
                                      vertex.skin.weights.z >= 0.0f &&
                                      vertex.skin.weights.w >= 0.0f;

                                 return math::finiteVec(vertex.position) &&
                                         math::finiteVec(vertex.normal) &&
                                         math::finiteVec(vertex.tangent) &&
                                         math::finiteVec(vertex.texCoord0) &&
                                         math::finiteVec(vertex.texCoord1) &&
                                         math::finiteVec(vertex.color0) &&
                   vertex.hasValidNormal(eps) &&
                                     math::finiteFloat(weightSum) &&
                   nonNegativeWeights &&
                   weightSum > eps;
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

            auto indicesValid = std::ranges::all_of(indices, [&](std::uint32_t index) {
                return static_cast<std::size_t>(index) < vertices.size();
            });
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

        auto submeshesValid = std::ranges::all_of(submeshes, [&](const Submesh &submesh) {
            auto begin = static_cast<std::uint64_t>(submesh.firstIndex);
            auto count = static_cast<std::uint64_t>(submesh.indexCount);
            auto end = begin + count;

            if (!submesh.localBounds.valid())
            {
                return false;
            }

            if (indexed())
            {
                if ((count % 3u != 0u) || (end > static_cast<std::uint64_t>(indices.size())))
                {
                    return false;
                }

                if ((count > 0u) && (static_cast<std::size_t>(submesh.vertexOffset) >= vertices.size()))
                {
                    return false;
                }

                auto beginIndex = static_cast<std::size_t>(submesh.firstIndex);
                auto countIndex = static_cast<std::size_t>(submesh.indexCount);
                auto referencedVerticesValid = std::ranges::all_of(
                    std::ranges::subrange(indices.begin() + static_cast<std::ptrdiff_t>(beginIndex),
                                          indices.begin() + static_cast<std::ptrdiff_t>(beginIndex + countIndex)),
                    [&](std::uint32_t localIndex) {
                        auto resolved = static_cast<std::uint64_t>(localIndex) + static_cast<std::uint64_t>(submesh.vertexOffset);
                        return resolved < static_cast<std::uint64_t>(vertices.size());
                    });

                return referencedVerticesValid;
            }

            return (count % 3u == 0u) && (end <= static_cast<std::uint64_t>(vertices.size()));
        });

        return submeshesValid;
    }
} // namespace nr::resource
