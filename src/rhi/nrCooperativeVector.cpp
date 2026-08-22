module nr.rhi;
import :cooperativeVector;
import dependency.vulkan;
import nr.utils;
import std;

namespace nr::rhi
{
namespace
{
[[nodiscard]] vk::CooperativeVectorMatrixLayoutNV toVulkanCooperativeVectorLayout(
    CooperativeVectorMatrixLayout layout) noexcept
{
    switch (layout)
    {
    case CooperativeVectorMatrixLayout::RowMajor:
        return vk::CooperativeVectorMatrixLayoutNV::eRowMajor;
    case CooperativeVectorMatrixLayout::InferencingOptimal:
        return vk::CooperativeVectorMatrixLayoutNV::eInferencingOptimal;
    case CooperativeVectorMatrixLayout::TrainingOptimal:
        return vk::CooperativeVectorMatrixLayoutNV::eTrainingOptimal;
    }
    return vk::CooperativeVectorMatrixLayoutNV::eTrainingOptimal;
}

[[nodiscard]] vk::ComponentTypeKHR toVulkanCooperativeVectorComponentType(
    CooperativeVectorComponentType componentType) noexcept
{
    return componentType == CooperativeVectorComponentType::FloatE4M3 ? vk::ComponentTypeKHR::eFloatE4M3
                                                                      : vk::ComponentTypeKHR::eFloat16;
}

[[nodiscard]] vk::DeviceSize cooperativeVectorComponentSize(CooperativeVectorComponentType componentType) noexcept
{
    return componentType == CooperativeVectorComponentType::FloatE4M3 ? vk::DeviceSize{sizeof(std::uint8_t)}
                                                                      : vk::DeviceSize{sizeof(std::uint16_t)};
}

[[nodiscard]] vk::DeviceSize checkedRowMajorMatrixSize(CooperativeVectorMatrixDesc desc)
{
    auto const elementSize = cooperativeVectorComponentSize(desc.componentType);
    nrAssert(desc.rows > 0 && desc.columns > 0,
             "Cooperative-vector matrix dimensions must both be non-zero (rows={}, columns={}).", desc.rows,
             desc.columns);
    // VUID-VkConvertCooperativeVectorMatrixInfoNV-srcLayout-10077 and
    // -dstLayout-10078 require a row-major stride large enough for one row
    // and divisible by the component size.
    nrAssert(desc.rowStrideBytes >= static_cast<vk::DeviceSize>(desc.columns) * elementSize,
             "Cooperative-vector row-major stride {} is smaller than {} columns of {} bytes.", desc.rowStrideBytes,
             desc.columns, elementSize);
    nrAssert(desc.rowStrideBytes % elementSize == 0u,
             "Cooperative-vector row-major stride {} must be a multiple of the {}-byte component size.",
             desc.rowStrideBytes, elementSize);
    auto const tightRowSize = static_cast<vk::DeviceSize>(desc.columns) * elementSize;
    // VkConvertCooperativeVectorMatrixInfoNV::srcSize/dstSize cover the final
    // row's elements, not trailing padding after that row. This mirrors the
    // standard row-major matrix layout used by VK_NV_cooperative_vector.
    auto const precedingRows = static_cast<vk::DeviceSize>(desc.rows - 1u);
    nrAssert(precedingRows <= (std::numeric_limits<vk::DeviceSize>::max() - tightRowSize) / desc.rowStrideBytes,
             "Cooperative-vector row-major matrix size overflows vk::DeviceSize.");
    return precedingRows * desc.rowStrideBytes + tightRowSize;
}

void validateCooperativeVectorMatrixDesc(CooperativeVectorMatrixDesc desc)
{
    nrAssert(desc.rows > 0 && desc.columns > 0,
             "Cooperative-vector matrix dimensions must both be non-zero (rows={}, columns={}).", desc.rows,
             desc.columns);
    if (desc.layout == CooperativeVectorMatrixLayout::RowMajor)
    {
        static_cast<void>(checkedRowMajorMatrixSize(desc));
        return;
    }
    nrAssert(desc.rowStrideBytes == 0,
             "Cooperative-vector optimal-layout matrices are opaque and must use a zero row stride.");
}

void validateCooperativeVectorMatrixLayoutSize(CooperativeVectorMatrixDesc desc,
                                               CooperativeVectorMatrixLayoutSize layoutSize,
                                               std::string_view role)
{
    nrAssert(layoutSize.byteSize > 0u, "Cooperative-vector {} matrix layout size must be non-zero.", role);
    if (desc.layout == CooperativeVectorMatrixLayout::RowMajor)
    {
        nrAssert(layoutSize.byteSize == checkedRowMajorMatrixSize(desc),
                 "Cooperative-vector {} row-major layout size {} does not match its stride-defined size {}.", role,
                 layoutSize.byteSize, checkedRowMajorMatrixSize(desc));
    }
}

void validateCooperativeVectorMatrixMemory(CooperativeVectorMatrixMemory memory,
                                           CooperativeVectorMatrixLayoutSize layoutSize, std::string_view role)
{
    nrAssert(memory.deviceAddress != 0, "Cooperative-vector {} matrix requires a non-zero device address.", role);
    nrAssert(memory.deviceAddress % kCooperativeVectorMatrixDeviceAddressAlignment == 0,
             "Cooperative-vector {} matrix device address {} must be {}-byte aligned.", role, memory.deviceAddress,
             kCooperativeVectorMatrixDeviceAddressAlignment);
    // VUID-vkCmdConvertCooperativeVectorMatrixNV-pInfo-10086/-10087 require
    // source/destination sizes to cover the respective matrix. Requiring the
    // exact queried region also prevents a caller from accidentally passing a
    // tail-of-buffer size for a packed per-layer allocation.
    nrAssert(memory.size == layoutSize.byteSize,
             "Cooperative-vector {} matrix region size {} must exactly equal its layout size {} bytes.", role,
             memory.size, layoutSize.byteSize);
}
} // namespace

[[nodiscard]] CooperativeVectorMatrixLayoutSize queryCooperativeVectorMatrixLayoutSize(
    const vk::raii::Device &device, CooperativeVectorMatrixDesc desc)
{
    validateCooperativeVectorMatrixDesc(desc);
    nrAssert(*device != nullptr, "Cooperative-vector matrix layout query requires a valid logical device.");

    if (desc.layout == CooperativeVectorMatrixLayout::RowMajor)
    {
        return CooperativeVectorMatrixLayoutSize{
            .byteSize = checkedRowMajorMatrixSize(desc),
        };
    }

    auto const elementSize = cooperativeVectorComponentSize(desc.componentType);
    auto const sourceStride = static_cast<vk::DeviceSize>(desc.columns) * elementSize;
    auto const sourceSize = checkedRowMajorMatrixSize(CooperativeVectorMatrixDesc{
        .rows = desc.rows,
        .columns = desc.columns,
        .layout = CooperativeVectorMatrixLayout::RowMajor,
        .rowStrideBytes = sourceStride,
        .componentType = desc.componentType,
    });
    nrAssert(sourceSize <= std::numeric_limits<std::size_t>::max(),
             "Cooperative-vector layout query source size exceeds size_t.");
    auto source = std::vector<std::byte>(static_cast<std::size_t>(sourceSize));
    auto destinationSize = std::size_t{0};
    auto info = vk::ConvertCooperativeVectorMatrixInfoNV{};
    info.srcSize = static_cast<std::size_t>(sourceSize);
    info.srcData.hostAddress = source.data();
    info.pDstSize = std::addressof(destinationSize);
    info.srcComponentType = toVulkanCooperativeVectorComponentType(desc.componentType);
    info.dstComponentType = toVulkanCooperativeVectorComponentType(desc.componentType);
    info.numRows = desc.rows;
    info.numColumns = desc.columns;
    info.srcLayout = vk::CooperativeVectorMatrixLayoutNV::eRowMajor;
    info.srcStride = static_cast<std::size_t>(sourceStride);
    info.dstLayout = toVulkanCooperativeVectorLayout(desc.layout);
    info.dstStride = 0;

    try
    {
        auto const result = device.convertCooperativeVectorMatrixNV(info);
        nrAssert(result == vk::Result::eSuccess || result == vk::Result::eIncomplete,
                 "Cooperative-vector matrix layout query returned unexpected Vulkan result {}.", vk::to_string(result));
    }
    catch (const vk::SystemError &error)
    {
        nrLog<LogLevel::error>("Cooperative-vector matrix layout query failed: {}", error.what());
        return {};
    }

    nrAssert(destinationSize > 0, "Cooperative-vector optimal-layout query returned a zero byte size.");
    return CooperativeVectorMatrixLayoutSize{
        .byteSize = static_cast<vk::DeviceSize>(destinationSize),
    };
}

void recordCooperativeVectorMatrixConversion(const vk::raii::CommandBuffer &commandBuffer,
                                             CooperativeVectorMatrixMemory source,
                                             CooperativeVectorMatrixDesc sourceDesc,
                                             CooperativeVectorMatrixLayoutSize sourceLayoutSize,
                                             CooperativeVectorMatrixMemory destination,
                                             CooperativeVectorMatrixDesc destinationDesc,
                                             CooperativeVectorMatrixLayoutSize destinationLayoutSize)
{
    validateCooperativeVectorMatrixDesc(sourceDesc);
    validateCooperativeVectorMatrixDesc(destinationDesc);
    nrAssert(sourceDesc.rows == destinationDesc.rows && sourceDesc.columns == destinationDesc.columns,
             "Cooperative-vector matrix conversion requires matching source and destination dimensions.");

    validateCooperativeVectorMatrixLayoutSize(sourceDesc, sourceLayoutSize, "source");
    validateCooperativeVectorMatrixLayoutSize(destinationDesc, destinationLayoutSize, "destination");
    validateCooperativeVectorMatrixMemory(source, sourceLayoutSize, "source");
    validateCooperativeVectorMatrixMemory(destination, destinationLayoutSize, "destination");
    nrAssert(sourceLayoutSize.byteSize <= std::numeric_limits<std::size_t>::max(),
             "Cooperative-vector source matrix layout size exceeds size_t.");
    nrAssert(destinationLayoutSize.byteSize <= std::numeric_limits<std::size_t>::max(),
             "Cooperative-vector destination matrix layout size exceeds size_t.");

    auto info = vk::ConvertCooperativeVectorMatrixInfoNV{};
    // pDstSize remains live for the immediate Vulkan-Hpp command call and is
    // the exact queried destination matrix size, never an allocation tail.
    auto destinationSize = static_cast<std::size_t>(destinationLayoutSize.byteSize);
    info.srcSize = static_cast<std::size_t>(sourceLayoutSize.byteSize);
    info.srcData.deviceAddress = source.deviceAddress;
    info.pDstSize = std::addressof(destinationSize);
    info.dstData.deviceAddress = destination.deviceAddress;
    info.srcComponentType = toVulkanCooperativeVectorComponentType(sourceDesc.componentType);
    info.dstComponentType = toVulkanCooperativeVectorComponentType(destinationDesc.componentType);
    info.numRows = sourceDesc.rows;
    info.numColumns = sourceDesc.columns;
    info.srcLayout = toVulkanCooperativeVectorLayout(sourceDesc.layout);
    info.srcStride = static_cast<std::size_t>(sourceDesc.rowStrideBytes);
    info.dstLayout = toVulkanCooperativeVectorLayout(destinationDesc.layout);
    info.dstStride = static_cast<std::size_t>(destinationDesc.rowStrideBytes);
    auto const infos = std::array{info};
    commandBuffer.convertCooperativeVectorMatrixNV(infos);
}
} // namespace nr::rhi
