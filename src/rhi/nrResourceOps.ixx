module;
export module nr.rhi:resourceOps;
import dependency;
import std;
import :command;
import :commandBatch;
import :commandPool;
import :queue;
import :resource;
import :resourcePool;
import :sync;
import :vk;

export namespace nr::rhi::ops
{

inline constexpr uint32_t kIgnoredQueueFamilyIndex = std::numeric_limits<uint32_t>::max();

/**
 * @brief Barrier concept accepted by BarrierBatch::add.
 */
template <typename T>
concept Barrier2Like = std::same_as<std::remove_cvref_t<T>, vk::MemoryBarrier2> ||
                       std::same_as<std::remove_cvref_t<T>, vk::BufferMemoryBarrier2> ||
                       std::same_as<std::remove_cvref_t<T>, vk::ImageMemoryBarrier2>;

namespace detail
{
/**
 * @brief Compile-time ownership barrier phase selector.
 */
enum class OwnershipBarrierPhase
{
    Release,
    Acquire,
};

/**
 * @brief Resource kind supported by queue-ownership transfer barrier templates.
 */
template <typename TResourceKind>
concept QueueOwnershipResource = std::same_as<std::remove_cvref_t<TResourceKind>, Buffer> ||
                                 std::same_as<std::remove_cvref_t<TResourceKind>, Image>;

/**
 * @brief Parameter pack for queue-ownership transfer barrier generation.
 */
struct QueueOwnershipBarrierConfig
{
    uint32_t srcQueueFamilyIndex = kIgnoredQueueFamilyIndex;
    uint32_t dstQueueFamilyIndex = kIgnoredQueueFamilyIndex;
    vk::PipelineStageFlags2 stages = vk::PipelineStageFlags2{};
    vk::AccessFlags2 access = vk::AccessFlags2{};
    vk::ImageLayout oldLayout = vk::ImageLayout::eUndefined;
    vk::ImageLayout newLayout = vk::ImageLayout::eUndefined;
    vk::DeviceSize offset = 0;
    vk::DeviceSize size = std::numeric_limits<vk::DeviceSize>::max();
};

/**
 * @brief Build queue-ownership barrier for Buffer/Image with one strongly-typed template path.
 * @tparam TOwnershipPhase Compile-time phase selector: `Release` writes src masks; `Acquire` writes dst masks.
 * @tparam TResourceKind Resource category (`Buffer` or `Image`) that determines barrier type/layout fields.
 */
template <OwnershipBarrierPhase TOwnershipPhase, QueueOwnershipResource TResourceKind>
[[nodiscard]] inline auto makeQueueOwnershipBarrier(const TResourceKind& resource, const QueueOwnershipBarrierConfig& config)
{
    constexpr bool kIsRelease = TOwnershipPhase == OwnershipBarrierPhase::Release;

    if constexpr (std::same_as<std::remove_cvref_t<TResourceKind>, Buffer>)
    {
        return vk::BufferMemoryBarrier2{
            kIsRelease ? config.stages : vk::PipelineStageFlags2{},
            kIsRelease ? config.access : vk::AccessFlags2{},
            kIsRelease ? vk::PipelineStageFlags2{} : config.stages,
            kIsRelease ? vk::AccessFlags2{} : config.access,
            config.srcQueueFamilyIndex,
            config.dstQueueFamilyIndex,
            resource.handle(),
            config.offset,
            config.size,
            nullptr,
        };
    }
    else
    {
        return vk::ImageMemoryBarrier2{
            kIsRelease ? config.stages : vk::PipelineStageFlags2{},
            kIsRelease ? config.access : vk::AccessFlags2{},
            kIsRelease ? vk::PipelineStageFlags2{} : config.stages,
            kIsRelease ? vk::AccessFlags2{} : config.access,
            config.oldLayout,
            config.newLayout,
            config.srcQueueFamilyIndex,
            config.dstQueueFamilyIndex,
            resource.handle(),
            vk::ImageSubresourceRange{
                inferAspectFlags(resource.format()),
                0,
                resource.mipLevels(),
                0,
                resource.arrayLayers(),
            },
            nullptr,
        };
    }
}
} // namespace detail

/**
 * @brief Build a default full-range subresource from image metadata.
 */
[[nodiscard]] inline vk::ImageSubresourceRange fullSubresourceRange(const Image& image)
{
    return vk::ImageSubresourceRange{
        inferAspectFlags(image.format()),
        0,
        image.mipLevels(),
        0,
        image.arrayLayers(),
    };
}

/**
 * @brief Attach a Buffer resource handle to an existing barrier object.
 *
 * This preserves all caller-provided sync masks, queue ownership indices,
 * and offset/range fields while only patching the target handle.
 */
[[nodiscard]] inline vk::BufferMemoryBarrier2 makeBufferBarrier(const Buffer& buffer, vk::BufferMemoryBarrier2 barrier)
{
    barrier.buffer = buffer.handle();
    return barrier;
}

/**
 * @brief Build a sync2 image barrier targeting an Image resource.
 *
 * If the caller passes an empty subresource range, this helper expands it to the
 * image full range inferred from format/mips/layers.
 */
[[nodiscard]] inline vk::ImageMemoryBarrier2 makeImageBarrier(const Image& image, vk::ImageMemoryBarrier2 barrier)
{
    if (barrier.subresourceRange.aspectMask == vk::ImageAspectFlags{})
    {
        barrier.subresourceRange = fullSubresourceRange(image);
    }
    barrier.image = image.handle();
    return barrier;
}

/**
 * @brief Common factory: transfer-write buffer -> shader-read buffer.
 */
[[nodiscard]] inline vk::BufferMemoryBarrier2 makeBufferTransferWriteToShaderReadBarrier(
    const Buffer& buffer,
    vk::PipelineStageFlags2 dstStages = vk::PipelineStageFlagBits2::eVertexShader |
                                         vk::PipelineStageFlagBits2::eFragmentShader |
                                         vk::PipelineStageFlagBits2::eComputeShader,
    vk::DeviceSize offset = 0,
    vk::DeviceSize size = std::numeric_limits<vk::DeviceSize>::max())
{
    return makeBufferBarrier(buffer, vk::BufferMemoryBarrier2{
        vk::PipelineStageFlagBits2::eTransfer,
        vk::AccessFlagBits2::eTransferWrite,
        dstStages,
        vk::AccessFlagBits2::eShaderRead,
        kIgnoredQueueFamilyIndex,
        kIgnoredQueueFamilyIndex,
        vk::Buffer{},
        offset,
        size,
        nullptr,
    });
}

/**
 * @brief Common factory: host-write buffer -> transfer-read buffer.
 */
[[nodiscard]] inline vk::BufferMemoryBarrier2 makeBufferHostWriteToTransferReadBarrier(
    const Buffer& buffer,
    vk::DeviceSize offset = 0,
    vk::DeviceSize size = std::numeric_limits<vk::DeviceSize>::max())
{
    return makeBufferBarrier(buffer, vk::BufferMemoryBarrier2{
        vk::PipelineStageFlagBits2::eHost,
        vk::AccessFlagBits2::eHostWrite,
        vk::PipelineStageFlagBits2::eTransfer,
        vk::AccessFlagBits2::eTransferRead,
        kIgnoredQueueFamilyIndex,
        kIgnoredQueueFamilyIndex,
        vk::Buffer{},
        offset,
        size,
        nullptr,
    });
}

/**
 * @brief Common factory: undefined image -> transfer-dst image.
 */
[[nodiscard]] inline vk::ImageMemoryBarrier2 makeImageUndefinedToTransferDstBarrier(const Image& image)
{
    return makeImageBarrier(image, vk::ImageMemoryBarrier2{
        vk::PipelineStageFlagBits2::eTopOfPipe,
        {},
        vk::PipelineStageFlagBits2::eTransfer,
        vk::AccessFlagBits2::eTransferWrite,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eTransferDstOptimal,
        kIgnoredQueueFamilyIndex,
        kIgnoredQueueFamilyIndex,
        vk::Image{},
        {},
        nullptr,
    });
}

/**
 * @brief Common factory: transfer-dst image -> shader-read image.
 */
[[nodiscard]] inline vk::ImageMemoryBarrier2 makeImageTransferDstToShaderReadBarrier(
    const Image& image,
    vk::PipelineStageFlags2 dstStages = vk::PipelineStageFlagBits2::eFragmentShader |
                                         vk::PipelineStageFlagBits2::eComputeShader)
{
    return makeImageBarrier(image, vk::ImageMemoryBarrier2{
        vk::PipelineStageFlagBits2::eTransfer,
        vk::AccessFlagBits2::eTransferWrite,
        dstStages,
        vk::AccessFlagBits2::eShaderRead,
        vk::ImageLayout::eTransferDstOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        kIgnoredQueueFamilyIndex,
        kIgnoredQueueFamilyIndex,
        vk::Image{},
        {},
        nullptr,
    });
}

/**
 * @brief Common factory: color-attachment image -> present image.
 */
[[nodiscard]] inline vk::ImageMemoryBarrier2 makeImageColorAttachmentToPresentBarrier(const Image& image)
{
    return makeImageBarrier(image, vk::ImageMemoryBarrier2{
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::PipelineStageFlagBits2::eBottomOfPipe,
        {},
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageLayout::ePresentSrcKHR,
        kIgnoredQueueFamilyIndex,
        kIgnoredQueueFamilyIndex,
        vk::Image{},
        {},
        nullptr,
    });
}

/**
 * @brief Generic buffer queue-ownership release barrier (sync2).
 *
 * Destination stage/access are intentionally empty for release semantics.
 */
[[nodiscard]] inline vk::BufferMemoryBarrier2 makeBufferOwnershipReleaseBarrier(
    const Buffer& buffer,
    uint32_t srcQueueFamilyIndex,
    uint32_t dstQueueFamilyIndex,
    vk::PipelineStageFlags2 srcStages,
    vk::AccessFlags2 srcAccess,
    vk::DeviceSize offset = 0,
    vk::DeviceSize size = std::numeric_limits<vk::DeviceSize>::max())
{
    return detail::makeQueueOwnershipBarrier<detail::OwnershipBarrierPhase::Release>(
        buffer,
        detail::QueueOwnershipBarrierConfig{
            .srcQueueFamilyIndex = srcQueueFamilyIndex,
            .dstQueueFamilyIndex = dstQueueFamilyIndex,
            .stages = srcStages,
            .access = srcAccess,
            .offset = offset,
            .size = size,
        });
}

/**
 * @brief Generic buffer queue-ownership acquire barrier (sync2).
 *
 * Source stage/access are intentionally empty for acquire semantics.
 */
[[nodiscard]] inline vk::BufferMemoryBarrier2 makeBufferOwnershipAcquireBarrier(
    const Buffer& buffer,
    uint32_t srcQueueFamilyIndex,
    uint32_t dstQueueFamilyIndex,
    vk::PipelineStageFlags2 dstStages,
    vk::AccessFlags2 dstAccess,
    vk::DeviceSize offset = 0,
    vk::DeviceSize size = std::numeric_limits<vk::DeviceSize>::max())
{
    return detail::makeQueueOwnershipBarrier<detail::OwnershipBarrierPhase::Acquire>(
        buffer,
        detail::QueueOwnershipBarrierConfig{
            .srcQueueFamilyIndex = srcQueueFamilyIndex,
            .dstQueueFamilyIndex = dstQueueFamilyIndex,
            .stages = dstStages,
            .access = dstAccess,
            .offset = offset,
            .size = size,
        });
}

/**
 * @brief Generic image queue-ownership release barrier (sync2).
 */
[[nodiscard]] inline vk::ImageMemoryBarrier2 makeImageOwnershipReleaseBarrier(
    const Image& image,
    uint32_t srcQueueFamilyIndex,
    uint32_t dstQueueFamilyIndex,
    vk::ImageLayout oldLayout,
    vk::ImageLayout newLayout,
    vk::PipelineStageFlags2 srcStages,
    vk::AccessFlags2 srcAccess)
{
    return detail::makeQueueOwnershipBarrier<detail::OwnershipBarrierPhase::Release>(
        image,
        detail::QueueOwnershipBarrierConfig{
            .srcQueueFamilyIndex = srcQueueFamilyIndex,
            .dstQueueFamilyIndex = dstQueueFamilyIndex,
            .stages = srcStages,
            .access = srcAccess,
            .oldLayout = oldLayout,
            .newLayout = newLayout,
        });
}

/**
 * @brief Generic image queue-ownership acquire barrier (sync2).
 */
[[nodiscard]] inline vk::ImageMemoryBarrier2 makeImageOwnershipAcquireBarrier(
    const Image& image,
    uint32_t srcQueueFamilyIndex,
    uint32_t dstQueueFamilyIndex,
    vk::ImageLayout oldLayout,
    vk::ImageLayout newLayout,
    vk::PipelineStageFlags2 dstStages,
    vk::AccessFlags2 dstAccess)
{
    return detail::makeQueueOwnershipBarrier<detail::OwnershipBarrierPhase::Acquire>(
        image,
        detail::QueueOwnershipBarrierConfig{
            .srcQueueFamilyIndex = srcQueueFamilyIndex,
            .dstQueueFamilyIndex = dstQueueFamilyIndex,
            .stages = dstStages,
            .access = dstAccess,
            .oldLayout = oldLayout,
            .newLayout = newLayout,
        });
}

/**
 * @brief Convenience: transfer queue release for a buffer written by transfer.
 */
[[nodiscard]] inline vk::BufferMemoryBarrier2 makeBufferTransferReleaseBarrier(
    const Buffer& buffer,
    uint32_t transferQueueFamilyIndex,
    uint32_t dstQueueFamilyIndex,
    vk::DeviceSize offset = 0,
    vk::DeviceSize size = std::numeric_limits<vk::DeviceSize>::max())
{
    return makeBufferOwnershipReleaseBarrier(
        buffer,
        transferQueueFamilyIndex,
        dstQueueFamilyIndex,
        vk::PipelineStageFlagBits2::eTransfer,
        vk::AccessFlagBits2::eTransferWrite,
        offset,
        size);
}

/**
 * @brief Convenience: graphics queue acquire for shader-read buffer use.
 */
[[nodiscard]] inline vk::BufferMemoryBarrier2 makeBufferGraphicsAcquireShaderReadBarrier(
    const Buffer& buffer,
    uint32_t srcQueueFamilyIndex,
    uint32_t graphicsQueueFamilyIndex,
    vk::PipelineStageFlags2 dstStages = vk::PipelineStageFlagBits2::eVertexShader |
                                         vk::PipelineStageFlagBits2::eFragmentShader |
                                         vk::PipelineStageFlagBits2::eComputeShader,
    vk::DeviceSize offset = 0,
    vk::DeviceSize size = std::numeric_limits<vk::DeviceSize>::max())
{
    return makeBufferOwnershipAcquireBarrier(
        buffer,
        srcQueueFamilyIndex,
        graphicsQueueFamilyIndex,
        dstStages,
        vk::AccessFlagBits2::eShaderRead,
        offset,
        size);
}

/**
 * @brief Owns sync2 barrier arrays and a ready-to-use DependencyInfo view.
 */
struct DependencyInfoPacket
{
    std::vector<vk::MemoryBarrier2> memoryBarriers;
    std::vector<vk::BufferMemoryBarrier2> bufferBarriers;
    std::vector<vk::ImageMemoryBarrier2> imageBarriers;
    vk::DependencyInfo info{};

