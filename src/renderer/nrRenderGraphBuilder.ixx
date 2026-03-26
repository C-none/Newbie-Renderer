module;
export module nr.renderer:renderGraphBuilder;

import nr.utils;
import std;
import :renderGraphType;
import :rendererType;

namespace nr::renderer
{
// Forward declaration for RenderGraphBuilder
class RenderGraphBuilder;

// Internal implementation class - not part of the public API
class RenderGraphNodeContext
{
  public:
    RenderGraphNodeContext(RenderGraphBuilder& builder, GraphNodeHandle node) noexcept
        : builder_(builder)
        , node(node)
    {
    }

    [[nodiscard]] GraphNodeHandle nodeHandle() const noexcept
    {
        return node;
    }

    [[nodiscard]] RenderGraphBuilder& builder() noexcept
    {
        return builder_.get();
    }

    // Generic resource addition interface - single method for all descriptor types.
    template <typename TDesc>
    [[nodiscard]] GraphResourceHandle addResource(const TDesc& desc);

    // Canonical node pass authoring path:
    // addPass(intentList, name, executeLambda[, prepareCallback][, isCopyPass]).
    [[nodiscard]] GraphPassHandle addPass(
        std::span<const PassResourceUseDesc> intentList,
        std::string_view debugName,
        PassRecordCallback executeLambda,
        PassPrepareCallback prepareCallback = nullptr,
        bool isCopyPass = false);

    [[nodiscard]] GraphSubmitHandle addSubmitNode(
        std::string_view debugName,
        SubmitBoundaryKind kind = SubmitBoundaryKind::Explicit);

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

    inline void clear()
    {
        frame_ = RenderGraphFrameDescription{};
        nextResource_ = 0;
        nextPass_ = 0;
        nextNode_ = 0;
        nextSubmit_ = 0;
    }

    [[nodiscard]] inline GraphNodeHandle addNode(
        std::string_view debugName,
        QueueDomain queue)
    {
        auto handle = GraphNodeHandle{nextNode_++};
        frame_.nodes.push_back(GraphNodeDesc{
            .handle = handle,
            .debugName = std::string(debugName),
            .queue = queue,
        });
        return handle;
    }

    [[nodiscard]] inline GraphNodeHandle addPresentNode(std::string_view debugName)
    {
        auto handle = GraphNodeHandle{nextNode_++};
        frame_.nodes.push_back(GraphNodeDesc{
            .handle = handle,
            .debugName = std::string(debugName),
            .queue = QueueDomain::Compute,
        });
        return handle;
    }

    [[nodiscard]] inline RenderGraphNodeContext makeNodeContext(GraphNodeHandle node)
    {
        nrAssert(node.valid(), "RenderGraphBuilder::makeNodeContext requires a valid node handle.");
        nrAssert(containsNode(node), "RenderGraphBuilder::makeNodeContext requires an existing node handle.");
        return RenderGraphNodeContext{*this, node};
    }

    // Generic resource addition interface - single method for all descriptor types.
    template <typename TDesc>
    [[nodiscard]] inline GraphResourceHandle addResource(const TDesc& desc)
    {
        auto handle = GraphResourceHandle{nextResource_++};
        frame_.resources.push_back(GraphResourceDesc{
            .handle = handle,
            .desc = desc,
        });
        return handle;
    }

    [[nodiscard]] inline GraphPassHandle addPass(
        std::string_view debugName,
        GraphNodeHandle node,
        std::span<const PassResourceUseDesc> intentList,
        PassRecordCallback executeLambda,
        PassPrepareCallback prepareCallback = nullptr,
        bool isCopyPass = false)
    {
        auto passHandle = addPassCore(debugName, node, isCopyPass);

        /// Add resource use intents
        std::ranges::for_each(intentList, [&](const PassResourceUseDesc& use) {
            auto passIt = findPass(passHandle);
            nrAssert(passIt != frame_.passes.end(), "RenderGraphBuilder::addPass resource use hook failed.");
            nrAssert(use.resource.valid(), "RenderGraphBuilder::addPass requires a valid resource handle.");
            nrAssert(containsResource(use.resource), "RenderGraphBuilder::addPass resource handle validation failed.");
            passIt->resourceUses.push_back(use);
        });

        /// Attach prepare callback if provided
        if (prepareCallback)
        {
            auto passIt = findPass(passHandle);
            nrAssert(passIt != frame_.passes.end(), "RenderGraphBuilder::addPass prepare callback hook failed.");
            passIt->prepare = std::move(prepareCallback);
        }

        /// Attach record callback if provided
        if (executeLambda)
        {
            auto passIt = findPass(passHandle);
            nrAssert(passIt != frame_.passes.end(), "RenderGraphBuilder::addPass record callback hook failed.");
            passIt->record = std::move(executeLambda);
        }

        return passHandle;
    }

