module nr.renderer;

import :renderGraphOps;
import dependency.vulkan;
import nr.utils;
import std;
import :renderGraphBuilder;
import :renderGraphType;
import :renderer;

namespace nr::renderer::ops
{
namespace
{
[[nodiscard]] vk::ImageAspectFlags imageAspectFlags(ImageAspectIntent aspect) noexcept
{
    switch (aspect)
    {
    case ImageAspectIntent::Color:
        return vk::ImageAspectFlagBits::eColor;
    case ImageAspectIntent::Depth:
        return vk::ImageAspectFlagBits::eDepth;
    case ImageAspectIntent::Stencil:
        return vk::ImageAspectFlagBits::eStencil;
    case ImageAspectIntent::DepthStencil:
        return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
    }
    return vk::ImageAspectFlagBits::eColor;
}

[[nodiscard]] std::optional<ImageAspectIntent> imageAspectFromMask(vk::ImageAspectFlags aspectMask) noexcept
{
    if (aspectMask == vk::ImageAspectFlags{})
    {
        return std::nullopt;
    }

    auto const hasDepth = (aspectMask & vk::ImageAspectFlagBits::eDepth) != vk::ImageAspectFlags{};
    auto const hasStencil = (aspectMask & vk::ImageAspectFlagBits::eStencil) != vk::ImageAspectFlags{};
    if (hasDepth && hasStencil)
    {
        return ImageAspectIntent::DepthStencil;
    }
    if (hasDepth)
    {
        return ImageAspectIntent::Depth;
    }
    if (hasStencil)
    {
        return ImageAspectIntent::Stencil;
    }
    return ImageAspectIntent::Color;
}

[[nodiscard]] vk::ImageSubresourceRange normalizeImageSubresourceRange(
    vk::ImageSubresourceRange range,
    const PassImageResource& resolvedImage,
    std::optional<ImageAspectIntent> aspect = std::nullopt)
{
    if (range.aspectMask == vk::ImageAspectFlags{} &&
        range.levelCount == 0u &&
        range.layerCount == 0u)
    {
        range = resolvedImage.subresourceRange;
    }

    if (range.aspectMask == vk::ImageAspectFlags{} && aspect.has_value())
    {
        range.aspectMask = imageAspectFlags(*aspect);
    }
    if (range.aspectMask == vk::ImageAspectFlags{})
    {
        range.aspectMask = resolvedImage.subresourceRange.aspectMask;
    }
    if (range.levelCount == 0u)
    {
        range.baseMipLevel = resolvedImage.subresourceRange.baseMipLevel;
        range.levelCount = resolvedImage.subresourceRange.levelCount;
    }
    if (range.layerCount == 0u)
    {
        range.baseArrayLayer = resolvedImage.subresourceRange.baseArrayLayer;
        range.layerCount = resolvedImage.subresourceRange.layerCount;
    }

    return range;
}

[[nodiscard]] ImageAspectIntent clearDepthStencilAspect(
    NodeBuildContext& context,
    const ClearDepthStencilImagePassDesc& desc)
{
    if (desc.aspect.has_value())
    {
        return *desc.aspect;
    }

    auto aspectFromRange = imageAspectFromMask(desc.subresourceRange.aspectMask);
    if (aspectFromRange.has_value())
    {
        return *aspectFromRange;
    }

    auto resourceDesc = context.describeImageResource(desc.image);
    if (resourceDesc.has_value())
    {
        return resourceDesc->aspect;
    }

    return ImageAspectIntent::DepthStencil;
}

[[nodiscard]] PassResourceUseDesc imageTransferDstUse(GraphResourceHandle image, ImageAspectIntent aspect) noexcept
{
    return use::make<use::spec::ImageTransferDst>(image, use::ImageUseOptions{
                                                            .aspect = aspect,
                                                        });
}

[[nodiscard]] GraphPassHandle addCopyPass(
    NodeBuildContext& context,
    std::string_view debugName,
    CopyPassDesc copy)
{
    return context.graphBuilder.get().addCopyPass(
        debugName,
        context.nodeHandle,
        std::move(copy));
}
} // namespace

[[nodiscard]] GraphPassHandle clearBuffer(
    NodeBuildContext& context,
    std::string_view debugName,
    ClearBufferPassDesc desc)
{
    nrAssert(desc.buffer.valid(), "clearBuffer requires a valid buffer resource.");
    nrAssert(desc.offset % 4u == 0u, "clearBuffer offset must be 4-byte aligned.");
    nrAssert(
        desc.size == vk::WholeSize || desc.size % 4u == 0u,
        "clearBuffer size must be vk::WholeSize or 4-byte aligned.");

    auto resourceUses = std::array{use::bufferTransferDst(desc.buffer)};
    return context.addPass(
        std::span<const PassResourceUseDesc>{resourceUses.data(), resourceUses.size()},
        debugName,
        [desc](const PassRecordContext& recordContext) {
            nrAssert(recordContext.commandBuffer.has_value(), "clearBuffer requires RAII command buffer access.");
            nrAssert(static_cast<bool>(recordContext.resolveBuffer), "clearBuffer requires a buffer resolver.");
            auto resolvedBuffer = recordContext.resolveBuffer(desc.buffer);
            nrAssert(resolvedBuffer.has_value(), "clearBuffer failed to resolve destination buffer.");
            recordContext.commandBuffer->get().fillBuffer(
                resolvedBuffer->buffer,
                desc.offset,
                desc.size,
                desc.value);
        },
        nullptr,
        false,
        vk::PipelineStageFlagBits2::eTransfer);
}

[[nodiscard]] GraphPassHandle clearColorImage(
    NodeBuildContext& context,
    std::string_view debugName,
    ClearColorImagePassDesc desc)
{
    nrAssert(desc.image.valid(), "clearColorImage requires a valid image resource.");

    auto resourceUses = std::array{imageTransferDstUse(desc.image, ImageAspectIntent::Color)};
    return context.addPass(
        std::span<const PassResourceUseDesc>{resourceUses.data(), resourceUses.size()},
        debugName,
        [desc](const PassRecordContext& recordContext) {
            nrAssert(recordContext.commandBuffer.has_value(), "clearColorImage requires RAII command buffer access.");
            nrAssert(static_cast<bool>(recordContext.resolveImage), "clearColorImage requires an image resolver.");
            auto resolvedImage = recordContext.resolveImage(desc.image);
            nrAssert(resolvedImage.has_value(), "clearColorImage failed to resolve destination image.");
            auto range = normalizeImageSubresourceRange(
                desc.subresourceRange,
                *resolvedImage,
                ImageAspectIntent::Color);
            recordContext.commandBuffer->get().clearColorImage(
                resolvedImage->image,
                vk::ImageLayout::eTransferDstOptimal,
                desc.value,
                range);
        },
        nullptr,
        false,
        vk::PipelineStageFlagBits2::eTransfer);
}

[[nodiscard]] GraphPassHandle clearDepthStencilImage(
    NodeBuildContext& context,
    std::string_view debugName,
    ClearDepthStencilImagePassDesc desc)
{
    nrAssert(desc.image.valid(), "clearDepthStencilImage requires a valid image resource.");
    auto aspect = clearDepthStencilAspect(context, desc);
    nrAssert(
        aspect != ImageAspectIntent::Color,
        "clearDepthStencilImage requires a depth, stencil, or depth-stencil image aspect.");

    auto resourceUses = std::array{imageTransferDstUse(desc.image, aspect)};
    return context.addPass(
        std::span<const PassResourceUseDesc>{resourceUses.data(), resourceUses.size()},
        debugName,
        [desc, aspect](const PassRecordContext& recordContext) {
            nrAssert(recordContext.commandBuffer.has_value(), "clearDepthStencilImage requires RAII command buffer access.");
            nrAssert(static_cast<bool>(recordContext.resolveImage), "clearDepthStencilImage requires an image resolver.");
            auto resolvedImage = recordContext.resolveImage(desc.image);
            nrAssert(resolvedImage.has_value(), "clearDepthStencilImage failed to resolve destination image.");
            auto range = normalizeImageSubresourceRange(desc.subresourceRange, *resolvedImage, aspect);
            recordContext.commandBuffer->get().clearDepthStencilImage(
                resolvedImage->image,
                vk::ImageLayout::eTransferDstOptimal,
                desc.value,
                range);
        },
        nullptr,
        false,
        vk::PipelineStageFlagBits2::eTransfer);
}

[[nodiscard]] GraphPassHandle copyBufferToBuffer(
    NodeBuildContext& context,
    std::string_view debugName,
    CopyBufferToBufferPassDesc desc)
{
    return addCopyPass(context, debugName, CopyPassDesc{desc});
}

[[nodiscard]] GraphPassHandle copyBufferToImage(
    NodeBuildContext& context,
    std::string_view debugName,
    CopyBufferToImagePassDesc desc)
{
    return addCopyPass(context, debugName, CopyPassDesc{desc});
}

[[nodiscard]] GraphPassHandle copyImageToBuffer(
    NodeBuildContext& context,
    std::string_view debugName,
    CopyImageToBufferPassDesc desc)
{
    return addCopyPass(context, debugName, CopyPassDesc{desc});
}

[[nodiscard]] GraphPassHandle copyImageToImage(
    NodeBuildContext& context,
    std::string_view debugName,
    CopyImageToImagePassDesc desc)
{
    return addCopyPass(context, debugName, CopyPassDesc{desc});
}
} // namespace nr::renderer::ops
