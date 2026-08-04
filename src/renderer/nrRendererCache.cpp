module nr.renderer;
import :rendererCache;

import std;

namespace nr::renderer
{
namespace
{
[[nodiscard]] RenderGraphCompileCache::ResourceUseSignature makeResourceUseSignature(const PassResourceUseDesc &use)
{
    return RenderGraphCompileCache::ResourceUseSignature{
        .resource = use.resource,
        .bufferUsage = use.bufferUsage,
        .bufferAccess = use.bufferAccess,
        .accelerationStructureUsage = use.accelerationStructureUsage,
        .accelerationStructureAccess = use.accelerationStructureAccess,
        .imageUsage = use.imageUsage,
        .imageAccess = use.imageAccess,
        .imageLayout = use.imageLayout,
        .imageAspect = use.imageAspect,
        .shaderStages = use.shaderStages,
        .ownershipDomain = use.ownershipDomain,
        .readOnly = use.readOnly,
        .requiresPreviousUseBarrier = use.requiresPreviousUseBarrier,
    };
}

[[nodiscard]] RenderGraphCompileCache::ResourceSignature makeResourceSignature(const GraphResourceDesc &resource)
{
    auto signature = RenderGraphCompileCache::ResourceSignature{
        .handle = resource.handle,
    };

    std::visit(
        [&](const auto &desc) {
            using DescT = std::remove_cvref_t<decltype(desc)>;
            if constexpr (std::same_as<DescT, GraphImportedBufferDesc>)
            {
                auto retained = RetainedExternalResourceState{};
                if (desc.retainedState.has_value())
                {
                    retained = desc.retainedState->get().common;
                }
                signature.desc = RenderGraphCompileCache::ImportedBufferResourceSignature{
                    .lifetime = desc.lifetime,
                    .residency = desc.residency,
                    .initialOwnership = desc.initialOwnership,
                    .size = desc.size,
                    .usageIntents = desc.usageIntents,
                    .retainedInitialized = retained.initialized,
                    .retainedOwnership = retained.ownership,
                    .retainedAccess = retained.access,
                };
            }
            else if constexpr (std::same_as<DescT, GraphImportedImageDesc>)
            {
                auto retained = RetainedImageState{};
                if (desc.retainedState.has_value())
                {
                    retained = desc.retainedState->get();
                }
                signature.desc = RenderGraphCompileCache::ImportedImageResourceSignature{
                    .lifetime = desc.lifetime,
                    .residency = desc.residency,
                    .initialOwnership = desc.initialOwnership,
                    .extent = desc.extent,
                    .format = desc.format,
                    .usageIntents = desc.usageIntents,
                    .initialLayout = desc.initialLayout,
                    .initialAccessScope = desc.initialAccessScope,
                    .aspect = desc.aspect,
                    .retainedInitialized = retained.common.initialized,
                    .retainedOwnership = retained.common.ownership,
                    .retainedAccess = retained.common.access,
                    .retainedLayout = retained.layout,
                };
            }
            else if constexpr (std::same_as<DescT, GraphImportedAccelerationStructureDesc>)
            {
                auto retained = RetainedExternalResourceState{};
                if (desc.retainedState.has_value())
                {
                    retained = desc.retainedState->get().common;
                }
                signature.desc = RenderGraphCompileCache::ImportedAccelerationStructureResourceSignature{
                    .lifetime = desc.lifetime,
                    .residency = desc.residency,
                    .initialOwnership = desc.initialOwnership,
                    .type = desc.type,
                    .size = desc.size,
                    .usageIntents = desc.usageIntents,
                    .retainedInitialized = retained.initialized,
                    .retainedOwnership = retained.ownership,
                    .retainedAccess = retained.access,
                };
            }
            else if constexpr (std::same_as<DescT, GraphImportedSwapchainImageDesc>)
            {
                signature.desc = RenderGraphCompileCache::ImportedSwapchainImageResourceSignature{
                    .lifetime = desc.lifetime,
                    .residency = desc.residency,
                    .initialOwnership = desc.initialOwnership,
                    .extent = desc.extent,
                    .format = desc.format,
                };
            }
            else if constexpr (std::same_as<DescT, GraphTransientBufferDesc>)
            {
                signature.desc = RenderGraphCompileCache::TransientBufferResourceSignature{
                    .lifetime = desc.lifetime,
                    .size = desc.size,
                    .usageIntents = desc.usageIntents,
                    .memoryUsage = desc.memoryUsage,
                };
            }
            else if constexpr (std::same_as<DescT, GraphTransientImageDesc>)
            {
                signature.desc = RenderGraphCompileCache::TransientImageResourceSignature{
                    .lifetime = desc.lifetime,
                    .extent = desc.extent,
                    .format = desc.format,
                    .usageIntents = desc.usageIntents,
                    .initialLayout = desc.initialLayout,
                    .aspect = desc.aspect,
                };
            }
        },
        resource.desc);

    return signature;
}

[[nodiscard]] std::string resourceDebugName(const GraphResourceDesc &resource)
{
    return std::visit([](const auto &desc) { return desc.debugName; }, resource.desc);
}

[[nodiscard]] std::map<GraphResourceHandle, std::reference_wrapper<GraphResourceDesc>> makeResourceLookup(
    RenderGraphFrameDescription &frame)
{
    auto lookup = std::map<GraphResourceHandle, std::reference_wrapper<GraphResourceDesc>>{};
    std::ranges::for_each(frame.resources,
                          [&](GraphResourceDesc &resource) { lookup.emplace(resource.handle, std::ref(resource)); });
    return lookup;
}

[[nodiscard]] std::map<GraphPassHandle, std::reference_wrapper<PassExecutionDesc>> makePassLookup(
    RenderGraphFrameDescription &frame)
{
    auto lookup = std::map<GraphPassHandle, std::reference_wrapper<PassExecutionDesc>>{};
    std::ranges::for_each(frame.passes, [&](PassExecutionDesc &pass) { lookup.emplace(pass.handle, std::ref(pass)); });
    return lookup;
}

[[nodiscard]] std::map<GraphSubmitHandle, std::reference_wrapper<const SubmitBoundaryDesc>> makeSubmitLookup(
    const RenderGraphFrameDescription &frame)
{
    auto lookup = std::map<GraphSubmitHandle, std::reference_wrapper<const SubmitBoundaryDesc>>{};
    std::ranges::for_each(frame.submitBoundaries,
                          [&](const SubmitBoundaryDesc &submit) { lookup.emplace(submit.handle, std::cref(submit)); });
    return lookup;
}
} // namespace

[[nodiscard]] RenderGraphCompileCache::FrameSignature RenderGraphCompileCache::makeSignature(
    const RenderGraphFrameDescription &frame)
{
    auto signature = FrameSignature{};

    signature.resources.reserve(frame.resources.size());
    std::ranges::for_each(frame.resources, [&](const GraphResourceDesc &resource) {
        signature.resources.push_back(makeResourceSignature(resource));
    });

    signature.nodes.reserve(frame.nodes.size());
    std::ranges::for_each(frame.nodes, [&](const GraphNodeDesc &node) {
        signature.nodes.push_back(NodeSignature{
            .handle = node.handle,
            .queue = node.queue,
        });
    });

    signature.passes.reserve(frame.passes.size());
    std::ranges::for_each(frame.passes, [&](const PassExecutionDesc &pass) {
        auto resourceUses =
            pass.resourceUses | std::views::transform(makeResourceUseSignature) | std::ranges::to<std::vector>();
        auto passSignature = PassSignature{
            .handle = pass.handle,
            .node = pass.node,
            .isCopyPass = pass.isCopyPass,
            .queue = pass.queue,
            .shaderStages = pass.shaderStages,
            .copy = pass.copy,
            .resourceUses = std::move(resourceUses),
            .hasPrepare = static_cast<bool>(pass.prepare),
            .hasRecord = static_cast<bool>(pass.record),
            .hasParallelRecord = pass.parallelRecord.has_value(),
        };
        if (pass.parallelRecord.has_value())
        {
            passSignature.parallelReplaySemantics = pass.parallelRecord->replaySemantics;
        }
        signature.passes.push_back(std::move(passSignature));
    });

    signature.submitBoundaries.reserve(frame.submitBoundaries.size());
    std::ranges::for_each(frame.submitBoundaries, [&](const SubmitBoundaryDesc &submit) {
        signature.submitBoundaries.push_back(SubmitBoundarySignature{
            .handle = submit.handle,
            .kind = submit.kind,
        });
    });

    signature.executionOrder.reserve(frame.executionOrder.size());
    std::ranges::for_each(frame.executionOrder, [&](const GraphExecutionStep &step) {
        signature.executionOrder.push_back(ExecutionStepSignature{
            .step = step,
        });
    });

    return signature;
}

[[nodiscard]] RenderGraphCompileCache::FrameSignature RenderGraphCompileCache::structuralSignature(
    const RenderGraphFrameDescription &frame)
{
    return makeSignature(frame);
}

[[nodiscard]] CompiledGraphFrame RenderGraphCompileCache::makeCompiledTemplate(const CompiledGraphFrame &compiled)
{
    auto compiledTemplate = CompiledGraphFrame{compiled};
    compiledTemplate.frameData.clear();
    compiledTemplate.debugView.clear();

    std::ranges::for_each(compiledTemplate.resources, [](CompiledResourceDesc &resource) {
        resource.debugName.clear();
        resource.importedBufferResource.reset();
        resource.importedImageResource.reset();
        resource.retainedBufferState.reset();
        resource.retainedImageState.reset();
        resource.retainedAccelerationStructureState.reset();
        resource.retainedState.reset();
        resource.importedAccelerationStructureResource.reset();
    });

    std::ranges::for_each(compiledTemplate.submitBatches, [](CompiledSubmitBatch &batch) {
        batch.openedBySubmitNodeDebugName.clear();
        std::ranges::for_each(batch.passes, [](CompiledPass &pass) {
            pass.debugName.clear();
            pass.prepare = nullptr;
            pass.record = nullptr;
            if (pass.parallelRecord.has_value())
            {
                pass.parallelRecord = PassParallelRecordDesc{
                    .replaySemantics = pass.parallelRecord->replaySemantics,
                };
            }
        });
    });

    return compiledTemplate;
}

void RenderGraphCompileCache::patchCompiledResources(std::vector<CompiledResourceDesc> &compiledResources,
                                                     RenderGraphFrameDescription &frame)
{
    auto resourceByHandle = makeResourceLookup(frame);

    std::ranges::for_each(compiledResources, [&](CompiledResourceDesc &compiledResource) {
        auto resourceIt = resourceByHandle.find(compiledResource.handle);
        nrAssert(resourceIt != resourceByHandle.end(),
                 "RenderGraphCompileCache cached resource is missing in current frame.");
        auto &frameResource = resourceIt->second.get();

        compiledResource.debugName = resourceDebugName(frameResource);
        compiledResource.importedBufferResource.reset();
        compiledResource.importedImageResource.reset();
        compiledResource.retainedBufferState.reset();
        compiledResource.retainedImageState.reset();
        compiledResource.retainedAccelerationStructureState.reset();
        compiledResource.retainedState.reset();
        compiledResource.importedAccelerationStructureResource.reset();

        std::visit(
            [&](const auto &desc) {
                using DescT = std::remove_cvref_t<decltype(desc)>;
                if constexpr (std::same_as<DescT, GraphImportedBufferDesc>)
                {
                    compiledResource.importedBufferResource = desc.importedResource;
                    compiledResource.retainedBufferState = desc.retainedState;
                }
                else if constexpr (std::same_as<DescT, GraphImportedImageDesc>)
                {
                    compiledResource.importedImageResource = desc.importedResource;
                    compiledResource.retainedImageState = desc.retainedState;
                    compiledResource.retainedState = desc.retainedState;
                }
                else if constexpr (std::same_as<DescT, GraphImportedAccelerationStructureDesc>)
                {
                    compiledResource.importedAccelerationStructureResource = desc.importedResource;
                    compiledResource.retainedAccelerationStructureState = desc.retainedState;
                    if (desc.importedResource.has_value())
                    {
                        compiledResource.resolvedAccelerationStructureType = desc.importedResource->get().type();
                        compiledResource.resolvedAccelerationStructureSize = desc.importedResource->get().size();
                    }
                    else
                    {
                        compiledResource.resolvedAccelerationStructureType = desc.type;
                        compiledResource.resolvedAccelerationStructureSize = desc.size;
                    }
                }
            },
            frameResource.desc);
    });
}

void RenderGraphCompileCache::patchCompiledPasses(std::vector<CompiledSubmitBatch> &submitBatches,
                                                  RenderGraphFrameDescription &frame)
{
    auto passByHandle = makePassLookup(frame);

    std::ranges::for_each(submitBatches, [&](CompiledSubmitBatch &batch) {
        std::ranges::for_each(batch.passes, [&](CompiledPass &compiledPass) {
            auto passIt = passByHandle.find(compiledPass.handle);
            nrAssert(passIt != passByHandle.end(), "RenderGraphCompileCache cached pass is missing in current frame.");
            auto &framePass = passIt->second.get();

            compiledPass.debugName = std::move(framePass.debugName);
            compiledPass.shaderStages = framePass.shaderStages;
            compiledPass.copy = std::move(framePass.copy);
            compiledPass.resourceUses = std::move(framePass.resourceUses);
            compiledPass.prepare = std::move(framePass.prepare);
            compiledPass.record = std::move(framePass.record);
            compiledPass.parallelRecord = std::move(framePass.parallelRecord);
        });
    });
}

void RenderGraphCompileCache::patchCompiledSubmitDebugNames(std::vector<CompiledSubmitBatch> &submitBatches,
                                                            const RenderGraphFrameDescription &frame)
{
    auto submitByHandle = makeSubmitLookup(frame);
    std::ranges::for_each(submitBatches, [&](CompiledSubmitBatch &batch) {
        if (!batch.openedBySubmitNode.has_value())
        {
            return;
        }

        auto submitIt = submitByHandle.find(*batch.openedBySubmitNode);
        nrAssert(submitIt != submitByHandle.end(),
                 "RenderGraphCompileCache cached submit node is missing in current frame.");
        batch.openedBySubmitNodeDebugName = submitIt->second.get().debugName;
        batch.openedBySubmitNodeKind = submitIt->second.get().kind;
    });
}

[[nodiscard]] CompiledGraphFrame RenderGraphCompileCache::materializeCachedFrame(
    const CompiledGraphFrame &compiledTemplate, RenderGraphFrameDescription &frame)
{
    auto compiled = CompiledGraphFrame{compiledTemplate};
    patchCompiledResources(compiled.resources, frame);
    patchCompiledPasses(compiled.submitBatches, frame);
    patchCompiledSubmitDebugNames(compiled.submitBatches, frame);
    compiled.frameData = std::move(frame.frameData);
    return compiled;
}

[[nodiscard]] CompiledGraphFrame RenderGraphCompileCache::compileConsumingCached(RenderGraphFrameDescription &frame)
{
    auto signature = makeSignature(frame);
    auto found = std::ranges::find_if(entries_, [&](const CacheEntry &entry) { return entry.signature == signature; });

    if (found != entries_.end())
    {
        auto entry = std::move(*found);
        entries_.erase(found);
        entries_.insert(entries_.begin(), std::move(entry));
        ++hitCount_;
        return materializeCachedFrame(entries_.front().compiledTemplate, frame);
    }

    ++missCount_;
    auto compiled = compiler_.compileConsuming(frame);
    entries_.insert(entries_.begin(), CacheEntry{
                                          .signature = std::move(signature),
                                          .compiledTemplate = makeCompiledTemplate(compiled),
                                      });
    if (entries_.size() > kMaxEntries)
    {
        entries_.resize(kMaxEntries);
    }
    return compiled;
}

void RenderGraphCompileCache::clear() noexcept
{
    entries_.clear();
    hitCount_ = 0;
    missCount_ = 0;
}

[[nodiscard]] RenderGraphCompileCacheStatistics RenderGraphCompileCache::statistics() const noexcept
{
    return RenderGraphCompileCacheStatistics{
        .hitCount = hitCount_,
        .missCount = missCount_,
        .entryCount = entries_.size(),
    };
}

[[nodiscard]] bool RenderGraphSkeletonCache::contains(const RenderGraphSkeletonKey &key) const
{
    return std::ranges::any_of(entries_, [&](const Entry &entry) { return entry.key == key; });
}

RenderGraphSkeletonPatchContext::RenderGraphSkeletonPatchContext(
    RenderGraphFrameDescription &frame, RenderGraphSkeletonNodePatchLayout layout,
    const std::map<std::string, GraphResourceHandle> &namedFrameResources,
    const std::map<std::string, GraphFrameDataHandle> &namedFrameData,
    const FrameGlobalResources *globalResources) noexcept
    : frame_(frame), layout_(layout), namedFrameResources_(namedFrameResources), namedFrameData_(namedFrameData),
      globalResources_(globalResources)
{
}

void RenderGraphSkeletonPatchContext::patchResource(std::size_t localSlot, GraphResourceDescVariant desc)
{
    nrAssert(localSlot < layout_.resourceCount, "Skeleton resource patch slot is outside the node layout.");
    auto const index = layout_.resourceBegin + localSlot;
    nrAssert(index < frame_.get().resources.size(), "Skeleton resource patch index is outside the frame.");
    frame_.get().resources[index].desc = std::move(desc);
}

void RenderGraphSkeletonPatchContext::patchFrameData(std::size_t localSlot, std::string_view debugName,
                                                     std::any payload)
{
    nrAssert(localSlot < layout_.frameDataCount, "Skeleton frame-data patch slot is outside the node layout.");
    auto const index = layout_.frameDataBegin + localSlot;
    nrAssert(index < frame_.get().frameData.size(), "Skeleton frame-data patch index is outside the frame.");
    auto &slot = frame_.get().frameData[index];
    slot.debugName = debugName;
    slot.payload = std::move(payload);
}

void RenderGraphSkeletonPatchContext::patchPass(std::size_t localSlot, std::string_view debugName,
                                                PassPrepareCallback prepare, PassRecordCallback record,
                                                std::optional<PassParallelRecordDesc> parallelRecord)
{
    nrAssert(localSlot < layout_.passCount, "Skeleton pass patch slot is outside the node layout.");
    auto const index = layout_.passBegin + localSlot;
    nrAssert(index < frame_.get().passes.size(), "Skeleton pass patch index is outside the frame.");
    auto &pass = frame_.get().passes[index];
    nrAssert(!pass.isCopyPass, "Skeleton callback patch cannot target a copy pass.");
    pass.debugName = debugName;
    pass.prepare = std::move(prepare);
    pass.record = std::move(record);
    pass.parallelRecord = std::move(parallelRecord);
}

void RenderGraphSkeletonPatchContext::patchCopy(std::size_t localSlot, std::string_view debugName, CopyPassDesc copy)
{
    nrAssert(localSlot < layout_.passCount, "Skeleton copy patch slot is outside the node layout.");
    auto const index = layout_.passBegin + localSlot;
    nrAssert(index < frame_.get().passes.size(), "Skeleton copy patch index is outside the frame.");
    auto &pass = frame_.get().passes[index];
    nrAssert(pass.isCopyPass, "Skeleton copy patch requires a predeclared copy pass.");
    pass.debugName = debugName;
    pass.copy = std::move(copy);
}

[[nodiscard]] GraphResourceHandle RenderGraphSkeletonPatchContext::namedResource(std::string_view name) const
{
    auto found = namedFrameResources_.get().find(std::string{name});
    nrAssert(found != namedFrameResources_.get().end(), "Skeleton named resource is absent from the cached layout.");
    return found->second;
}

[[nodiscard]] GraphFrameDataHandle RenderGraphSkeletonPatchContext::namedFrameData(std::string_view name) const
{
    auto found = namedFrameData_.get().find(std::string{name});
    nrAssert(found != namedFrameData_.get().end(), "Skeleton named frame data is absent from the cached layout.");
    return found->second;
}

[[nodiscard]] std::optional<std::reference_wrapper<const std::any>> RenderGraphSkeletonPatchContext::
    resolveFrameDataPayload(GraphFrameDataHandle handle) const
{
    nrAssert(handle.valid(), "Skeleton frame-data payload resolution requires a valid handle.");
    auto const found = std::ranges::find(frame_.get().frameData, handle, &GraphFrameDataDesc::handle);
    if (found == frame_.get().frameData.end() || !found->payload.has_value())
    {
        return {};
    }
    return std::cref(found->payload);
}

[[nodiscard]] const FrameGlobalResources &RenderGraphSkeletonPatchContext::globalResources() const noexcept
{
    nrAssert(globalResources_ != nullptr, "Skeleton patch requires current renderer-global resources.");
    return *globalResources_;
}

[[nodiscard]] GraphResourceHandle RenderGraphSkeletonPatchContext::resource(std::size_t localSlot) const
{
    nrAssert(localSlot < layout_.resourceCount, "Skeleton resource slot is outside the node layout.");
    return frame_.get().resources[layout_.resourceBegin + localSlot].handle;
}

[[nodiscard]] QueueDomain RenderGraphSkeletonPatchContext::queue() const noexcept
{
    return layout_.queue;
}

[[nodiscard]] GraphFrameDataHandle RenderGraphSkeletonPatchContext::frameData(std::size_t localSlot) const
{
    nrAssert(localSlot < layout_.frameDataCount, "Skeleton frame-data slot is outside the node layout.");
    return frame_.get().frameData[layout_.frameDataBegin + localSlot].handle;
}

[[nodiscard]] GraphPassHandle RenderGraphSkeletonPatchContext::passHandle(std::size_t localSlot) const
{
    nrAssert(localSlot < layout_.passCount, "Skeleton pass slot is outside the node layout.");
    return frame_.get().passes[layout_.passBegin + localSlot].handle;
}

[[nodiscard]] GraphResourceHandle RenderGraphSkeletonPatchContext::passResource(std::size_t localPassSlot,
                                                                                std::size_t useSlot) const
{
    nrAssert(localPassSlot < layout_.passCount, "Skeleton pass slot is outside the node layout.");
    auto const &pass = frame_.get().passes[layout_.passBegin + localPassSlot];
    nrAssert(useSlot < pass.resourceUses.size(), "Skeleton pass resource-use slot is outside the cached topology.");
    return pass.resourceUses[useSlot].resource;
}

[[nodiscard]] bool RenderGraphSkeletonPatchContext::hasNamedResource(std::string_view name) const
{
    return namedFrameResources_.get().contains(std::string{name});
}

[[nodiscard]] bool RenderGraphSkeletonPatchContext::hasNamedFrameData(std::string_view name) const
{
    return namedFrameData_.get().contains(std::string{name});
}

[[nodiscard]] std::optional<RenderGraphSkeletonImageResourceDesc> RenderGraphSkeletonPatchContext::
    describeImageResource(GraphResourceHandle resource) const
{
    auto found = std::ranges::find(frame_.get().resources, resource, &GraphResourceDesc::handle);
    nrAssert(found != frame_.get().resources.end(), "Skeleton image description resource is absent from the frame.");
    return std::visit(
        [](const auto &desc) -> std::optional<RenderGraphSkeletonImageResourceDesc> {
            using DescT = std::remove_cvref_t<decltype(desc)>;
            if constexpr (std::same_as<DescT, GraphTransientImageDesc> || std::same_as<DescT, GraphImportedImageDesc>)
            {
                return RenderGraphSkeletonImageResourceDesc{
                    .debugName = desc.debugName,
                    .extent = desc.extent,
                    .format = desc.format,
                    .aspect = desc.aspect,
                };
            }
            else if constexpr (std::same_as<DescT, GraphImportedSwapchainImageDesc>)
            {
                return RenderGraphSkeletonImageResourceDesc{
                    .debugName = desc.debugName,
                    .extent = desc.extent,
                    .format = desc.format,
                };
            }
            return std::nullopt;
        },
        found->desc);
}

[[nodiscard]] std::shared_ptr<const RenderGraphSkeletonTemplate> RenderGraphSkeletonCache::lookup(
    const RenderGraphSkeletonKey &key) const
{
    auto found = std::ranges::find_if(entries_, [&](const Entry &entry) { return entry.key == key; });
    return found != entries_.end() ? found->skeleton : nullptr;
}

[[nodiscard]] RenderGraphFrameDescription RenderGraphSkeletonCache::instantiate(
    const RenderGraphSkeletonTemplate &skeleton)
{
    return skeleton.staticFrame;
}

[[nodiscard]] RenderGraphSkeletonTemplate RenderGraphSkeletonCache::makeTemplate(
    const RenderGraphFrameDescription &frame, RenderGraphSkeletonCapture capture)
{
    auto skeleton = RenderGraphSkeletonTemplate{
        .staticFrame = frame,
        .globalPatchLayout = capture.globalPatchLayout,
        .nodePatchLayouts = std::move(capture.nodePatchLayouts),
        .namedFrameResources = std::move(capture.namedFrameResources),
        .namedFrameData = std::move(capture.namedFrameData),
    };

    std::ranges::for_each(skeleton.staticFrame.resources, [](GraphResourceDesc &resource) {
        std::visit(
            [](auto &desc) {
                using DescT = std::remove_cvref_t<decltype(desc)>;
                desc.debugName.clear();
                if constexpr (std::same_as<DescT, GraphImportedBufferDesc>)
                {
                    desc.importedResource.reset();
                    desc.retainedState.reset();
                    desc.initialOwnership = ResourceOwnershipDomain::Undefined;
                }
                else if constexpr (std::same_as<DescT, GraphImportedImageDesc>)
                {
                    desc.importedResource.reset();
                    desc.retainedState.reset();
                    desc.initialOwnership = ResourceOwnershipDomain::Undefined;
                    desc.initialLayout = ImageLayoutIntent::Undefined;
                    desc.initialAccessScope = {};
                }
                else if constexpr (std::same_as<DescT, GraphImportedAccelerationStructureDesc>)
                {
                    desc.importedResource.reset();
                    desc.retainedState.reset();
                    desc.initialOwnership = ResourceOwnershipDomain::Undefined;
                }
            },
            resource.desc);
    });

    std::ranges::for_each(skeleton.staticFrame.frameData, [](GraphFrameDataDesc &frameData) {
        frameData.debugName.clear();
        frameData.payload.reset();
    });

    std::ranges::for_each(skeleton.staticFrame.passes, [](PassExecutionDesc &pass) {
        pass.debugName.clear();
        pass.prepare = nullptr;
        pass.record = nullptr;
        if (pass.parallelRecord.has_value())
        {
            pass.parallelRecord = PassParallelRecordDesc{
                .replaySemantics = pass.parallelRecord->replaySemantics,
            };
        }
        if (pass.isCopyPass)
        {
            pass.copy.reset();
        }
    });

    if (skeleton.nodePatchLayouts.empty())
    {
        skeleton.nodePatchLayouts.reserve(frame.nodes.size());
        std::ranges::for_each(frame.nodes, [&](const GraphNodeDesc &node) {
            auto firstPass = std::ranges::find_if(
                frame.passes, [&](const PassExecutionDesc &pass) { return pass.node == node.handle; });
            auto passCount = static_cast<std::size_t>(std::ranges::count_if(
                frame.passes, [&](const PassExecutionDesc &pass) { return pass.node == node.handle; }));
            skeleton.nodePatchLayouts.push_back(RenderGraphSkeletonNodePatchLayout{
                .queue = node.queue,
                .passBegin = firstPass != frame.passes.end()
                                 ? static_cast<std::size_t>(std::ranges::distance(frame.passes.begin(), firstPass))
                                 : 0u,
                .passCount = passCount,
            });
        });
    }
    return skeleton;
}

[[nodiscard]] RenderGraphSkeletonCache::ProbeResult RenderGraphSkeletonCache::acceptMaterialized(
    RenderGraphSkeletonKey key, const RenderGraphFrameDescription &frame, RenderGraphSkeletonCapture capture)
{
    auto structure = RenderGraphCompileCache::structuralSignature(frame);
    auto found = std::ranges::find_if(entries_, [&](const Entry &entry) { return entry.key == key; });
    if (found == entries_.end())
    {
        ++statistics_.missCount;
        statistics_.lastMissReason = RenderGraphSkeletonMissReason::KeyNotFound;
        entries_.insert(entries_.begin(), Entry{
                                              .key = std::move(key),
                                              .structure = std::move(structure),
                                              .skeleton = std::make_shared<const RenderGraphSkeletonTemplate>(
                                                  makeTemplate(frame, std::move(capture))),
                                          });
        if (entries_.size() > kMaxEntries)
        {
            entries_.resize(kMaxEntries);
        }
        statistics_.entryCount = entries_.size();
        return ProbeResult{
            .missReason = RenderGraphSkeletonMissReason::KeyNotFound,
        };
    }

    if (found->structure != structure)
    {
        ++statistics_.missCount;
        ++statistics_.structureMismatchCount;
        statistics_.lastMissReason = RenderGraphSkeletonMissReason::StructureMismatch;
        found->structure = std::move(structure);
        found->skeleton = std::make_shared<const RenderGraphSkeletonTemplate>(makeTemplate(frame, std::move(capture)));
        auto replacement = std::move(*found);
        entries_.erase(found);
        entries_.insert(entries_.begin(), std::move(replacement));
        return ProbeResult{
            .keyHit = true,
            .missReason = RenderGraphSkeletonMissReason::StructureMismatch,
        };
    }

    ++statistics_.hitCount;
    statistics_.lastMissReason = RenderGraphSkeletonMissReason::None;
    auto hit = std::move(*found);
    entries_.erase(found);
    entries_.insert(entries_.begin(), std::move(hit));
    return ProbeResult{
        .keyHit = true,
        .structureMatches = true,
    };
}

void RenderGraphSkeletonCache::recordHit() noexcept
{
    ++statistics_.hitCount;
    statistics_.lastMissReason = RenderGraphSkeletonMissReason::None;
}

void RenderGraphSkeletonCache::refreshMaterialized(RenderGraphSkeletonKey key, const RenderGraphFrameDescription &frame,
                                                   RenderGraphSkeletonCapture capture)
{
    auto structure = RenderGraphCompileCache::structuralSignature(frame);
    auto found = std::ranges::find_if(entries_, [&](const Entry &entry) { return entry.key == key; });
    auto replacement = Entry{
        .key = std::move(key),
        .structure = std::move(structure),
        .skeleton = std::make_shared<const RenderGraphSkeletonTemplate>(makeTemplate(frame, std::move(capture))),
    };
    if (found != entries_.end())
    {
        entries_.erase(found);
    }
    entries_.insert(entries_.begin(), std::move(replacement));
    if (entries_.size() > kMaxEntries)
    {
        entries_.resize(kMaxEntries);
    }
    statistics_.entryCount = entries_.size();
}

void RenderGraphSkeletonCache::recordMiss(RenderGraphSkeletonMissReason reason) noexcept
{
    ++statistics_.missCount;
    statistics_.lastMissReason = reason;
}

void RenderGraphSkeletonCache::clear(RenderGraphSkeletonMissReason reason) noexcept
{
    if (!entries_.empty())
    {
        ++statistics_.invalidationCount;
    }
    entries_.clear();
    statistics_.entryCount = 0;
    statistics_.lastMissReason = reason;
}

[[nodiscard]] RenderGraphSkeletonCacheStatistics RenderGraphSkeletonCache::statistics() const noexcept
{
    auto result = statistics_;
    result.entryCount = entries_.size();
    return result;
}

void BindlessImageTableCache::clear() noexcept
{
    tables_.clear();
}

void BindlessImageTableCache::invalidateTableForFrame(std::uintptr_t ownerKey, std::string_view tableKey,
                                                      std::uint32_t frameIndex) noexcept
{
    auto tableIt = tables_.find(TableKey{
        .ownerKey = ownerKey,
        .tableKey = std::string(tableKey),
    });
    if (tableIt == tables_.end())
    {
        return;
    }

    auto const frameSlot = static_cast<std::size_t>(frameIndex % nr::maxFrameInFlight);
    tableIt->second.initialized[frameSlot] = false;
    tableIt->second.versions[frameSlot] = 0;
    tableIt->second.descriptorIdsByFrame[frameSlot].clear();
}

[[nodiscard]] nr::rhi::ShaderBindingSnapshot BindlessImageTableCache::makeSnapshotForFrameCore(
    std::uintptr_t ownerKey, const nr::rhi::ShaderCursor &tableCursor, const nr::rhi::ShaderCursor &root,
    std::uint32_t frameIndex, const BindlessImageTableRequest &request)
{
    auto &tableState = tables_[TableKey{
        .ownerKey = ownerKey,
        .tableKey = request.tableKey,
    }];

    auto const frameSlot = static_cast<std::size_t>(frameIndex % nr::maxFrameInFlight);
    auto &initialized = tableState.initialized[frameSlot];
    auto &cachedVersion = tableState.versions[frameSlot];
    auto &previousDescriptorIds = tableState.descriptorIdsByFrame[frameSlot];

    auto const cacheHit = initialized && cachedVersion == request.tableVersion;
    if (cacheHit && !request.refreshActiveDescriptorsOnCacheHit)
    {
        return {};
    }

    auto currentDescriptorIds = request.descriptorsById | std::views::keys | std::ranges::to<std::set<std::uint32_t>>();

    auto writeDescriptor = [&](std::uint32_t descriptorId, const BindlessImageDescriptor &descriptor) {
        nrAssert(descriptorId < request.descriptorCapacity,
                 std::format("Bindless image table '{}' descriptor id {} exceeds capacity {}.", request.tableKey,
                             descriptorId, request.descriptorCapacity));

        auto elementCursor = tableCursor[descriptorId];
        if (descriptor.image.has_value())
        {
            if (request.usesImmutableSampler)
            {
                static_cast<void>(elementCursor.setObject(descriptor.image->get(), descriptor.layout));
                return;
            }

            static_cast<void>(elementCursor.setObject(descriptor.image->get(), request.sampler, descriptor.layout));
            return;
        }

        nrAssert(descriptor.logicalResourceId != 0u || !descriptor.debugName.empty(),
                 std::format("Bindless image table '{}' logical descriptor requires an id or debug name.",
                             request.tableKey));
        auto logicalWrite = nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = descriptor.logicalResourceId,
            .debugName = descriptor.debugName,
            .imageLayout = descriptor.layout,
        };
        if (!request.usesImmutableSampler)
        {
            logicalWrite.sampler = request.sampler;
        }
        static_cast<void>(elementCursor.setObject(logicalWrite));
    };

