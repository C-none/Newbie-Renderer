module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mikktspace.h>
#include <numeric>
#include <span>
#include <vector>

module dependency.assets;

namespace nr::dependency::mikktspace
{
namespace
{
struct GenerationContext
{
    std::span<const std::uint32_t> faceVertexCounts{};
    std::span<const Corner> corners{};
    std::span<Tangent> tangents{};
    std::vector<std::size_t> faceOffsets{};
};

[[nodiscard]] GenerationContext &generationContext(const SMikkTSpaceContext &context) noexcept
{
    return *static_cast<GenerationContext *>(context.m_pUserData);
}

[[nodiscard]] std::size_t cornerIndex(const GenerationContext &context, int face, int vertex) noexcept
{
    return context.faceOffsets[static_cast<std::size_t>(face)] + static_cast<std::size_t>(vertex);
}

[[nodiscard]] int getFaceCount(const SMikkTSpaceContext *context) noexcept
{
    return static_cast<int>(generationContext(*context).faceVertexCounts.size());
}

[[nodiscard]] int getFaceVertexCount(const SMikkTSpaceContext *context, int face) noexcept
{
    auto const &generation = generationContext(*context);
    return static_cast<int>(generation.faceVertexCounts[static_cast<std::size_t>(face)]);
}

void getPosition(const SMikkTSpaceContext *context, float output[], int face, int vertex) noexcept
{
    auto const &generation = generationContext(*context);
    auto const &position = generation.corners[cornerIndex(generation, face, vertex)].position;
    std::ranges::copy(position, output);
}

void getNormal(const SMikkTSpaceContext *context, float output[], int face, int vertex) noexcept
{
    auto const &generation = generationContext(*context);
    auto const &normal = generation.corners[cornerIndex(generation, face, vertex)].normal;
    std::ranges::copy(normal, output);
}

void getTexCoord(const SMikkTSpaceContext *context, float output[], int face, int vertex) noexcept
{
    auto const &generation = generationContext(*context);
    auto const &texCoord = generation.corners[cornerIndex(generation, face, vertex)].texCoord;
    std::ranges::copy(texCoord, output);
}

void setTangent(const SMikkTSpaceContext *context, const float direction[], float sign, int face, int vertex) noexcept
{
    auto &generation = generationContext(*context);
    generation.tangents[cornerIndex(generation, face, vertex)] = Tangent{
        .direction = {direction[0], direction[1], direction[2]},
        .sign = sign,
    };
}
} // namespace

[[nodiscard]] bool generateTangents(std::span<const std::uint32_t> faceVertexCounts, std::span<const Corner> corners,
                                    std::span<Tangent> tangents)
{
    constexpr auto intMax = static_cast<std::size_t>(std::numeric_limits<int>::max());
    if (faceVertexCounts.empty() || faceVertexCounts.size() > intMax || corners.size() != tangents.size() ||
        std::ranges::any_of(faceVertexCounts,
                            [](std::uint32_t count) { return count < 3u || static_cast<std::size_t>(count) > intMax; }))
    {
        return false;
    }

    auto faceOffsets = std::vector<std::size_t>(faceVertexCounts.size());
    std::exclusive_scan(faceVertexCounts.begin(), faceVertexCounts.end(), faceOffsets.begin(), std::size_t{0});
    auto const cornerCount = std::accumulate(faceVertexCounts.begin(), faceVertexCounts.end(), std::size_t{0});
    if (cornerCount != corners.size())
    {
        return false;
    }

    auto generation = GenerationContext{
        .faceVertexCounts = faceVertexCounts,
        .corners = corners,
        .tangents = tangents,
        .faceOffsets = std::move(faceOffsets),
    };

    auto interface = SMikkTSpaceInterface{};
    interface.m_getNumFaces = getFaceCount;
    interface.m_getNumVerticesOfFace = getFaceVertexCount;
    interface.m_getPosition = getPosition;
    interface.m_getNormal = getNormal;
    interface.m_getTexCoord = getTexCoord;
    interface.m_setTSpaceBasic = setTangent;

    auto context = SMikkTSpaceContext{
        .m_pInterface = &interface,
        .m_pUserData = &generation,
    };
    return genTangSpaceDefault(&context) != 0;
}
} // namespace nr::dependency::mikktspace
