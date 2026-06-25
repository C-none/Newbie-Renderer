import std;
import dependency;
import nr.rhi;
import nr.renderer;
import nr.test;

namespace
{
[[nodiscard]] nr::renderer::RenderGraphFrameDescription buildCrossQueueFrame()
{
    auto builder = nr::renderer::RenderGraphBuilder{};
    auto graphicsNode = builder.addNode("Geometry", nr::renderer::QueueDomain::Graphics);
    auto computeNode = builder.addNode("Resolve", nr::renderer::QueueDomain::Compute);

    auto color = builder.addResource(nr::renderer::GraphTransientImageDesc{
        .debugName = "Geometry.Color",
        .extent = vk::Extent3D{320, 180, 1},
        .format = vk::Format::eR8G8B8A8Unorm,
    });
    auto output = builder.addResource(nr::renderer::GraphTransientImageDesc{
        .debugName = "Resolve.Output",
        .extent = vk::Extent3D{320, 180, 1},
        .format = vk::Format::eR8G8B8A8Unorm,
    });

    auto graphicsUses = std::array{nr::renderer::use::colorWrite(color)};
    auto graphicsPass = builder.addPass("Geometry.Color", graphicsNode, graphicsUses, [](const nr::renderer::PassRecordContext &) {});

    auto submit = builder.addSubmitNode("GeometryToResolve");

    auto computeUses = std::array{
        nr::renderer::use::sampledRead(color),
        nr::renderer::use::storageWrite(output),
    };
    auto computePass = builder.addPass("Resolve.Compose", computeNode, computeUses, [](const nr::renderer::PassRecordContext &) {});

    nr::test::require(graphicsPass.valid(), "graphics pass should be valid");
    nr::test::require(submit.valid(), "submit boundary should be valid");
    nr::test::require(computePass.valid(), "compute pass should be valid");

    return builder.build();
}

[[nodiscard]] nr::renderer::RenderGraphFrameDescription buildMultiPassGraphicsFrame()
{
    auto builder = nr::renderer::RenderGraphBuilder{};
    auto node = builder.addNode("Graphics", nr::renderer::QueueDomain::Graphics);

    auto first = builder.addResource(nr::renderer::GraphTransientImageDesc{
        .debugName = "First",
        .extent = vk::Extent3D{64, 64, 1},
        .format = vk::Format::eR8G8B8A8Unorm,
    });
    auto second = builder.addResource(nr::renderer::GraphTransientImageDesc{
        .debugName = "Second",
        .extent = vk::Extent3D{64, 64, 1},
        .format = vk::Format::eR8G8B8A8Unorm,
    });
    auto third = builder.addResource(nr::renderer::GraphTransientImageDesc{
        .debugName = "Third",
        .extent = vk::Extent3D{64, 64, 1},
        .format = vk::Format::eR8G8B8A8Unorm,
    });

    auto firstUses = std::array{nr::renderer::use::colorWrite(first)};
    auto secondUses = std::array{nr::renderer::use::sampledRead(first), nr::renderer::use::colorWrite(second)};
    auto thirdUses = std::array{nr::renderer::use::sampledRead(second), nr::renderer::use::colorWrite(third)};

    static_cast<void>(builder.addPass("Graphics.First", node, firstUses, [](const nr::renderer::PassRecordContext&) {}));
    static_cast<void>(builder.addPass("Graphics.Second", node, secondUses, [](const nr::renderer::PassRecordContext&) {}));
    static_cast<void>(builder.addPass("Graphics.Third", node, thirdUses, [](const nr::renderer::PassRecordContext&) {}));

    return builder.build();
}

[[nodiscard]] nr::renderer::RenderGraphFrameDescription buildAccelerationStructureFrame()
{
    auto builder = nr::renderer::RenderGraphBuilder{};
    auto node = builder.addNode("RayTracing", nr::renderer::QueueDomain::Compute);

    auto tlas = builder.addResource(nr::renderer::GraphImportedAccelerationStructureDesc{
        .debugName = "Scene.TLAS",
        .type = vk::AccelerationStructureTypeKHR::eTopLevel,
        .size = 8192,
    });

    auto buildUses = std::array{nr::renderer::use::accelerationStructureBuildWrite(tlas)};
    static_cast<void>(builder.addPass("RayTracing.BuildTlas", node, buildUses, [](const nr::renderer::PassRecordContext&) {}));

    static_cast<void>(builder.addSubmitNode("BuildToTrace"));

    auto traceUses = std::array{nr::renderer::use::accelerationStructureTraceRead(tlas)};
    static_cast<void>(builder.addPass("RayTracing.Trace", node, traceUses, [](const nr::renderer::PassRecordContext&) {}));

    return builder.build();
}

const nr::test::CaseRegistrar compilerMappingCase{
    "render graph compiler maps usage and access intents",
    [] {
        nr::test::require(nr::renderer::RenderGraphCompiler::mapBufferUsageIntent(nr::renderer::BufferUsageIntent::ShaderBindingTable) ==
                          vk::BufferUsageFlagBits::eShaderBindingTableKHR);
        nr::test::require(nr::renderer::RenderGraphCompiler::mapBufferUsageIntent(nr::renderer::BufferUsageIntent::AccelerationStructureStorage) ==
                          vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR);
        nr::test::require(nr::renderer::RenderGraphCompiler::mapImageUsageIntent(nr::renderer::ImageUsageIntent::PresentSource) ==
                          vk::ImageUsageFlagBits::eTransferDst);
        nr::test::require(nr::renderer::RenderGraphCompiler::mapImageLayoutIntent(nr::renderer::ImageLayoutIntent::PresentSrc) ==
                          vk::ImageLayout::ePresentSrcKHR);
        nr::test::require(nr::renderer::RenderGraphCompiler::mapImageAspectIntent(nr::renderer::ImageAspectIntent::DepthStencil) ==
                          (vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil));

        auto graphicsUniform = nr::renderer::RenderGraphCompiler::mapBufferAccessIntent(
            nr::renderer::BufferAccessIntent::UniformRead,
            nr::renderer::QueueDomain::Graphics);
        nr::test::require(graphicsUniform.stages == vk::PipelineStageFlagBits2::eAllGraphics);
        nr::test::require(graphicsUniform.access == vk::AccessFlagBits2::eUniformRead);

        auto computeSample = nr::renderer::RenderGraphCompiler::mapImageAccessIntent(
            nr::renderer::ImageAccessIntent::SampledRead,
            nr::renderer::QueueDomain::Compute);
        nr::test::require(computeSample.stages ==
                          (vk::PipelineStageFlagBits2::eComputeShader |
                           vk::PipelineStageFlagBits2::eRayTracingShaderKHR));
        nr::test::require(computeSample.access == vk::AccessFlagBits2::eShaderSampledRead);

        auto colorReadWrite = nr::renderer::RenderGraphCompiler::mapImageAccessIntent(
            nr::renderer::ImageAccessIntent::ColorAttachmentReadWrite,
            nr::renderer::QueueDomain::Graphics);
        nr::test::require(colorReadWrite.stages == vk::PipelineStageFlagBits2::eColorAttachmentOutput);
        nr::test::require(colorReadWrite.access ==
                          (vk::AccessFlagBits2::eColorAttachmentRead |
                           vk::AccessFlagBits2::eColorAttachmentWrite));

        auto depthReadWrite = nr::renderer::RenderGraphCompiler::mapImageAccessIntent(
            nr::renderer::ImageAccessIntent::DepthStencilReadWrite,
            nr::renderer::QueueDomain::Graphics);
        nr::test::require(depthReadWrite.stages ==
                          (vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                           vk::PipelineStageFlagBits2::eLateFragmentTests));
        nr::test::require(depthReadWrite.access ==
                          (vk::AccessFlagBits2::eDepthStencilAttachmentRead |
                           vk::AccessFlagBits2::eDepthStencilAttachmentWrite));

        auto storageBufferReadWrite = nr::renderer::RenderGraphCompiler::mapBufferAccessIntent(
            nr::renderer::BufferAccessIntent::ShaderStorageReadWrite,
            nr::renderer::QueueDomain::Compute);
        nr::test::require(storageBufferReadWrite.stages ==
                          (vk::PipelineStageFlagBits2::eComputeShader |
                           vk::PipelineStageFlagBits2::eRayTracingShaderKHR));
        nr::test::require(storageBufferReadWrite.access ==
                          (vk::AccessFlagBits2::eShaderStorageRead |
                           vk::AccessFlagBits2::eShaderStorageWrite));

        auto accelerationStructureRead = nr::renderer::RenderGraphCompiler::mapBufferAccessIntent(
            nr::renderer::BufferAccessIntent::AccelerationStructureRead,
            nr::renderer::QueueDomain::Compute);
        nr::test::require(accelerationStructureRead.stages ==
                          (vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR |
                           vk::PipelineStageFlagBits2::eRayTracingShaderKHR));
        nr::test::require(accelerationStructureRead.access == vk::AccessFlagBits2::eAccelerationStructureReadKHR);

        auto shaderBindingTableRead = nr::renderer::RenderGraphCompiler::mapBufferAccessIntent(
            nr::renderer::BufferAccessIntent::ShaderBindingTableRead,
            nr::renderer::QueueDomain::Compute);
        nr::test::require(shaderBindingTableRead.stages == vk::PipelineStageFlagBits2::eRayTracingShaderKHR);
        nr::test::require(shaderBindingTableRead.access == vk::AccessFlagBits2::eShaderBindingTableReadKHR);

        auto asBuildWrite = nr::renderer::RenderGraphCompiler::mapAccelerationStructureAccessIntent(
            nr::renderer::AccelerationStructureAccessIntent::BuildWrite);
        nr::test::require(asBuildWrite.stages == vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR);
        nr::test::require(asBuildWrite.access == vk::AccessFlagBits2::eAccelerationStructureWriteKHR);

        auto asTraceRead = nr::renderer::RenderGraphCompiler::mapAccelerationStructureAccessIntent(
            nr::renderer::AccelerationStructureAccessIntent::TraceRead);
        nr::test::require(asTraceRead.stages == vk::PipelineStageFlagBits2::eRayTracingShaderKHR);
        nr::test::require(asTraceRead.access == vk::AccessFlagBits2::eAccelerationStructureReadKHR);

        auto asCopyWrite = nr::renderer::RenderGraphCompiler::mapAccelerationStructureAccessIntent(
            nr::renderer::AccelerationStructureAccessIntent::CopyWrite);
        nr::test::require(asCopyWrite.stages == vk::PipelineStageFlagBits2::eAccelerationStructureCopyKHR);
        nr::test::require(asCopyWrite.access == vk::AccessFlagBits2::eAccelerationStructureWriteKHR);
    }};

const nr::test::CaseRegistrar compilerCrossQueueCase{
    "render graph compiler emits explicit cross-queue ownership transition",
    [] {
        auto frame = buildCrossQueueFrame();
        nr::test::require(nr::renderer::RenderGraphCompiler::hasExplicitSubmitBoundariesForQueueTransitions(frame),
                          "frame should include explicit submit boundary");

        auto compiled = nr::renderer::RenderGraphCompiler{}.compile(frame);
        nr::test::requireEqual(compiled.resources.size(), std::size_t{2});
        nr::test::requireEqual(compiled.submitBatches.size(), std::size_t{2});
        nr::test::requireEqual(compiled.submitBatches[0].queue, nr::renderer::QueueDomain::Graphics);
        nr::test::requireEqual(compiled.submitBatches[1].queue, nr::renderer::QueueDomain::Compute);
        nr::test::require(compiled.submitBatches[1].openedBySubmitNode.has_value(), "compute batch should be opened by submit boundary");
        nr::test::requireEqual(compiled.submitBatches[1].openedBySubmitNodeDebugName, std::string{"GeometryToResolve"});

        auto const &computePass = compiled.submitBatches[1].passes.front();
        nr::test::requireEqual(computePass.preBarriers.size(), std::size_t{2});
        auto crossQueue = std::ranges::find_if(computePass.preBarriers, [](const nr::renderer::ResourceStateTransition &transition) {
            return transition.strength == nr::renderer::DependencyStrength::ReleaseAcquireRequired;
        });
        nr::test::require(crossQueue != computePass.preBarriers.end(), "compute pass should include a cross-queue transition");
        nr::test::requireEqual(crossQueue->srcQueue, nr::renderer::QueueDomain::Graphics);
        nr::test::requireEqual(crossQueue->dstQueue, nr::renderer::QueueDomain::Compute);
        nr::test::requireEqual(crossQueue->oldLayout, nr::renderer::ImageLayoutIntent::ColorAttachment);
        nr::test::requireEqual(crossQueue->newLayout, nr::renderer::ImageLayoutIntent::ShaderReadOnly);
        nr::test::requireEqual(compiled.ownershipTransitions.size(), std::size_t{1});
        nr::test::require(compiled.debugView.find("ownershipTransition") != std::string::npos,
                          "debug view should include ownership transition diagnostics");
        nr::test::require(compiled.debugView.find("GeometryToResolve") != std::string::npos,
                          "debug view should include submit boundary names");
    }};

const nr::test::CaseRegistrar compilerPassOrderCase{
    "render graph compiler preserves compiled pass order for executor merge",
    [] {
        auto frame = buildMultiPassGraphicsFrame();
        auto compiled = nr::renderer::RenderGraphCompiler{}.compile(frame);

        nr::test::requireEqual(compiled.submitBatches.size(), std::size_t{1});
        nr::test::requireEqual(compiled.submitBatches.front().passes.size(), std::size_t{3});
        nr::test::requireEqual(compiled.submitBatches.front().passes[0].debugName, std::string{"Graphics.First"});
        nr::test::requireEqual(compiled.submitBatches.front().passes[1].debugName, std::string{"Graphics.Second"});
        nr::test::requireEqual(compiled.submitBatches.front().passes[2].debugName, std::string{"Graphics.Third"});
    }};

const nr::test::CaseRegistrar compilerAccelerationStructureCase{
    "render graph compiler tracks acceleration structure resources",
    [] {
        auto frame = buildAccelerationStructureFrame();
        auto compiled = nr::renderer::RenderGraphCompiler{}.compile(frame);

        nr::test::requireEqual(compiled.resources.size(), std::size_t{1});
        nr::test::require(compiled.resources.front().isAccelerationStructure, "TLAS should compile as an AS resource");
        nr::test::requireEqual(compiled.resources.front().resolvedAccelerationStructureSize, vk::DeviceSize{8192});
        nr::test::requireEqual(compiled.submitBatches.size(), std::size_t{2});

        auto const& tracePass = compiled.submitBatches[1].passes.front();
        nr::test::requireEqual(tracePass.preBarriers.size(), std::size_t{1});
        auto const& barrier = tracePass.preBarriers.front();
        nr::test::requireEqual(barrier.strength, nr::renderer::DependencyStrength::BarrierRequired);
        nr::test::require(barrier.srcScope.stages == vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR);
        nr::test::require(barrier.srcScope.access == vk::AccessFlagBits2::eAccelerationStructureWriteKHR);
        nr::test::require(barrier.dstScope.stages == vk::PipelineStageFlagBits2::eRayTracingShaderKHR);
        nr::test::require(barrier.dstScope.access == vk::AccessFlagBits2::eAccelerationStructureReadKHR);
        nr::test::require(
            compiled.debugView.find("type=AccelerationStructure") != std::string::npos,
            "debug view should identify AS resources");
    }};

const nr::test::CaseRegistrar compilerPrepareRecordSplitCase{
    "render graph compiler keeps prepare and record callbacks separate",
    [] {
        auto builder = nr::renderer::RenderGraphBuilder{};
        auto node = builder.addNode("Bindings", nr::renderer::QueueDomain::Graphics);
        auto color = builder.addResource(nr::renderer::GraphTransientImageDesc{
            .debugName = "Bindings.Color",
            .extent = vk::Extent3D{32, 32, 1},
            .format = vk::Format::eR8G8B8A8Unorm,
        });

        auto uses = std::array{nr::renderer::use::colorWrite(color)};
        auto pass = builder.addPass(
            "Bindings.Split",
            node,
            uses,
            [](const nr::renderer::PassRecordContext&) {},
            [](const nr::renderer::PassPrepareContext&) {});
        nr::test::require(pass.valid(), "split binding pass should be valid");

        auto frame = builder.build();
        nr::test::require(static_cast<bool>(frame.passes.front().prepare), "builder should retain prepare callback");
        nr::test::require(static_cast<bool>(frame.passes.front().record), "builder should retain record callback");

        auto compiled = nr::renderer::RenderGraphCompiler{}.compile(frame);
        auto const& compiledPass = compiled.submitBatches.front().passes.front();
        nr::test::require(static_cast<bool>(compiledPass.prepare), "compiler should retain prepare callback");
        nr::test::require(static_cast<bool>(compiledPass.record), "compiler should retain record callback");
        nr::test::requireEqual(compiledPass.debugName, std::string{"Bindings.Split"});
    }};

const nr::test::CaseRegistrar compilerFrameDataCase{
    "render graph compiler carries typed frame data handles",
    [] {
        auto builder = nr::renderer::RenderGraphBuilder{};
        auto node = builder.addNode("FrameData", nr::renderer::QueueDomain::Graphics);
        auto frameData = builder.addFrameData("SceneBridgeFrame", std::uint32_t{42});
        auto color = builder.addResource(nr::renderer::GraphTransientImageDesc{
            .debugName = "FrameData.Color",
            .extent = vk::Extent3D{16, 16, 1},
            .format = vk::Format::eR8G8B8A8Unorm,
        });

        auto uses = std::array{nr::renderer::use::colorWrite(color)};
        static_cast<void>(builder.addPass(
            "FrameData.Pass",
            node,
            uses,
            [](const nr::renderer::PassRecordContext&) {}));

        auto compiled = nr::renderer::RenderGraphCompiler{}.compileConsuming(builder.mutableFrame());
        nr::test::requireEqual(compiled.frameData.size(), std::size_t{1});
        nr::test::requireEqual(compiled.frameData.front().handle, frameData);

        auto recordContext = nr::renderer::PassRecordContext{
            .resolveFrameDataPayload = [&](nr::renderer::GraphFrameDataHandle handle)
                -> std::optional<std::reference_wrapper<const std::any>> {
                auto frameDataIt = std::ranges::find_if(
                    compiled.frameData,
                    [handle](const nr::renderer::GraphFrameDataDesc& desc) {
                        return desc.handle == handle;
                    });
                if (frameDataIt == compiled.frameData.end())
                {
                    return {};
                }

                return std::cref(frameDataIt->payload);
            },
        };

        nr::test::requireEqual(recordContext.frameData<std::uint32_t>(frameData), std::uint32_t{42});
    }};
} // namespace