    [[nodiscard]] const vk::DependencyInfo& dependencyInfo() const noexcept
    {
        return info;
    }
};

/**
 * @brief Lightweight collector for sync2 barriers.
 *
 * The batch is intentionally state-light and non-owning for resources.
 */
class BarrierBatch
{
  public:
    void clear()
    {
        memoryBarriers_.clear();
        bufferBarriers_.clear();
        imageBarriers_.clear();
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return memoryBarriers_.empty() && bufferBarriers_.empty() && imageBarriers_.empty();
    }

    template <Barrier2Like TBarrier>
    void add(TBarrier&& barrier)
    {
        using BarrierT = std::remove_cvref_t<TBarrier>;
        if constexpr (std::same_as<BarrierT, vk::MemoryBarrier2>)
        {
            memoryBarriers_.push_back(std::forward<TBarrier>(barrier));
        }
        else if constexpr (std::same_as<BarrierT, vk::BufferMemoryBarrier2>)
        {
            bufferBarriers_.push_back(std::forward<TBarrier>(barrier));
        }
        else if constexpr (std::same_as<BarrierT, vk::ImageMemoryBarrier2>)
        {
            imageBarriers_.push_back(std::forward<TBarrier>(barrier));
        }
        else
        {
            static_assert(std::same_as<BarrierT, void>, "Unsupported barrier type.");
        }
    }

