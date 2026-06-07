export module nr.renderer:renderer;
import dependency;

import nr.rhi;
import nr.scene;
import nr.resource;
import nr.utils;
import std;
import :frameServices;
import :renderGraphBuilder;
import :renderGraphCompiler;
import :renderGraphExecutor;
import :rendererSubmission;

export namespace nr::renderer
{
struct RendererCreateInfo
{
    std::string appName = "NewbieRenderer";
    std::string engineName = "NewbieRenderer";
};

struct NodePort
{
    std::string name{};
};

struct NodeConfig
{
    std::string instanceName{};
    QueueDomain queue = QueueDomain::Graphics;
};

struct NodeDescription
{
    std::string name{};
    std::vector<NodePort> inputPorts{};
    std::vector<NodePort> outputPorts{};
};

struct NodeFrameParameters
{
    std::uint32_t frameIndex = 0;
    std::uint32_t swapchainImageIndex = 0;
    vk::Extent2D swapchainExtent{1, 1};
    vk::Format swapchainFormat = vk::Format::eUndefined;

    std::optional<std::reference_wrapper<const nr::scene::SceneBridgeFrame>> sceneBridgeFrame{};
    std::optional<std::reference_wrapper<const nr::scene::ScenePacketSet>> scenePackets{};
    std::optional<std::reference_wrapper<const nr::scene::SceneResolvedCamera>> primaryCamera{};
    std::optional<std::reference_wrapper<FrameServices>> frameServices{};
};

struct RendererCameraOverride
{
    nr::scene::SceneBridgeFrameConstants frameConstants{};
    nr::scene::SceneFrustum frustum{};
};

struct NodeInitContext
{
    std::reference_wrapper<nr::rhi::Device> device;
};

struct NodeShutdownContext
{
    std::reference_wrapper<nr::rhi::Device> device;
};

struct NodeBuildContext
{
    std::reference_wrapper<RenderGraphBuilder> graphBuilder;
    GraphNodeHandle nodeHandle{};
    std::function<GraphResourceHandle(std::string_view)> resolveInputPort{};
    std::function<void(std::string_view, GraphResourceHandle)> publishOutputPort{};

    [[nodiscard]] GraphResourceHandle resolveInput(std::string_view portName) const
    {
        if (!resolveInputPort)
        {
            return {};
        }
        return resolveInputPort(portName);
    }

    void publishOutput(std::string_view portName, GraphResourceHandle resource)
    {
        if (!publishOutputPort)
        {
            return;
        }
        publishOutputPort(portName, resource);
    }

    // Node-scoped graph authoring helpers: Generic resource addition interface.
    template <typename TDesc>
    [[nodiscard]] GraphResourceHandle addResource(const TDesc& desc)
    {
        return graphBuilder.get().addResource(desc);
    }

    [[nodiscard]] GraphPassHandle addPass(
        std::span<const PassResourceUseDesc> intentList,
        std::string_view debugName,
        PassRecordCallback executeLambda,
        PassPrepareCallback prepareCallback = nullptr,
        bool isCopyPass = false)
    {
        return graphBuilder.get().addPass(
            debugName,
            nodeHandle,
            intentList,
            std::move(executeLambda),
            std::move(prepareCallback),
            isCopyPass);
    }

    [[nodiscard]] GraphSubmitHandle addSubmitNode(
        std::string_view debugName,
        SubmitBoundaryKind kind = SubmitBoundaryKind::Explicit)
    {
        return graphBuilder.get().addSubmitNode(debugName, kind);
    }
};

class NodeRuntime
{
  public:
    virtual ~NodeRuntime() = default;

    [[nodiscard]] virtual NodeDescription describe() const = 0;

        // Stage 1 (initialize): create persistent node state.
        // Typical work: shader/pipeline creation and long-lived GPU allocations.
    virtual void initialize(NodeInitContext&)
    {
    }

        // Stage 2 (build): declare per-frame intents and register execute lambdas.
        // Canonical path: context.addPass(intentList, name, executeLambda[, isCopyPass]).
        // Build should capture stable per-pass snapshots used later by execute lambdas.
    virtual void build(NodeBuildContext& context, const NodeFrameParameters& frameParameters) = 0;

