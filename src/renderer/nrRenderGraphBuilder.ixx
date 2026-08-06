export module nr.renderer:renderGraphBuilder;

import dependency.vulkan;
import nr.utils;
import std;
import :renderGraphType;
import :rendererType;

export namespace nr::renderer
{
struct RenderGraphDeclarationCounts
{
    std::size_t nodes = 0;
    std::size_t resources = 0;
    std::size_t frameData = 0;
    std::size_t passes = 0;
    std::size_t submitNodes = 0;

    [[nodiscard]] std::size_t total() const noexcept
    {
        return nodes + resources + frameData + passes + submitNodes;
    }
};

class RenderGraphBuilder
{
  public:
    RenderGraphBuilder() = default;

    void clear();

    [[nodiscard]] GraphNodeHandle addNode(std::string_view debugName, QueueDomain queue);

    [[nodiscard]] GraphNodeHandle addPresentNode(std::string_view debugName);

    // Generic resource addition interface - single method for all descriptor types.
    template <typename TDesc> [[nodiscard]] inline GraphResourceHandle addResource(const TDesc &desc)
    {
        ++declarationCounts_.resources;
        auto handle = GraphResourceHandle{nextResource_++};
        auto index = frame_.resources.size();
        frame_.resources.push_back(GraphResourceDesc{
            .handle = handle,
            .desc = desc,
        });
        auto insertResult = resourceIndexByHandle_.emplace(handle, index);
        nrAssert(insertResult.second, "RenderGraphBuilder::addResource generated a duplicate resource handle.");
        return handle;
    }

    template <typename TPayload>
    [[nodiscard]] inline GraphFrameDataHandle addFrameData(std::string_view debugName, TPayload &&payload)
    {
        using Payload = std::remove_cvref_t<TPayload>;
        static_assert(std::copy_constructible<Payload>,
                      "RenderGraphBuilder::addFrameData requires copy-constructible frame data.");

        ++declarationCounts_.frameData;
        auto handle = GraphFrameDataHandle{nextFrameData_++};
        frame_.frameData.push_back(GraphFrameDataDesc{
            .handle = handle,
            .debugName = std::string(debugName),
            .payload = std::make_any<Payload>(std::forward<TPayload>(payload)),
        });
        return handle;
    }

    [[nodiscard]] GraphPassHandle addPass(std::string_view debugName, GraphNodeHandle node,
                                          std::span<const PassResourceUseDesc> intentList,
                                          PassRecordCallback executeLambda,
                                          PassPrepareCallback prepareCallback = nullptr, bool isCopyPass = false,
                                          vk::PipelineStageFlags2 shaderStages = vk::PipelineStageFlags2{},
                                          std::span<const GraphFrameDataHandle> frameDataUses = {});

    [[nodiscard]] GraphPassHandle addPass(std::string_view debugName, GraphNodeHandle node,
                                          std::span<const PassResourceUseDesc> intentList,
                                          PassParallelRecordDesc parallelRecord,
                                          PassPrepareCallback prepareCallback = nullptr,
                                          vk::PipelineStageFlags2 shaderStages = vk::PipelineStageFlags2{},
                                          std::span<const GraphFrameDataHandle> frameDataUses = {});

    [[nodiscard]] GraphPassHandle addCopyPass(std::string_view debugName, GraphNodeHandle node, CopyPassDesc copy);

    [[nodiscard]] GraphSubmitHandle addSubmitNode(std::string_view debugName,
                                                  SubmitBoundaryKind kind = SubmitBoundaryKind::QueueSubmission);

    [[nodiscard]] const RenderGraphFrameDescription &frame() const noexcept;

    [[nodiscard]] RenderGraphFrameDescription &mutableFrame() noexcept;

    [[nodiscard]] RenderGraphFrameDescription build() const;

    [[nodiscard]] RenderGraphDeclarationCounts declarationCounts() const noexcept;

