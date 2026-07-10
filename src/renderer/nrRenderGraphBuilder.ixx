export module nr.renderer:renderGraphBuilder;

import dependency.vulkan;
import nr.utils;
import std;
import :renderGraphType;
import :rendererType;

export namespace nr::renderer
{
class RenderGraphBuilder;
}

namespace nr::renderer
{

// Internal implementation class - not part of the public API
class RenderGraphNodeContext
{
  public:
    RenderGraphNodeContext(RenderGraphBuilder& builder, GraphNodeHandle node) noexcept;

    [[nodiscard]] GraphNodeHandle nodeHandle() const noexcept;

    [[nodiscard]] RenderGraphBuilder& builder() noexcept;

    // Generic resource addition interface - single method for all descriptor types.
    template <typename TDesc>
    [[nodiscard]] GraphResourceHandle addResource(const TDesc& desc);

    template <typename TPayload>
    [[nodiscard]] GraphFrameDataHandle addFrameData(std::string_view debugName, TPayload&& payload);

    // Canonical node pass authoring path:
    // addPass(intentList, name, executeLambda[, prepareCallback][, isCopyPass]).
    [[nodiscard]] GraphPassHandle addPass(
        std::span<const PassResourceUseDesc> intentList,
        std::string_view debugName,
        PassRecordCallback executeLambda,
        PassPrepareCallback prepareCallback = nullptr,
        bool isCopyPass = false,
        vk::PipelineStageFlags2 shaderStages = vk::PipelineStageFlags2{});

    [[nodiscard]] GraphPassHandle addPass(
        std::span<const PassResourceUseDesc> intentList,
        std::string_view debugName,
        PassParallelRecordDesc parallelRecord,
        PassPrepareCallback prepareCallback = nullptr,
        vk::PipelineStageFlags2 shaderStages = vk::PipelineStageFlags2{});

    [[nodiscard]] GraphPassHandle addCopyPass(
        std::string_view debugName,
        CopyPassDesc copy);

    [[nodiscard]] GraphSubmitHandle addSubmitNode(
        std::string_view debugName);

  private:
    std::reference_wrapper<RenderGraphBuilder> builder_;
    GraphNodeHandle node{};
};

} // namespace nr::renderer (internal)

export namespace nr::renderer
{
class RenderGraphBuilder
{
  public:
    RenderGraphBuilder() = default;

    void clear();

    [[nodiscard]] GraphNodeHandle addNode(
        std::string_view debugName,
        QueueDomain queue);

    [[nodiscard]] GraphNodeHandle addPresentNode(std::string_view debugName);

    [[nodiscard]] RenderGraphNodeContext makeNodeContext(GraphNodeHandle node);

