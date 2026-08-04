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

        device.waitIdle();
    }};
} // namespace
