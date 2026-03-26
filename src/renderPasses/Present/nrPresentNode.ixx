module;
export module nr.renderPasses:presentNode;

import dependency;
import nr.renderer;
import nr.rhi;
import nr.utils;
import std;
import :nodeType;

export namespace nr::renderPasses
{
struct PresentNodeInput
{
    vk::Extent2D viewportExtent{1, 1};
    vk::Format format = vk::Format::eR8G8B8A8Unorm;
};

struct PresentNodeOutput
{
    nr::renderer::GraphResourceHandle swapchainImage{};
};

class PresentNode final : public Node
{
  public:
    PresentNodeInput input{};
    PresentNodeOutput output{};

    [[nodiscard]] NodeDescription describe() const override
    {
        return NodeDescription{
            .name = "Present",
            .inputPorts = {
                NodePort{.name = "sourceColor"},
            },
            .outputPorts = {
                NodePort{.name = "swapchain"},
            },
        };
    }

    void initialize(NodeInitContext&) override
    {
        // Phase 1: No persistent runtime state needed for single copy pass.
    }

    void build(NodeBuildContext& context, const NodeFrameParameters& frameParameters) override
    {
        auto sourceColor = context.resolveInput("sourceColor");
        nr::nrAssert(sourceColor.valid(), "Present node requires a valid sourceColor input from upstream graph connection.");

        auto viewportExtent = input.viewportExtent;
        if (viewportExtent.width == 1 && viewportExtent.height == 1)
        {
            viewportExtent = frameParameters.swapchainExtent;
        }

        auto swapchainImage = context.addResource(nr::renderer::GraphImportedSwapchainImageDesc{
            .debugName = "Swapchain.Image",
            .lifetime = nr::renderer::ResourceLifetime::SwapchainRelative,
            .residency = nr::renderer::ResourceResidency::Swapchain,
            .initialOwnership = nr::renderer::ResourceOwnershipDomain::Compute,
            .swapchainImageIndex = frameParameters.swapchainImageIndex,
            .extent = vk::Extent3D{viewportExtent.width, viewportExtent.height, 1},
            .format = frameParameters.swapchainFormat == vk::Format::eUndefined
                          ? input.format
                          : frameParameters.swapchainFormat,
        });

        output.swapchainImage = swapchainImage;

        // Phase 1: Single copy pass with direct image copy operation.
        auto passIntents = std::array{
            nr::renderer::PassResourceUseDesc{
                .resource = sourceColor,
                .imageUsage = nr::renderer::ImageUsageIntent::TransferSrc,
                .imageAccess = nr::renderer::ImageAccessIntent::TransferRead,
                .imageLayout = nr::renderer::ImageLayoutIntent::TransferSrc,
                .imageAspect = nr::renderer::ImageAspectIntent::Color,
                .ownershipDomain = nr::renderer::ResourceOwnershipDomain::Undefined,
                .readOnly = true,
            },
            nr::renderer::PassResourceUseDesc{
                .resource = swapchainImage,
                .imageUsage = nr::renderer::ImageUsageIntent::TransferDst,
                .imageAccess = nr::renderer::ImageAccessIntent::TransferWrite,
                .imageLayout = nr::renderer::ImageLayoutIntent::TransferDst,
                .imageAspect = nr::renderer::ImageAspectIntent::Color,
                .ownershipDomain = nr::renderer::ResourceOwnershipDomain::Undefined,
                .readOnly = false,
            },
            nr::renderer::PassResourceUseDesc{
                .resource = swapchainImage,
                .imageUsage = nr::renderer::ImageUsageIntent::PresentSource,
                .imageAccess = nr::renderer::ImageAccessIntent::PresentRead,
                .imageLayout = nr::renderer::ImageLayoutIntent::PresentSrc,
                .imageAspect = nr::renderer::ImageAspectIntent::Color,
                .ownershipDomain = nr::renderer::ResourceOwnershipDomain::Compute,
                .readOnly = true,
            },
        };

        auto sourceHandle = sourceColor;
        auto swapchainHandle = swapchainImage;

        [[maybe_unused]] auto copyPassHandle = context.addPass(
            std::span<const nr::renderer::PassResourceUseDesc>{passIntents.data(), passIntents.size()},
            "Present.CopyToSwapchain",
            [sourceHandle, swapchainHandle](const nr::renderer::PassRecordContext& recordContext) {
                nr::nrAssert(static_cast<bool>(recordContext.resolveImage), "Present pass requires image resolver callback.");
                nr::nrAssert(recordContext.commandBuffer.has_value(), "Present pass requires RAII command buffer access.");

                auto sourceImage = recordContext.resolveImage(sourceHandle);
                auto swapchainImageResolved = recordContext.resolveImage(swapchainHandle);

                nr::nrAssert(sourceImage.has_value(), "Present pass failed to resolve source color image resource.");
                nr::nrAssert(swapchainImageResolved.has_value(), "Present pass failed to resolve swapchain image resource.");
                nr::nrAssert(sourceImage->image != vk::Image{}, "Present pass requires a valid source color image handle.");
                nr::nrAssert(swapchainImageResolved->image != vk::Image{}, "Present pass requires a valid swapchain image handle.");

                auto& commandBuffer = recordContext.commandBuffer->get();

                nr::rhi::ops::copyImageToImage(
                    *commandBuffer,
                    sourceImage->image,
                    sourceImage->extent,
                    sourceImage->subresourceRange.aspectMask,
                    swapchainImageResolved->image,
                    swapchainImageResolved->extent,
                    swapchainImageResolved->subresourceRange.aspectMask,
                    vk::ImageLayout::eTransferSrcOptimal,
                    vk::ImageLayout::eTransferDstOptimal);

                auto postCopyBarriers = nr::rhi::ops::BarrierBatch{};
                postCopyBarriers.add(vk::ImageMemoryBarrier2{
                    vk::PipelineStageFlagBits2::eTransfer,
                    vk::AccessFlagBits2::eTransferWrite,
                    vk::PipelineStageFlagBits2::eBottomOfPipe,
                    vk::AccessFlags2{},
                    vk::ImageLayout::eTransferDstOptimal,
                    vk::ImageLayout::ePresentSrcKHR,
                    nr::rhi::ops::kIgnoredQueueFamilyIndex,
                    nr::rhi::ops::kIgnoredQueueFamilyIndex,
                    swapchainImageResolved->image,
                    swapchainImageResolved->subresourceRange,
                    nullptr,
                });
                nr::rhi::ops::pipelineBarrier(*commandBuffer, postCopyBarriers);
            },
            nullptr,
            true);

        context.publishOutput("swapchain", swapchainImage);
    }

    void shutdown(NodeShutdownContext&) override
    {
        // Phase 1: No persistent state to release.
    }
};
} // namespace nr::renderPasses