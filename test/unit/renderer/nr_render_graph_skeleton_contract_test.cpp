import std;
import dependency.vulkan;
import nr.options;
import nr.renderer;
import nr.rhi;
import nr.test;

namespace
{
[[nodiscard]] const nr::options::OptionFrameSnapshot &emptyOptionSnapshot()
{
    static auto const catalog = nr::options::OptionCatalogBuilder{}.build().catalog;
    static auto const snapshot = nr::options::OptionFrameSnapshot{
        .catalog = catalog,
    };
    return snapshot;
}

[[nodiscard]] nr::renderer::NodeFrameParameters nodeFrameParameters()
{
    return nr::renderer::NodeFrameParameters{
        .optionSnapshot = std::cref(emptyOptionSnapshot()),
    };
}

[[nodiscard]] nr::renderer::RenderGraphFrameDescription makeFrame(std::uint32_t dynamicValue,
                                                                  bool addSecondPass = false)
{
    auto builder = nr::renderer::RenderGraphBuilder{};
    auto node = builder.addNode("Skeleton.Node", nr::renderer::QueueDomain::Compute);
    auto buffer = builder.addResource(nr::renderer::GraphTransientBufferDesc{
        .debugName = std::format("Skeleton.Buffer.{}", dynamicValue),
        .size = 64u,
        .usageIntents = {nr::renderer::BufferUsageIntent::StorageWrite},
    });
    static_cast<void>(builder.addFrameData("Skeleton.Dynamic", dynamicValue));
    auto uses = std::array{nr::renderer::use::storageBufferWrite(buffer)};
    static_cast<void>(
        builder.addPass(std::format("Skeleton.Pass.{}", dynamicValue), node, uses,
                        [dynamicValue](const nr::renderer::PassRecordContext &) { static_cast<void>(dynamicValue); }));
    if (addSecondPass)
    {
        static_cast<void>(
            builder.addPass("Skeleton.Second", node, uses, [](const nr::renderer::PassRecordContext &) {}));
    }
    return builder.build();
}

[[nodiscard]] nr::renderer::RenderGraphSkeletonKey makeKey(std::uint64_t configRevision = 1u,
                                                           std::uint64_t shaderGeneration = 3u,
                                                           std::uint64_t swapchainGeneration = 5u,
                                                           std::string branch = "stable")
{
    return nr::renderer::RenderGraphSkeletonKey{
        .installedGraphGeneration = 2u,
        .displayExtent = vk::Extent2D{1920u, 1080u},
        .renderExtent = vk::Extent2D{1280u, 720u},
        .swapchainExtent = vk::Extent2D{1920u, 1080u},
        .swapchainFormat = vk::Format::eR8G8B8A8Unorm,
        .swapchainColorSpace = vk::ColorSpaceKHR::eSrgbNonlinear,
        .shaderSessionGeneration = shaderGeneration,
        .swapchainRecreationGeneration = swapchainGeneration,
        .submitAcquirePolicyRevision = 2u,
        .nodes =
            {
                nr::renderer::RenderGraphSkeletonNodeKey{
                    .configurationRevision = configRevision,
                    .runtimeConfigurationRevision = 7u,
                    .structuralBranchKey = std::move(branch),
                },
            },
    };
}

class UnsupportedNode final : public nr::renderer::NodeRuntime
{
  public:
    void build(nr::renderer::NodeBuildContext &, const nr::renderer::NodeFrameParameters &) override
    {
    }
};

class CountingSkeletonNode final : public nr::renderer::NodeRuntime
{
  public:
    std::uint32_t buildCalls = 0;
    std::uint32_t materializeCalls = 0;

    [[nodiscard]] bool supportsRenderGraphSkeleton() const noexcept override
    {
        return true;
    }

    void build(nr::renderer::NodeBuildContext &, const nr::renderer::NodeFrameParameters &) override
    {
        ++buildCalls;
    }

    bool materializeRenderGraphSkeleton(nr::renderer::RenderGraphSkeletonPatchContext &,
                                        const nr::renderer::NodeFrameParameters &, const StructuralSnapshot &) override
    {
        ++materializeCalls;
        return true;
    }
};

class FailingSkeletonNode final : public nr::renderer::NodeRuntime
{
  public:
    [[nodiscard]] bool supportsRenderGraphSkeleton() const noexcept override
    {
        return true;
    }

