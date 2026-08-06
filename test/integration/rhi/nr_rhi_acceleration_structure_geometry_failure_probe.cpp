import std;
import dependency.vulkan;
import nr.rhi;
import nr.utils;

namespace
{
inline constexpr auto vertexStride = vk::DeviceSize{12u};
inline constexpr auto geometryBufferUsage = vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
                                            vk::BufferUsageFlagBits::eShaderDeviceAddress;

[[nodiscard]] nr::rhi::Buffer createGeometryBuffer(nr::rhi::Device &device, vk::DeviceSize byteSize,
                                                   std::string_view debugName)
{
    auto buffer = device.resourceFactory.createBuffer(nr::rhi::makeBufferCreateInfo(byteSize, geometryBufferUsage),
                                                      nr::rhi::MemoryUsage::CpuToGpu, debugName);
    nr::nrAssert(buffer.valid(), std::format("{} should be a valid geometry buffer.", debugName));
    return buffer;
}

[[nodiscard]] nr::rhi::BlasGeometryRecord makeIndexedRecord(const nr::rhi::Buffer &vertexBuffer,
                                                            const nr::rhi::Buffer &indexBuffer, vk::DeviceSize stride,
                                                            std::uint32_t maxVertex, std::uint32_t firstVertex,
                                                            std::uint32_t primitiveOffset)
{
    return nr::rhi::makeBlasTriangleGeometryRecord(
        nr::rhi::BlasTriangleGeometryBuffers{
            .vertex = std::cref(vertexBuffer),
            .index = std::cref(indexBuffer),
        },
        nr::rhi::BlasGeometryLayout{
            .vertexStride = stride,
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
                                                               vk::DeviceSize stride, std::uint32_t maxVertex,
                                                               std::uint32_t firstVertex, std::uint32_t primitiveOffset)
{
    return nr::rhi::makeBlasTriangleGeometryRecord(
        nr::rhi::BlasTriangleGeometryBuffers{
            .vertex = std::cref(vertexBuffer),
        },
        nr::rhi::BlasGeometryLayout{
            .vertexStride = stride,
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

void queryRecords(nr::rhi::Device &device, std::span<const nr::rhi::BlasGeometryRecord> records)
{
    static_cast<void>(nr::rhi::queryBlasBuildSizes(device.device, records));
}
} // namespace

int main(int argc, char **argv)
{
    nr::nrAssert(argc == 2, "nr_rhi_acceleration_structure_geometry_failure_probe requires one scenario argument.");
    auto const scenario = std::string_view{argv[1]};
    auto device = nr::rhi::Device{};
    device.initialize("nr_rhi_acceleration_structure_geometry_failure_probe", "NewbieRenderer");

    auto vertices = createGeometryBuffer(device, 6u * vertexStride, "as_range_failure_vertices");
    auto indices = createGeometryBuffer(device, 3u * sizeof(std::uint32_t), "as_range_failure_indices");
    auto validIndexed = makeIndexedRecord(vertices, indices, vertexStride, 2u, 0u, 0u);

    if (scenario == "primitive-offset-after-indexed" || scenario == "primitive-offset-before-indexed")
    {
        auto invalidNonIndexed = makeNonIndexedRecord(vertices, vertexStride, 2u, 0u, 4u * vertexStride);
        auto indexedFirst = std::array{validIndexed, invalidNonIndexed};
        auto nonIndexedFirst = std::array{invalidNonIndexed, validIndexed};
        queryRecords(device, scenario == "primitive-offset-after-indexed"
                                 ? std::span<const nr::rhi::BlasGeometryRecord>{indexedFirst}
                                 : std::span<const nr::rhi::BlasGeometryRecord>{nonIndexedFirst});
        return 0;
    }

    if (scenario == "primitive-offset-plus-first-vertex")
    {
        auto invalid = makeNonIndexedRecord(vertices, vertexStride, 3u, 1u, 3u * vertexStride);
        queryRecords(device, std::span{&invalid, 1u});
        return 0;
    }

    if (scenario == "nonindexed-max-vertex")
    {
        auto invalid = makeNonIndexedRecord(vertices, vertexStride, 2u, 1u, 0u);
        queryRecords(device, std::span{&invalid, 1u});
        return 0;
    }

    if (scenario == "indexed-first-vertex")
    {
        auto invalid = makeIndexedRecord(vertices, indices, vertexStride, 1u, 2u, 0u);
        queryRecords(device, std::span{&invalid, 1u});
        return 0;
    }

    if (scenario == "vertex-offset-multiply-overflow")
    {
        auto invalid = makeIndexedRecord(vertices, indices, std::numeric_limits<vk::DeviceSize>::max(), 2u, 2u, 0u);
        queryRecords(device, std::span{&invalid, 1u});
        return 0;
    }

    if (scenario == "vertex-offset-add-overflow")
    {
        auto invalid = makeNonIndexedRecord(vertices, std::numeric_limits<vk::DeviceSize>::max(), 3u, 1u, 1u);
        queryRecords(device, std::span{&invalid, 1u});
        return 0;
    }

    if (scenario == "vertex-size-overflow")
    {
        auto invalid = makeNonIndexedRecord(vertices, std::numeric_limits<vk::DeviceSize>::max(), 2u, 0u, 0u);
        queryRecords(device, std::span{&invalid, 1u});
        return 0;
    }

    if (scenario == "indexed-vertex-range")
    {
        auto invalid = makeIndexedRecord(vertices, indices, vertexStride, 6u, 0u, 0u);
        queryRecords(device, std::span{&invalid, 1u});
        return 0;
    }

    if (scenario == "indexed-index-range")
    {
        auto invalid = makeIndexedRecord(vertices, indices, vertexStride, 2u, 0u, sizeof(std::uint32_t));
        queryRecords(device, std::span{&invalid, 1u});
        return 0;
    }

    nr::nrAssert(false, "nr_rhi_acceleration_structure_geometry_failure_probe received an unknown scenario.");
}
