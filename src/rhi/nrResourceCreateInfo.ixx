export module nr.rhi:resourceCreateInfo;
import dependency.vulkan;
import std;

export namespace nr::rhi
{

template <vk::ImageType ImageType = vk::ImageType::e2D>
[[nodiscard]] constexpr vk::ImageCreateInfo makeImageCreateInfo(
    vk::Format format,
    vk::Extent3D extent,
    vk::ImageUsageFlags usage,
    std::uint32_t mipLevels = 1u,
    std::uint32_t arrayLayers = 1u) noexcept
{
    auto imageInfo = vk::ImageCreateInfo{};
    imageInfo.imageType = ImageType;
    imageInfo.format = format;
    imageInfo.extent = extent;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = arrayLayers;
    imageInfo.usage = usage;
    return imageInfo;
}

template <vk::ImageType ImageType = vk::ImageType::e2D>
[[nodiscard]] constexpr vk::ImageCreateInfo makeImageCreateInfo(
    vk::Format format,
    vk::Extent2D extent,
    vk::ImageUsageFlags usage,
    std::uint32_t mipLevels = 1u,
    std::uint32_t arrayLayers = 1u) noexcept
{
    return makeImageCreateInfo<ImageType>(
        format,
        vk::Extent3D{extent.width, extent.height, 1u},
        usage,
        mipLevels,
        arrayLayers);
}

[[nodiscard]] constexpr vk::BufferCreateInfo makeBufferCreateInfo(
    vk::DeviceSize size,
    vk::BufferUsageFlags usage) noexcept
{
    auto bufferInfo = vk::BufferCreateInfo{};
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    return bufferInfo;
}

} // namespace nr::rhi