  private:
    [[nodiscard]] GraphPassHandle addPassCore(std::string_view debugName, GraphNodeHandle node, bool isCopyPass,
                                              vk::PipelineStageFlags2 shaderStages);

    [[nodiscard]] std::vector<PassExecutionDesc>::iterator findPass(GraphPassHandle handle);

    [[nodiscard]] const GraphResourceDesc &resourceDesc(GraphResourceHandle handle) const;

    [[nodiscard]] ImageAspectIntent imageAspectFor(GraphResourceHandle resource,
                                                   std::optional<ImageAspectIntent> requestedAspect,
                                                   vk::ImageAspectFlags regionAspect = vk::ImageAspectFlags{}) const;

    [[nodiscard]] std::vector<PassResourceUseDesc> makeCopyPassResourceUses(const CopyPassDesc &copy) const;

    [[nodiscard]] static bool isBufferResourceDesc(const GraphResourceDesc &desc) noexcept;

    [[nodiscard]] static bool isImageResourceDesc(const GraphResourceDesc &desc) noexcept;

    [[nodiscard]] static bool isAccelerationStructureResourceDesc(const GraphResourceDesc &desc) noexcept;

    [[nodiscard]] static bool hasBufferIntentFields(const PassResourceUseDesc &use) noexcept;

    [[nodiscard]] static bool hasAccelerationStructureIntentFields(const PassResourceUseDesc &use) noexcept;

    [[nodiscard]] static bool hasImageIntentFields(const PassResourceUseDesc &use) noexcept;

    [[nodiscard]] static bool bufferAccessUsesShaderStages(BufferAccessIntent intent) noexcept;

    [[nodiscard]] static bool imageAccessUsesShaderStages(ImageAccessIntent intent) noexcept;

    [[nodiscard]] static bool isCopySourceUse(const PassResourceUseDesc &use) noexcept;

    [[nodiscard]] static bool isCopyDestinationUse(const PassResourceUseDesc &use) noexcept;

    [[nodiscard]] static bool isPresentUse(const PassResourceUseDesc &use) noexcept;

    [[nodiscard]] static bool isImageCopyDestinationUse(const PassResourceUseDesc &use) noexcept;

    [[nodiscard]] static bool isImplicitCopyPresentTransition(const PassResourceUseDesc &previous,
                                                              const PassResourceUseDesc &current) noexcept;

    static void validatePassCallbackContract(const PassRecordCallback &executeLambda,
                                             const std::optional<PassParallelRecordDesc> &parallelRecord,
                                             bool isCopyPass);

    void validatePassResourceUse(const PassResourceUseDesc &use) const;

    [[nodiscard]] std::vector<PassResourceUseDesc> canonicalizePassResourceUses(
        std::span<const PassResourceUseDesc> intentList, bool allowImplicitCopyPresentTransition) const;

    [[nodiscard]] std::vector<GraphFrameDataHandle>
    canonicalizeFrameDataUses(std::span<const GraphFrameDataHandle> frameDataUses) const;

    static void validateCopyPassResourceUses(std::span<const PassResourceUseDesc> resourceUses);

    [[nodiscard]] std::vector<GraphNodeDesc>::const_iterator findNode(GraphNodeHandle handle) const;

    [[nodiscard]] bool containsResource(GraphResourceHandle handle) const;

    [[nodiscard]] bool containsFrameData(GraphFrameDataHandle handle) const;

    RenderGraphFrameDescription frame_{};
    std::map<GraphResourceHandle, std::size_t> resourceIndexByHandle_{};
    std::uint32_t nextResource_ = 0;
    std::uint32_t nextFrameData_ = 0;
    std::uint32_t nextPass_ = 0;
    std::uint32_t nextNode_ = 0;
    std::uint32_t nextSubmit_ = 0;
    RenderGraphDeclarationCounts declarationCounts_{};
};

} // namespace nr::renderer