    void addBuffer(const Buffer& buffer, vk::BufferMemoryBarrier2 barrier)
    {
        add(makeBufferBarrier(buffer, std::move(barrier)));
    }

    void addImage(const Image& image, vk::ImageMemoryBarrier2 barrier)
    {
        add(makeImageBarrier(image, std::move(barrier)));
    }

    [[nodiscard]] DependencyInfoPacket buildDependencyInfo() const
    {
        DependencyInfoPacket packet{};
        packet.memoryBarriers = memoryBarriers_;
        packet.bufferBarriers = bufferBarriers_;
        packet.imageBarriers = imageBarriers_;

        packet.info = vk::DependencyInfo{};
        packet.info.memoryBarrierCount = static_cast<uint32_t>(packet.memoryBarriers.size());
        packet.info.pMemoryBarriers = packet.memoryBarriers.data();
        packet.info.bufferMemoryBarrierCount = static_cast<uint32_t>(packet.bufferBarriers.size());
        packet.info.pBufferMemoryBarriers = packet.bufferBarriers.data();
        packet.info.imageMemoryBarrierCount = static_cast<uint32_t>(packet.imageBarriers.size());
        packet.info.pImageMemoryBarriers = packet.imageBarriers.data();
        return packet;
    }

  private:
    std::vector<vk::MemoryBarrier2> memoryBarriers_;
    std::vector<vk::BufferMemoryBarrier2> bufferBarriers_;
    std::vector<vk::ImageMemoryBarrier2> imageBarriers_;
};

/**
 * @brief Apply one sync2 barrier batch into a command buffer.
 */
inline void pipelineBarrier(vk::CommandBuffer commandBuffer, const BarrierBatch& barriers)
{
    auto packet = barriers.buildDependencyInfo();
    commandBuffer.pipelineBarrier2(packet.dependencyInfo());
}

/**
 * @brief Transition one image using a single sync2 image barrier.
 */
inline void transitionImage(vk::CommandBuffer commandBuffer, const Image& image, vk::ImageMemoryBarrier2 barrier)
{
    BarrierBatch barriers{};
    barriers.addImage(image, std::move(barrier));
    pipelineBarrier(commandBuffer, barriers);
}

/**
 * @brief Copy one buffer into another with an optional explicit size.
 */
inline void copyBuffer(vk::CommandBuffer commandBuffer, const Buffer& src, const Buffer& dst, vk::DeviceSize size = 0)
{
    vk::DeviceSize copySize = size == 0 ? std::min(src.size(), dst.size()) : size;
    vk::BufferCopy region{0, 0, copySize};
    commandBuffer.copyBuffer(src.handle(), dst.handle(), {region});
}

/**
 * @brief Copy buffer data into an image.
 */
inline void copyBufferToImage(vk::CommandBuffer commandBuffer, const Buffer& src, const Image& dst, vk::ImageLayout dstLayout = vk::ImageLayout::eTransferDstOptimal, const vk::BufferImageCopy& region = {})
{
    vk::BufferImageCopy effectiveRegion = region;
    if (effectiveRegion.imageSubresource.aspectMask == vk::ImageAspectFlags{})
    {
        effectiveRegion.imageSubresource = vk::ImageSubresourceLayers{inferAspectFlags(dst.format()), 0, 0, 1};
    }
    if (effectiveRegion.imageExtent == vk::Extent3D{})
    {
        effectiveRegion.imageExtent = dst.extent();
    }
    commandBuffer.copyBufferToImage(src.handle(), dst.handle(), dstLayout, {effectiveRegion});
}

/**
 * @brief Copy image data into a buffer.
 */
inline void copyImageToBuffer(vk::CommandBuffer commandBuffer, const Image& src, const Buffer& dst, vk::ImageLayout srcLayout = vk::ImageLayout::eTransferSrcOptimal, const vk::BufferImageCopy& region = {})
{
    vk::BufferImageCopy effectiveRegion = region;
    if (effectiveRegion.imageSubresource.aspectMask == vk::ImageAspectFlags{})
    {
        effectiveRegion.imageSubresource = vk::ImageSubresourceLayers{inferAspectFlags(src.format()), 0, 0, 1};
    }
    if (effectiveRegion.imageExtent == vk::Extent3D{})
    {
        effectiveRegion.imageExtent = src.extent();
    }
    commandBuffer.copyImageToBuffer(src.handle(), srcLayout, dst.handle(), {effectiveRegion});
}

struct RenderingAttachmentDesc
{
    vk::ImageView imageView{};
    vk::ImageLayout imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
    vk::ResolveModeFlagBits resolveMode = vk::ResolveModeFlagBits::eNone;
    vk::ImageView resolveImageView{};
    vk::ImageLayout resolveImageLayout = vk::ImageLayout::eUndefined;
    vk::AttachmentLoadOp loadOp = vk::AttachmentLoadOp::eLoad;
    vk::AttachmentStoreOp storeOp = vk::AttachmentStoreOp::eStore;
    vk::ClearValue clearValue{};
};

struct RenderingDepthStencilAttachmentDesc
{
    vk::ImageView imageView{};
    vk::ImageLayout imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
    vk::ResolveModeFlagBits resolveMode = vk::ResolveModeFlagBits::eNone;
    vk::ImageView resolveImageView{};
    vk::ImageLayout resolveImageLayout = vk::ImageLayout::eUndefined;
    vk::AttachmentLoadOp depthLoadOp = vk::AttachmentLoadOp::eLoad;
    vk::AttachmentStoreOp depthStoreOp = vk::AttachmentStoreOp::eStore;
    vk::AttachmentLoadOp stencilLoadOp = vk::AttachmentLoadOp::eLoad;
    vk::AttachmentStoreOp stencilStoreOp = vk::AttachmentStoreOp::eStore;
    vk::ClearDepthStencilValue clearValue{1.0f, 0u};
};

struct RenderingScopeDesc
{
    vk::Rect2D renderArea{};
    uint32_t layerCount = 1;
    uint32_t viewMask = 0;
    vk::RenderingFlags flags{};
    std::span<const RenderingAttachmentDesc> colorAttachments{};
    std::optional<RenderingDepthStencilAttachmentDesc> depthAttachment;
    std::optional<RenderingDepthStencilAttachmentDesc> stencilAttachment;
};

namespace detail
{
[[nodiscard]] inline vk::RenderingAttachmentInfo makeRenderingAttachmentInfo(const RenderingAttachmentDesc& desc)
{
    vk::RenderingAttachmentInfo info{};
    info.imageView = desc.imageView;
    info.imageLayout = desc.imageLayout;
    info.resolveMode = desc.resolveMode;
    info.resolveImageView = desc.resolveImageView;
    info.resolveImageLayout = desc.resolveImageLayout;
    info.loadOp = desc.loadOp;
    info.storeOp = desc.storeOp;
    info.clearValue = desc.clearValue;
    return info;
}

[[nodiscard]] inline vk::RenderingAttachmentInfo makeRenderingAttachmentInfo(const RenderingDepthStencilAttachmentDesc& desc)
{
    vk::RenderingAttachmentInfo info{};
    info.imageView = desc.imageView;
    info.imageLayout = desc.imageLayout;
    info.resolveMode = desc.resolveMode;
    info.resolveImageView = desc.resolveImageView;
    info.resolveImageLayout = desc.resolveImageLayout;
    info.loadOp = desc.depthLoadOp;
    info.storeOp = desc.depthStoreOp;
    info.clearValue = vk::ClearValue{desc.clearValue};
    return info;
}
} // namespace detail

class ScopedRendering
{
  public:
    ScopedRendering(vk::CommandBuffer commandBuffer, const RenderingScopeDesc& desc)
        : commandBuffer_(commandBuffer)
        , colorAttachmentInfos_(desc.colorAttachments |
                                std::views::transform([](const RenderingAttachmentDesc& attachment) { return detail::makeRenderingAttachmentInfo(attachment); }) |
                                std::ranges::to<std::vector>())
    {
        nrAssert(commandBuffer_ != nullptr, "ScopedRendering requires a valid command buffer.");

        renderingInfo_.renderArea = desc.renderArea;
        renderingInfo_.layerCount = desc.layerCount;
        renderingInfo_.viewMask = desc.viewMask;
        renderingInfo_.flags = desc.flags;
        renderingInfo_.colorAttachmentCount = static_cast<uint32_t>(colorAttachmentInfos_.size());
        renderingInfo_.pColorAttachments = colorAttachmentInfos_.data();

        if (desc.depthAttachment.has_value())
        {
            depthAttachmentInfo_ = detail::makeRenderingAttachmentInfo(*desc.depthAttachment);
            renderingInfo_.pDepthAttachment = &(*depthAttachmentInfo_);
        }

        if (desc.stencilAttachment.has_value())
        {
            auto stencilAttachmentInfo = detail::makeRenderingAttachmentInfo(*desc.stencilAttachment);
            stencilAttachmentInfo.loadOp = desc.stencilAttachment->stencilLoadOp;
            stencilAttachmentInfo.storeOp = desc.stencilAttachment->stencilStoreOp;
            stencilAttachmentInfo_.emplace(stencilAttachmentInfo);
            renderingInfo_.pStencilAttachment = &(*stencilAttachmentInfo_);
        }

        commandBuffer_.beginRendering(renderingInfo_);
        isActive_ = true;
    }

