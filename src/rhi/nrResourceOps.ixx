export module nr.rhi:resourceOps;
import dependency.vulkan;
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

inline constexpr std::uint32_t kIgnoredQueueFamilyIndex = std::numeric_limits<std::uint32_t>::max();
inline constexpr vk::DeviceSize kDefaultUploadReadbackRingSize = 128ull * 1024ull * 1024ull;

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

struct QueueFamilyTransferPolicy
{
    bool maintenance9 = false;
    std::vector<std::uint32_t> optimalImageTransferToQueueFamilies{};

    [[nodiscard]] bool hasUsableQueueFamilyPair(std::uint32_t srcQueueFamilyIndex,
                                                std::uint32_t dstQueueFamilyIndex) const noexcept
    {
        return maintenance9 && srcQueueFamilyIndex != kIgnoredQueueFamilyIndex &&
               dstQueueFamilyIndex != kIgnoredQueueFamilyIndex && srcQueueFamilyIndex != dstQueueFamilyIndex;
    }

    [[nodiscard]] bool canOmitBufferQueueFamilyTransfer(std::uint32_t srcQueueFamilyIndex,
                                                        std::uint32_t dstQueueFamilyIndex) const noexcept
    {
        return hasUsableQueueFamilyPair(srcQueueFamilyIndex, dstQueueFamilyIndex);
    }

    [[nodiscard]] bool canOmitImageQueueFamilyTransfer(std::uint32_t srcQueueFamilyIndex,
                                                       std::uint32_t dstQueueFamilyIndex, vk::ImageTiling tiling,
                                                       vk::ImageUsageFlags usage,
                                                       bool isSwapchain = false) const noexcept
    {
        if (isSwapchain || !hasUsableQueueFamilyPair(srcQueueFamilyIndex, dstQueueFamilyIndex))
        {
            return false;
        }

        if (tiling == vk::ImageTiling::eLinear)
        {
            return true;
        }

        if (tiling != vk::ImageTiling::eOptimal || srcQueueFamilyIndex >= optimalImageTransferToQueueFamilies.size() ||
            dstQueueFamilyIndex >= std::numeric_limits<std::uint32_t>::digits)
        {
            return false;
        }

        constexpr auto disallowedOptimalUsage =
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eDepthStencilAttachment |
            vk::ImageUsageFlagBits::eTransientAttachment | vk::ImageUsageFlagBits::eInputAttachment |
            vk::ImageUsageFlagBits::eAttachmentFeedbackLoopEXT |
            vk::ImageUsageFlagBits::eFragmentShadingRateAttachmentKHR;

        const auto destinationMask = std::uint32_t{1u} << dstQueueFamilyIndex;
        return (usage & disallowedOptimalUsage) == vk::ImageUsageFlags{} &&
               (optimalImageTransferToQueueFamilies[srcQueueFamilyIndex] & destinationMask) != 0u;
    }

    [[nodiscard]] bool canOmitImageQueueFamilyTransfer(const Image &image, std::uint32_t srcQueueFamilyIndex,
                                                       std::uint32_t dstQueueFamilyIndex,
                                                       bool isSwapchain = false) const noexcept
    {
        return canOmitImageQueueFamilyTransfer(srcQueueFamilyIndex, dstQueueFamilyIndex, image.tiling(), image.usage(),
                                               isSwapchain);
    }
};

