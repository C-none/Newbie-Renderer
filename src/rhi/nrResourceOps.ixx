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

/**
 * @brief Compile-time ownership barrier phase selector for queue-ownership barriers.
 */
enum class OwnershipBarrierPhase
{
    Release,
    Acquire,
};

namespace detail
{
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
 * @tparam TOwnershipPhase Compile-time phase selector: `Release` writes source-side
 *         scopes and pins the ignored destination side to `eBottomOfPipe`; `Acquire`
 *         writes destination-side scopes and pins the ignored source side to
 *         `eTopOfPipe`.
 * @tparam TResourceKind Resource category (`Buffer` or `Image`) that determines barrier type/layout fields.
 */
template <OwnershipBarrierPhase TOwnershipPhase, QueueOwnershipResource TResourceKind>
[[nodiscard]] inline auto makeQueueOwnershipBarrier(const TResourceKind& resource, const QueueOwnershipBarrierConfig& config)
{
    constexpr bool kIsRelease = TOwnershipPhase == OwnershipBarrierPhase::Release;
    constexpr auto kAcquireBoundaryStage = vk::PipelineStageFlagBits2::eTopOfPipe;
    constexpr auto kReleaseBoundaryStage = vk::PipelineStageFlagBits2::eBottomOfPipe;

    if constexpr (std::same_as<std::remove_cvref_t<TResourceKind>, Buffer>)
    {
        return vk::BufferMemoryBarrier2{
            kIsRelease ? config.stages : kAcquireBoundaryStage,
            kIsRelease ? config.access : vk::AccessFlags2{},
            kIsRelease ? kReleaseBoundaryStage : config.stages,
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
            kIsRelease ? config.stages : kAcquireBoundaryStage,
            kIsRelease ? config.access : vk::AccessFlags2{},
            kIsRelease ? kReleaseBoundaryStage : config.stages,
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
    const Buffer& buffer, vk::PipelineStageFlags2 dstStages = vk::PipelineStageFlagBits2::eMeshShaderEXT |
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
 * @brief Generic image layout transition helper.
 */
[[nodiscard]] inline vk::ImageMemoryBarrier2 makeImageLayoutTransitionBarrier(
    const Image& image,
    vk::ImageLayout oldLayout,
    vk::ImageLayout newLayout,
    vk::PipelineStageFlags2 srcStages,
    vk::AccessFlags2 srcAccess,
    vk::PipelineStageFlags2 dstStages,
    vk::AccessFlags2 dstAccess)
{
    return makeImageBarrier(image, vk::ImageMemoryBarrier2{
        srcStages,
        srcAccess,
        dstStages,
        dstAccess,
        oldLayout,
        newLayout,
        kIgnoredQueueFamilyIndex,
        kIgnoredQueueFamilyIndex,
        vk::Image{},
        {},
        nullptr,
    });
}

/**
 * @brief Compile-time branch selector for common image-transition destinations.
 */
enum class ImageTransitionBranch
{
    TransferSrc,
    TransferDst,
    ShaderReadOnly,
    General,
    ColorAttachment,
    DepthStencilAttachment,
    PresentSrc,
};

namespace detail
{
template <auto TValue>
inline constexpr bool kAlwaysFalse = false;

template <ImageTransitionBranch TBranch>
[[nodiscard]] consteval vk::ImageLayout imageTransitionDstLayout()
{
    if constexpr (TBranch == ImageTransitionBranch::TransferSrc)
    {
        return vk::ImageLayout::eTransferSrcOptimal;
    }
    else if constexpr (TBranch == ImageTransitionBranch::TransferDst)
    {
        return vk::ImageLayout::eTransferDstOptimal;
    }
    else if constexpr (TBranch == ImageTransitionBranch::ShaderReadOnly)
    {
        return vk::ImageLayout::eShaderReadOnlyOptimal;
    }
    else if constexpr (TBranch == ImageTransitionBranch::General)
    {
        return vk::ImageLayout::eGeneral;
    }
    else if constexpr (TBranch == ImageTransitionBranch::ColorAttachment)
    {
        return vk::ImageLayout::eColorAttachmentOptimal;
    }
    else if constexpr (TBranch == ImageTransitionBranch::DepthStencilAttachment)
    {
        return vk::ImageLayout::eDepthStencilAttachmentOptimal;
    }
    else if constexpr (TBranch == ImageTransitionBranch::PresentSrc)
    {
        return vk::ImageLayout::ePresentSrcKHR;
    }
    else
    {
        static_assert(kAlwaysFalse<TBranch>, "Unsupported image transition branch.");
    }
}

template <ImageTransitionBranch TBranch>
[[nodiscard]] consteval vk::PipelineStageFlags2 imageTransitionDefaultDstStages()
{
    if constexpr (TBranch == ImageTransitionBranch::TransferSrc ||
                  TBranch == ImageTransitionBranch::TransferDst)
    {
        return vk::PipelineStageFlagBits2::eTransfer;
    }
    else if constexpr (TBranch == ImageTransitionBranch::ShaderReadOnly)
    {
        return vk::PipelineStageFlagBits2::eFragmentShader |
               vk::PipelineStageFlagBits2::eComputeShader;
    }
    else if constexpr (TBranch == ImageTransitionBranch::General)
    {
        return vk::PipelineStageFlagBits2::eAllCommands;
    }
    else if constexpr (TBranch == ImageTransitionBranch::ColorAttachment)
    {
        return vk::PipelineStageFlagBits2::eColorAttachmentOutput;
    }
    else if constexpr (TBranch == ImageTransitionBranch::DepthStencilAttachment)
    {
        return vk::PipelineStageFlagBits2::eEarlyFragmentTests |
               vk::PipelineStageFlagBits2::eLateFragmentTests;
    }
    else if constexpr (TBranch == ImageTransitionBranch::PresentSrc)
    {
        return vk::PipelineStageFlagBits2::eBottomOfPipe;
    }
    else
    {
        static_assert(kAlwaysFalse<TBranch>, "Unsupported image transition branch.");
    }
}

template <ImageTransitionBranch TBranch>
[[nodiscard]] consteval vk::AccessFlags2 imageTransitionDefaultDstAccess()
{
    if constexpr (TBranch == ImageTransitionBranch::TransferSrc)
    {
        return vk::AccessFlagBits2::eTransferRead;
    }
    else if constexpr (TBranch == ImageTransitionBranch::TransferDst)
    {
        return vk::AccessFlagBits2::eTransferWrite;
    }
    else if constexpr (TBranch == ImageTransitionBranch::ShaderReadOnly)
    {
        return vk::AccessFlagBits2::eShaderRead;
    }
    else if constexpr (TBranch == ImageTransitionBranch::General)
    {
        return vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;
    }
    else if constexpr (TBranch == ImageTransitionBranch::ColorAttachment)
    {
        return vk::AccessFlagBits2::eColorAttachmentRead |
               vk::AccessFlagBits2::eColorAttachmentWrite;
    }
    else if constexpr (TBranch == ImageTransitionBranch::DepthStencilAttachment)
    {
        return vk::AccessFlagBits2::eDepthStencilAttachmentRead |
               vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
    }
    else if constexpr (TBranch == ImageTransitionBranch::PresentSrc)
    {
        return {};
    }
    else
    {
        static_assert(kAlwaysFalse<TBranch>, "Unsupported image transition branch.");
    }
}

} // namespace detail

/**
 * @brief Template image transition helper selected by compile-time branch enum.
 */
template <ImageTransitionBranch TBranch>
[[nodiscard]] inline vk::ImageMemoryBarrier2 makeImageTransitionBarrier(
    const Image& image,
    vk::ImageLayout oldLayout,
    vk::PipelineStageFlags2 srcStages = vk::PipelineStageFlagBits2::eAllCommands,
    vk::AccessFlags2 srcAccess = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
    vk::PipelineStageFlags2 dstStages = detail::imageTransitionDefaultDstStages<TBranch>(),
    vk::AccessFlags2 dstAccess = detail::imageTransitionDefaultDstAccess<TBranch>())
{
    return makeImageLayoutTransitionBarrier(
        image,
        oldLayout,
        detail::imageTransitionDstLayout<TBranch>(),
        srcStages,
        srcAccess,
        dstStages,
        dstAccess);
}

/**
 * @brief Generic image transition helper to transfer-src layout.
 */
[[nodiscard]] inline vk::ImageMemoryBarrier2 makeImageToTransferSrcBarrier(
    const Image& image,
    vk::ImageLayout oldLayout,
    vk::PipelineStageFlags2 srcStages = vk::PipelineStageFlagBits2::eAllCommands,
    vk::AccessFlags2 srcAccess = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite)
{
    return makeImageTransitionBarrier<ImageTransitionBranch::TransferSrc>(
        image,
        oldLayout,
        srcStages,
        srcAccess);
}

/**
 * @brief Generic image transition helper to transfer-dst layout.
 */
[[nodiscard]] inline vk::ImageMemoryBarrier2 makeImageToTransferDstBarrier(
    const Image& image,
    vk::ImageLayout oldLayout,
    vk::PipelineStageFlags2 srcStages = vk::PipelineStageFlagBits2::eAllCommands,
    vk::AccessFlags2 srcAccess = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite)
{
    return makeImageTransitionBarrier<ImageTransitionBranch::TransferDst>(
        image,
        oldLayout,
        srcStages,
        srcAccess);
}

/**
 * @brief Generic image transition helper to shader-read-only layout.
 */
[[nodiscard]] inline vk::ImageMemoryBarrier2 makeImageToShaderReadOnlyBarrier(
    const Image& image,
    vk::ImageLayout oldLayout,
    vk::PipelineStageFlags2 srcStages = vk::PipelineStageFlagBits2::eAllCommands,
    vk::AccessFlags2 srcAccess = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
    vk::PipelineStageFlags2 dstStages = vk::PipelineStageFlagBits2::eFragmentShader |
                                         vk::PipelineStageFlagBits2::eComputeShader)
{
    return makeImageTransitionBarrier<ImageTransitionBranch::ShaderReadOnly>(
        image,
        oldLayout,
        srcStages,
        srcAccess,
        dstStages,
        detail::imageTransitionDefaultDstAccess<ImageTransitionBranch::ShaderReadOnly>());
}

/**
 * @brief Generic image transition helper to general layout.
 */
[[nodiscard]] inline vk::ImageMemoryBarrier2 makeImageToGeneralBarrier(
    const Image& image,
    vk::ImageLayout oldLayout,
    vk::PipelineStageFlags2 srcStages = vk::PipelineStageFlagBits2::eAllCommands,
    vk::AccessFlags2 srcAccess = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
    vk::PipelineStageFlags2 dstStages = vk::PipelineStageFlagBits2::eAllCommands,
    vk::AccessFlags2 dstAccess = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite)
{
    return makeImageTransitionBarrier<ImageTransitionBranch::General>(
        image,
        oldLayout,
        srcStages,
        srcAccess,
        dstStages,
        dstAccess);
}

/**
 * @brief Generic image transition helper to color-attachment layout.
 */
[[nodiscard]] inline vk::ImageMemoryBarrier2 makeImageToColorAttachmentBarrier(
    const Image& image,
    vk::ImageLayout oldLayout,
    vk::PipelineStageFlags2 srcStages = vk::PipelineStageFlagBits2::eAllCommands,
    vk::AccessFlags2 srcAccess = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite)
{
    return makeImageTransitionBarrier<ImageTransitionBranch::ColorAttachment>(
        image,
        oldLayout,
        srcStages,
        srcAccess);
}

/**
 * @brief Generic image transition helper to depth-stencil-attachment layout.
 */
[[nodiscard]] inline vk::ImageMemoryBarrier2 makeImageToDepthStencilAttachmentBarrier(
    const Image& image,
    vk::ImageLayout oldLayout,
    vk::PipelineStageFlags2 srcStages = vk::PipelineStageFlagBits2::eAllCommands,
    vk::AccessFlags2 srcAccess = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite)
{
    return makeImageTransitionBarrier<ImageTransitionBranch::DepthStencilAttachment>(
        image,
        oldLayout,
        srcStages,
        srcAccess);
}

/**
 * @brief Generic image transition helper to present-src layout.
 */
[[nodiscard]] inline vk::ImageMemoryBarrier2 makeImageToPresentSrcBarrier(
    const Image& image,
    vk::ImageLayout oldLayout,
    vk::PipelineStageFlags2 srcStages = vk::PipelineStageFlagBits2::eAllCommands,
    vk::AccessFlags2 srcAccess = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite)
{
    return makeImageTransitionBarrier<ImageTransitionBranch::PresentSrc>(
        image,
        oldLayout,
        srcStages,
        srcAccess);
}

/**
 * @brief Common factory: undefined image -> transfer-dst image.
 */
[[nodiscard]] inline vk::ImageMemoryBarrier2 makeImageUndefinedToTransferDstBarrier(const Image& image)
{
    return makeImageToTransferDstBarrier(
        image,
        vk::ImageLayout::eUndefined,
        vk::PipelineStageFlagBits2::eTopOfPipe,
        {});
}

/**
 * @brief Common factory: transfer-dst image -> shader-read image.
 */
[[nodiscard]] inline vk::ImageMemoryBarrier2 makeImageTransferDstToShaderReadBarrier(
    const Image& image,
    vk::PipelineStageFlags2 dstStages = vk::PipelineStageFlagBits2::eFragmentShader |
                                         vk::PipelineStageFlagBits2::eComputeShader)
{
    return makeImageToShaderReadOnlyBarrier(
        image,
        vk::ImageLayout::eTransferDstOptimal,
        vk::PipelineStageFlagBits2::eTransfer,
        vk::AccessFlagBits2::eTransferWrite,
        dstStages);
}

/**
 * @brief Common factory: color-attachment image -> present image.
 */
[[nodiscard]] inline vk::ImageMemoryBarrier2 makeImageColorAttachmentToPresentBarrier(const Image& image)
{
    return makeImageToPresentSrcBarrier(
        image,
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eColorAttachmentWrite);
}

/**
 * @brief Queue-family ownership request used on either the release side or the acquire side.
 *
 * The same type is intentionally reused for both phases. When the request is
 * named `release`, `stages/access` describe the last access on the source queue.
 * When it is named `acquire`, `stages/access` describe the first access on the
 * destination queue.
 */
struct QueueOwnershipRequest
{
    uint32_t srcQueueFamilyIndex = kIgnoredQueueFamilyIndex;
    uint32_t dstQueueFamilyIndex = kIgnoredQueueFamilyIndex;
    vk::PipelineStageFlags2 stages = vk::PipelineStageFlagBits2::eAllCommands;
    vk::AccessFlags2 access = vk::AccessFlagBits2::eMemoryWrite;

    [[nodiscard]] bool valid() const noexcept
    {
        return srcQueueFamilyIndex != kIgnoredQueueFamilyIndex &&
               dstQueueFamilyIndex != kIgnoredQueueFamilyIndex &&
               srcQueueFamilyIndex != dstQueueFamilyIndex;
    }
};

/**
 * @brief Stage/access scope used to construct ownership requests.
 */
struct QueueAccessScope
{
    vk::PipelineStageFlags2 stages = vk::PipelineStageFlagBits2::eAllCommands;
    vk::AccessFlags2 access = vk::AccessFlagBits2::eMemoryRead;

    [[nodiscard]] bool valid() const noexcept
    {
        return stages != vk::PipelineStageFlags2{};
    }
};

/**
 * @brief Optional semaphore wait payload used by ownership helpers.
 */
struct QueueOwnershipWait
{
    vk::Semaphore semaphore = vk::Semaphore{};
    uint64_t value = 0;

    [[nodiscard]] bool valid() const noexcept
    {
        return semaphore != vk::Semaphore{};
    }
};

[[nodiscard]] inline QueueOwnershipRequest makeQueueOwnershipRequest(
    uint32_t srcQueueFamilyIndex,
    uint32_t dstQueueFamilyIndex,
    const QueueAccessScope& scope)
{
    nrAssert(scope.valid(), "makeQueueOwnershipRequest requires non-empty stage mask.");
    return QueueOwnershipRequest{
        .srcQueueFamilyIndex = srcQueueFamilyIndex,
        .dstQueueFamilyIndex = dstQueueFamilyIndex,
        .stages = scope.stages,
        .access = scope.access,
    };
}

/**
 * @brief Full queue-family hand-off plan: release, optional semaphore wait, and acquire.
 *
 * The release and acquire descriptions must mirror the same queue-family pair.
 * The semaphore wait is optional for generic use, but upload paths require it
 * whenever ownership is acquired into the transfer queue from another queue.
 */
struct QueueOwnershipTransfer
{
    QueueOwnershipRequest release{};
    QueueOwnershipRequest acquire{
        .stages = vk::PipelineStageFlagBits2::eAllCommands,
        .access = vk::AccessFlagBits2::eMemoryRead,
    };
    vk::Semaphore waitSemaphore{};
    uint64_t waitValue = 0;

    [[nodiscard]] bool valid() const noexcept
    {
        return release.valid() &&
               acquire.valid() &&
               release.srcQueueFamilyIndex == acquire.srcQueueFamilyIndex &&
               release.dstQueueFamilyIndex == acquire.dstQueueFamilyIndex;
    }

    [[nodiscard]] bool hasWait() const noexcept
    {
        return waitSemaphore != vk::Semaphore{};
    }
};

[[nodiscard]] inline QueueOwnershipTransfer makeQueueOwnershipTransfer(
    uint32_t srcQueueFamilyIndex,
    uint32_t dstQueueFamilyIndex,
    const QueueAccessScope& releaseScope,
    const QueueAccessScope& acquireScope,
    std::optional<QueueOwnershipWait> wait = std::nullopt)
{
    auto transfer = QueueOwnershipTransfer{
        .release = makeQueueOwnershipRequest(srcQueueFamilyIndex, dstQueueFamilyIndex, releaseScope),
        .acquire = makeQueueOwnershipRequest(srcQueueFamilyIndex, dstQueueFamilyIndex, acquireScope),
    };

    if (wait.has_value())
    {
        nrAssert(wait->valid(), "makeQueueOwnershipTransfer requires a valid wait semaphore when wait payload is provided.");
        transfer.waitSemaphore = wait->semaphore;
        transfer.waitValue = wait->value;
    }

    return transfer;
}

/**
 * @brief Ownership plan for uploads that may need an incoming acquire to transfer
 *        and always end with a release from transfer to the destination queue.
 */
struct BufferUploadOwnershipPlan
{
    std::optional<QueueOwnershipTransfer> acquireToTransfer;
    QueueOwnershipTransfer releaseToDestination{};

    /**
     * @brief Check if this plan is for same-queue-family upload (no ownership transfer needed).
     */
    [[nodiscard]] bool isSameQueueFamily() const noexcept
    {
        return !acquireToTransfer.has_value() &&
               releaseToDestination.release.srcQueueFamilyIndex ==
                   releaseToDestination.release.dstQueueFamilyIndex;
    }

    [[nodiscard]] bool valid(uint32_t transferQueueFamilyIndex) const noexcept
    {
        // Same-queue-family plans have a different validation path
        if (isSameQueueFamily())
        {
            return releaseToDestination.release.srcQueueFamilyIndex == transferQueueFamilyIndex &&
                   releaseToDestination.release.stages != vk::PipelineStageFlags2{} &&
                   releaseToDestination.acquire.stages != vk::PipelineStageFlags2{};
        }

        auto outgoingValid =
            releaseToDestination.valid() &&
            releaseToDestination.release.srcQueueFamilyIndex == transferQueueFamilyIndex &&
            releaseToDestination.acquire.srcQueueFamilyIndex == transferQueueFamilyIndex;

        auto incomingValid =
            !acquireToTransfer.has_value() ||
            (acquireToTransfer->valid() &&
             acquireToTransfer->hasWait() &&
             acquireToTransfer->release.dstQueueFamilyIndex == transferQueueFamilyIndex &&
             acquireToTransfer->acquire.dstQueueFamilyIndex == transferQueueFamilyIndex);

        return outgoingValid && incomingValid;
    }
};

/**
 * @brief Helper bundle for upload ownership setup across source/transfer/destination queues.
 *
 * `sourceReleaseToTransfer` must be recorded by the source queue command buffer
 * before invoking upload when source queue family differs from transfer.
 */
struct UploadQueueOwnershipBundle
{
    std::optional<QueueOwnershipRequest> sourceReleaseToTransfer;
    BufferUploadOwnershipPlan uploadPlan{};
};

/**
 * @brief Build a consistent upload ownership bundle and remove hand-written duplication.
 *
 * - Always emits transfer -> destination ownership transfer in `uploadPlan.releaseToDestination`.
 * - Optionally emits source -> transfer ownership transfer with wait payload.
 * - When source queue family equals transfer queue family, incoming transfer acquire is omitted.
 */
[[nodiscard]] inline UploadQueueOwnershipBundle makeUploadQueueOwnershipBundle(
    uint32_t sourceQueueFamilyIndex,
    uint32_t transferQueueFamilyIndex,
    uint32_t destinationQueueFamilyIndex,
    const QueueAccessScope& sourceReleaseScope,
    const QueueAccessScope& destinationAcquireScope,
    std::optional<QueueOwnershipWait> sourceReleaseWait = std::nullopt,
    const QueueAccessScope& transferWriteScope = QueueAccessScope{
        .stages = vk::PipelineStageFlagBits2::eTransfer,
        .access = vk::AccessFlagBits2::eTransferWrite,
    })
{
    nrAssert(
        destinationQueueFamilyIndex != transferQueueFamilyIndex,
        "makeUploadQueueOwnershipBundle requires destination queue family different from transfer queue family.");

    UploadQueueOwnershipBundle bundle{};
    bundle.uploadPlan.releaseToDestination = makeQueueOwnershipTransfer(
        transferQueueFamilyIndex,
        destinationQueueFamilyIndex,
        transferWriteScope,
        destinationAcquireScope);

    if (sourceQueueFamilyIndex == transferQueueFamilyIndex)
    {
        return bundle;
    }

    nrAssert(
        sourceReleaseWait.has_value() && sourceReleaseWait->valid(),
        "makeUploadQueueOwnershipBundle requires a valid sourceReleaseWait when source queue family differs from transfer queue family.");

    auto sourceToTransfer = makeQueueOwnershipTransfer(
        sourceQueueFamilyIndex,
        transferQueueFamilyIndex,
        sourceReleaseScope,
        transferWriteScope,
        sourceReleaseWait);

    bundle.sourceReleaseToTransfer = sourceToTransfer.release;
    bundle.uploadPlan.acquireToTransfer = sourceToTransfer;
    return bundle;
}

/**
 * @brief Generic buffer queue-ownership barrier (sync2).
 *
 * `TOwnershipPhase = OwnershipBarrierPhase::Release` emits source-side
 * stage/access scopes and pins destination side to `eBottomOfPipe`.
 * `TOwnershipPhase = OwnershipBarrierPhase::Acquire` emits destination-side
 * stage/access scopes and pins source side to `eTopOfPipe`.
 */
template <OwnershipBarrierPhase TOwnershipPhase>
[[nodiscard]] inline vk::BufferMemoryBarrier2 makeBufferOwnershipBarrier(
    const Buffer& buffer,
    const QueueOwnershipRequest& request,
    vk::DeviceSize offset = 0,
    vk::DeviceSize size = std::numeric_limits<vk::DeviceSize>::max())
{
    nrAssert(request.valid(), "makeBufferOwnershipBarrier requires valid queue-family indices.");
    return detail::makeQueueOwnershipBarrier<TOwnershipPhase>(
        buffer,
        detail::QueueOwnershipBarrierConfig{
            .srcQueueFamilyIndex = request.srcQueueFamilyIndex,
            .dstQueueFamilyIndex = request.dstQueueFamilyIndex,
            .stages = request.stages,
            .access = request.access,
            .offset = offset,
            .size = size,
        });
}

/**
 * @brief Generic image queue-ownership barrier (sync2).
 */
template <OwnershipBarrierPhase TOwnershipPhase>
[[nodiscard]] inline vk::ImageMemoryBarrier2 makeImageOwnershipBarrier(
    const Image& image,
    vk::ImageLayout oldLayout,
    vk::ImageLayout newLayout,
    const QueueOwnershipRequest& request)
{
    nrAssert(request.valid(), "makeImageOwnershipBarrier requires valid queue-family indices.");
    return detail::makeQueueOwnershipBarrier<TOwnershipPhase>(
        image,
        detail::QueueOwnershipBarrierConfig{
            .srcQueueFamilyIndex = request.srcQueueFamilyIndex,
            .dstQueueFamilyIndex = request.dstQueueFamilyIndex,
            .stages = request.stages,
            .access = request.access,
            .oldLayout = oldLayout,
            .newLayout = newLayout,
        });
}

namespace detail
{
template <OwnershipBarrierPhase TOwnershipPhase>
[[nodiscard]] inline const QueueOwnershipRequest& selectOwnershipRequest(const QueueOwnershipTransfer& transfer)
{
    if constexpr (TOwnershipPhase == OwnershipBarrierPhase::Release)
    {
        return transfer.release;
    }
    else
    {
        return transfer.acquire;
    }
}
} // namespace detail

/**
 * @brief Bridge QueueOwnershipTransfer to the generic buffer ownership-barrier API.
 */
template <OwnershipBarrierPhase TOwnershipPhase>
[[nodiscard]] inline vk::BufferMemoryBarrier2 makeBufferOwnershipTransferBarrier(
    const Buffer& buffer,
    const QueueOwnershipTransfer& transfer,
    vk::DeviceSize offset = 0,
    vk::DeviceSize size = std::numeric_limits<vk::DeviceSize>::max())
{
    nrAssert(transfer.valid(), "makeBufferOwnershipTransferBarrier requires a valid QueueOwnershipTransfer.");
    return makeBufferOwnershipBarrier<TOwnershipPhase>(
        buffer,
        detail::selectOwnershipRequest<TOwnershipPhase>(transfer),
        offset,
        size);
}

/**
 * @brief Bridge QueueOwnershipTransfer to the generic image ownership-barrier API.
 */
template <OwnershipBarrierPhase TOwnershipPhase>
[[nodiscard]] inline vk::ImageMemoryBarrier2 makeImageOwnershipTransferBarrier(
    const Image& image,
    vk::ImageLayout oldLayout,
    vk::ImageLayout newLayout,
    const QueueOwnershipTransfer& transfer)
{
    nrAssert(transfer.valid(), "makeImageOwnershipTransferBarrier requires a valid QueueOwnershipTransfer.");
    return makeImageOwnershipBarrier<TOwnershipPhase>(
        image,
        oldLayout,
        newLayout,
        detail::selectOwnershipRequest<TOwnershipPhase>(transfer));
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

[[nodiscard]] inline vk::BufferImageCopy normalizeBufferImageCopyRegion(const Image& image, vk::BufferImageCopy region)
{
    if (region.imageSubresource.aspectMask == vk::ImageAspectFlags{})
    {
        region.imageSubresource = vk::ImageSubresourceLayers{inferAspectFlags(image.format()), 0, 0, 1};
    }
    if (region.imageExtent == vk::Extent3D{})
    {
        region.imageExtent = image.extent();
    }
    return region;
}

[[nodiscard]] inline vk::DeviceSize linearBufferImageCopySize(const vk::BufferImageCopy& region, vk::DeviceSize elementSize)
{
    const auto rowLength = region.bufferRowLength > 0 ? region.bufferRowLength : region.imageExtent.width;
    const auto imageHeight = region.bufferImageHeight > 0 ? region.bufferImageHeight : region.imageExtent.height;
    const auto layerCount = std::max(1u, region.imageSubresource.layerCount);

    return std::max<vk::DeviceSize>(
        1,
        static_cast<vk::DeviceSize>(rowLength) *
            static_cast<vk::DeviceSize>(imageHeight) *
            static_cast<vk::DeviceSize>(region.imageExtent.depth) *
            static_cast<vk::DeviceSize>(layerCount) *
            elementSize);
}

/**
 * @brief Copy buffer data into an image.
 */
inline void copyBufferToImage(vk::CommandBuffer commandBuffer, const Buffer& src, const Image& dst, vk::ImageLayout dstLayout = vk::ImageLayout::eTransferDstOptimal, const vk::BufferImageCopy& region = {})
{
    auto effectiveRegion = normalizeBufferImageCopyRegion(dst, region);
    commandBuffer.copyBufferToImage(src.handle(), dst.handle(), dstLayout, {effectiveRegion});
}

/**
 * @brief Copy image data into a buffer.
 */
inline void copyImageToBuffer(vk::CommandBuffer commandBuffer, const Image& src, const Buffer& dst, vk::ImageLayout srcLayout = vk::ImageLayout::eTransferSrcOptimal, const vk::BufferImageCopy& region = {})
{
    auto effectiveRegion = normalizeBufferImageCopyRegion(src, region);
    commandBuffer.copyImageToBuffer(src.handle(), srcLayout, dst.handle(), {effectiveRegion});
}

[[nodiscard]] inline vk::ImageCopy normalizeImageCopyRegion(const Image& src, const Image& dst, vk::ImageCopy region)
{
    if (region.srcSubresource.aspectMask == vk::ImageAspectFlags{})
    {
        region.srcSubresource = vk::ImageSubresourceLayers{inferAspectFlags(src.format()), 0, 0, 1};
    }
    if (region.dstSubresource.aspectMask == vk::ImageAspectFlags{})
    {
        region.dstSubresource = vk::ImageSubresourceLayers{inferAspectFlags(dst.format()), 0, 0, 1};
    }
    if (region.extent == vk::Extent3D{})
    {
        auto srcExtent = src.extent();
        auto dstExtent = dst.extent();
        region.extent = vk::Extent3D{
            std::min(srcExtent.width, dstExtent.width),
            std::min(srcExtent.height, dstExtent.height),
            std::min(srcExtent.depth, dstExtent.depth),
        };
    }
    return region;
}

[[nodiscard]] inline vk::ImageCopy normalizeImageCopyRegion(
    vk::Extent3D srcExtent,
    vk::ImageAspectFlags srcAspect,
    vk::Extent3D dstExtent,
    vk::ImageAspectFlags dstAspect,
    vk::ImageCopy region)
{
    auto effectiveSrcAspect = srcAspect == vk::ImageAspectFlags{} ? vk::ImageAspectFlagBits::eColor : srcAspect;
    auto effectiveDstAspect = dstAspect == vk::ImageAspectFlags{} ? vk::ImageAspectFlagBits::eColor : dstAspect;

    if (region.srcSubresource.aspectMask == vk::ImageAspectFlags{})
    {
        region.srcSubresource = vk::ImageSubresourceLayers{effectiveSrcAspect, 0, 0, 1};
    }
    if (region.dstSubresource.aspectMask == vk::ImageAspectFlags{})
    {
        region.dstSubresource = vk::ImageSubresourceLayers{effectiveDstAspect, 0, 0, 1};
    }
    if (region.extent == vk::Extent3D{})
    {
        region.extent = vk::Extent3D{
            std::min(srcExtent.width, dstExtent.width),
            std::min(srcExtent.height, dstExtent.height),
            std::min(srcExtent.depth, dstExtent.depth),
        };
    }

    return region;
}

/**
 * @brief Copy image data from one image to another.
 */
inline void copyImageToImage(
    vk::CommandBuffer commandBuffer,
    const Image& src,
    const Image& dst,
    vk::ImageLayout srcLayout = vk::ImageLayout::eTransferSrcOptimal,
    vk::ImageLayout dstLayout = vk::ImageLayout::eTransferDstOptimal,
    const vk::ImageCopy& region = {})
{
    auto effectiveRegion = normalizeImageCopyRegion(src, dst, region);
    commandBuffer.copyImage(src.handle(), srcLayout, dst.handle(), dstLayout, {effectiveRegion});
}

inline void copyImageToImage(
    vk::CommandBuffer commandBuffer,
    vk::Image src,
    vk::Extent3D srcExtent,
    vk::ImageAspectFlags srcAspect,
    vk::Image dst,
    vk::Extent3D dstExtent,
    vk::ImageAspectFlags dstAspect,
    vk::ImageLayout srcLayout = vk::ImageLayout::eTransferSrcOptimal,
    vk::ImageLayout dstLayout = vk::ImageLayout::eTransferDstOptimal,
    const vk::ImageCopy& region = {})
{
    auto effectiveRegion = normalizeImageCopyRegion(srcExtent, srcAspect, dstExtent, dstAspect, region);
    commandBuffer.copyImage(src, srcLayout, dst, dstLayout, {effectiveRegion});
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
    ScopedRendering(const vk::raii::CommandBuffer& commandBuffer, const RenderingScopeDesc& desc)
        : commandBuffer_(std::cref(commandBuffer))
        , colorAttachmentInfos_(desc.colorAttachments |
                                std::views::transform([](const RenderingAttachmentDesc& attachment) { return detail::makeRenderingAttachmentInfo(attachment); }) |
                                std::ranges::to<std::vector>())
    {
        nrAssert(*commandBuffer_.get() != nullptr, "ScopedRendering requires a valid command buffer.");

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

        commandBuffer_.get().beginRendering(renderingInfo_);
        isActive_ = true;
    }

    ~ScopedRendering()
    {
        if (isActive_ && *commandBuffer_.get() != nullptr)
        {
            commandBuffer_.get().endRendering();
        }
    }

    ScopedRendering(const ScopedRendering&) = delete;
    ScopedRendering& operator=(const ScopedRendering&) = delete;
    ScopedRendering(ScopedRendering&&) = delete;
    ScopedRendering& operator=(ScopedRendering&&) = delete;

  private:
    std::reference_wrapper<const vk::raii::CommandBuffer> commandBuffer_;
    std::vector<vk::RenderingAttachmentInfo> colorAttachmentInfos_;
    std::optional<vk::RenderingAttachmentInfo> depthAttachmentInfo_;
    std::optional<vk::RenderingAttachmentInfo> stencilAttachmentInfo_;
    vk::RenderingInfo renderingInfo_{};
    bool isActive_ = false;
};

struct BufferUploadTicket
{
    std::optional<std::reference_wrapper<const Buffer>> buffer;
    vk::DeviceSize dstOffset = 0;
    vk::DeviceSize size = 0;
    uint64_t signalValue = 0;
    std::optional<QueueOwnershipTransfer> ownership;

    [[nodiscard]] bool valid() const noexcept
    {
        return buffer.has_value() && size > 0 && signalValue > 0 && ownership.has_value();
    }
};

struct ImageUploadTicket
{
    std::optional<std::reference_wrapper<const Image>> image;
    vk::ImageLayout layout = vk::ImageLayout::eUndefined;
    uint64_t signalValue = 0;
    std::optional<QueueOwnershipTransfer> ownership;

    [[nodiscard]] bool valid() const noexcept
    {
        return image.has_value() &&
               layout != vk::ImageLayout::eUndefined &&
               signalValue > 0 &&
               ownership.has_value();
    }
};

struct ReadbackTicket
{
    vk::DeviceSize offset = 0;
    vk::DeviceSize size = 0;
    uint64_t signalValue = 0;
};

/**
 * @brief One sync scope used by readback pre-copy or post-copy barriers.
 */
struct ReadbackSyncScope
{
    vk::PipelineStageFlags2 stages = vk::PipelineStageFlags2{};
    vk::AccessFlags2 access = vk::AccessFlags2{};

    [[nodiscard]] bool valid() const noexcept
    {
        return stages != vk::PipelineStageFlags2{};
    }
};

/**
 * @brief Readback synchronization plan with explicit pre/post stage+access scopes.
 */
struct ReadbackSyncPlan
{
    ReadbackSyncScope preCopy{};
    ReadbackSyncScope postCopy{};

    [[nodiscard]] bool valid() const noexcept
    {
        return preCopy.valid() && postCopy.valid();
    }
};

/**
 * @brief Upload/readback helper built on persistent mapped ring buffers.
 *
 * Upload keeps using a dedicated transfer queue with explicit ownership handoff.
 * Readback records copy work directly on the source resource queue
 * (graphics or compute only) and uses one readback ring shared by graphics/compute
 * queue families (concurrent mode only when those families differ).
 *
 * Upload path follows Vulkan's recommended queue-family transfer flow:
 *   1. source queue records a release barrier,
 *   2. a semaphore orders the queue submissions,
 *   3. destination queue records the matching acquire barrier.
 *
 * Readback path still requires explicit synchronization:
 *   1. pre-copy barrier for producer-write -> transfer-read visibility,
 *   2. timeline wait for submission completion,
 *   3. host invalidate before CPU reads mapped bytes.
 */
class UploadReadbackContext
{
  public:
    UploadReadbackContext(
        const vk::raii::Device& device,
        ResourceFactory& resourceFactory,
        QueueManager& queueManager,
        vk::DeviceSize uploadRingSize = 64u * 1024u * 1024u,
        vk::DeviceSize readbackRingSize = 64u * 1024u * 1024u)
        : device_(std::cref(device))
        , queueManager_(std::ref(queueManager))
        , uploadCapacity_(uploadRingSize)
        , readbackCapacity_(readbackRingSize)
        , transferPool_(device, queueManager.transfer().queueFamilyIndex(), vk::CommandPoolCreateFlagBits::eTransient)
        , graphicsReadbackPool_(device, queueManager.graphics().queueFamilyIndex(), vk::CommandPoolCreateFlagBits::eTransient)
        , computeReadbackPool_(device, queueManager.compute().queueFamilyIndex(), vk::CommandPoolCreateFlagBits::eTransient)
    {
        vk::BufferCreateInfo uploadInfo{};
        uploadInfo.size = uploadRingSize;
        uploadInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
        uploadInfo.sharingMode = vk::SharingMode::eExclusive;
        uploadRing_ = resourceFactory.createBuffer(uploadInfo, MemoryUsage::CpuToGpu, "upload_ring");

        auto readbackQueueFamilies = uniqueReadbackQueueFamilies(std::array{
            queueManager.graphics().queueFamilyIndex(),
            queueManager.compute().queueFamilyIndex(),
        });
        nrAssert(
            !readbackQueueFamilies.empty(),
            "UploadReadbackContext requires at least one valid queue family for readback ring setup.");

        vk::BufferCreateInfo readbackInfo{};
        readbackInfo.size = readbackRingSize;
        readbackInfo.usage = vk::BufferUsageFlagBits::eTransferDst;
        if (readbackQueueFamilies.size() == 1)
        {
            readbackInfo.sharingMode = vk::SharingMode::eExclusive;
        }
        else
        {
            readbackInfo.sharingMode = vk::SharingMode::eConcurrent;
            readbackInfo.queueFamilyIndexCount = static_cast<uint32_t>(readbackQueueFamilies.size());
            readbackInfo.pQueueFamilyIndices = readbackQueueFamilies.data();
        }
        readbackRing_ = resourceFactory.createBuffer(readbackInfo, MemoryUsage::GpuToCpu, "readback_ring");

        uploadTimeline_ = sync::createTimelineSemaphore(device, 0u);
        readbackTimeline_ = sync::createTimelineSemaphore(device, 0u);
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return uploadRing_.valid() &&
               readbackRing_.valid() &&
               transferPool_.valid() &&
               graphicsReadbackPool_.valid() &&
               computeReadbackPool_.valid() &&
               *uploadTimeline_ != nullptr &&
               *readbackTimeline_ != nullptr;
    }

    [[nodiscard]] const vk::raii::Semaphore& uploadTimelineSemaphore() const noexcept
    {
        return uploadTimeline_;
    }

    [[nodiscard]] const vk::raii::Semaphore& readbackTimelineSemaphore() const noexcept
    {
        return readbackTimeline_;
    }

    /**
     * @brief Block until upload timeline reaches @p signalValue.
     *
     * Passing `0` waits for the latest upload issued by this context.
     */
    void waitUploadComplete(uint64_t signalValue = 0)
    {
        if (signalValue == 0)
        {
            signalValue = nextUploadSignalValue_ > 1 ? (nextUploadSignalValue_ - 1) : 0;
        }

        if (signalValue == 0)
        {
            return;
        }

        waitTimelineValue(uploadTimeline_, signalValue);
        reclaimQueue(uploadInFlight_, uploadReclaimCursor_, queryTimelineValue(uploadTimeline_));
    }

    /**
     * @brief Block until readback timeline reaches @p signalValue.
     *
     * Passing `0` waits for the latest readback issued by this context.
     */
    void waitReadbackComplete(uint64_t signalValue = 0)
    {
        if (signalValue == 0)
        {
            signalValue = nextReadbackSignalValue_ > 1 ? (nextReadbackSignalValue_ - 1) : 0;
        }

        if (signalValue == 0)
        {
            return;
        }

        waitTimelineValue(readbackTimeline_, signalValue);
        reclaimQueue(readbackInFlight_, readbackReclaimCursor_, queryTimelineValue(readbackTimeline_));
    }

    /**
     * @brief Build acquire barrier from upload ticket.
     */
    [[nodiscard]] vk::BufferMemoryBarrier2 makeAcquireBarrier(const BufferUploadTicket& ticket) const
    {
        nrAssert(ticket.valid(), "UploadReadbackContext::makeAcquireBarrier requires a valid ticket.");
        return makeBufferOwnershipBarrier<OwnershipBarrierPhase::Acquire>(
            ticket.buffer->get(),
            ticket.ownership->acquire,
            ticket.dstOffset,
            ticket.size);
    }

    /**
     * @brief Record acquire barrier from upload ticket into destination queue command buffer.
     */
    void recordAcquireBarrier(vk::CommandBuffer commandBuffer, const BufferUploadTicket& ticket) const
    {
        BarrierBatch acquireBarriers{};
        acquireBarriers.add(makeAcquireBarrier(ticket));
        pipelineBarrier(commandBuffer, acquireBarriers);
    }

    /**
     * @brief Build acquire barrier from image upload ticket.
     */
    [[nodiscard]] vk::ImageMemoryBarrier2 makeImageAcquireBarrier(const ImageUploadTicket& ticket) const
    {
        nrAssert(ticket.valid(), "UploadReadbackContext::makeImageAcquireBarrier requires a valid ticket.");
        return makeImageOwnershipBarrier<OwnershipBarrierPhase::Acquire>(
            ticket.image->get(),
            vk::ImageLayout::eTransferDstOptimal,
            ticket.layout,
            ticket.ownership->acquire);
    }

    /**
     * @brief Record acquire barrier from image upload ticket into destination queue command buffer.
     */
    void recordImageAcquireBarrier(vk::CommandBuffer commandBuffer, const ImageUploadTicket& ticket) const
    {
        BarrierBatch acquireBarriers{};
        acquireBarriers.add(makeImageAcquireBarrier(ticket));
        pipelineBarrier(commandBuffer, acquireBarriers);
    }

    /**
     * @brief Upload raw bytes into a destination buffer via transfer queue staging ring.
     *
     * This enforces non-UBO upload policy: transfer copy + queue ownership transfer.
     * If `ownership.acquireToTransfer` is present, the first transfer submit waits
     * on `acquireToTransfer.waitSemaphore` and records the matching acquire barrier
     * at the beginning of the transfer pipeline. If it is omitted, this upload path
     * assumes the written destination range does not need its previous contents
     * preserved before the transfer queue starts overwriting it.
     * If payload size exceeds ring capacity, data is chunked and submitted in-order.
     */
    [[nodiscard]] BufferUploadTicket uploadBuffer(
        std::span<const std::byte> data,
        const Buffer& dst,
        vk::DeviceSize dstOffset,
        const BufferUploadOwnershipPlan& ownership)
    {
        nrAssert(valid(), "UploadReadbackContext::uploadBuffer requires a valid context.");
        nrAssert(!data.empty(), "UploadReadbackContext::uploadBuffer requires non-empty data.");

        const auto transferQueueFamilyIndex = queueManager_->get().transfer().queueFamilyIndex();
        nrAssert(
            ownership.valid(transferQueueFamilyIndex),
            "UploadReadbackContext::uploadBuffer requires a valid ownership plan for the transfer queue.");

        const auto totalSize = static_cast<vk::DeviceSize>(data.size_bytes());
        nrAssert(dstOffset + totalSize <= dst.size(), "UploadReadbackContext::uploadBuffer destination range exceeds buffer size.");
        nrAssert(
            (dst.usage() & vk::BufferUsageFlagBits::eTransferDst) != vk::BufferUsageFlags{},
            "UploadReadbackContext::uploadBuffer destination buffer must include eTransferDst usage.");
        nrAssert(uploadRing_.mapped() != nullptr, "UploadReadbackContext::uploadBuffer requires a persistently mapped upload ring.");

        auto remainingSize = totalSize;
        auto uploadedSize = vk::DeviceSize{0};
        auto lastSignalValue = uint64_t{0};

        while (remainingSize > 0)
        {
            const auto chunkSize = std::min(remainingSize, uploadCapacity_);
            auto allocation = reserveUpload(chunkSize, uploadTimeline_);

            std::memcpy(
                static_cast<std::byte*>(uploadRing_.mapped()) + allocation.offset,
                data.data() + static_cast<size_t>(uploadedSize),
                static_cast<size_t>(chunkSize));
            uploadRing_.flush(allocation.offset, chunkSize);

            auto commandBuffers = transferPool_.allocatePrimary(1);
            auto& commandBuffer = commandBuffers.front();

            CommandRecorder::beginPrimary(commandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
            {
                auto raw = *commandBuffer;

                if (uploadedSize == 0 && ownership.acquireToTransfer.has_value())
                {
                    BarrierBatch transferAcquireBarrier{};
                    transferAcquireBarrier.add(makeBufferOwnershipBarrier<OwnershipBarrierPhase::Acquire>(
                        dst,
                        ownership.acquireToTransfer->acquire,
                        dstOffset,
                        totalSize));
                    pipelineBarrier(raw, transferAcquireBarrier);
                }

                // The Vulkan submission itself performs the required host->device
                // domain operation after flush; no extra buffer barrier is needed here.
                vk::BufferCopy copyRegion{};
                copyRegion.srcOffset = allocation.offset;
                copyRegion.dstOffset = dstOffset + uploadedSize;
                copyRegion.size = chunkSize;
                raw.copyBuffer(uploadRing_.handle(), dst.handle(), {copyRegion});

                if (remainingSize == chunkSize)
                {
                    BarrierBatch releaseBarrier{};
                    releaseBarrier.add(makeBufferOwnershipBarrier<OwnershipBarrierPhase::Release>(
                        dst,
                        ownership.releaseToDestination.release,
                        dstOffset,
                        totalSize));
                    pipelineBarrier(raw, releaseBarrier);
                }
            }
            CommandRecorder::end(commandBuffer);

            const auto signalValue = consumeNextUploadSignalValue();
            CommandBatch batch{};
            if (uploadedSize == 0 && ownership.acquireToTransfer.has_value())
            {
                batch.addWait(
                    ownership.acquireToTransfer->waitSemaphore,
                    ownership.acquireToTransfer->acquire.stages,
                    ownership.acquireToTransfer->waitValue);
            }
            batch.addCommandBuffer(commandBuffer);
            batch.addSignal(uploadTimeline_, signalValue, 0, vk::PipelineStageFlagBits2::eBottomOfPipe);
            queueManager_->get().transfer().submit(batch);

            addInFlight(uploadInFlight_, allocation, signalValue, std::move(commandBuffers));

            lastSignalValue = signalValue;
            uploadedSize += chunkSize;
            remainingSize -= chunkSize;
        }

        return BufferUploadTicket{
            .buffer = std::cref(dst),
            .dstOffset = dstOffset,
            .size = totalSize,
            .signalValue = lastSignalValue,
            .ownership = ownership.releaseToDestination,
        };
    }

    /**
     * @brief Upload one image payload via staging buffer -> copyBufferToImage.
     *
     * This path intentionally does not create a staging image. It performs:
     *   1) optional acquire-to-transfer ownership barrier + wait,
     *   2) transition to eTransferDstOptimal,
     *   3) copyBufferToImage from upload ring,
     *   4) release to destination queue with destination layout.
     *
     * Note: current implementation requires payload to fit in the upload ring.
     */
    [[nodiscard]] ImageUploadTicket uploadImage(
        std::span<const std::byte> data,
        const Image& dst,
        vk::ImageLayout sourceLayout,
        vk::ImageLayout destinationLayout,
        const BufferUploadOwnershipPlan& ownership,
        const vk::BufferImageCopy& region = {})
    {
        nrAssert(valid(), "UploadReadbackContext::uploadImage requires a valid context.");
        nrAssert(!data.empty(), "UploadReadbackContext::uploadImage requires non-empty data.");
        nrAssert(destinationLayout != vk::ImageLayout::eUndefined, "UploadReadbackContext::uploadImage requires a valid destination layout.");

        const auto transferQueueFamilyIndex = queueManager_->get().transfer().queueFamilyIndex();
        nrAssert(
            ownership.valid(transferQueueFamilyIndex),
            "UploadReadbackContext::uploadImage requires a valid ownership plan for the transfer queue.");
        nrAssert(
            (dst.usage() & vk::ImageUsageFlagBits::eTransferDst) != vk::ImageUsageFlags{},
            "UploadReadbackContext::uploadImage destination image must include eTransferDst usage.");
        nrAssert(uploadRing_.mapped() != nullptr, "UploadReadbackContext::uploadImage requires a persistently mapped upload ring.");

        auto effectiveRegion = normalizeBufferImageCopyRegion(dst, region);
        const auto payloadSize = linearBufferImageCopySize(effectiveRegion, bytesPerPixel(dst.format()));

        nrAssert(
            static_cast<vk::DeviceSize>(data.size_bytes()) == payloadSize,
            std::format(
                "UploadReadbackContext::uploadImage payload size mismatch: data={} bytes, expected={} bytes.",
                static_cast<vk::DeviceSize>(data.size_bytes()),
                payloadSize));
        nrAssert(payloadSize <= uploadCapacity_, 
            std::format("UploadReadbackContext::uploadImage payload size ({} bytes) exceeds upload ring capacity ({} bytes). "
                        "Consider increasing ring buffer size for large textures.",
                        payloadSize, uploadCapacity_));

        auto allocation = reserveUpload(payloadSize, uploadTimeline_);
        std::memcpy(
            static_cast<std::byte*>(uploadRing_.mapped()) + allocation.offset,
            data.data(),
            static_cast<size_t>(payloadSize));
        uploadRing_.flush(allocation.offset, payloadSize);

        auto commandBuffers = transferPool_.allocatePrimary(1);
        auto& commandBuffer = commandBuffers.front();
        CommandRecorder::beginPrimary(commandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        {
            auto raw = *commandBuffer;

            if (ownership.acquireToTransfer.has_value())
            {
                BarrierBatch transferAcquireBarrier{};
                transferAcquireBarrier.add(makeImageOwnershipBarrier<OwnershipBarrierPhase::Acquire>(
                    dst,
                    sourceLayout,
                    vk::ImageLayout::eTransferDstOptimal,
                    ownership.acquireToTransfer->acquire));
                pipelineBarrier(raw, transferAcquireBarrier);
            }
            else
            {
                BarrierBatch toTransferDstBarrier{};
                toTransferDstBarrier.add(makeImageBarrier(dst, vk::ImageMemoryBarrier2{
                    vk::PipelineStageFlagBits2::eTopOfPipe,
                    vk::AccessFlags2{},
                    vk::PipelineStageFlagBits2::eTransfer,
                    vk::AccessFlagBits2::eTransferWrite,
                    sourceLayout,
                    vk::ImageLayout::eTransferDstOptimal,
                    kIgnoredQueueFamilyIndex,
                    kIgnoredQueueFamilyIndex,
                    vk::Image{},
                    {},
                    nullptr,
                }));
                pipelineBarrier(raw, toTransferDstBarrier);
            }

            effectiveRegion.bufferOffset += allocation.offset;
            raw.copyBufferToImage(uploadRing_.handle(), dst.handle(), vk::ImageLayout::eTransferDstOptimal, {effectiveRegion});

            BarrierBatch releaseBarrier{};
            if (ownership.isSameQueueFamily())
            {
                // Same queue family: use simple layout transition, no ownership transfer
                releaseBarrier.add(makeImageBarrier(dst, vk::ImageMemoryBarrier2{
                    ownership.releaseToDestination.release.stages,
                    ownership.releaseToDestination.release.access,
                    ownership.releaseToDestination.acquire.stages,
                    ownership.releaseToDestination.acquire.access,
                    vk::ImageLayout::eTransferDstOptimal,
                    destinationLayout,
                    kIgnoredQueueFamilyIndex,
                    kIgnoredQueueFamilyIndex,
                    vk::Image{},
                    {},
                    nullptr,
                }));
            }
            else
            {
                // Cross-queue: use ownership transfer barrier
                releaseBarrier.add(makeImageOwnershipBarrier<OwnershipBarrierPhase::Release>(
                    dst,
                    vk::ImageLayout::eTransferDstOptimal,
                    destinationLayout,
                    ownership.releaseToDestination.release));
            }
            pipelineBarrier(raw, releaseBarrier);
        }
        CommandRecorder::end(commandBuffer);

        const auto signalValue = consumeNextUploadSignalValue();
        CommandBatch batch{};
        if (ownership.acquireToTransfer.has_value())
        {
            batch.addWait(
                ownership.acquireToTransfer->waitSemaphore,
                ownership.acquireToTransfer->acquire.stages,
                ownership.acquireToTransfer->waitValue);
        }
        batch.addCommandBuffer(commandBuffer);
        batch.addSignal(uploadTimeline_, signalValue, 0, vk::PipelineStageFlagBits2::eBottomOfPipe);
        queueManager_->get().transfer().submit(batch);

        addInFlight(uploadInFlight_, allocation, signalValue, std::move(commandBuffers));

        return ImageUploadTicket{
            .image = std::cref(dst),
            .layout = destinationLayout,
            .signalValue = signalValue,
            .ownership = ownership.releaseToDestination,
        };
    }

    /**
     * @brief Copy a GPU buffer into readback ring and return a ticket.
     *
     * `queueRole` must be `Graphics` or `Compute`; transfer queue is not supported.
     * The caller provides explicit pre/post stage+access scopes:
     *   1) `syncPlan.preCopy` for producer visibility before copy,
     *   2) `syncPlan.postCopy` for source-resource restore visibility after copy.
     */
    [[nodiscard]] ReadbackTicket readbackBuffer(
        const Buffer& src,
        vk::DeviceSize srcOffset,
        vk::DeviceSize size,
        QueueRole queueRole,
        const ReadbackSyncPlan& syncPlan)
    {
        nrAssert(valid(), "UploadReadbackContext::readbackBuffer requires a valid context.");
        nrAssert(
            isReadbackQueueRoleSupported(queueRole),
            "UploadReadbackContext::readbackBuffer supports only graphics/compute queue roles.");
        nrAssert(size > 0, "UploadReadbackContext::readbackBuffer requires size > 0.");
        nrAssert(syncPlan.valid(), "UploadReadbackContext::readbackBuffer requires non-empty pre/post stage masks.");
        nrAssert(srcOffset + size <= src.size(), "UploadReadbackContext::readbackBuffer source range exceeds buffer size.");
        nrAssert(
            (src.usage() & vk::BufferUsageFlagBits::eTransferSrc) != vk::BufferUsageFlags{},
            "UploadReadbackContext::readbackBuffer source buffer must include eTransferSrc usage.");
        nrAssert(readbackRing_.mapped() != nullptr, "UploadReadbackContext::readbackBuffer requires a persistently mapped readback ring.");

        auto route = readbackRouteFor(queueRole);
        auto& readbackQueue = route.queue.get();
        auto& readbackPool = route.pool.get();

        auto allocation = reserveReadback(size);

        auto commandBuffers = readbackPool.allocatePrimary(1);
        auto& commandBuffer = commandBuffers.front();
        CommandRecorder::beginPrimary(commandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        {
            auto raw = *commandBuffer;

            BarrierBatch preCopyBarrier{};
            preCopyBarrier.add(makeBufferBarrier(src, vk::BufferMemoryBarrier2{
                syncPlan.preCopy.stages,
                syncPlan.preCopy.access,
                vk::PipelineStageFlagBits2::eTransfer,
                vk::AccessFlagBits2::eTransferRead,
                kIgnoredQueueFamilyIndex,
                kIgnoredQueueFamilyIndex,
                vk::Buffer{},
                srcOffset,
                size,
                nullptr,
            }));
            pipelineBarrier(raw, preCopyBarrier);

            recordReadbackRingToTransferWriteBarrier(raw, allocation.offset, size);

            vk::BufferCopy copyRegion{};
            copyRegion.srcOffset = srcOffset;
            copyRegion.dstOffset = allocation.offset;
            copyRegion.size = size;
            raw.copyBuffer(src.handle(), readbackRing_.handle(), {copyRegion});

            recordReadbackRingHostVisibilityBarrier(raw, allocation.offset, size);

            BarrierBatch postCopyBarrier{};
            postCopyBarrier.add(makeBufferBarrier(src, vk::BufferMemoryBarrier2{
                vk::PipelineStageFlagBits2::eTransfer,
                vk::AccessFlagBits2::eTransferRead,
                syncPlan.postCopy.stages,
                syncPlan.postCopy.access,
                kIgnoredQueueFamilyIndex,
                kIgnoredQueueFamilyIndex,
                vk::Buffer{},
                srcOffset,
                size,
                nullptr,
            }));
            pipelineBarrier(raw, postCopyBarrier);
        }
        CommandRecorder::end(commandBuffer);

        const auto signalValue = consumeNextReadbackSignalValue();
        CommandBatch batch{};
        if (signalValue > 1)
        {
            batch.addWait(readbackTimeline_, vk::PipelineStageFlagBits2::eTransfer, signalValue - 1);
        }
        batch.addCommandBuffer(commandBuffer);
        batch.addSignal(readbackTimeline_, signalValue, 0, vk::PipelineStageFlagBits2::eBottomOfPipe);
        readbackQueue.submit(batch);

        addInFlight(readbackInFlight_, allocation, signalValue, std::move(commandBuffers));

        return ReadbackTicket{allocation.offset, size, signalValue};
    }

    /**
     * @brief Copy a GPU image into readback ring and return a ticket.
     *
     * `queueRole` must be `Graphics` or `Compute`; transfer queue is not supported.
     * The caller provides explicit pre/post stage+access scopes:
     *   1) `syncPlan.preCopy` + layout transition to transfer-src,
     *   2) transfer-read -> `syncPlan.postCopy`, then layout restore.
     */
    [[nodiscard]] ReadbackTicket readbackImage(
        const Image& src,
        vk::ImageLayout sourceLayout,
        QueueRole queueRole,
        const ReadbackSyncPlan& syncPlan,
        const vk::BufferImageCopy& region = {})
    {
        nrAssert(valid(), "UploadReadbackContext::readbackImage requires a valid context.");
        nrAssert(
            isReadbackQueueRoleSupported(queueRole),
            "UploadReadbackContext::readbackImage supports only graphics/compute queue roles.");
        nrAssert(syncPlan.valid(), "UploadReadbackContext::readbackImage requires non-empty pre/post stage masks.");
        nrAssert(
            (src.usage() & vk::ImageUsageFlagBits::eTransferSrc) != vk::ImageUsageFlags{},
            "UploadReadbackContext::readbackImage source image must include eTransferSrc usage.");
        nrAssert(readbackRing_.mapped() != nullptr, "UploadReadbackContext::readbackImage requires a persistently mapped readback ring.");

        auto route = readbackRouteFor(queueRole);
        auto& readbackQueue = route.queue.get();
        auto& readbackPool = route.pool.get();

        auto effectiveRegion = normalizeBufferImageCopyRegion(src, region);
        const auto readbackSize = linearBufferImageCopySize(effectiveRegion, bytesPerPixel(src.format()));
        auto allocation = reserveReadback(readbackSize);

        auto subresourceRange = readbackImageSubresourceRange(src, effectiveRegion);

        auto commandBuffers = readbackPool.allocatePrimary(1);
        auto& commandBuffer = commandBuffers.front();
        CommandRecorder::beginPrimary(commandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        {
            auto raw = *commandBuffer;

            BarrierBatch preCopyBarrier{};
            preCopyBarrier.add(makeImageBarrier(src, vk::ImageMemoryBarrier2{
                syncPlan.preCopy.stages,
                syncPlan.preCopy.access,
                vk::PipelineStageFlagBits2::eTransfer,
                vk::AccessFlagBits2::eTransferRead,
                sourceLayout,
                vk::ImageLayout::eTransferSrcOptimal,
                kIgnoredQueueFamilyIndex,
                kIgnoredQueueFamilyIndex,
                vk::Image{},
                subresourceRange,
                nullptr,
            }));
            pipelineBarrier(raw, preCopyBarrier);

            recordReadbackRingToTransferWriteBarrier(raw, allocation.offset, readbackSize);

            effectiveRegion.bufferOffset += allocation.offset;
            raw.copyImageToBuffer(src.handle(), vk::ImageLayout::eTransferSrcOptimal, readbackRing_.handle(), {effectiveRegion});

            recordReadbackRingHostVisibilityBarrier(raw, allocation.offset, readbackSize);

            BarrierBatch restoreBarrier{};
            restoreBarrier.add(makeImageBarrier(src, vk::ImageMemoryBarrier2{
                vk::PipelineStageFlagBits2::eTransfer,
                vk::AccessFlagBits2::eTransferRead,
                syncPlan.postCopy.stages,
                syncPlan.postCopy.access,
                vk::ImageLayout::eTransferSrcOptimal,
                sourceLayout,
                kIgnoredQueueFamilyIndex,
                kIgnoredQueueFamilyIndex,
                vk::Image{},
                subresourceRange,
                nullptr,
            }));
            pipelineBarrier(raw, restoreBarrier);
        }
        CommandRecorder::end(commandBuffer);

        const auto signalValue = consumeNextReadbackSignalValue();
        CommandBatch batch{};
        if (signalValue > 1)
        {
            batch.addWait(readbackTimeline_, vk::PipelineStageFlagBits2::eTransfer, signalValue - 1);
        }
        batch.addCommandBuffer(commandBuffer);
        batch.addSignal(readbackTimeline_, signalValue, 0, vk::PipelineStageFlagBits2::eBottomOfPipe);
        readbackQueue.submit(batch);

        addInFlight(readbackInFlight_, allocation, signalValue, std::move(commandBuffers));
        return ReadbackTicket{allocation.offset, readbackSize, signalValue};
    }

    /**
     * @brief Block until ticket is ready, invalidate cache, and copy bytes out.
     */
    [[nodiscard]] std::vector<std::byte> readbackBytes(const ReadbackTicket& ticket)
    {
        nrAssert(ticket.signalValue > 0, "UploadReadbackContext::readbackBytes requires ticket.signalValue > 0.");
        nrAssert(ticket.size > 0, "UploadReadbackContext::readbackBytes requires ticket.size > 0.");

        waitReadbackComplete(ticket.signalValue);
        readbackRing_.invalidate(ticket.offset, ticket.size);

        std::vector<std::byte> data(static_cast<size_t>(ticket.size));
        auto* src = static_cast<const std::byte*>(readbackRing_.mapped()) + ticket.offset;
        std::memcpy(data.data(), src, data.size());
        return data;
    }

  private:
    struct ReadbackRoute
    {
        std::reference_wrapper<GpuQueue> queue;
        std::reference_wrapper<CommandPool> pool;
    };

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

    [[nodiscard]] uint64_t consumeNextUploadSignalValue()
    {
        auto value = nextUploadSignalValue_;
        ++nextUploadSignalValue_;
        nrAssert(nextUploadSignalValue_ > value, "UploadReadbackContext timeline signal value overflow.");
        return value;
    }

    [[nodiscard]] uint64_t consumeNextReadbackSignalValue()
    {
        auto value = nextReadbackSignalValue_;
        ++nextReadbackSignalValue_;
        nrAssert(nextReadbackSignalValue_ > value, "UploadReadbackContext readback timeline signal value overflow.");
        return value;
    }

    [[nodiscard]] ReadbackRoute readbackRouteFor(QueueRole queueRole)
    {
        nrAssert(
            isReadbackQueueRoleSupported(queueRole),
            "UploadReadbackContext::readbackRouteFor supports only graphics/compute queue roles.");

        if (queueRole == QueueRole::Graphics)
        {
            return ReadbackRoute{std::ref(queueManager_->get().graphics()), std::ref(graphicsReadbackPool_)};
        }

        return ReadbackRoute{std::ref(queueManager_->get().compute()), std::ref(computeReadbackPool_)};
    }

    [[nodiscard]] static std::vector<uint32_t> uniqueReadbackQueueFamilies(std::array<uint32_t, 2> queueFamilies)
    {
        std::ranges::sort(queueFamilies);
        auto uniqueRange = std::ranges::unique(queueFamilies);
        return std::vector<uint32_t>(queueFamilies.begin(), uniqueRange.begin());
    }

    [[nodiscard]] static bool isReadbackQueueRoleSupported(QueueRole queueRole) noexcept
    {
        return queueRole == QueueRole::Graphics || queueRole == QueueRole::Compute;
    }

    void recordReadbackRingToTransferWriteBarrier(vk::CommandBuffer commandBuffer, vk::DeviceSize offset, vk::DeviceSize size) const
    {
        BarrierBatch ringBarrier{};
        ringBarrier.add(makeBufferBarrier(readbackRing_, vk::BufferMemoryBarrier2{
            vk::PipelineStageFlagBits2::eHost,
            vk::AccessFlagBits2::eHostRead,
            vk::PipelineStageFlagBits2::eTransfer,
            vk::AccessFlagBits2::eTransferWrite,
            kIgnoredQueueFamilyIndex,
            kIgnoredQueueFamilyIndex,
            vk::Buffer{},
            offset,
            size,
            nullptr,
        }));
        pipelineBarrier(commandBuffer, ringBarrier);
    }

    void recordReadbackRingHostVisibilityBarrier(vk::CommandBuffer commandBuffer, vk::DeviceSize offset, vk::DeviceSize size) const
    {
        BarrierBatch ringBarrier{};
        ringBarrier.add(makeBufferBarrier(readbackRing_, vk::BufferMemoryBarrier2{
            vk::PipelineStageFlagBits2::eTransfer,
            vk::AccessFlagBits2::eTransferWrite,
            vk::PipelineStageFlagBits2::eHost,
            vk::AccessFlagBits2::eHostRead,
            kIgnoredQueueFamilyIndex,
            kIgnoredQueueFamilyIndex,
            vk::Buffer{},
            offset,
            size,
            nullptr,
        }));
        pipelineBarrier(commandBuffer, ringBarrier);
    }

    [[nodiscard]] static vk::ImageSubresourceRange readbackImageSubresourceRange(const Image& image, const vk::BufferImageCopy& copyRegion)
    {
        auto layers = copyRegion.imageSubresource;
        if (layers.aspectMask == vk::ImageAspectFlags{})
        {
            layers.aspectMask = inferAspectFlags(image.format());
        }

        const auto layerCount = std::max(1u, layers.layerCount);
        nrAssert(
            layers.baseArrayLayer + layerCount <= image.arrayLayers(),
            std::format(
                "UploadReadbackContext::readbackImageSubresourceRange layer range [{}..{}) exceeds image layer count {}.",
                layers.baseArrayLayer,
                layers.baseArrayLayer + layerCount,
                image.arrayLayers()));

        return vk::ImageSubresourceRange{
            layers.aspectMask,
            layers.mipLevel,
            1,
            layers.baseArrayLayer,
            layerCount,
        };
    }

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

    [[nodiscard]] RingAllocation reserveReadback(vk::DeviceSize size)
    {
        return reserveRing(size, readbackCapacity_, readbackWriteCursor_, readbackReclaimCursor_, readbackInFlight_, readbackTimeline_);
    }

    [[nodiscard]] RingAllocation reserveRing(
        vk::DeviceSize size,
        vk::DeviceSize capacity,
        uint64_t& writeCursor,
        uint64_t& reclaimCursor,
        std::deque<InFlightBatch>& queue,
        const vk::raii::Semaphore& timelineSemaphore)
    {
        // Validate that the requested size can fit in the ring buffer
        nrAssert(size <= capacity, 
            std::format("UploadReadbackContext ring allocation size ({} bytes) exceeds ring capacity ({} bytes). "
                        "Consider increasing the ring buffer size or using chunked uploads.",
                        size, capacity));

        reclaimQueue(queue, reclaimCursor, queryTimelineValue(timelineSemaphore));

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

        // Wait for pending transfers to complete and try to reclaim space
        while (!queue.empty())
        {
            waitTimelineValue(timelineSemaphore, queue.front().signalValue);
            reclaimQueue(queue, reclaimCursor, queryTimelineValue(timelineSemaphore));
            if (auto allocation = tryReserve())
            {
                writeCursor = allocation->end;
                return *allocation;
            }
        }

        // If queue is empty, reset cursors to start fresh from the beginning
        // This handles the edge case where cursors have drifted far apart
        if (queue.empty())
        {
            writeCursor = 0;
            reclaimCursor = 0;
            if (auto allocation = tryReserve())
            {
                writeCursor = allocation->end;
                return *allocation;
            }
        }

        nrAssert(false, 
            std::format("UploadReadbackContext failed to reserve ring allocation of {} bytes with capacity {} bytes.",
                        size, capacity));
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
    CommandPool graphicsReadbackPool_;
    CommandPool computeReadbackPool_;

    vk::raii::Semaphore uploadTimeline_ = {nullptr};
    vk::raii::Semaphore readbackTimeline_ = {nullptr};
    uint64_t nextUploadSignalValue_ = 1;
    uint64_t nextReadbackSignalValue_ = 1;

    uint64_t uploadWriteCursor_ = 0;
    uint64_t uploadReclaimCursor_ = 0;
    uint64_t readbackWriteCursor_ = 0;
    uint64_t readbackReclaimCursor_ = 0;

    std::deque<InFlightBatch> uploadInFlight_;
    std::deque<InFlightBatch> readbackInFlight_;
};

} // namespace nr::rhi::ops