        // Stage 3 (shutdown): release persistent node state.
    virtual void shutdown(NodeShutdownContext&)
    {
    }
};

struct NodeCreateInfo
{
    std::shared_ptr<NodeRuntime> runtime{};
    NodeConfig config{};
};

struct NodePortRef
{
    std::string nodeName{};
    std::string portName{};
};

struct NodeConnection
{
    NodePortRef from{};
    NodePortRef to{};
};

struct SubmitNodeSpec
{
    std::string debugName{};
    SubmitBoundaryKind kind = SubmitBoundaryKind::Explicit;
    std::size_t afterNodeIndex = 0;
};

struct RendererGraphSpec
{
    std::vector<NodeCreateInfo> nodes{};
    std::vector<NodeConnection> connections{};
    std::vector<SubmitNodeSpec> submitNodes{};
};

struct RendererFrameInput
{
    std::optional<std::reference_wrapper<nr::scene::Scene>> scene{};
    std::uint64_t acquireTimeout = std::numeric_limits<std::uint64_t>::max();
    std::optional<nr::scene::SceneExtractInput> sceneExtractInput{};
    std::optional<RendererCameraOverride> cameraOverride{};
    std::optional<std::reference_wrapper<FrameServices>> frameServices{};
};

struct RendererFrameResult
{
    bool rendered = false;
    std::uint32_t frameIndex = 0;
    std::uint32_t swapchainImageIndex = 0;
    vk::Result presentResult = vk::Result::eSuccess;

    std::size_t compiledSubmitBatchCount = 0;
    std::size_t submittedBatchCount = 0;

    std::size_t invokedPassPrepareCount = 0;
    std::size_t invokedPassRecordCount = 0;
    std::size_t appliedInPassBarrierCount = 0;
    std::size_t appliedAcquireBarrierCount = 0;
    std::size_t appliedReleaseBarrierCount = 0;

    bool syntheticPresentBatchUsed = false;

    bool usedScenePath = false;
    bool usedCameraOverride = false;
    bool sceneExtractProfileCreated = false;
    std::size_t sceneBridgeDrawCount = 0;
    std::size_t sceneRasterPacketCount = 0;
    std::size_t sceneRtPacketCount = 0;
    std::size_t sceneTlasPacketCount = 0;
};

class Renderer
{
  public:
    Renderer() = default;

    void initialize(const RendererCreateInfo& info = {})
    {
        if (device_)
        {
            return;
        }

        auto& shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        device_ = std::make_unique<nr::rhi::Device>();
        device_->initialize(info.appName, info.engineName);
        submissionTimeline_.initialize(device_->device, 0);
    }

