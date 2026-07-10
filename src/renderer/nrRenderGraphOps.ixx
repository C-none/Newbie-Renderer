export module nr.renderer:renderGraphOps;

import dependency.vulkan;
import std;
import :renderGraphType;
import :renderer;

export namespace nr::renderer::ops
{
struct ClearBufferPassDesc
{
    GraphResourceHandle buffer{};
    vk::DeviceSize offset = 0;
    vk::DeviceSize size = vk::WholeSize;
    std::uint32_t value = 0;
};

struct ClearColorImagePassDesc
{
    GraphResourceHandle image{};
    vk::ClearColorValue value{};
    vk::ImageSubresourceRange subresourceRange{};
};

struct ClearDepthStencilImagePassDesc
{
    GraphResourceHandle image{};
    vk::ClearDepthStencilValue value{1.0f, 0u};
    vk::ImageSubresourceRange subresourceRange{};
    std::optional<ImageAspectIntent> aspect{};
};

[[nodiscard]] GraphPassHandle clearBuffer(
    NodeBuildContext& context,
    std::string_view debugName,
    ClearBufferPassDesc desc);

[[nodiscard]] GraphPassHandle clearColorImage(
    NodeBuildContext& context,
    std::string_view debugName,
    ClearColorImagePassDesc desc);

[[nodiscard]] GraphPassHandle clearDepthStencilImage(
    NodeBuildContext& context,
    std::string_view debugName,
    ClearDepthStencilImagePassDesc desc);

[[nodiscard]] GraphPassHandle copyBufferToBuffer(
    NodeBuildContext& context,
    std::string_view debugName,
    CopyBufferToBufferPassDesc desc);

[[nodiscard]] GraphPassHandle copyBufferToImage(
    NodeBuildContext& context,
    std::string_view debugName,
    CopyBufferToImagePassDesc desc);

[[nodiscard]] GraphPassHandle copyImageToBuffer(
    NodeBuildContext& context,
    std::string_view debugName,
    CopyImageToBufferPassDesc desc);

[[nodiscard]] GraphPassHandle copyImageToImage(
    NodeBuildContext& context,
    std::string_view debugName,
    CopyImageToImagePassDesc desc);
} // namespace nr::renderer::ops