    if (request.fallbackDescriptor.has_value())
    {
        if (!initialized)
        {
            auto descriptorSlots = std::views::iota(std::uint32_t{0}, request.descriptorCapacity);
            std::ranges::for_each(descriptorSlots, [&](std::uint32_t descriptorId) {
                writeDescriptor(descriptorId, *request.fallbackDescriptor);
            });
        }
        else
        {
            std::ranges::for_each(previousDescriptorIds, [&](std::uint32_t previousDescriptorId) {
                if (!currentDescriptorIds.contains(previousDescriptorId))
                {
                    writeDescriptor(previousDescriptorId, *request.fallbackDescriptor);
                }
            });
        }
    }

    std::ranges::for_each(request.descriptorsById,
                          [&](const auto &entry) { writeDescriptor(entry.first, entry.second); });

    auto snapshot = root.snapshot();
    if (cacheHit && request.refreshActiveDescriptorsOnCacheHit)
    {
        snapshot.forceDescriptorWrites();
    }
    root.clearSnapshot();
    initialized = true;
    cachedVersion = request.tableVersion;
    previousDescriptorIds = std::move(currentDescriptorIds);
    return snapshot;
}

[[nodiscard]] RendererSceneTextureDescriptorTable RendererGlobalDescriptorTableCache::buildSceneTextureDescriptorTable(
    const RendererSceneTextureDescriptorTableInput &input)
{
    auto descriptorsById = std::map<std::uint32_t, SceneTextureDescriptorBinding>{};
    auto descriptorKeys = std::map<std::uint32_t, SceneTextureDescriptorKey>{};

    descriptorsById.insert_or_assign(nr::scene::kDefaultSceneTextureId,
                                     SceneTextureDescriptorBinding{
                                         .descriptorIndex = nr::scene::kDefaultSceneTextureId,
                                         .image = input.fallbackImage,
                                         .layout = vk::ImageLayout::eShaderReadOnlyOptimal,
                                         .gpuVersion = 1,
                                     });
    descriptorKeys.insert_or_assign(nr::scene::kDefaultSceneTextureId, SceneTextureDescriptorKey{
                                                                           .gpuVersion = 1,
                                                                       });

    if (input.scene.has_value())
    {
        auto const &scene = input.scene->get();
        std::ranges::for_each(input.sceneTextureHandlesById, [&](const auto &entry) {
            auto const textureId = entry.first;
            auto const textureHandle = entry.second;
            if (textureId == nr::scene::kDefaultSceneTextureId)
            {
                return;
            }

            nrAssert(textureId <= nr::scene::kMaxSceneTextureId,
                     std::format("Scene texture descriptor id {} exceeds packed material texture id capacity {}.",
                                 textureId, nr::scene::kMaxSceneTextureId));

            auto binding = scene.tryGetSampledTextureBinding(textureHandle);
            nrAssert(binding.has_value(),
                     std::format("Renderer scene texture descriptor table expected resident sampled texture id {} for "
                                 "texture handle (slot={}, generation={}).",
                                 textureId, textureHandle.slot, textureHandle.generation));

            nrAssert(binding->descriptorIndex == textureId,
                     std::format("Scene texture binding id mismatch. expected={}, actual={}.", textureId,
                                 binding->descriptorIndex));
            nrAssert(binding->layout == vk::ImageLayout::eShaderReadOnlyOptimal,
                     std::format("Scene texture {} must be imported from shader-read layout, got {}.", textureId,
                                 vk::to_string(binding->layout)));

            descriptorsById.insert_or_assign(textureId, SceneTextureDescriptorBinding{
                                                            .descriptorIndex = textureId,
                                                            .image = binding->image,
                                                            .layout = binding->layout,
                                                            .gpuVersion = binding->gpuVersion,
                                                        });
            descriptorKeys.insert_or_assign(textureId, SceneTextureDescriptorKey{
                                                           .texture = textureHandle,
                                                           .gpuVersion = binding->gpuVersion,
                                                       });
        });
    }

    if (descriptorKeys != sceneTextureDescriptorKeys_)
    {
        sceneTextureDescriptorKeys_ = std::move(descriptorKeys);
        ++sceneTextureDescriptorVersion_;
    }

    return RendererSceneTextureDescriptorTable{
        .descriptorsById = std::move(descriptorsById),
        .version = sceneTextureDescriptorVersion_,
    };
}

void RendererGlobalDescriptorTableCache::clear() noexcept
{
    sceneTextureDescriptorKeys_.clear();
    sceneTextureDescriptorVersion_ = 1;
}

void RendererCacheSuite::clear() noexcept
{
    skeletonCache.clear();
    compileCache.clear();
    bindlessImageTableCache.clear();
    globalDescriptorTableCache.clear();
}
} // namespace nr::renderer