namespace detail
{
/**
 * @brief Resource kind supported by queue-ownership transfer barrier templates.
 */
template <typename TResourceKind>
concept QueueOwnershipResource =
    std::same_as<std::remove_cvref_t<TResourceKind>, Buffer> || std::same_as<std::remove_cvref_t<TResourceKind>, Image>;

/**
 * @brief Parameter pack for queue-ownership transfer barrier generation.
 */
struct QueueOwnershipBarrierConfig
{
    std::uint32_t srcQueueFamilyIndex = kIgnoredQueueFamilyIndex;
    std::uint32_t dstQueueFamilyIndex = kIgnoredQueueFamilyIndex;
    vk::PipelineStageFlags2 stages = vk::PipelineStageFlags2{};
    vk::AccessFlags2 access = vk::AccessFlags2{};
    vk::ImageLayout oldLayout = vk::ImageLayout::eUndefined;
    vk::ImageLayout newLayout = vk::ImageLayout::eUndefined;
    vk::DeviceSize offset = 0;
    vk::DeviceSize size = std::numeric_limits<vk::DeviceSize>::max();
};

/**
 * @brief Build queue-ownership barrier for Buffer/Image with one strongly-typed template path.
 * @tparam TOwnershipPhase Compile-time phase selector: `Release` populates only
 *         the source access scope and `Acquire` populates only the destination
 *         access scope. Maintenance8 keeps both stage scopes equal to the active
 *         phase stage so queue-family operations participate in precise sync.
 * @tparam TResourceKind Resource category (`Buffer` or `Image`) that determines barrier type/layout fields.
 */
template <OwnershipBarrierPhase TOwnershipPhase, QueueOwnershipResource TResourceKind>
[[nodiscard]] inline auto makeQueueOwnershipBarrier(const TResourceKind &resource,
                                                    const QueueOwnershipBarrierConfig &config)
{
    constexpr bool kIsRelease = TOwnershipPhase == OwnershipBarrierPhase::Release;

    if constexpr (std::same_as<std::remove_cvref_t<TResourceKind>, Buffer>)
    {
        return vk::BufferMemoryBarrier2{
            config.stages,
            kIsRelease ? config.access : vk::AccessFlags2{},
            config.stages,
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
            config.stages,
            kIsRelease ? config.access : vk::AccessFlags2{},
            config.stages,
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
[[nodiscard]] vk::ImageSubresourceRange fullSubresourceRange(const Image &image);

/**
 * @brief Attach a Buffer resource handle to an existing barrier object.
 *
 * This preserves all caller-provided sync masks, queue ownership indices,
 * and offset/range fields while only patching the target handle.
 */
[[nodiscard]] vk::BufferMemoryBarrier2 makeBufferBarrier(const Buffer &buffer, vk::BufferMemoryBarrier2 barrier);

/**
 * @brief Build a sync2 image barrier targeting an Image resource.
 *
 * If the caller passes an empty subresource range, this helper expands it to the
 * image full range inferred from format/mips/layers.
 */
[[nodiscard]] vk::ImageMemoryBarrier2 makeImageBarrier(const Image &image, vk::ImageMemoryBarrier2 barrier);

/**
 * @brief Generic image layout transition helper.
 */
[[nodiscard]] vk::ImageMemoryBarrier2 makeImageLayoutTransitionBarrier(
    const Image &image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, vk::PipelineStageFlags2 srcStages,
    vk::AccessFlags2 srcAccess, vk::PipelineStageFlags2 dstStages, vk::AccessFlags2 dstAccess);

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
template <auto TValue> inline constexpr bool kAlwaysFalse = false;

template <ImageTransitionBranch TBranch> [[nodiscard]] consteval vk::ImageLayout imageTransitionDstLayout()
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
    if constexpr (TBranch == ImageTransitionBranch::TransferSrc || TBranch == ImageTransitionBranch::TransferDst)
    {
        return vk::PipelineStageFlagBits2::eTransfer;
    }
    else if constexpr (TBranch == ImageTransitionBranch::ShaderReadOnly)
    {
        return vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eComputeShader;
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
        return vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests;
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

template <ImageTransitionBranch TBranch> [[nodiscard]] consteval vk::AccessFlags2 imageTransitionDefaultDstAccess()
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
        return vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite;
    }
    else if constexpr (TBranch == ImageTransitionBranch::DepthStencilAttachment)
    {
        return vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
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
    const Image &image, vk::ImageLayout oldLayout, vk::PipelineStageFlags2 srcStages, vk::AccessFlags2 srcAccess,
    vk::PipelineStageFlags2 dstStages = detail::imageTransitionDefaultDstStages<TBranch>(),
    vk::AccessFlags2 dstAccess = detail::imageTransitionDefaultDstAccess<TBranch>())
{
    return makeImageLayoutTransitionBarrier(image, oldLayout, detail::imageTransitionDstLayout<TBranch>(), srcStages,
                                            srcAccess, dstStages, dstAccess);
}

/**
 * @brief Stage/access scope used by ownership and synchronization operations.
 */
struct QueueAccessScope
{
    vk::PipelineStageFlags2 stages = vk::PipelineStageFlagBits2::eAllCommands;
    vk::AccessFlags2 access = vk::AccessFlagBits2::eMemoryRead;

    [[nodiscard]] bool valid() const noexcept;
};

[[nodiscard]] vk::MemoryBarrier2 makeAccelerationStructureBuildToTraceReadBarrier();

[[nodiscard]] vk::MemoryBarrier2 makeAccelerationStructureCopyToTraceReadBarrier();

/**
 * @brief Optional semaphore wait payload used by ownership helpers.
 */
struct QueueOwnershipWait
{
    vk::Semaphore semaphore = vk::Semaphore{};
    std::uint64_t value = 0;

    [[nodiscard]] bool valid() const noexcept;
};

/**
 * @brief Full queue-family hand-off plan: a single queue-family pair plus the
 *        release-side and acquire-side stage/access scopes.
 *
 * The queue-family pair is stored once at the top level, so release and acquire
 * always describe the same hand-off direction by construction. The semaphore
 * wait is optional for generic use, but upload paths require it whenever
 * ownership is acquired into the transfer queue from another queue.
 */
struct QueueOwnershipTransfer
{
    std::uint32_t srcQueueFamilyIndex = kIgnoredQueueFamilyIndex;
    std::uint32_t dstQueueFamilyIndex = kIgnoredQueueFamilyIndex;
    QueueAccessScope release{};
    QueueAccessScope acquire{};
    vk::Semaphore waitSemaphore{};
    std::uint64_t waitValue = 0;

    [[nodiscard]] bool valid() const noexcept;

    [[nodiscard]] bool hasWait() const noexcept;
};

[[nodiscard]] QueueOwnershipTransfer makeQueueOwnershipTransfer(std::uint32_t srcQueueFamilyIndex,
                                                                std::uint32_t dstQueueFamilyIndex,
                                                                const QueueAccessScope &releaseScope,
                                                                const QueueAccessScope &acquireScope,
                                                                std::optional<QueueOwnershipWait> wait = std::nullopt);

/**
 * @brief Upload queue-transition plan that may need an incoming acquire to transfer
 *        and describes the destination queue sync scope after the transfer copy.
 *
 * Maintenance9 may make the explicit release/acquire QFOT optional, but callers
 * still provide this plan so upload can submit the required destination-queue wait
 * and record any remaining acquire barrier when ownership is still required.
 */
struct BufferUploadOwnershipPlan
{
    std::optional<QueueOwnershipTransfer> acquireToTransfer{};
    QueueOwnershipTransfer releaseToDestination{};

    /**
     * @brief Check if this plan is for same-queue-family upload (no ownership transfer needed).
     */
    [[nodiscard]] bool isSameQueueFamily() const noexcept;

    [[nodiscard]] bool valid(std::uint32_t transferQueueFamilyIndex) const noexcept;
};

/**
 * @brief Build the canned transfer-queue upload ownership plan.
 *
 * The release scope is fixed to transfer-write completion; only the
 * destination-queue acquire scope varies between consumers.
 */
[[nodiscard]] BufferUploadOwnershipPlan
makeTransferUploadOwnershipPlan(std::uint32_t transferQueueFamilyIndex, std::uint32_t dstQueueFamilyIndex,
                                const QueueAccessScope &acquireScope);

namespace detail
{
template <OwnershipBarrierPhase TOwnershipPhase>
[[nodiscard]] inline const QueueAccessScope &selectOwnershipScope(const QueueOwnershipTransfer &transfer)
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
    const Buffer &buffer, const QueueOwnershipTransfer &transfer, vk::DeviceSize offset = 0,
    vk::DeviceSize size = std::numeric_limits<vk::DeviceSize>::max())
{
    nrAssert(transfer.valid(), "makeBufferOwnershipTransferBarrier requires a valid QueueOwnershipTransfer.");
    const auto &scope = detail::selectOwnershipScope<TOwnershipPhase>(transfer);
    return detail::makeQueueOwnershipBarrier<TOwnershipPhase>(buffer,
                                                              detail::QueueOwnershipBarrierConfig{
                                                                  .srcQueueFamilyIndex = transfer.srcQueueFamilyIndex,
                                                                  .dstQueueFamilyIndex = transfer.dstQueueFamilyIndex,
                                                                  .stages = scope.stages,
                                                                  .access = scope.access,
                                                                  .offset = offset,
                                                                  .size = size,
                                                              });
}

/**
 * @brief Bridge QueueOwnershipTransfer to the generic image ownership-barrier API.
 */
template <OwnershipBarrierPhase TOwnershipPhase>
[[nodiscard]] inline vk::ImageMemoryBarrier2 makeImageOwnershipTransferBarrier(const Image &image,
                                                                               vk::ImageLayout oldLayout,
                                                                               vk::ImageLayout newLayout,
                                                                               const QueueOwnershipTransfer &transfer)
{
    nrAssert(transfer.valid(), "makeImageOwnershipTransferBarrier requires a valid QueueOwnershipTransfer.");
    const auto &scope = detail::selectOwnershipScope<TOwnershipPhase>(transfer);
    return detail::makeQueueOwnershipBarrier<TOwnershipPhase>(image,
                                                              detail::QueueOwnershipBarrierConfig{
                                                                  .srcQueueFamilyIndex = transfer.srcQueueFamilyIndex,
                                                                  .dstQueueFamilyIndex = transfer.dstQueueFamilyIndex,
                                                                  .stages = scope.stages,
                                                                  .access = scope.access,
                                                                  .oldLayout = oldLayout,
                                                                  .newLayout = newLayout,
                                                              });
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

    [[nodiscard]] const vk::DependencyInfo &dependencyInfo() const noexcept;
};

/**
 * @brief Lightweight collector for sync2 barriers.
 *
 * The batch is intentionally state-light and non-owning for resources. Adding
 * an explicit cross-family buffer/image barrier automatically enables the
 * maintenance8 all-stage QFOT dependency flag for the resulting packet.
 */
class BarrierBatch
{
  public:
    void clear();

    [[nodiscard]] bool empty() const noexcept;

    void addDependencyFlags(vk::DependencyFlags flags) noexcept;

    template <Barrier2Like TBarrier> void add(TBarrier &&barrier)
    {
        using BarrierT = std::remove_cvref_t<TBarrier>;
        if constexpr (std::same_as<BarrierT, vk::MemoryBarrier2>)
        {
            memoryBarriers_.push_back(std::forward<TBarrier>(barrier));
        }
        else if constexpr (std::same_as<BarrierT, vk::BufferMemoryBarrier2> ||
                           std::same_as<BarrierT, vk::ImageMemoryBarrier2>)
        {
            if (barrier.srcQueueFamilyIndex != kIgnoredQueueFamilyIndex &&
                barrier.dstQueueFamilyIndex != kIgnoredQueueFamilyIndex &&
                barrier.srcQueueFamilyIndex != barrier.dstQueueFamilyIndex)
            {
                dependencyFlags_ |= vk::DependencyFlagBits::eQueueFamilyOwnershipTransferUseAllStagesKHR;
            }

            if constexpr (std::same_as<BarrierT, vk::BufferMemoryBarrier2>)
            {
                bufferBarriers_.push_back(std::forward<TBarrier>(barrier));
            }
            else
            {
                imageBarriers_.push_back(std::forward<TBarrier>(barrier));
            }
        }
        else
        {
            static_assert(std::same_as<BarrierT, void>, "Unsupported barrier type.");
        }
    }

    void addBuffer(const Buffer &buffer, vk::BufferMemoryBarrier2 barrier);

    void addImage(const Image &image, vk::ImageMemoryBarrier2 barrier);

    [[nodiscard]] DependencyInfoPacket buildDependencyInfo() const;

  private:
    vk::DependencyFlags dependencyFlags_{};
    std::vector<vk::MemoryBarrier2> memoryBarriers_;
    std::vector<vk::BufferMemoryBarrier2> bufferBarriers_;
    std::vector<vk::ImageMemoryBarrier2> imageBarriers_;
};

/**
 * @brief Apply one sync2 barrier batch into a command buffer.
 */
void pipelineBarrier(const vk::raii::CommandBuffer &commandBuffer, const BarrierBatch &barriers);

/**
 * @brief Convert an original buffer copy region to the pNext-capable copy-2 form.
 */
[[nodiscard]] vk::BufferCopy2 toBufferCopy2(vk::BufferCopy region);

/**
 * @brief Convert an original buffer-image copy region to the pNext-capable copy-2 form.
 */
[[nodiscard]] vk::BufferImageCopy2 toBufferImageCopy2(vk::BufferImageCopy region);

/**
 * @brief Convert an original image copy region to the pNext-capable copy-2 form.
 */
[[nodiscard]] vk::ImageCopy2 toImageCopy2(vk::ImageCopy region);

/**
 * @brief Record a Vulkan 1.3+ copy-buffer command with a pNext-capable region.
 */
void copyBuffer2(const vk::raii::CommandBuffer &commandBuffer, vk::Buffer src, vk::Buffer dst, vk::BufferCopy2 region);

/**
 * @brief Record a Vulkan 1.3+ copy-buffer-to-image command with a pNext-capable region.
 */
void copyBufferToImage2(const vk::raii::CommandBuffer &commandBuffer, vk::Buffer src, vk::Image dst,
                        vk::ImageLayout dstLayout, vk::BufferImageCopy2 region);

/**
 * @brief Record a Vulkan 1.3+ copy-image-to-buffer command with a pNext-capable region.
 */
void copyImageToBuffer2(const vk::raii::CommandBuffer &commandBuffer, vk::Image src, vk::ImageLayout srcLayout,
                        vk::Buffer dst, vk::BufferImageCopy2 region);

/**
 * @brief Record a Vulkan 1.3+ copy-image command with a pNext-capable region.
 */
void copyImage2(const vk::raii::CommandBuffer &commandBuffer, vk::Image src, vk::ImageLayout srcLayout, vk::Image dst,
                vk::ImageLayout dstLayout, vk::ImageCopy2 region);

struct LinearImageUploadChunk
{
    vk::DeviceSize sourceOffset = 0;
    vk::DeviceSize byteSize = 0;
    vk::BufferImageCopy copyRegion{};
};

/**
 * @brief Split one linear buffer-image payload into ring-sized row chunks.
 *
 * Each chunk addresses exactly one array-layer/depth slice and a contiguous
 * row range. Source padding expressed by bufferRowLength/bufferImageHeight is
 * preserved when calculating source offsets, while each emitted copy uses a
 * tightly scoped image extent suitable for an independent transfer submit.
 */
[[nodiscard]] std::vector<LinearImageUploadChunk> planLinearImageUploadChunks(const vk::BufferImageCopy &region,
                                                                              vk::DeviceSize elementSize,
                                                                              vk::DeviceSize ringCapacity);

void copyImageToImage(const vk::raii::CommandBuffer &commandBuffer, vk::Image src, vk::Extent3D srcExtent,
                      vk::ImageAspectFlags srcAspect, vk::Image dst, vk::Extent3D dstExtent,
                      vk::ImageAspectFlags dstAspect, vk::ImageLayout srcLayout = vk::ImageLayout::eTransferSrcOptimal,
                      vk::ImageLayout dstLayout = vk::ImageLayout::eTransferDstOptimal,
                      const vk::ImageCopy &region = {});

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
    std::uint32_t layerCount = 1;
    std::uint32_t viewMask = 0;
    vk::RenderingFlags flags{};
    std::span<const RenderingAttachmentDesc> colorAttachments{};
    std::optional<RenderingDepthStencilAttachmentDesc> depthAttachment{};
    std::optional<RenderingDepthStencilAttachmentDesc> stencilAttachment{};
};

namespace detail
{
[[nodiscard]] vk::RenderingAttachmentInfo makeRenderingAttachmentInfo(const RenderingAttachmentDesc &desc);

[[nodiscard]] vk::RenderingAttachmentInfo makeRenderingAttachmentInfo(const RenderingDepthStencilAttachmentDesc &desc);
} // namespace detail

class ScopedRendering
{
  public:
    ScopedRendering(const vk::raii::CommandBuffer &commandBuffer, const RenderingScopeDesc &desc)
        : commandBuffer_(std::cref(commandBuffer)),
          colorAttachmentInfos_(desc.colorAttachments |
                                std::views::transform([](const RenderingAttachmentDesc &attachment) {
                                    return detail::makeRenderingAttachmentInfo(attachment);
                                }) |
                                std::ranges::to<std::vector>())
    {
        nrAssert(*commandBuffer_.get() != nullptr, "ScopedRendering requires a valid command buffer.");

        renderingInfo_.renderArea = desc.renderArea;
        renderingInfo_.layerCount = desc.layerCount;
        renderingInfo_.viewMask = desc.viewMask;
        renderingInfo_.flags = desc.flags;
        renderingInfo_.colorAttachmentCount = static_cast<std::uint32_t>(colorAttachmentInfos_.size());
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

    ScopedRendering(const ScopedRendering &) = delete;
    ScopedRendering &operator=(const ScopedRendering &) = delete;
    ScopedRendering(ScopedRendering &&) = delete;
    ScopedRendering &operator=(ScopedRendering &&) = delete;

  private:
    std::reference_wrapper<const vk::raii::CommandBuffer> commandBuffer_;
    std::vector<vk::RenderingAttachmentInfo> colorAttachmentInfos_;
    std::optional<vk::RenderingAttachmentInfo> depthAttachmentInfo_{};
    std::optional<vk::RenderingAttachmentInfo> stencilAttachmentInfo_{};
    vk::RenderingInfo renderingInfo_{};
    bool isActive_ = false;
};

struct BufferUploadTicket
{
    std::optional<std::reference_wrapper<const Buffer>> buffer{};
    vk::DeviceSize dstOffset = 0;
    vk::DeviceSize size = 0;
    std::uint64_t signalValue = 0;
    std::optional<QueueOwnershipTransfer> ownership{};

    [[nodiscard]] bool valid() const noexcept;
};

struct ImageUploadTicket
{
    std::optional<std::reference_wrapper<const Image>> image{};
    vk::ImageLayout layout = vk::ImageLayout::eUndefined;
    std::uint64_t signalValue = 0;
    std::optional<QueueOwnershipTransfer> ownership{};

    [[nodiscard]] bool valid() const noexcept;
};

class UploadReadbackContext;

/**
 * @brief Move-only handle for one context-owned readback result.
 *
 * `UploadReadbackContext::readbackBytes` consumes the ticket exactly once.
 * Results not consumed before ring reuse are preserved in host memory until
 * the ticket is consumed or the issuing context is destroyed. A ticket must
 * not outlive its issuing context.
 */
class ReadbackTicket
{
  public:
    ReadbackTicket(const ReadbackTicket &) = delete;
    ReadbackTicket &operator=(const ReadbackTicket &) = delete;
    ReadbackTicket(ReadbackTicket &&other) noexcept
        : context_(std::exchange(other.context_, {})), offset_(std::exchange(other.offset_, 0)),
          size_(std::exchange(other.size_, 0)), signalValue_(std::exchange(other.signalValue_, 0))
    {
    }

    ReadbackTicket &operator=(ReadbackTicket &&) = delete;
    ~ReadbackTicket() = default;

  private:
    friend class UploadReadbackContext;

    ReadbackTicket(const UploadReadbackContext &context, vk::DeviceSize offset, vk::DeviceSize size,
                   std::uint64_t signalValue)
        : context_(std::cref(context)), offset_(offset), size_(size), signalValue_(signalValue)
    {
    }

    std::optional<std::reference_wrapper<const UploadReadbackContext>> context_{};
    vk::DeviceSize offset_ = 0;
    vk::DeviceSize size_ = 0;
    std::uint64_t signalValue_ = 0;
};

/**
 * @brief One sync scope used by readback pre-copy or post-copy barriers.
 */
struct ReadbackSyncScope
{
    vk::PipelineStageFlags2 stages = vk::PipelineStageFlags2{};
    vk::AccessFlags2 access = vk::AccessFlags2{};

    [[nodiscard]] bool valid() const noexcept;
};

/**
 * @brief Readback synchronization plan with explicit pre/post stage+access scopes.
 */
struct ReadbackSyncPlan
{
    ReadbackSyncScope preCopy{};
    ReadbackSyncScope postCopy{};

    [[nodiscard]] bool valid() const noexcept;
};

/**
 * @brief Upload/readback helper built on persistent mapped ring buffers.
 *
 * Upload keeps using a dedicated transfer queue with timeline semaphore handoff.
 * Maintenance9-backed transfer policy omits explicit queue-family ownership
 * transfers when Vulkan guarantees content preservation for the resource.
 * Readback records copy work directly on the source resource queue
 * (graphics or compute only) and uses one readback ring shared by graphics/compute
 * queue families (concurrent mode only when those families differ).
 *
 * Upload path keeps cross-queue execution and memory ordering on semaphores.
 * When QFOT is required it records the source release and destination acquire
 * barriers; when QFOT is omitted, image uploads still record the required layout
 * transition with ignored queue-family indices.
 *
 * Readback path still requires explicit synchronization:
 *   1. pre-copy barrier for producer-write -> transfer-read visibility,
 *   2. timeline wait for submission completion,
 *   3. host invalidate before CPU reads mapped bytes.
 * Destruction waits for submissions owned by this context. Callers must still
 * finish external queue submissions that wait on the exposed upload timeline
 * before destroying a standalone context.
 */
class UploadReadbackContext
{
  public:
    UploadReadbackContext(const vk::raii::Device &device, ResourceFactory &resourceFactory, QueueManager &queueManager,
                          QueueFamilyTransferPolicy queueFamilyTransferPolicy,
                          vk::DeviceSize uploadRingSize = kDefaultUploadReadbackRingSize,
                          vk::DeviceSize readbackRingSize = kDefaultUploadReadbackRingSize);

    UploadReadbackContext(const UploadReadbackContext &) = delete;
    UploadReadbackContext &operator=(const UploadReadbackContext &) = delete;
    UploadReadbackContext(UploadReadbackContext &&) = delete;
    UploadReadbackContext &operator=(UploadReadbackContext &&) = delete;
    ~UploadReadbackContext();

    [[nodiscard]] bool valid() const noexcept;

    [[nodiscard]] const vk::raii::Semaphore &uploadTimelineSemaphore() const noexcept;

    /**
     * @brief Reclaim upload ring allocations whose timeline signal has completed.
     */
    void reclaimCompletedUploads();

    /**
     * @brief Block until upload timeline reaches @p signalValue.
     *
     * Passing `0` waits for the latest upload issued by this context.
     */
    void waitUploadComplete(std::uint64_t signalValue = 0);

    /**
     * @brief Record acquire barrier from upload ticket when the ticket carries ownership.
     */
    void recordAcquireBarrier(const vk::raii::CommandBuffer &commandBuffer, const BufferUploadTicket &ticket) const;

    /**
     * @brief Record acquire barrier from image upload ticket when the ticket carries ownership.
     */
    void recordImageAcquireBarrier(const vk::raii::CommandBuffer &commandBuffer, const ImageUploadTicket &ticket) const;

    /**
     * @brief Upload raw bytes into a destination buffer via transfer queue staging ring.
     *
     * This enforces non-UBO upload policy: transfer copy plus destination queue
     * synchronization. Explicit QFOT is recorded only when required by policy.
     * If `ownership.acquireToTransfer` is present, the first transfer submit waits
     * on `acquireToTransfer.waitSemaphore` and records the matching acquire barrier
     * at the beginning of the transfer pipeline. If it is omitted, this upload path
     * requires a destination on first use or one already owned by the transfer queue.
     * If payload size exceeds ring capacity, data is chunked and submitted in-order.
     * The destination buffer must outlive transfer completion and any recorded
     * acquire operation that consumes the returned ticket.
     */
    [[nodiscard]] BufferUploadTicket uploadBuffer(std::span<const std::byte> data, const Buffer &dst,
                                                  vk::DeviceSize dstOffset, const BufferUploadOwnershipPlan &ownership);

    /**
     * @brief Upload raw bytes into a concurrently-shared destination buffer.
     *
     * This records transfer-queue copy work and returns a timeline ticket, but
     * deliberately does not emit queue-family ownership barriers. Use this only
     * for buffers created with concurrent sharing across the transfer queue and
     * all consumer queue families.
     * The destination buffer must outlive transfer completion.
     */
    [[nodiscard]] BufferUploadTicket uploadBuffer(std::span<const std::byte> data, const Buffer &dst,
                                                  vk::DeviceSize dstOffset);

    /**
     * @brief Upload one image payload via staging buffer -> copyBufferToImage2.
     *
     * This path intentionally does not create a staging image. It performs:
     *   1) optional acquire-to-transfer synchronization and ownership barrier,
     *   2) transition to eTransferDstOptimal,
     *   3) copyBufferToImage2 from upload ring,
     *   4) transition to destination layout and, when required, release to the destination queue.
     * Omitting `ownership.acquireToTransfer` likewise requires first use or existing
     * transfer-queue ownership.
     *
     * Payloads larger than the upload ring are split by array layer, depth
     * slice, and row range. The first submit performs the transfer-layout
     * acquire/transition and the final submit performs the destination release.
     * Layout and ownership transitions cover the whole image even when the copy
     * region selects only a subresource.
     * The destination image must outlive transfer completion and any recorded
     * acquire operation that consumes the returned ticket.
     */
    [[nodiscard]] ImageUploadTicket uploadImage(std::span<const std::byte> data, const Image &dst,
                                                vk::ImageLayout sourceLayout, vk::ImageLayout destinationLayout,
                                                const BufferUploadOwnershipPlan &ownership,
                                                const vk::BufferImageCopy &region = {});

    /**
     * @brief Copy a GPU buffer into readback ring and return a ticket.
     *
     * `queueRole` must be `Graphics` or `Compute`; transfer queue is not supported.
     * The caller provides explicit pre/post stage+access scopes:
     *   1) `syncPlan.preCopy` for producer visibility before copy,
     *   2) `syncPlan.postCopy` for source-resource restore visibility after copy.
     * The source buffer must outlive the readback timeline completion.
     */
    [[nodiscard]] ReadbackTicket readbackBuffer(const Buffer &src, vk::DeviceSize srcOffset, vk::DeviceSize size,
                                                QueueRole queueRole, const ReadbackSyncPlan &syncPlan);

    /**
     * @brief Copy a GPU image into readback ring and return a ticket.
     *
     * `queueRole` must be `Graphics` or `Compute`; transfer queue is not supported.
     * The caller provides explicit pre/post stage+access scopes:
     *   1) `syncPlan.preCopy` + layout transition to transfer-src,
     *   2) transfer-read -> `syncPlan.postCopy`, then layout restore.
     * `region` is payload-relative and tightly packed: buffer offset, row length,
     * and image height must all be zero.
     * The source image must outlive the readback timeline completion.
     */
    [[nodiscard]] ReadbackTicket readbackImage(const Image &src, vk::ImageLayout sourceLayout, QueueRole queueRole,
                                               const ReadbackSyncPlan &syncPlan,
                                               const vk::BufferImageCopy &region = {});

    /**
     * @brief Block until ticket is ready, invalidate cache, copy bytes out, and consume the ticket.
     */
    [[nodiscard]] std::vector<std::byte> readbackBytes(ReadbackTicket &ticket);

  private:
    struct ReadbackRoute
    {
        std::reference_wrapper<GpuQueue> queue;
        std::reference_wrapper<CommandPool> pool;
    };

    struct RingAllocation
    {
        std::uint64_t begin = 0;
        std::uint64_t end = 0;
        vk::DeviceSize offset = 0;
    };

    struct InFlightBatch
    {
        std::uint64_t end = 0;
        std::uint64_t signalValue = 0;
        vk::DeviceSize offset = 0;
        vk::DeviceSize size = 0;
        bool ringReclaimable = true;
        vk::raii::CommandBuffers commandBuffers;
    };

    struct BufferUploadControl
    {
        std::optional<std::reference_wrapper<const BufferUploadOwnershipPlan>> ownership{};
        bool omitReleaseOwnership = false;
        bool omitAcquireToTransferOwnership = false;
        bool ticketCarriesOwnership = false;
    };

    [[nodiscard]] BufferUploadTicket uploadBufferCore(std::span<const std::byte> data, const Buffer &dst,
                                                      vk::DeviceSize dstOffset, const BufferUploadControl &control);

    [[nodiscard]] std::uint64_t consumeNextUploadSignalValue();

    [[nodiscard]] std::uint64_t consumeNextReadbackSignalValue();

    [[nodiscard]] ReadbackRoute readbackRouteFor(QueueRole queueRole);

    [[nodiscard]] std::uint64_t submitUploadCommandBuffers(
        vk::raii::CommandBuffers commandBuffers, const RingAllocation &allocation,
        std::optional<std::reference_wrapper<const QueueOwnershipTransfer>> acquireToTransferWait);

    [[nodiscard]] std::uint64_t submitReadbackCommandBuffers(GpuQueue &readbackQueue,
                                                             vk::raii::CommandBuffers commandBuffers,
                                                             const RingAllocation &allocation);

    [[nodiscard]] static std::vector<std::uint32_t> uniqueReadbackQueueFamilies(
        std::array<std::uint32_t, 2> queueFamilies);

    [[nodiscard]] static bool isReadbackQueueRoleSupported(QueueRole queueRole) noexcept;

    void recordReadbackRingToTransferWriteBarrier(const vk::raii::CommandBuffer &commandBuffer, vk::DeviceSize offset,
                                                  vk::DeviceSize size) const;

    void recordReadbackRingHostVisibilityBarrier(const vk::raii::CommandBuffer &commandBuffer, vk::DeviceSize offset,
                                                 vk::DeviceSize size) const;

    [[nodiscard]] static std::uint64_t alignUp(std::uint64_t value, std::uint64_t alignment);

    [[nodiscard]] static vk::DeviceSize bytesPerPixel(vk::Format format);

    [[nodiscard]] std::uint64_t queryTimelineValue(const vk::raii::Semaphore &timelineSemaphore) const;

    void waitTimelineValue(const vk::raii::Semaphore &timelineSemaphore, std::uint64_t targetValue) const;

    static void reclaimQueue(std::deque<InFlightBatch> &queue, std::uint64_t &reclaimCursor,
                             std::uint64_t completedValue);

    void spillReadback(InFlightBatch &batch);

    [[nodiscard]] RingAllocation reserveUpload(vk::DeviceSize size);

    [[nodiscard]] RingAllocation reserveReadback(vk::DeviceSize size);

    [[nodiscard]] RingAllocation reserveRing(vk::DeviceSize size, vk::DeviceSize capacity, std::uint64_t &writeCursor,
                                             std::uint64_t &reclaimCursor, std::deque<InFlightBatch> &queue,
                                             const vk::raii::Semaphore &timelineSemaphore,
                                             bool requiresCpuConsumption);

    static void addInFlight(std::deque<InFlightBatch> &queue, const RingAllocation &allocation,
                            std::uint64_t signalValue, bool ringReclaimable,
                            vk::raii::CommandBuffers commandBuffers);

    std::optional<std::reference_wrapper<const vk::raii::Device>> device_{};
    std::optional<std::reference_wrapper<QueueManager>> queueManager_{};
    QueueFamilyTransferPolicy queueFamilyTransferPolicy_{};

    Buffer uploadRing_;
    Buffer readbackRing_;
    vk::DeviceSize uploadCapacity_ = 0;
    vk::DeviceSize readbackCapacity_ = 0;

    CommandPool transferPool_;
    CommandPool graphicsReadbackPool_;
    CommandPool computeReadbackPool_;

    vk::raii::Semaphore uploadTimeline_ = {nullptr};
    vk::raii::Semaphore readbackTimeline_ = {nullptr};
    std::uint64_t nextUploadSignalValue_ = 1;
    std::uint64_t nextReadbackSignalValue_ = 1;

    std::uint64_t uploadWriteCursor_ = 0;
    std::uint64_t uploadReclaimCursor_ = 0;
    std::uint64_t readbackWriteCursor_ = 0;
    std::uint64_t readbackReclaimCursor_ = 0;

    std::deque<InFlightBatch> uploadInFlight_;
    std::deque<InFlightBatch> readbackInFlight_;
    std::map<std::uint64_t, std::vector<std::byte>> spilledReadbacks_;
};

} // namespace nr::rhi::ops