    // Generic resource addition interface - single method for all descriptor types.
    template <typename TDesc>
    [[nodiscard]] inline GraphResourceHandle addResource(const TDesc& desc)
    {
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
    [[nodiscard]] inline GraphFrameDataHandle addFrameData(std::string_view debugName, TPayload&& payload)
    {
        using Payload = std::remove_cvref_t<TPayload>;
        static_assert(
            std::copy_constructible<Payload>,
            "RenderGraphBuilder::addFrameData requires copy-constructible frame data.");

        auto handle = GraphFrameDataHandle{nextFrameData_++};
        frame_.frameData.push_back(GraphFrameDataDesc{
            .handle = handle,
            .debugName = std::string(debugName),
            .payload = std::make_any<Payload>(std::forward<TPayload>(payload)),
        });
        return handle;
    }

    [[nodiscard]] GraphPassHandle addPass(
        std::string_view debugName,
        GraphNodeHandle node,
        std::span<const PassResourceUseDesc> intentList,
        PassRecordCallback executeLambda,
        PassPrepareCallback prepareCallback = nullptr,
        bool isCopyPass = false,
        vk::PipelineStageFlags2 shaderStages = vk::PipelineStageFlags2{});

    [[nodiscard]] GraphPassHandle addPass(
        std::string_view debugName,
        GraphNodeHandle node,
        std::span<const PassResourceUseDesc> intentList,
        PassParallelRecordDesc parallelRecord,
        PassPrepareCallback prepareCallback = nullptr,
        vk::PipelineStageFlags2 shaderStages = vk::PipelineStageFlags2{});

    [[nodiscard]] GraphPassHandle addCopyPass(
        std::string_view debugName,
        GraphNodeHandle node,
        CopyPassDesc copy);

    [[nodiscard]] GraphSubmitHandle addSubmitNode(
        std::string_view debugName);

    [[nodiscard]] const RenderGraphFrameDescription& frame() const noexcept;

    [[nodiscard]] RenderGraphFrameDescription& mutableFrame() noexcept;

    [[nodiscard]] RenderGraphFrameDescription build() const;

  private:
    [[nodiscard]] GraphPassHandle addPassCore(
        std::string_view debugName,
        GraphNodeHandle node,
        bool isCopyPass,
        vk::PipelineStageFlags2 shaderStages);

    [[nodiscard]] std::vector<PassExecutionDesc>::iterator findPass(GraphPassHandle handle);

    [[nodiscard]] const GraphResourceDesc& resourceDesc(GraphResourceHandle handle) const;

    [[nodiscard]] ImageAspectIntent imageAspectFor(
        GraphResourceHandle resource,
        std::optional<ImageAspectIntent> requestedAspect,
        vk::ImageAspectFlags regionAspect = vk::ImageAspectFlags{}) const;

    [[nodiscard]] std::vector<PassResourceUseDesc> makeCopyPassResourceUses(const CopyPassDesc& copy) const;

    [[nodiscard]] static bool isBufferResourceDesc(const GraphResourceDesc& desc) noexcept;

    [[nodiscard]] static bool isImageResourceDesc(const GraphResourceDesc& desc) noexcept;

    [[nodiscard]] static bool isAccelerationStructureResourceDesc(const GraphResourceDesc& desc) noexcept;

    [[nodiscard]] static bool hasBufferIntentFields(const PassResourceUseDesc& use) noexcept;

    [[nodiscard]] static bool hasAccelerationStructureIntentFields(const PassResourceUseDesc& use) noexcept;

    [[nodiscard]] static bool hasImageIntentFields(const PassResourceUseDesc& use) noexcept;

    [[nodiscard]] static bool bufferAccessReads(BufferAccessIntent intent) noexcept;

    [[nodiscard]] static bool bufferAccessWrites(BufferAccessIntent intent) noexcept;

    [[nodiscard]] static bool imageAccessReads(ImageAccessIntent intent) noexcept;

    [[nodiscard]] static bool imageAccessWrites(ImageAccessIntent intent) noexcept;

    [[nodiscard]] static bool bufferAccessUsesShaderStages(BufferAccessIntent intent) noexcept;

    [[nodiscard]] static bool imageAccessUsesShaderStages(ImageAccessIntent intent) noexcept;

    [[nodiscard]] static bool accelerationStructureAccessReads(AccelerationStructureAccessIntent intent) noexcept;

    [[nodiscard]] static bool accelerationStructureAccessWrites(AccelerationStructureAccessIntent intent) noexcept;

    [[nodiscard]] static bool accelerationStructureUsageReads(AccelerationStructureUsageIntent intent) noexcept;

    [[nodiscard]] static bool accelerationStructureUsageWrites(AccelerationStructureUsageIntent intent) noexcept;

    [[nodiscard]] static bool bufferUsageReads(BufferUsageIntent intent) noexcept;

    [[nodiscard]] static bool bufferUsageWrites(BufferUsageIntent intent) noexcept;

    [[nodiscard]] static bool imageUsageReads(ImageUsageIntent intent) noexcept;

    [[nodiscard]] static bool imageUsageWrites(ImageUsageIntent intent) noexcept;

    [[nodiscard]] static bool isCopySourceUse(const PassResourceUseDesc& use) noexcept;

    [[nodiscard]] static bool isCopyDestinationUse(const PassResourceUseDesc& use) noexcept;

    [[nodiscard]] static bool isPresentUse(const PassResourceUseDesc& use) noexcept;

    [[nodiscard]] static bool isImageCopyDestinationUse(const PassResourceUseDesc& use) noexcept;

    static void validatePassCallbackContract(
        const PassRecordCallback& executeLambda,
        const std::optional<PassParallelRecordDesc>& parallelRecord,
        bool isCopyPass);

    static void validatePassUseReadOnlyContract(const PassResourceUseDesc& use);

    void validatePassResourceUse(const PassResourceUseDesc& use) const;

    void validatePassResourceUses(
        std::span<const PassResourceUseDesc> intentList,
        bool isCopyPass) const;

    [[nodiscard]] std::vector<GraphNodeDesc>::const_iterator findNode(GraphNodeHandle handle) const;

    [[nodiscard]] bool containsNode(GraphNodeHandle handle) const;

    [[nodiscard]] bool containsResource(GraphResourceHandle handle) const;

    RenderGraphFrameDescription frame_{};
    std::map<GraphResourceHandle, std::size_t> resourceIndexByHandle_{};
    std::uint32_t nextResource_ = 0;
    std::uint32_t nextFrameData_ = 0;
    std::uint32_t nextPass_ = 0;
    std::uint32_t nextNode_ = 0;
    std::uint32_t nextSubmit_ = 0;
};

template <typename TDesc>
inline GraphResourceHandle RenderGraphNodeContext::addResource(const TDesc& desc)
{
    return builder_.get().addResource(desc);
}

template <typename TPayload>
inline GraphFrameDataHandle RenderGraphNodeContext::addFrameData(std::string_view debugName, TPayload&& payload)
{
    return builder_.get().addFrameData(debugName, std::forward<TPayload>(payload));
}

} // namespace nr::renderer
