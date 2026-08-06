import std;
import dependency.vulkan;
import nr.rhi;
import nr.renderer;
import nr.test;

namespace
{
const nr::test::CaseRegistrar maintenance9OwnershipCase{
    "renderer maintenance9 specialization removes retained initial ownership submit", [] {
        auto device = nr::rhi::Device{};
        device.initialize("nr_renderer_maintenance9_ownership_contract_test", "NewbieRenderer");

        auto const guideUsage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled |
                                vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc;
        auto guideInfo =
            nr::rhi::makeImageCreateInfo(vk::Format::eR16G16B16A16Sfloat, vk::Extent2D{128u, 72u}, guideUsage);
        auto guide = device.resourceFactory.createImage(guideInfo, nr::rhi::MemoryUsage::GpuOnly,
                                                        "Maintenance9.PathTracingGuide");

        auto retainedState = nr::renderer::RetainedImageState{
            .common =
                nr::renderer::RetainedExternalResourceState{
                    .initialized = true,
                    .ownership = nr::renderer::ResourceOwnershipDomain::Compute,
                    .access =
                        nr::renderer::AccessScope{
                            .stages = vk::PipelineStageFlagBits2::eComputeShader,
                            .access = vk::AccessFlagBits2::eShaderRead,
                        },
                    .lastSubmissionTimelineValue = 1u,
                },
            .layout = nr::renderer::ImageLayoutIntent::ShaderReadOnly,
        };

        auto builder = nr::renderer::RenderGraphBuilder{};
        auto node = builder.addNode("Maintenance9.PathTracing", nr::renderer::QueueDomain::Graphics);
        auto guideResource = builder.addResource(nr::renderer::GraphImportedImageDesc{
            .debugName = "Maintenance9.PathTracingGuide",
            .lifetime = nr::renderer::ResourceLifetime::RendererPersistent,
            .initialOwnership = retainedState.common.ownership,
            .extent = vk::Extent3D{128u, 72u, 1u},
            .format = vk::Format::eR16G16B16A16Sfloat,
            .usageIntents =
                {
                    nr::renderer::ImageUsageIntent::StorageWrite,
                    nr::renderer::ImageUsageIntent::Sampled,
                    nr::renderer::ImageUsageIntent::TransferDst,
                    nr::renderer::ImageUsageIntent::TransferSrc,
                },
            .initialLayout = retainedState.layout,
            .initialAccessScope = retainedState.common.access,
            .importedResource = std::cref(guide),
            .retainedState = std::ref(retainedState),
        });
        auto uses = std::array{nr::renderer::use::storageWrite(guideResource)};
        static_cast<void>(builder.addPass(
            "Maintenance9.PathTracing.Write", node, uses, [](const nr::renderer::PassRecordContext &) {}, nullptr,
            false, vk::PipelineStageFlagBits2::eRayTracingShaderKHR));

        auto compiled = nr::renderer::RenderGraphCompiler{}.compile(builder.build());
        auto unspecializedPlan = nr::renderer::RenderGraphExecutor{}.buildPlan(compiled);
        nr::test::requireEqual(unspecializedPlan.initialReleaseBatches.size(), std::size_t{1},
                               "compile-time ownership should retain the conservative initial release fallback");

        auto executor = nr::renderer::RenderGraphExecutor{};
        auto prepared = executor.prepareFrame(std::move(compiled), nr::renderer::RenderGraphExecutor::ExecuteContext{
                                                                       .device = device,
                                                                   });

        nr::test::require(prepared.plan.initialReleaseBatches.empty(),
                          "maintenance9-eligible retained guide should not create an initial ownership submit");
        nr::test::require(prepared.plan.batches.front().headAcquireTransitions.empty(),
                          "maintenance9-eligible retained guide should not record an ownership acquire");
        nr::test::requireEqual(prepared.plan.batches.front().initialResourceWaits.size(), std::size_t{1},
                               "implicit initial acquire should wait on the retained source submission");
        nr::test::requireEqual(prepared.plan.batches.front().initialResourceWaits.front().token,
                               nr::renderer::RendererSubmitToken{
                                   .queue = nr::renderer::QueueDomain::Compute,
                                   .value = 1u,
                               });
        nr::test::requireEqual(prepared.plan.totalInPassBarrierCount, std::size_t{1},
                               "omitted image ownership should retain its consumer-side layout transition");
        nr::test::require(prepared.compiled.ownershipTransitions.empty(),
                          "prepared ownership diagnostics should exclude the omitted transfer");

        auto const &transition = prepared.compiled.submitBatches.front().passes.front().preBarriers.front();
        nr::test::requireEqual(transition.strength, nr::renderer::DependencyStrength::BarrierRequired);
        nr::test::require(transition.srcScope.access == vk::AccessFlags2{});
        nr::test::require(transition.srcScope.stages == transition.dstScope.stages);

        auto sameLayoutCrossQueueState = nr::renderer::RetainedImageState{
            .common =
                nr::renderer::RetainedExternalResourceState{
                    .initialized = true,
                    .ownership = nr::renderer::ResourceOwnershipDomain::Compute,
                    .access =
                        nr::renderer::AccessScope{
                            .stages = vk::PipelineStageFlagBits2::eComputeShader,
                            .access = vk::AccessFlagBits2::eShaderStorageWrite,
                        },
                    .lastSubmissionTimelineValue = 17u,
                },
            .layout = nr::renderer::ImageLayoutIntent::General,
        };
        auto crossQueueBuilder = nr::renderer::RenderGraphBuilder{};
        auto crossQueueNode =
            crossQueueBuilder.addNode("Maintenance9.CrossQueueSameLayout", nr::renderer::QueueDomain::Graphics);
        auto crossQueueResource = crossQueueBuilder.addResource(nr::renderer::GraphImportedImageDesc{
            .debugName = "Maintenance9.CrossQueueSameLayout.Image",
            .lifetime = nr::renderer::ResourceLifetime::RendererPersistent,
            .initialOwnership = sameLayoutCrossQueueState.common.ownership,
            .extent = vk::Extent3D{128u, 72u, 1u},
            .format = vk::Format::eR16G16B16A16Sfloat,
            .usageIntents = {nr::renderer::ImageUsageIntent::StorageRead},
            .initialLayout = sameLayoutCrossQueueState.layout,
            .initialAccessScope = sameLayoutCrossQueueState.common.access,
            .importedResource = std::cref(guide),
            .retainedState = std::ref(sameLayoutCrossQueueState),
        });
        auto crossQueueUses = std::array{nr::renderer::use::storageRead(crossQueueResource)};
        static_cast<void>(crossQueueBuilder.addPass(
            "Maintenance9.CrossQueueSameLayout.Read", crossQueueNode, crossQueueUses,
            [](const nr::renderer::PassRecordContext &) {}, nullptr, false,
            vk::PipelineStageFlagBits2::eFragmentShader));

        auto crossQueueCompiled = nr::renderer::RenderGraphCompiler{}.compile(crossQueueBuilder.build());
        auto crossQueuePrepared = nr::renderer::RenderGraphExecutor{}.prepareFrame(
            std::move(crossQueueCompiled), nr::renderer::RenderGraphExecutor::ExecuteContext{
                                               .device = device,
                                           });
        auto const &crossQueueTransition =
            crossQueuePrepared.compiled.submitBatches.front().passes.front().preBarriers.front();
        nr::test::requireEqual(crossQueueTransition.strength, nr::renderer::DependencyStrength::InOrder,
                               "same-layout maintenance9 QFOT omission should remove the in-pass barrier");
        nr::test::requireEqual(crossQueuePrepared.plan.totalInPassBarrierCount, std::size_t{0});
        nr::test::requireEqual(crossQueuePrepared.plan.batches.front().initialResourceWaits.size(), std::size_t{1});
        nr::test::requireEqual(crossQueuePrepared.plan.batches.front().initialResourceWaits.front().token,
                               nr::renderer::RendererSubmitToken{
                                   .queue = nr::renderer::QueueDomain::Compute,
                                   .value = 17u,
                               },
                               "QFOT omission must retain the exact producer timeline value");

        auto sameQueueState = sameLayoutCrossQueueState;
        sameQueueState.common.ownership = nr::renderer::ResourceOwnershipDomain::Graphics;
        sameQueueState.common.lastSubmissionTimelineValue = 23u;
        auto sameQueueBuilder = nr::renderer::RenderGraphBuilder{};
        auto sameQueueNode = sameQueueBuilder.addNode("Maintenance9.SameQueue", nr::renderer::QueueDomain::Graphics);
        auto sameQueueResource = sameQueueBuilder.addResource(nr::renderer::GraphImportedImageDesc{
            .debugName = "Maintenance9.SameQueue.Image",
            .lifetime = nr::renderer::ResourceLifetime::RendererPersistent,
            .initialOwnership = sameQueueState.common.ownership,
            .extent = vk::Extent3D{128u, 72u, 1u},
            .format = vk::Format::eR16G16B16A16Sfloat,
            .usageIntents = {nr::renderer::ImageUsageIntent::StorageRead},
            .initialLayout = sameQueueState.layout,
            .initialAccessScope = sameQueueState.common.access,
            .importedResource = std::cref(guide),
            .retainedState = std::ref(sameQueueState),
        });
        auto sameQueueUses = std::array{nr::renderer::use::storageRead(sameQueueResource)};
        static_cast<void>(sameQueueBuilder.addPass(
            "Maintenance9.SameQueue.Read", sameQueueNode, sameQueueUses, [](const nr::renderer::PassRecordContext &) {},
            nullptr, false, vk::PipelineStageFlagBits2::eFragmentShader));

        auto sameQueueCompiled = nr::renderer::RenderGraphCompiler{}.compile(sameQueueBuilder.build());
        auto sameQueuePrepared = nr::renderer::RenderGraphExecutor{}.prepareFrame(
            std::move(sameQueueCompiled), nr::renderer::RenderGraphExecutor::ExecuteContext{
                                              .device = device,
                                          });
        auto const &sameQueueTransition =
            sameQueuePrepared.compiled.submitBatches.front().passes.front().preBarriers.front();
        nr::test::requireEqual(sameQueueTransition.strength, nr::renderer::DependencyStrength::BarrierRequired);
        nr::test::requireEqual(sameQueuePrepared.plan.totalInPassBarrierCount, std::size_t{1});
        nr::test::require(sameQueuePrepared.plan.batches.front().initialResourceWaits.empty(),
                          "same-queue retained hazard should not create an initial timeline wait");

        auto capabilityBuilder = nr::renderer::RenderGraphBuilder{};
        auto const capabilityNode =
            capabilityBuilder.addNode("ExecutorCapability.Record", nr::renderer::QueueDomain::Compute);
        static_cast<void>(capabilityBuilder.addSubmitNode("ExecutorCapability.AcquireSwapchain",
                                                          nr::renderer::SubmitBoundaryKind::SwapchainAcquire));
        auto const swapchainExtent = device.presentationContext.swapchainExtent();
        auto const capabilitySwapchain =
            capabilityBuilder.addResource(nr::renderer::GraphImportedSwapchainImageDesc{
                .debugName = "ExecutorCapability.Swapchain",
                .initialOwnership = nr::renderer::ResourceOwnershipDomain::Compute,
                .extent = vk::Extent3D{swapchainExtent.width, swapchainExtent.height, 1u},
                .format = device.presentationContext.swapchainFormat(),
            });
        auto const capabilityImage = capabilityBuilder.addResource(nr::renderer::GraphImportedImageDesc{
            .debugName = "ExecutorCapability.Image",
            .lifetime = nr::renderer::ResourceLifetime::FrameLocal,
            .initialOwnership = nr::renderer::ResourceOwnershipDomain::Compute,
            .extent = vk::Extent3D{128u, 72u, 1u},
            .format = vk::Format::eR16G16B16A16Sfloat,
            .usageIntents = {
                nr::renderer::ImageUsageIntent::StorageRead,
                nr::renderer::ImageUsageIntent::StorageWrite,
            },
            .importedResource = std::cref(guide),
        });
        auto const capabilityFrameData =
            capabilityBuilder.addFrameData("ExecutorCapability.FrameData", std::uint32_t{77u});
        auto const capabilityFrameDataUses = std::array{capabilityFrameData};
        auto serialResolved = std::atomic_bool{false};
        auto planningResolved = std::atomic_bool{false};
        auto rangeResolved = std::atomic_bool{false};
        auto recordedItems = std::atomic_size_t{0u};

        auto const serialUses = std::array{nr::renderer::use::storageWrite(capabilityImage)};
        static_cast<void>(capabilityBuilder.addPass(
            "ExecutorCapability.Serial", capabilityNode, serialUses,
            [capabilityImage, capabilityFrameData, &serialResolved](const nr::renderer::PassRecordContext &context) {
                auto const image = context.resolveImage(capabilityImage);
                auto const wrongType = context.resolveBuffer(capabilityImage);
                auto logicalResolver = nr::renderer::makeDefaultLogicalDescriptorResolver(context);
                auto descriptor = logicalResolver(
                    nr::rhi::LogicalResourceDescriptorWrite{
                        .logicalResourceId = capabilityImage.value,
                        .debugName = "ExecutorCapability.Image",
                    },
                    nr::rhi::DescriptorBindingInfo{
                        .descriptorType = vk::DescriptorType::eStorageImage,
                        .debugPath = "gImage",
                    },
                    0u);
                serialResolved.store(image.has_value() && !wrongType.has_value() && descriptor.has_value() &&
                                         std::holds_alternative<nr::rhi::ImageDescriptorWrite>(*descriptor) &&
                                         context.frameData<std::uint32_t>(capabilityFrameData) == 77u,
                                     std::memory_order_relaxed);
            },
            nullptr, false, vk::PipelineStageFlagBits2::eComputeShader, capabilityFrameDataUses));

        auto const parallelUses = std::array{nr::renderer::use::storageRead(capabilityImage)};
        static_cast<void>(capabilityBuilder.addPass(
            "ExecutorCapability.Parallel", capabilityNode, parallelUses,
            nr::renderer::PassParallelRecordDesc{
                .itemCount =
                    [capabilityImage, capabilityFrameData,
                     &planningResolved](const nr::renderer::PassRecordContext &context) -> std::size_t {
                    auto const image = context.resolveImage(capabilityImage);
                    planningResolved.store(image.has_value() && !context.resolveBuffer(capabilityImage).has_value() &&
                                               context.frameData<std::uint32_t>(capabilityFrameData) == 77u,
                                           std::memory_order_relaxed);
                    return 4u;
                },
                .recordRange =
                    [capabilityImage, capabilityFrameData, &rangeResolved,
                     &recordedItems](const nr::renderer::PassRangeRecordContext &context) {
                    auto const image = context.pass.resolveImage(capabilityImage);
                    rangeResolved.store(image.has_value() &&
                                            context.pass.frameData<std::uint32_t>(capabilityFrameData) == 77u,
                                        std::memory_order_relaxed);
                    recordedItems.fetch_add(context.range.size(), std::memory_order_relaxed);
                },
            },
            nullptr, vk::PipelineStageFlagBits2::eComputeShader, capabilityFrameDataUses));
        auto const presentUses = std::array{nr::renderer::use::presentRead(
            capabilitySwapchain, nr::renderer::ResourceOwnershipDomain::Compute)};
        static_cast<void>(capabilityBuilder.addPass(
            "ExecutorCapability.Present", capabilityNode, presentUses,
            [](const nr::renderer::PassRecordContext &) {}));

        auto capabilityCompiled = nr::renderer::RenderGraphCompiler{}.compile(capabilityBuilder.build());
        auto const begin = device.beginFrame();
        auto capabilityExecutor = nr::renderer::RenderGraphExecutor{};
        auto const capabilityContext = nr::renderer::RenderGraphExecutor::ExecuteContext{
            .device = device,
            .frameIndex = begin.frameIndex,
        };
        auto capabilityPrepared = capabilityExecutor.prepareFrame(std::move(capabilityCompiled), capabilityContext);
        auto const capabilityReport = capabilityExecutor.executePrepared(capabilityPrepared, capabilityContext);
        static_cast<void>(device.presentFrame());

        device.waitIdle();
        nr::test::require(serialResolved.load(std::memory_order_relaxed),
                          "serial record should resolve declared resource/frame-data capabilities");
        nr::test::require(planningResolved.load(std::memory_order_relaxed),
                          "parallel planning should resolve declared resource/frame-data capabilities");
        nr::test::require(rangeResolved.load(std::memory_order_relaxed),
                          "parallel range record should resolve declared resource/frame-data capabilities");
        nr::test::requireEqual(recordedItems.load(std::memory_order_relaxed), std::size_t{4u});
        nr::test::requireEqual(capabilityReport.submittedCompiledBatchIndices.size(), std::size_t{1u});
        nr::test::require(capabilityReport.swapchainImageIndex.has_value());
    }};
} // namespace