    void installGraph(const RendererGraphSpec& spec)
    {
        nrAssert(static_cast<bool>(device_), "Renderer::installGraph requires initialize() before graph installation.");
        teardownInstalledGraph();

        auto installed = std::vector<InstalledNode>{};
        installed.reserve(spec.nodes.size());

        auto knownNames = std::set<std::string>{};
        auto initContext = NodeInitContext{
            .device = std::ref(*device_),
        };

        std::ranges::for_each(spec.nodes, [&](const NodeCreateInfo& createInfo) {
            nrAssert(static_cast<bool>(createInfo.runtime), "Renderer::installGraph requires a valid node runtime in NodeCreateInfo.");

            auto description = createInfo.runtime->describe();
            auto runtimeName = createInfo.config.instanceName.empty()
                                   ? description.name
                                   : createInfo.config.instanceName;

            nrAssert(!runtimeName.empty(), "Renderer::installGraph requires each node to have a non-empty runtime name.");
            auto [_, inserted] = knownNames.insert(runtimeName);
            nrAssert(inserted, "Renderer::installGraph found duplicate node names in RendererGraphSpec.");

            createInfo.runtime->initialize(initContext);

            installed.push_back(InstalledNode{
                .runtime = createInfo.runtime,
                .description = std::move(description),
                .config = createInfo.config,
                .runtimeName = std::move(runtimeName),
            });
        });

        auto nodeIndexByName = std::map<std::string, std::size_t>{};
        auto nodeOrdinals = std::views::iota(std::size_t{0}, installed.size());
        std::ranges::for_each(nodeOrdinals, [&](std::size_t nodeIndex) {
            nodeIndexByName.emplace(installed[nodeIndex].runtimeName, nodeIndex);
        });

        auto connectionsByTarget = std::map<std::string, std::string>{};
        std::ranges::for_each(spec.connections, [&](const NodeConnection& connection) {
            auto fromNodeIt = nodeIndexByName.find(connection.from.nodeName);
            nrAssert(fromNodeIt != nodeIndexByName.end(), "Renderer::installGraph connection references unknown source node.");

            auto toNodeIt = nodeIndexByName.find(connection.to.nodeName);
            nrAssert(toNodeIt != nodeIndexByName.end(), "Renderer::installGraph connection references unknown target node.");

            nrAssert(
                fromNodeIt->second < toNodeIt->second,
                "Renderer::installGraph currently requires source node order before target node order.");

            auto targetKey = makePortKey(connection.to.nodeName, connection.to.portName);
            auto sourceKey = makePortKey(connection.from.nodeName, connection.from.portName);

            auto [_, inserted] = connectionsByTarget.emplace(std::move(targetKey), std::move(sourceKey));
            nrAssert(inserted, "Renderer::installGraph found multiple sources bound to the same target input port.");
        });

        auto submitNodesByAfterIndex = std::multimap<std::size_t, SubmitNodeSpec>{};
        std::ranges::for_each(spec.submitNodes, [&](const SubmitNodeSpec& submitSpec) {
            nrAssert(
                submitSpec.afterNodeIndex < installed.size(),
                "Renderer::installGraph submit node index is out of range for installed nodes.");
            submitNodesByAfterIndex.emplace(submitSpec.afterNodeIndex, submitSpec);
        });

        installedNodes_ = std::move(installed);
        nodeIndexByName_ = std::move(nodeIndexByName);
        connectionsByTargetPort_ = std::move(connectionsByTarget);
        submitNodesByAfterIndex_ = std::move(submitNodesByAfterIndex);
        graphInstalled_ = true;
    }

    void shutdown()
    {
        if (!device_)
        {
            return;
        }

        device_->waitIdle();
        // Release frame-local graph callbacks and retained command buffers while Device is still valid.
        builder_.clear();
        executor_.clearRetainedState();
        teardownInstalledGraph();
        submissionTimeline_ = RendererSubmissionTimeline{};
        activeScene_.reset();
        sceneExtractProfile_.reset();
        device_.reset();
    }

    [[nodiscard]] bool initialized() const noexcept
    {
        return static_cast<bool>(device_);
    }

    [[nodiscard]] bool graphInstalled() const noexcept
    {
        return graphInstalled_;
    }

    void resize()
    {
        if (!device_)
        {
            return;
        }
        device_->presentationContext.recreate(device_->physicalDevice, device_->device, device_->queueManager);
    }