    ~ScopedRendering()
    {
        if (isActive_ && commandBuffer_ != nullptr)
        {
            commandBuffer_.endRendering();
        }
    }

    ScopedRendering(const ScopedRendering&) = delete;
    ScopedRendering& operator=(const ScopedRendering&) = delete;
    ScopedRendering(ScopedRendering&&) = delete;
    ScopedRendering& operator=(ScopedRendering&&) = delete;

  private:
    vk::CommandBuffer commandBuffer_{};
    std::vector<vk::RenderingAttachmentInfo> colorAttachmentInfos_;
    std::optional<vk::RenderingAttachmentInfo> depthAttachmentInfo_;
    std::optional<vk::RenderingAttachmentInfo> stencilAttachmentInfo_;
    vk::RenderingInfo renderingInfo_{};
    bool isActive_ = false;
};

struct QueueOwnershipRequest
{
    uint32_t srcQueueFamilyIndex = kIgnoredQueueFamilyIndex;
    uint32_t dstQueueFamilyIndex = kIgnoredQueueFamilyIndex;
    vk::PipelineStageFlags2 dstStages = vk::PipelineStageFlagBits2::eAllCommands;
    vk::AccessFlags2 dstAccess = vk::AccessFlagBits2::eMemoryRead;
};

struct ReadbackTicket
{
    vk::DeviceSize offset = 0;
    vk::DeviceSize size = 0;
    uint64_t signalValue = 0;
};

/**
 * @brief Upload/readback helper built on persistent mapped ring buffers.
 *
 * Uses transfer queue + submit2 and timeline semaphore values supplied by caller.
 */
class UploadReadbackContext
{
  public:
    UploadReadbackContext(
        const vk::raii::Device& device,
        ResourceFactory& resourceFactory,
        QueueManager& queueManager,
        vk::DeviceSize uploadRingSize = 8u * 1024u * 1024u,
        vk::DeviceSize readbackRingSize = 8u * 1024u * 1024u)
        : device_(std::cref(device))
        , queueManager_(std::ref(queueManager))
        , uploadCapacity_(uploadRingSize)
        , readbackCapacity_(readbackRingSize)
        , transferPool_(device, queueManager.transfer().queueFamilyIndex(), vk::CommandPoolCreateFlagBits::eTransient)
    {
        vk::BufferCreateInfo uploadInfo{};
        uploadInfo.size = uploadRingSize;
        uploadInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
        uploadInfo.sharingMode = vk::SharingMode::eExclusive;
        uploadRing_ = resourceFactory.createBuffer(uploadInfo, MemoryUsage::CpuToGpu, "upload_ring");

        vk::BufferCreateInfo readbackInfo{};
        readbackInfo.size = readbackRingSize;
        readbackInfo.usage = vk::BufferUsageFlagBits::eTransferDst;
        readbackInfo.sharingMode = vk::SharingMode::eExclusive;
        readbackRing_ = resourceFactory.createBuffer(readbackInfo, MemoryUsage::GpuToCpu, "readback_ring");
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return uploadRing_.valid() && readbackRing_.valid() && transferPool_.valid();
    }