    [[nodiscard]] inline GraphSubmitHandle addSubmitNode(
        std::string_view debugName,
        SubmitBoundaryKind kind = SubmitBoundaryKind::Explicit)
    {
        auto handle = GraphSubmitHandle{nextSubmit_++};
        frame_.submitBoundaries.push_back(SubmitBoundaryDesc{
            .handle = handle,
            .debugName = std::string(debugName),
            .kind = kind,
        });
        frame_.executionOrder.push_back(handle);
        return handle;
    }

    [[nodiscard]] inline const RenderGraphFrameDescription& frame() const noexcept
    {
        return frame_;
    }

    [[nodiscard]] inline RenderGraphFrameDescription build() const
    {
        return frame_;
    }

  private:
    [[nodiscard]] inline GraphPassHandle addPassCore(
        std::string_view debugName,
        GraphNodeHandle node,
        bool isCopyPass)
    {
        nrAssert(node.valid(), "RenderGraphBuilder::addPass requires a valid node handle.");

        auto nodeIt = findNode(node);
        nrAssert(nodeIt != frame_.nodes.end(), "RenderGraphBuilder::addPass requires a registered node handle.");

        auto handle = GraphPassHandle{nextPass_++};
        frame_.passes.push_back(PassExecutionDesc{
            .handle = handle,
            .node = node,
            .debugName = std::string(debugName),
            .isCopyPass = isCopyPass,
            .queue = nodeIt->queue,
            .resourceUses = {},
            .record = {},
        });
        frame_.executionOrder.push_back(handle);
        return handle;
    }

    [[nodiscard]] inline std::vector<PassExecutionDesc>::iterator findPass(GraphPassHandle handle)
    {
        return std::ranges::find_if(frame_.passes, [handle](const PassExecutionDesc& desc) {
            return desc.handle == handle;
        });
    }

    [[nodiscard]] inline std::vector<GraphNodeDesc>::const_iterator findNode(GraphNodeHandle handle) const
    {
        return std::ranges::find_if(frame_.nodes, [handle](const GraphNodeDesc& desc) {
            return desc.handle == handle;
        });
    }

    [[nodiscard]] inline bool containsNode(GraphNodeHandle handle) const
    {
        return findNode(handle) != frame_.nodes.end();
    }

    [[nodiscard]] inline bool containsResource(GraphResourceHandle handle) const
    {
        return std::ranges::any_of(frame_.resources, [handle](const GraphResourceDesc& desc) {
            return desc.handle == handle;
        });
    }

    RenderGraphFrameDescription frame_{};
    std::uint32_t nextResource_ = 0;
    std::uint32_t nextPass_ = 0;
    std::uint32_t nextNode_ = 0;
    std::uint32_t nextSubmit_ = 0;
};

template <typename TDesc>
inline GraphResourceHandle RenderGraphNodeContext::addResource(const TDesc& desc)
{
    return builder_.get().addResource(desc);
}

inline GraphPassHandle RenderGraphNodeContext::addPass(
    std::span<const PassResourceUseDesc> intentList,
    std::string_view debugName,
    PassRecordCallback executeLambda,
    PassPrepareCallback prepareCallback,
    bool isCopyPass)
{
    return builder_.get().addPass(
        debugName,
        node,
        intentList,
        std::move(executeLambda),
        std::move(prepareCallback),
        isCopyPass);
}

inline GraphSubmitHandle RenderGraphNodeContext::addSubmitNode(
    std::string_view debugName,
    SubmitBoundaryKind kind)
{
    return builder_.get().addSubmitNode(debugName, kind);
}
} // namespace nr::renderer