    [[nodiscard]] RendererFrameResult renderFrame(const RendererFrameInput& input = {})
    {
        if (!device_ || !graphInstalled_)
        {
            return RendererFrameResult{};
        }

        auto begin = device_->beginFrame(input.acquireTimeout);

        auto scenePackets = std::optional<nr::scene::ScenePacketSet>{};
        auto primaryCamera = std::optional<nr::scene::SceneResolvedCamera>{};
        auto sceneBridgeFrame = std::optional<nr::scene::SceneBridgeFrame>{};
        auto sceneExtractProfileCreated = false;
        auto sceneCameraOverride = input.cameraOverride;

        if (input.scene.has_value())
        {
            auto& scene = input.scene->get();
            scene.beginFrame(begin.frameIndex);
            scene.uploadPending();

            auto [profile, created] = ensureSceneExtractProfile(scene);
            sceneExtractProfileCreated = created;

            auto extractInput = input.sceneExtractInput.value_or(nr::scene::SceneExtractInput{});
            if (!extractInput.viewportExtent.has_value())
            {
                auto extent = device_->presentationContext.swapchainExtent();
                extractInput.viewportExtent = glm::uvec2{extent.width, extent.height};
            }

            if (sceneCameraOverride.has_value())
            {
                extractInput.visibility = nr::scene::SceneVisibilityMode::customFrustum;
                extractInput.customFrustum = sceneCameraOverride->frustum;
            }

            scenePackets = scene.extractPackets(profile, extractInput);
            if (!sceneCameraOverride.has_value())
            {
                primaryCamera = scene.tryGetPrimaryCamera(extractInput.viewportExtent);
            }

            auto bridgeBuildInput = nr::scene::SceneRenderBridgeBuildInput{
                .packetSet = std::cref(*scenePackets),
                .primaryCamera = std::nullopt,
                .frameConstantsOverride = std::nullopt,
            };

            if (sceneCameraOverride.has_value())
            {
                bridgeBuildInput.frameConstantsOverride = sceneCameraOverride->frameConstants;
            }

            bridgeBuildInput.resolveRasterDrawGeometry =
                [&](nr::resource::MeshHandle meshHandle, std::uint32_t submeshIndex)
                -> std::optional<nr::scene::SceneBridgeDrawGeometry> {
                auto meshRecordRef = scene.tryGetMeshAsset(meshHandle);
                if (!meshRecordRef.has_value())
                {
                    return std::nullopt;
                }

                auto const &meshRecord = meshRecordRef->get();
                if (!meshRecord.cpuReady || !meshRecord.gpu.has_value())
                {
                    return std::nullopt;
                }

                if (!meshRecord.gpu->vertexBuffer.valid())
                {
                    return std::nullopt;
                }

                if (submeshIndex >= meshRecord.cpu.submeshes.size())
                {
                    return std::nullopt;
                }

                auto const &submesh = meshRecord.cpu.submeshes[submeshIndex];
                auto geometry = nr::scene::SceneBridgeDrawGeometry{};
                geometry.vertexBuffer = nr::scene::SceneBridgeBufferBinding{
                    .buffer = std::cref(meshRecord.gpu->vertexBuffer),
                    .offset = 0,
                };
                geometry.frontFace = meshRecord.cpu.clockwiseFrontFace
                                         ? vk::FrontFace::eClockwise
                                         : vk::FrontFace::eCounterClockwise;

                auto const indexedGeometry = meshRecord.gpu->indexBuffer.valid() && !meshRecord.cpu.indices.empty();
                if (indexedGeometry)
                {
                    geometry.indexBuffer = nr::scene::SceneBridgeBufferBinding{
                        .buffer = std::cref(meshRecord.gpu->indexBuffer),
                        .offset = 0,
                    };
                    geometry.firstIndex = submesh.firstIndex;
                    geometry.indexCount = submesh.indexCount > 0
                                              ? submesh.indexCount
                                              : meshRecord.gpu->indexCount;
                    geometry.vertexOffset = submesh.vertexOffset <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())
                                                ? static_cast<std::int32_t>(submesh.vertexOffset)
                                                : std::numeric_limits<std::int32_t>::max();
                    geometry.indexType = vk::IndexType::eUint32;
                    return geometry;
                }

                geometry.firstVertex = submesh.vertexOffset;
                geometry.vertexCount = submesh.indexCount > 0
                                           ? submesh.indexCount
                                           : meshRecord.gpu->vertexCount;
                return geometry;
            };

            if (primaryCamera.has_value())
            {
                bridgeBuildInput.primaryCamera = std::cref(*primaryCamera);
            }

            sceneBridgeFrame = nr::scene::SceneRenderBridge::buildFrame(bridgeBuildInput);
        }

        auto frameParameters = NodeFrameParameters{
            .frameIndex = begin.frameIndex,
            .swapchainImageIndex = begin.swapchainImageIndex,
            .swapchainExtent = device_->presentationContext.swapchainExtent(),
            .swapchainFormat = device_->presentationContext.swapchainFormat(),
            .sceneBridgeFrame = std::nullopt,
            .scenePackets = std::nullopt,
            .primaryCamera = std::nullopt,
            .frameServices = input.frameServices,
        };

        if (sceneBridgeFrame.has_value())
        {
            frameParameters.sceneBridgeFrame = std::cref(*sceneBridgeFrame);
        }