    void reclaimCompleted(const vk::raii::Semaphore& timelineSemaphore)
    {
        const uint64_t completedValue = queryTimelineValue(timelineSemaphore);
        reclaimQueue(uploadInFlight_, uploadReclaimCursor_, completedValue);
        reclaimQueue(readbackInFlight_, readbackReclaimCursor_, completedValue);
    }

    /**
     * @brief Upload raw bytes into a destination buffer via transfer queue.
     *
     * If ownership is provided, records a release barrier on transfer queue.
     */
    void uploadBuffer(
        std::span<const std::byte> data,
        const Buffer& dst,
        vk::DeviceSize dstOffset,
        const vk::raii::Semaphore& timelineSemaphore,
        uint64_t signalValue,
        std::optional<QueueOwnershipRequest> ownership = std::nullopt)
    {
        nrAssert(valid(), "UploadReadbackContext::uploadBuffer requires a valid context.");
        nrAssert(!data.empty(), "UploadReadbackContext::uploadBuffer requires non-empty data.");

        auto allocation = reserveUpload(static_cast<vk::DeviceSize>(data.size_bytes()), timelineSemaphore);

        std::memcpy(static_cast<std::byte*>(uploadRing_.mapped()) + allocation.offset, data.data(), data.size_bytes());
        uploadRing_.flush(allocation.offset, static_cast<vk::DeviceSize>(data.size_bytes()));

        auto commandBuffers = transferPool_.allocatePrimary(1);
        auto& commandBuffer = commandBuffers.front();

        CommandRecorder::beginPrimary(commandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        {
            auto raw = *commandBuffer;

            BarrierBatch stagingBarrier{};
            stagingBarrier.add(makeBufferHostWriteToTransferReadBarrier(uploadRing_, allocation.offset, static_cast<vk::DeviceSize>(data.size_bytes())));
            pipelineBarrier(raw, stagingBarrier);

            vk::BufferCopy copyRegion{};
            copyRegion.srcOffset = allocation.offset;
            copyRegion.dstOffset = dstOffset;
            copyRegion.size = static_cast<vk::DeviceSize>(data.size_bytes());
            raw.copyBuffer(uploadRing_.handle(), dst.handle(), {copyRegion});

            if (ownership.has_value())
            {
                BarrierBatch releaseBarrier{};
                releaseBarrier.add(makeBufferOwnershipReleaseBarrier(
                    dst,
                    ownership->srcQueueFamilyIndex,
                    ownership->dstQueueFamilyIndex,
                    vk::PipelineStageFlagBits2::eTransfer,
                    vk::AccessFlagBits2::eTransferWrite,
                    dstOffset,
                    static_cast<vk::DeviceSize>(data.size_bytes())));
                pipelineBarrier(raw, releaseBarrier);
            }
        }
        CommandRecorder::end(commandBuffer);

        CommandBatch batch{};
        batch.addCommandBuffer(commandBuffer);
        batch.addSignal(timelineSemaphore, signalValue, 0, vk::PipelineStageFlagBits2::eTransfer);
        queueManager_->get().transfer().submit(batch);

        addInFlight(uploadInFlight_, allocation, signalValue, std::move(commandBuffers));
    }

    /**
     * @brief Upload to an image via transfer queue copy.
     */
    void uploadImage(
        std::span<const std::byte> data,
        const Image& dst,
        vk::ImageLayout dstLayout,
        const vk::raii::Semaphore& timelineSemaphore,
        uint64_t signalValue,
        const vk::BufferImageCopy& region = {},
        std::optional<QueueOwnershipRequest> ownership = std::nullopt)
    {
        nrAssert(valid(), "UploadReadbackContext::uploadImage requires a valid context.");
        nrAssert(!data.empty(), "UploadReadbackContext::uploadImage requires non-empty data.");

        auto allocation = reserveUpload(static_cast<vk::DeviceSize>(data.size_bytes()), timelineSemaphore);

        std::memcpy(static_cast<std::byte*>(uploadRing_.mapped()) + allocation.offset, data.data(), data.size_bytes());
        uploadRing_.flush(allocation.offset, static_cast<vk::DeviceSize>(data.size_bytes()));

        auto commandBuffers = transferPool_.allocatePrimary(1);
        auto& commandBuffer = commandBuffers.front();

        CommandRecorder::beginPrimary(commandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        {
            auto raw = *commandBuffer;

            BarrierBatch stagingBarrier{};
            stagingBarrier.add(makeBufferHostWriteToTransferReadBarrier(uploadRing_, allocation.offset, static_cast<vk::DeviceSize>(data.size_bytes())));
            pipelineBarrier(raw, stagingBarrier);

            vk::BufferImageCopy effectiveRegion = region;
            if (effectiveRegion.imageSubresource.aspectMask == vk::ImageAspectFlags{})
            {
                effectiveRegion.imageSubresource = vk::ImageSubresourceLayers{inferAspectFlags(dst.format()), 0, 0, 1};
            }
            if (effectiveRegion.imageExtent == vk::Extent3D{})
            {
                effectiveRegion.imageExtent = dst.extent();
            }
            effectiveRegion.bufferOffset += allocation.offset;
            raw.copyBufferToImage(uploadRing_.handle(), dst.handle(), dstLayout, {effectiveRegion});

            if (ownership.has_value())
            {
                BarrierBatch releaseBarrier{};
                releaseBarrier.add(makeImageOwnershipReleaseBarrier(
                    dst,
                    ownership->srcQueueFamilyIndex,
                    ownership->dstQueueFamilyIndex,
                    dstLayout,
                    dstLayout,
                    vk::PipelineStageFlagBits2::eTransfer,
                    vk::AccessFlagBits2::eTransferWrite));
                pipelineBarrier(raw, releaseBarrier);
            }
        }
        CommandRecorder::end(commandBuffer);

        CommandBatch batch{};
        batch.addCommandBuffer(commandBuffer);
        batch.addSignal(timelineSemaphore, signalValue, 0, vk::PipelineStageFlagBits2::eTransfer);
        queueManager_->get().transfer().submit(batch);

        addInFlight(uploadInFlight_, allocation, signalValue, std::move(commandBuffers));
    }

    /**
     * @brief Copy a GPU buffer into readback ring and return a ticket.
     */
    [[nodiscard]] ReadbackTicket readbackBuffer(
        const Buffer& src,
        vk::DeviceSize srcOffset,
        vk::DeviceSize size,
        const vk::raii::Semaphore& timelineSemaphore,
        uint64_t signalValue)
    {
        nrAssert(valid(), "UploadReadbackContext::readbackBuffer requires a valid context.");
        nrAssert(size > 0, "UploadReadbackContext::readbackBuffer requires size > 0.");

        auto allocation = reserveReadback(size, timelineSemaphore);

        auto commandBuffers = transferPool_.allocatePrimary(1);
        auto& commandBuffer = commandBuffers.front();
        CommandRecorder::beginPrimary(commandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        {
            auto raw = *commandBuffer;
            vk::BufferCopy copyRegion{};
            copyRegion.srcOffset = srcOffset;
            copyRegion.dstOffset = allocation.offset;
            copyRegion.size = size;
            raw.copyBuffer(src.handle(), readbackRing_.handle(), {copyRegion});
        }
        CommandRecorder::end(commandBuffer);

        CommandBatch batch{};
        batch.addCommandBuffer(commandBuffer);
        batch.addSignal(timelineSemaphore, signalValue, 0, vk::PipelineStageFlagBits2::eTransfer);
        queueManager_->get().transfer().submit(batch);

        addInFlight(readbackInFlight_, allocation, signalValue, std::move(commandBuffers));

        return ReadbackTicket{allocation.offset, size, signalValue};
    }

    /**
     * @brief Copy a GPU image into readback ring and return a ticket.
     */
    [[nodiscard]] ReadbackTicket readbackImage(
        const Image& src,
        vk::ImageLayout srcLayout,
        const vk::raii::Semaphore& timelineSemaphore,
        uint64_t signalValue,
        const vk::BufferImageCopy& region = {})
    {
        nrAssert(valid(), "UploadReadbackContext::readbackImage requires a valid context.");

        vk::BufferImageCopy effectiveRegion = region;
        if (effectiveRegion.imageSubresource.aspectMask == vk::ImageAspectFlags{})
        {
            effectiveRegion.imageSubresource = vk::ImageSubresourceLayers{inferAspectFlags(src.format()), 0, 0, 1};
        }
        if (effectiveRegion.imageExtent == vk::Extent3D{})
        {
            effectiveRegion.imageExtent = src.extent();
        }

        const auto elementSize = bytesPerPixel(src.format());
        const auto rowLength = effectiveRegion.bufferRowLength > 0 ? effectiveRegion.bufferRowLength : effectiveRegion.imageExtent.width;
        const auto imageHeight = effectiveRegion.bufferImageHeight > 0 ? effectiveRegion.bufferImageHeight : effectiveRegion.imageExtent.height;
        const auto layerCount = std::max(1u, effectiveRegion.imageSubresource.layerCount);

        const auto readbackSize = std::max<vk::DeviceSize>(
            1,
            static_cast<vk::DeviceSize>(rowLength) *
                static_cast<vk::DeviceSize>(imageHeight) *
                static_cast<vk::DeviceSize>(effectiveRegion.imageExtent.depth) *
                static_cast<vk::DeviceSize>(layerCount) *
                elementSize);
        auto allocation = reserveReadback(readbackSize, timelineSemaphore);

        auto commandBuffers = transferPool_.allocatePrimary(1);
        auto& commandBuffer = commandBuffers.front();
        CommandRecorder::beginPrimary(commandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        {
            auto raw = *commandBuffer;
            effectiveRegion.bufferOffset += allocation.offset;
            raw.copyImageToBuffer(src.handle(), srcLayout, readbackRing_.handle(), {effectiveRegion});
        }
        CommandRecorder::end(commandBuffer);

        CommandBatch batch{};
        batch.addCommandBuffer(commandBuffer);
        batch.addSignal(timelineSemaphore, signalValue, 0, vk::PipelineStageFlagBits2::eTransfer);
        queueManager_->get().transfer().submit(batch);

        addInFlight(readbackInFlight_, allocation, signalValue, std::move(commandBuffers));
        return ReadbackTicket{allocation.offset, readbackSize, signalValue};
    }

    /**
     * @brief Block until ticket is ready, invalidate cache, and copy bytes out.
     */
    [[nodiscard]] std::vector<std::byte> readbackBytes(const ReadbackTicket& ticket, const vk::raii::Semaphore& timelineSemaphore)
    {
        waitTimelineValue(timelineSemaphore, ticket.signalValue);
        readbackRing_.invalidate(ticket.offset, ticket.size);

        std::vector<std::byte> data(static_cast<size_t>(ticket.size));
        auto* src = static_cast<const std::byte*>(readbackRing_.mapped()) + ticket.offset;
        std::memcpy(data.data(), src, data.size());
        return data;
    }

  private:
    struct RingAllocation
    {
        uint64_t begin = 0;
        uint64_t end = 0;
        vk::DeviceSize offset = 0;
    };

    struct InFlightBatch
    {
        uint64_t begin = 0;
        uint64_t end = 0;
        uint64_t signalValue = 0;
        std::optional<vk::raii::CommandBuffers> commandBuffers{};
    };

    [[nodiscard]] static uint64_t alignUp(uint64_t value, uint64_t alignment)
    {
        if (alignment <= 1)
            return value;
        nrAssert((alignment & (alignment - 1)) == 0, std::format("UploadReadbackContext::alignUp requires power-of-2 alignment, got {}", alignment));
        return (value + alignment - 1) & ~(alignment - 1);
    }

    [[nodiscard]] static vk::DeviceSize bytesPerPixel(vk::Format format)
    {
        switch (format)
        {
        case vk::Format::eR8Unorm:
        case vk::Format::eR8Snorm:
        case vk::Format::eR8Uint:
        case vk::Format::eR8Sint:
            return 1;
        case vk::Format::eR8G8Unorm:
        case vk::Format::eR8G8Snorm:
        case vk::Format::eR8G8Uint:
        case vk::Format::eR8G8Sint:
        case vk::Format::eR16Sfloat:
        case vk::Format::eR16Uint:
        case vk::Format::eR16Sint:
            return 2;
        case vk::Format::eR8G8B8A8Unorm:
        case vk::Format::eR8G8B8A8Srgb:
        case vk::Format::eB8G8R8A8Unorm:
        case vk::Format::eB8G8R8A8Srgb:
        case vk::Format::eR16G16Sfloat:
        case vk::Format::eR16G16Uint:
        case vk::Format::eR16G16Sint:
        case vk::Format::eR32Sfloat:
        case vk::Format::eR32Uint:
        case vk::Format::eR32Sint:
            return 4;
        case vk::Format::eR16G16B16A16Sfloat:
        case vk::Format::eR16G16B16A16Uint:
        case vk::Format::eR16G16B16A16Sint:
        case vk::Format::eR32G32Sfloat:
        case vk::Format::eR32G32Uint:
        case vk::Format::eR32G32Sint:
            return 8;
        case vk::Format::eR32G32B32A32Sfloat:
        case vk::Format::eR32G32B32A32Uint:
        case vk::Format::eR32G32B32A32Sint:
            return 16;
        default:
            nrAssert(false, std::format("UploadReadbackContext::readbackImage unsupported format for readback size estimation: {}", vk::to_string(format)));
            return 0;
        }
    }

    [[nodiscard]] uint64_t queryTimelineValue(const vk::raii::Semaphore& timelineSemaphore) const
    {
        return sync::timelineValue(timelineSemaphore);
    }

    void waitTimelineValue(const vk::raii::Semaphore& timelineSemaphore, uint64_t targetValue) const
    {
        auto ok = sync::waitTimeline(device_->get(), timelineSemaphore, targetValue);
        nrAssert(ok, "UploadReadbackContext::waitTimelineValue failed while waiting timeline semaphore.");
    }

    static void reclaimQueue(std::deque<InFlightBatch>& queue, uint64_t& reclaimCursor, uint64_t completedValue)
    {
        while (!queue.empty() && queue.front().signalValue <= completedValue)
        {
            reclaimCursor = queue.front().end;
            queue.pop_front();
        }
    }

    [[nodiscard]] RingAllocation reserveUpload(vk::DeviceSize size, const vk::raii::Semaphore& timelineSemaphore)
    {
        return reserveRing(size, uploadCapacity_, uploadWriteCursor_, uploadReclaimCursor_, uploadInFlight_, timelineSemaphore);
    }

    [[nodiscard]] RingAllocation reserveReadback(vk::DeviceSize size, const vk::raii::Semaphore& timelineSemaphore)
    {
        return reserveRing(size, readbackCapacity_, readbackWriteCursor_, readbackReclaimCursor_, readbackInFlight_, timelineSemaphore);
    }

    [[nodiscard]] RingAllocation reserveRing(
        vk::DeviceSize size,
        vk::DeviceSize capacity,
        uint64_t& writeCursor,
        uint64_t& reclaimCursor,
        std::deque<InFlightBatch>& queue,
        const vk::raii::Semaphore& timelineSemaphore)
    {
        nrAssert(size <= capacity, "UploadReadbackContext ring allocation exceeds ring capacity.");

        reclaimCompleted(timelineSemaphore);

        auto tryReserve = [&]() -> std::optional<RingAllocation> {
            constexpr uint64_t alignment = 16;
            uint64_t candidate = alignUp(writeCursor, alignment);
            uint64_t cap = static_cast<uint64_t>(capacity);

            auto ringOffset = static_cast<vk::DeviceSize>(candidate % cap);
            if (ringOffset + size > capacity)
            {
                candidate = alignUp(candidate + (cap - ringOffset), alignment);
                ringOffset = 0;
            }

            uint64_t end = candidate + static_cast<uint64_t>(size);
            if (end - reclaimCursor > cap)
            {
                return std::nullopt;
            }
            return RingAllocation{candidate, end, ringOffset};
        };

        if (auto allocation = tryReserve())
        {
            writeCursor = allocation->end;
            return *allocation;
        }

        while (!queue.empty())
        {
            waitTimelineValue(timelineSemaphore, queue.front().signalValue);
            reclaimCompleted(timelineSemaphore);
            if (auto allocation = tryReserve())
            {
                writeCursor = allocation->end;
                return *allocation;
            }
        }

        nrAssert(false, "UploadReadbackContext failed to reserve ring allocation.");
        return RingAllocation{};
    }

    static void addInFlight(std::deque<InFlightBatch>& queue, const RingAllocation& allocation, uint64_t signalValue, vk::raii::CommandBuffers commandBuffers)
    {
        if (!queue.empty())
        {
            nrAssert(signalValue > queue.back().signalValue, "Timeline signal values must be strictly increasing.");
        }
        queue.push_back(InFlightBatch{
            allocation.begin,
            allocation.end,
            signalValue,
            std::optional<vk::raii::CommandBuffers>{std::move(commandBuffers)},
        });
    }

    std::optional<std::reference_wrapper<const vk::raii::Device>> device_;
    std::optional<std::reference_wrapper<QueueManager>> queueManager_;

    Buffer uploadRing_;
    Buffer readbackRing_;
    vk::DeviceSize uploadCapacity_ = 0;
    vk::DeviceSize readbackCapacity_ = 0;

    CommandPool transferPool_;

    uint64_t uploadWriteCursor_ = 0;
    uint64_t uploadReclaimCursor_ = 0;
    uint64_t readbackWriteCursor_ = 0;
    uint64_t readbackReclaimCursor_ = 0;

    std::deque<InFlightBatch> uploadInFlight_;
    std::deque<InFlightBatch> readbackInFlight_;
};

} // namespace nr::rhi::ops