    void build(nr::renderer::NodeBuildContext &context, const nr::renderer::NodeFrameParameters &) override
    {
        auto resource = context.addResource(nr::renderer::GraphTransientBufferDesc{
            .debugName = "Fallback.Buffer",
            .size = 16u,
            .usageIntents = {nr::renderer::BufferUsageIntent::StorageWrite},
        });
        auto uses = std::array{nr::renderer::use::storageBufferWrite(resource)};
        static_cast<void>(context.addPass(uses, "Fallback.Pass", [](const nr::renderer::PassRecordContext &) {}));
    }

    bool materializeRenderGraphSkeleton(nr::renderer::RenderGraphSkeletonPatchContext &,
                                        const nr::renderer::NodeFrameParameters &, const StructuralSnapshot &) override
    {
        return false;
    }
};

const nr::test::CaseRegistrar exactKeyAndDynamicPatchCase{
    "render graph Skeleton exact key hits while dynamic payloads stay outside cached structure", [] {
        auto cache = nr::renderer::RenderGraphSkeletonCache{};
        auto first = makeFrame(11u);
        auto firstResult = cache.acceptMaterialized(makeKey(), first);
        nr::test::require(!firstResult.keyHit);
        nr::test::requireEqual(firstResult.missReason, nr::renderer::RenderGraphSkeletonMissReason::KeyNotFound);

        auto current = makeFrame(22u);
        auto hit = cache.acceptMaterialized(makeKey(), current);
        nr::test::require(hit.keyHit);
        nr::test::require(hit.structureMatches);
        auto statistics = cache.statistics();
        nr::test::requireEqual(statistics.hitCount, std::uint64_t{1u});
        nr::test::requireEqual(statistics.entryCount, std::size_t{1u});
    }};

const nr::test::CaseRegistrar ownedTemplateAndPatchOnlyContextCase{
    "Skeleton owns a stripped static template and patch context substitutes current dynamic slots", [] {
        auto cache = nr::renderer::RenderGraphSkeletonCache{};
        auto cold = makeFrame(11u);
        static_cast<void>(cache.acceptMaterialized(makeKey(), cold));
        auto skeleton = cache.lookup(makeKey());
        nr::test::require(static_cast<bool>(skeleton));
        nr::test::require(!skeleton->staticFrame.frameData.front().payload.has_value());
        nr::test::require(skeleton->staticFrame.frameData.front().debugName.empty());
        nr::test::require(!skeleton->staticFrame.passes.front().prepare);
        nr::test::require(!skeleton->staticFrame.passes.front().record);
        nr::test::require(!skeleton->staticFrame.passes.front().parallelRecord.has_value());

        auto current = nr::renderer::RenderGraphSkeletonCache::instantiate(*skeleton);
        auto namedResources = std::map<std::string, nr::renderer::GraphResourceHandle>{
            {"output", current.resources.front().handle},
        };
        auto namedFrameData = std::map<std::string, nr::renderer::GraphFrameDataHandle>{
            {"dynamic", current.frameData.front().handle},
        };
        auto bindlessCache = nr::renderer::BindlessImageTableCache{};
        auto globals = nr::renderer::FrameGlobalResources{
            .bindlessImageTableCache = std::ref(bindlessCache),
        };
        auto patch = nr::renderer::RenderGraphSkeletonPatchContext{
            current,
            nr::renderer::RenderGraphSkeletonNodePatchLayout{
                .resourceCount = 1u,
                .frameDataCount = 1u,
                .passCount = 1u,
            },
            namedResources,
            namedFrameData,
            std::addressof(globals),
        };
        auto recorded = std::uint32_t{0u};
        patch.patchResource(0u, nr::renderer::GraphTransientBufferDesc{
                                    .debugName = "Current.Buffer",
                                    .size = 128u,
                                    .usageIntents = {nr::renderer::BufferUsageIntent::StorageWrite},
                                });
        patch.patchFrameData(0u, "Current.Dynamic", std::make_any<std::uint32_t>(22u));
        patch.patchPass(0u, "Current.Pass", nullptr,
                        [&recorded](const nr::renderer::PassRecordContext &) { recorded = 22u; });
        nr::test::requireEqual(std::any_cast<std::uint32_t>(current.frameData.front().payload), std::uint32_t{22u});
        nr::test::requireEqual(patch.namedResource("output"), current.resources.front().handle);
        nr::test::requireEqual(patch.namedFrameData("dynamic"), current.frameData.front().handle);
        nr::test::requireEqual(patch.resolveFrameData<std::uint32_t>(patch.namedFrameData("dynamic"))->get(),
                               std::uint32_t{22u});
        nr::test::require(std::addressof(patch.globalResources()) == std::addressof(globals));
        current.passes.front().record(nr::renderer::PassRecordContext{});
        nr::test::requireEqual(recorded, std::uint32_t{22u});
    }};

const nr::test::CaseRegistrar exactInvalidationDimensionsCase{
    "render graph Skeleton key distinguishes branch config shader and swapchain generations", [] {
        auto cache = nr::renderer::RenderGraphSkeletonCache{};
        auto frame = makeFrame(1u);
        static_cast<void>(cache.acceptMaterialized(makeKey(), frame));
        nr::test::require(!cache.contains(makeKey(2u)));
        nr::test::require(!cache.contains(makeKey(1u, 4u)));
        nr::test::require(!cache.contains(makeKey(1u, 3u, 6u)));
        nr::test::require(!cache.contains(makeKey(1u, 3u, 5u, "other-branch")));
        auto bridgeVariant = makeKey();
        bridgeVariant.hasSceneBridgeFrame = true;
        nr::test::require(!cache.contains(bridgeVariant));
    }};

const nr::test::CaseRegistrar explicitCaptureAndZeroDeclarationHitCase{
    "Skeleton explicit capture restores exact ranges and a synthetic chain hit declares no structure", [] {
        auto coldBuilder = nr::renderer::RenderGraphBuilder{};
        auto firstNode = coldBuilder.addNode("First", nr::renderer::QueueDomain::Compute);
        auto firstResource = coldBuilder.addResource(nr::renderer::GraphTransientBufferDesc{
            .debugName = "First.Buffer",
            .size = 16u,
            .usageIntents = {nr::renderer::BufferUsageIntent::StorageWrite},
        });
        auto firstData = coldBuilder.addFrameData("First.Data", std::uint32_t{1u});
        auto firstUses = std::array{nr::renderer::use::storageBufferWrite(firstResource)};
        static_cast<void>(
            coldBuilder.addPass("First.Pass", firstNode, firstUses, [](const nr::renderer::PassRecordContext &) {}));
        auto secondNode = coldBuilder.addNode("Second", nr::renderer::QueueDomain::Compute);
        auto secondResource = coldBuilder.addResource(nr::renderer::GraphTransientBufferDesc{
            .debugName = "Second.Buffer",
            .size = 32u,
            .usageIntents = {nr::renderer::BufferUsageIntent::StorageRead},
        });
        auto secondUses = std::array{nr::renderer::use::storageBufferRead(secondResource)};
        static_cast<void>(
            coldBuilder.addPass("Second.Pass", secondNode, secondUses, [](const nr::renderer::PassRecordContext &) {}));
        static_cast<void>(coldBuilder.addSubmitNode("After.Second"));

        auto capture = nr::renderer::RenderGraphSkeletonCapture{
            .nodePatchLayouts =
                {
                    nr::renderer::RenderGraphSkeletonNodePatchLayout{
                        .resourceCount = 1u,
                        .frameDataCount = 1u,
                        .passCount = 1u,
                    },
                    nr::renderer::RenderGraphSkeletonNodePatchLayout{
                        .resourceBegin = 1u,
                        .resourceCount = 1u,
                        .frameDataBegin = 1u,
                        .passBegin = 1u,
                        .passCount = 1u,
                    },
                },
            .namedFrameResources =
                {
                    {"first", firstResource},
                    {"second", secondResource},
                },
            .namedFrameData =
                {
                    {"first", firstData},
                },
        };
        auto cache = nr::renderer::RenderGraphSkeletonCache{};
        static_cast<void>(cache.acceptMaterialized(makeKey(), coldBuilder.frame(), std::move(capture)));
        auto skeleton = cache.lookup(makeKey());
        nr::test::require(static_cast<bool>(skeleton));
        nr::test::requireEqual(skeleton->nodePatchLayouts[1].resourceBegin, std::size_t{1u});
        nr::test::requireEqual(skeleton->namedFrameResources.at("second"), secondResource);

        auto current = nr::renderer::RenderGraphSkeletonCache::instantiate(*skeleton);
        auto firstPatch = nr::renderer::RenderGraphSkeletonPatchContext{
            current,
            skeleton->nodePatchLayouts[0],
            skeleton->namedFrameResources,
            skeleton->namedFrameData,
        };
        auto secondPatch = nr::renderer::RenderGraphSkeletonPatchContext{
            current,
            skeleton->nodePatchLayouts[1],
            skeleton->namedFrameResources,
            skeleton->namedFrameData,
        };
        firstPatch.patchResource(0u, nr::renderer::GraphTransientBufferDesc{
                                         .debugName = "First.Current",
                                         .size = 64u,
                                         .usageIntents = {nr::renderer::BufferUsageIntent::StorageWrite},
                                     });
        firstPatch.patchFrameData(0u, "First.CurrentData", std::make_any<std::uint32_t>(9u));
        firstPatch.patchPass(0u, "First.CurrentPass", nullptr, [](const nr::renderer::PassRecordContext &) {});
        secondPatch.patchResource(0u, nr::renderer::GraphTransientBufferDesc{
                                          .debugName = "Second.Current",
                                          .size = 96u,
                                          .usageIntents = {nr::renderer::BufferUsageIntent::StorageRead},
                                      });
        secondPatch.patchPass(0u, "Second.CurrentPass", nullptr, [](const nr::renderer::PassRecordContext &) {});

        auto hitBuilder = nr::renderer::RenderGraphBuilder{};
        hitBuilder.clear();
        hitBuilder.mutableFrame() = std::move(current);
        nr::test::requireEqual(hitBuilder.declarationCounts().total(), std::size_t{0u});
        nr::test::requireEqual(std::any_cast<std::uint32_t>(hitBuilder.frame().frameData.front().payload),
                               std::uint32_t{9u});
        nr::test::requireEqual(hitBuilder.frame().submitBoundaries.size(), std::size_t{1u});
    }};

const nr::test::CaseRegistrar patchFailureColdFallbackCase{
    "Skeleton patch failure discards the partial instance before a fresh cold declaration", [] {
        auto cached = makeFrame(1u);
        auto cache = nr::renderer::RenderGraphSkeletonCache{};
        static_cast<void>(cache.acceptMaterialized(makeKey(), cached));
        auto skeleton = cache.lookup(makeKey());
        auto partial = nr::renderer::RenderGraphSkeletonCache::instantiate(*skeleton);
        auto names = std::map<std::string, nr::renderer::GraphResourceHandle>{};
        auto dataNames = std::map<std::string, nr::renderer::GraphFrameDataHandle>{};
        auto patch = nr::renderer::RenderGraphSkeletonPatchContext{
            partial,
            skeleton->nodePatchLayouts.front(),
            names,
            dataNames,
        };
        auto node = FailingSkeletonNode{};
        auto parameters = nodeFrameParameters();
        auto snapshot = node.structuralSnapshot(parameters);
        nr::test::require(snapshot.has_value());
        nr::test::require(!node.materializeRenderGraphSkeleton(patch, parameters, *snapshot));

        auto coldBuilder = nr::renderer::RenderGraphBuilder{};
        auto resources = std::map<std::string, nr::renderer::GraphResourceHandle>{};
        auto frameData = std::map<std::string, nr::renderer::GraphFrameDataHandle>{};
        auto bindlessCache = nr::renderer::BindlessImageTableCache{};
        auto globals = nr::renderer::FrameGlobalResources{.bindlessImageTableCache = std::ref(bindlessCache)};
        auto context = nr::renderer::NodeBuildContext{
            .graphBuilder = std::ref(coldBuilder),
            .nodeHandle = coldBuilder.addNode("Fallback", nr::renderer::QueueDomain::Compute),
            .queue = nr::renderer::QueueDomain::Compute,
            .globalResources = std::cref(globals),
            .frameResources = std::ref(resources),
            .frameDataResources = std::ref(frameData),
        };
        node.build(context, parameters);
        nr::test::requireEqual(coldBuilder.frame().resources.size(), std::size_t{1u});
        nr::test::requireEqual(coldBuilder.frame().passes.size(), std::size_t{1u});
        nr::test::require(coldBuilder.declarationCounts().total() > 0u);
    }};

const nr::test::CaseRegistrar structureMismatchCase{
    "render graph Skeleton Differential cold comparison refreshes an authoritative structural mismatch", [] {
        auto cache = nr::renderer::RenderGraphSkeletonCache{};
        auto cold = makeFrame(1u);
        static_cast<void>(cache.acceptMaterialized(makeKey(), cold));
        auto const beforeMismatch = cache.statistics();

        auto changed = makeFrame(2u, true);
        auto mismatch = cache.acceptMaterialized(makeKey(), changed);
        nr::test::require(mismatch.keyHit);
        nr::test::require(!mismatch.structureMatches);
        nr::test::requireEqual(mismatch.missReason, nr::renderer::RenderGraphSkeletonMissReason::StructureMismatch);
        auto const afterMismatch = cache.statistics();
        nr::test::requireEqual(afterMismatch.hitCount, beforeMismatch.hitCount);
        nr::test::requireEqual(afterMismatch.missCount, beforeMismatch.missCount + 1u);
        nr::test::requireEqual(afterMismatch.structureMismatchCount, beforeMismatch.structureMismatchCount + 1u);
        nr::test::requireEqual(afterMismatch.lastMissReason,
                               nr::renderer::RenderGraphSkeletonMissReason::StructureMismatch);

        auto refreshed = cache.lookup(makeKey());
        nr::test::require(static_cast<bool>(refreshed));
        nr::test::requireEqual(refreshed->staticFrame.passes.size(), std::size_t{2u});

        auto sameChangedStructure = makeFrame(3u, true);
        auto hit = cache.acceptMaterialized(makeKey(), sameChangedStructure);
        nr::test::require(hit.keyHit);
        nr::test::require(hit.structureMatches);
        nr::test::requireEqual(hit.missReason, nr::renderer::RenderGraphSkeletonMissReason::None);
        auto const afterHit = cache.statistics();
        nr::test::requireEqual(afterHit.hitCount, afterMismatch.hitCount + 1u);
        nr::test::requireEqual(afterHit.missCount, afterMismatch.missCount);
        nr::test::requireEqual(afterHit.structureMismatchCount, afterMismatch.structureMismatchCount);
        nr::test::requireEqual(afterHit.entryCount, std::size_t{1u});
        nr::test::requireEqual(afterHit.lastMissReason, nr::renderer::RenderGraphSkeletonMissReason::None);
    }};

const nr::test::CaseRegistrar unsupportedNodeCase{"node without Skeleton contract reports unsupported snapshot", [] {
                                                      auto node = UnsupportedNode{};
                                                      nr::test::require(!node.supportsRenderGraphSkeleton());
                                                      nr::test::require(
                                                          !node.structuralSnapshot(nodeFrameParameters()).has_value());
                                                  }};

const nr::test::CaseRegistrar hitMaterializationBypassesBuildEntryCase{
    "Skeleton hit materialization does not call the node cold-build entrypoint", [] {
        auto builder = nr::renderer::RenderGraphBuilder{};
        static_cast<void>(builder.addNode("Counting", nr::renderer::QueueDomain::Compute));
        auto current = builder.build();
        auto resources = std::map<std::string, nr::renderer::GraphResourceHandle>{};
        auto frameData = std::map<std::string, nr::renderer::GraphFrameDataHandle>{};
        auto context = nr::renderer::RenderGraphSkeletonPatchContext{
            current,
            nr::renderer::RenderGraphSkeletonNodePatchLayout{},
            resources,
            frameData,
        };
        auto parameters = nodeFrameParameters();
        auto node = CountingSkeletonNode{};
        auto bindlessCache = nr::renderer::BindlessImageTableCache{};
        auto globals = nr::renderer::FrameGlobalResources{.bindlessImageTableCache = std::ref(bindlessCache)};
        auto buildContext = nr::renderer::NodeBuildContext{
            .graphBuilder = std::ref(builder),
            .nodeHandle = builder.frame().nodes.front().handle,
            .globalResources = std::cref(globals),
            .frameResources = std::ref(resources),
            .frameDataResources = std::ref(frameData),
        };
        node.build(buildContext, parameters);
        auto snapshot = node.structuralSnapshot(parameters);
        nr::test::require(snapshot.has_value());
        nr::test::require(node.materializeRenderGraphSkeleton(context, parameters, *snapshot));
        nr::test::requireEqual(node.buildCalls, std::uint32_t{1u});
        nr::test::requireEqual(node.materializeCalls, std::uint32_t{1u});
    }};

const nr::test::CaseRegistrar retainedBufferAndAccelerationStructureCase{
    "compiler and compile cache consume and patch retained buffer and acceleration-structure state", [] {
        auto bufferState = nr::renderer::RetainedBufferState{
            .common =
                nr::renderer::RetainedExternalResourceState{
                    .initialized = true,
                    .ownership = nr::renderer::ResourceOwnershipDomain::Graphics,
                    .access =
                        nr::renderer::AccessScope{
                            .stages = vk::PipelineStageFlagBits2::eVertexShader,
                            .access = vk::AccessFlagBits2::eShaderRead,
                        },
                    .lastSubmissionTimelineValue = 17u,
                },
        };
        auto accelerationStructureState = nr::renderer::RetainedAccelerationStructureState{
            .common =
                nr::renderer::RetainedExternalResourceState{
                    .initialized = true,
                    .ownership = nr::renderer::ResourceOwnershipDomain::Transfer,
                    .access =
                        nr::renderer::AccessScope{
                            .stages = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
                            .access = vk::AccessFlagBits2::eAccelerationStructureWriteKHR,
                        },
                    .lastSubmissionTimelineValue = 19u,
                },
        };
        auto builder = nr::renderer::RenderGraphBuilder{};
        auto node = builder.addNode("Retained.External", nr::renderer::QueueDomain::Compute);
        auto buffer = builder.addResource(nr::renderer::GraphImportedBufferDesc{
            .debugName = "Retained.Buffer",
            .size = 256u,
            .usageIntents = {nr::renderer::BufferUsageIntent::StorageRead},
            .retainedState = std::ref(bufferState),
        });
        auto accelerationStructure = builder.addResource(nr::renderer::GraphImportedAccelerationStructureDesc{
            .debugName = "Retained.AS",
            .size = 512u,
            .usageIntents = {nr::renderer::AccelerationStructureUsageIntent::TraceInput},
            .retainedState = std::ref(accelerationStructureState),
        });
        auto uses = std::array{
            nr::renderer::use::storageBufferRead(buffer),
            nr::renderer::use::accelerationStructureTraceRead(accelerationStructure),
        };
        static_cast<void>(builder.addPass("Retained.Use", node, uses, [](const nr::renderer::PassRecordContext &) {}));
        auto frame = builder.build();
        auto compiled = nr::renderer::RenderGraphCompiler{}.compile(frame);
        nr::test::requireEqual(compiled.resources[0].initialOwnership, nr::renderer::ResourceOwnershipDomain::Graphics);
        nr::test::requireEqual(compiled.resources[1].initialOwnership, nr::renderer::ResourceOwnershipDomain::Transfer);
        nr::test::require(compiled.resources[0].retainedBufferState.has_value());
        nr::test::require(compiled.resources[1].retainedAccelerationStructureState.has_value());

        auto cache = nr::renderer::RenderGraphCompileCache{};
        auto cachedFrame = builder.build();
        static_cast<void>(cache.compileConsumingCached(cachedFrame));
        auto currentBufferState = bufferState;
        std::get<nr::renderer::GraphImportedBufferDesc>(frame.resources[0].desc).retainedState =
            std::ref(currentBufferState);
        auto current = cache.compileConsumingCached(frame);
        nr::test::require(std::addressof(current.resources[0].retainedBufferState->get()) ==
                          std::addressof(currentBufferState));
    }};
} // namespace