        if (scenePackets.has_value())
        {
            frameParameters.scenePackets = std::cref(*scenePackets);
        }

        if (primaryCamera.has_value())
        {
            frameParameters.primaryCamera = std::cref(*primaryCamera);
        }

        auto frameDesc = buildInstalledGraph(frameParameters);
        auto compiled = compiler_.compile(frameDesc);

        auto executeContext = RenderGraphExecutor::ExecuteContext{
            .device = *device_,
            .frameIndex = begin.frameIndex,
            .swapchainImageIndex = begin.swapchainImageIndex,
            .submissionTimeline = submissionTimeline_.valid()
                                    ? std::optional<std::reference_wrapper<RendererSubmissionTimeline>>(std::ref(submissionTimeline_))
                                    : std::nullopt,
        };

        auto prepared = executor_.prepareFrame(std::move(compiled), executeContext);
        auto executeReport = executor_.executePrepared(prepared, executeContext);

        auto present = device_->presentFrame();

        return RendererFrameResult{
            .rendered = true,
            .frameIndex = begin.frameIndex,
            .swapchainImageIndex = begin.swapchainImageIndex,
            .presentResult = present.result,
            .compiledSubmitBatchCount = prepared.compiled.submitBatches.size(),
            .submittedBatchCount = executeReport.submittedBatchCount,
            .invokedPassPrepareCount = executeReport.invokedPassPrepareCount,
            .invokedPassRecordCount = executeReport.invokedPassRecordCount,
            .appliedInPassBarrierCount = executeReport.appliedInPassBarrierCount,
            .appliedAcquireBarrierCount = executeReport.appliedAcquireBarrierCount,
            .appliedReleaseBarrierCount = executeReport.appliedReleaseBarrierCount,
            .syntheticPresentBatchUsed = executeReport.plan.requiresSyntheticPresentBatch,
            .usedScenePath = input.scene.has_value(),
            .usedCameraOverride = sceneCameraOverride.has_value(),
            .sceneExtractProfileCreated = sceneExtractProfileCreated,
            .sceneBridgeDrawCount = sceneBridgeFrame.has_value() ? sceneBridgeFrame->rasterDraws.size() : 0,
            .sceneRasterPacketCount = scenePackets.has_value() ? scenePackets->rasterDraws.size() : 0,
            .sceneRtPacketCount = scenePackets.has_value() ? scenePackets->rtInstances.size() : 0,
            .sceneTlasPacketCount = scenePackets.has_value() ? scenePackets->tlasBuildInputs.size() : 0,
        };
    }

    [[nodiscard]] nr::rhi::Device& device()
    {
        return *device_;
    }

    [[nodiscard]] const nr::rhi::Device& device() const
    {
        return *device_;
    }

    [[nodiscard]] RenderGraphExecutor& graphExecutor() noexcept
    {
        return executor_;
    }

    [[nodiscard]] const RenderGraphExecutor& graphExecutor() const noexcept
    {
        return executor_;
    }

  private:
    struct InstalledNode
    {
        std::shared_ptr<NodeRuntime> runtime{};
        NodeDescription description{};
        NodeConfig config{};
        std::string runtimeName{};
    };

    [[nodiscard]] static std::string makePortKey(std::string_view nodeName, std::string_view portName)
    {
        return std::format("{}::{}", nodeName, portName);
    }

