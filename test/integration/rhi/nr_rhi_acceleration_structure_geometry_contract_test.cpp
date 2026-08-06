import std;
import dependency.vulkan;
import nr.rhi;
import nr.test;

namespace
{
struct TestVertex
{
    std::array<float, 3> position{};
};

static_assert(sizeof(TestVertex) == 12u);

inline constexpr auto geometryBufferUsage = vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
                                            vk::BufferUsageFlagBits::eShaderDeviceAddress;

[[nodiscard]] nr::rhi::Buffer createGeometryBuffer(nr::rhi::Device &device, vk::DeviceSize byteSize,
                                                   std::string_view debugName)
{
    auto buffer = device.resourceFactory.createBuffer(nr::rhi::makeBufferCreateInfo(byteSize, geometryBufferUsage),
                                                      nr::rhi::MemoryUsage::CpuToGpu, debugName);
    nr::test::require(buffer.valid(), std::format("{} should be a valid geometry buffer", debugName));
    return buffer;
}

[[nodiscard]] nr::rhi::BlasGeometryRecord makeIndexedRecord(const nr::rhi::Buffer &vertexBuffer,
                                                            const nr::rhi::Buffer &indexBuffer, std::uint32_t maxVertex,
                                                            std::uint32_t firstVertex, std::uint32_t primitiveOffset)
{
    return nr::rhi::makeBlasTriangleGeometryRecord(
        nr::rhi::BlasTriangleGeometryBuffers{
            .vertex = std::cref(vertexBuffer),
            .index = std::cref(indexBuffer),
        },
        nr::rhi::BlasGeometryLayout{
            .vertexStride = sizeof(TestVertex),
            .indexType = vk::IndexType::eUint32,
            .maxVertex = maxVertex,
        },
        nr::rhi::BlasGeometryInput{
            .vertexAddress = vertexBuffer.deviceAddress(),
            .indexAddress = indexBuffer.deviceAddress(),
            .primitiveCount = 1u,
            .firstVertex = firstVertex,
            .primitiveOffset = primitiveOffset,
        });
}

[[nodiscard]] nr::rhi::BlasGeometryRecord makeNonIndexedRecord(const nr::rhi::Buffer &vertexBuffer,
                                                               std::uint32_t maxVertex, std::uint32_t firstVertex,
                                                               std::uint32_t primitiveOffset)
{
    return nr::rhi::makeBlasTriangleGeometryRecord(
        nr::rhi::BlasTriangleGeometryBuffers{
            .vertex = std::cref(vertexBuffer),
        },
        nr::rhi::BlasGeometryLayout{
            .vertexStride = sizeof(TestVertex),
            .indexType = vk::IndexType::eNoneKHR,
            .maxVertex = maxVertex,
        },
        nr::rhi::BlasGeometryInput{
            .vertexAddress = vertexBuffer.deviceAddress(),
            .primitiveCount = 1u,
            .firstVertex = firstVertex,
            .primitiveOffset = primitiveOffset,
        });
}

void requireBuildSizes(const nr::rhi::AsBuildSizes &sizes, std::string_view label)
{
    nr::test::require(sizes.accelerationStructureSize > 0u, std::format("{} should report non-zero AS storage", label));
    nr::test::require(sizes.buildScratchSize > 0u, std::format("{} should report non-zero scratch storage", label));
}

const nr::test::CaseRegistrar triangleGeometryRangeCase{
    "RHI triangle geometry range validation preserves indexed and non-indexed Vulkan offsets", [] {
        auto device = nr::rhi::Device{};
        device.initialize("nr_rhi_acceleration_structure_geometry_contract_test", "NewbieRenderer");

        auto indexedVertices = createGeometryBuffer(device, 4u * sizeof(TestVertex), "as_range_indexed_vertices");
        auto indexedIndices = createGeometryBuffer(device, 6u * sizeof(std::uint32_t), "as_range_indexed_indices");
        auto indexed = makeIndexedRecord(indexedVertices, indexedIndices, 3u, 1u, 3u * sizeof(std::uint32_t));
        requireBuildSizes(nr::rhi::queryBlasBuildSizes(device.device, std::span{&indexed, 1u}),
                          "exact indexed vertex/index ranges");

        auto nonIndexedVertices = createGeometryBuffer(device, 6u * sizeof(TestVertex), "as_range_nonindexed_vertices");
        auto nonIndexed =
            makeNonIndexedRecord(nonIndexedVertices, 3u, 1u, 2u * static_cast<std::uint32_t>(sizeof(TestVertex)));
        requireBuildSizes(nr::rhi::queryBlasBuildSizes(device.device, std::span{&nonIndexed, 1u}),
                          "exact non-indexed primitiveOffset plus firstVertex range");

        auto indexedFirst = std::array{indexed, nonIndexed};
        auto nonIndexedFirst = std::array{nonIndexed, indexed};
        requireBuildSizes(nr::rhi::queryBlasBuildSizes(device.device, std::span{indexedFirst}),
                          "indexed-first mixed geometry");
        requireBuildSizes(nr::rhi::queryBlasBuildSizes(device.device, std::span{nonIndexedFirst}),
                          "non-indexed-first mixed geometry");

        auto degenerateVertices = createGeometryBuffer(device, sizeof(TestVertex), "as_range_degenerate_vertices");
        auto degenerateIndices =
            createGeometryBuffer(device, 3u * sizeof(std::uint32_t), "as_range_degenerate_indices");
        auto zeroIndices = std::array<std::uint32_t, 3>{};
        degenerateIndices.writeMappedAndFlush(std::span{zeroIndices});
        auto degenerateIndexed = makeIndexedRecord(degenerateVertices, degenerateIndices, 0u, 0u, 0u);
        requireBuildSizes(nr::rhi::queryBlasBuildSizes(device.device, std::span{&degenerateIndexed, 1u}),
                          "indexed maxVertex zero geometry");
        requireBuildSizes(nr::rhi::queryBlasBuildSizes(device.device,
                                                       nr::rhi::BlasGeometryLayout{
                                                           .vertexStride = sizeof(TestVertex),
                                                           .indexType = vk::IndexType::eUint32,
                                                           .maxVertex = 0u,
                                                       },
                                                       1u),
                          "single-layout indexed maxVertex zero geometry");

        device.waitIdle();
    }};
} // namespace