    [[nodiscard]] RenderGraphFrameDescription buildInstalledGraph(const NodeFrameParameters& frameParameters)
    {
        nrAssert(graphInstalled_, "Renderer::buildInstalledGraph requires installGraph() before rendering.");

        builder_.clear();
        auto publishedOutputs = std::map<std::string, GraphResourceHandle>{};

        auto nodeOrdinals = std::views::iota(std::size_t{0}, installedNodes_.size());
        std::ranges::for_each(nodeOrdinals, [&](std::size_t nodeIndex) {
            auto& installedNode = installedNodes_[nodeIndex];

            auto nodeHandle = builder_.addNode(
                installedNode.runtimeName,
                installedNode.config.queue);

            auto resolveInputPort = [&](std::string_view inputPortName) -> GraphResourceHandle {
                auto targetKey = makePortKey(installedNode.runtimeName, inputPortName);
                auto connectionIt = connectionsByTargetPort_.find(targetKey);
                if (connectionIt == connectionsByTargetPort_.end())
                {
                    return {};
                }

                auto sourceIt = publishedOutputs.find(connectionIt->second);
                if (sourceIt == publishedOutputs.end())
                {
                    return {};
                }

                return sourceIt->second;
            };

            auto publishOutputPort = [&](std::string_view outputPortName, GraphResourceHandle resource) {
                nrAssert(resource.valid(), "Renderer::buildInstalledGraph output port publish requires a valid resource handle.");
                auto outputKey = makePortKey(installedNode.runtimeName, outputPortName);
                publishedOutputs.insert_or_assign(outputKey, resource);
            };

            auto buildContext = NodeBuildContext{
                .graphBuilder = std::ref(builder_),
                .nodeHandle = nodeHandle,
                .resolveInputPort = resolveInputPort,
                .publishOutputPort = publishOutputPort,
            };

            installedNode.runtime->build(buildContext, frameParameters);

            auto boundaries = submitNodesByAfterIndex_.equal_range(nodeIndex);
            std::ranges::for_each(std::ranges::subrange(boundaries.first, boundaries.second), [&](const auto& entry) {
                auto debugName = entry.second.debugName.empty()
                                     ? std::format("Submit.After.{}", installedNode.runtimeName)
                                     : entry.second.debugName;
                auto submitHandle = builder_.addSubmitNode(debugName, entry.second.kind);
                nrAssert(submitHandle.valid(), "Renderer::buildInstalledGraph failed to add a valid submit node.");
            });
        });

        return builder_.build();
    }

    void teardownInstalledGraph()
    {
        if (device_)
        {
            auto shutdownContext = NodeShutdownContext{
                .device = std::ref(*device_),
            };
            std::ranges::for_each(installedNodes_, [&](InstalledNode& installedNode) {
                if (installedNode.runtime)
                {
                    installedNode.runtime->shutdown(shutdownContext);
                }
            });
        }

        installedNodes_.clear();
        nodeIndexByName_.clear();
        connectionsByTargetPort_.clear();
        submitNodesByAfterIndex_.clear();
        graphInstalled_ = false;
    }

    [[nodiscard]] std::pair<nr::scene::SceneExtractProfileHandle, bool> ensureSceneExtractProfile(nr::scene::Scene& scene)
    {
        auto sameScene = activeScene_.has_value() && std::addressof(activeScene_->get()) == std::addressof(scene);
        auto needsCreate = !sameScene || !sceneExtractProfile_.has_value() || !sceneExtractProfile_->valid();

        if (needsCreate)
        {
            activeScene_ = std::ref(scene);
            sceneExtractProfile_ = scene.registerExtractProfile(nr::scene::SceneExtractProfileCreateInfo{
                .debugName = "Renderer.DefaultRasterExtract",
                .domain = nr::scene::ScenePacketDomain::rasterDraw,
                .selection = {},
                .requireReadyForDomain = true,
                .requireActiveInstances = true,
                .enableCoarseGrouping = true,
            });
            return {*sceneExtractProfile_, true};
        }

        return {*sceneExtractProfile_, false};
    }

    std::unique_ptr<nr::rhi::Device> device_{};
    RenderGraphBuilder builder_{};
    RenderGraphCompiler compiler_{};
    RenderGraphExecutor executor_{};
    RendererSubmissionTimeline submissionTimeline_{};

    bool graphInstalled_ = false;
    std::vector<InstalledNode> installedNodes_{};
    std::map<std::string, std::size_t> nodeIndexByName_{};
    std::map<std::string, std::string> connectionsByTargetPort_{};
    std::multimap<std::size_t, SubmitNodeSpec> submitNodesByAfterIndex_{};

    std::optional<std::reference_wrapper<nr::scene::Scene>> activeScene_{};
    std::optional<nr::scene::SceneExtractProfileHandle> sceneExtractProfile_{};
};
} // namespace nr::renderer
