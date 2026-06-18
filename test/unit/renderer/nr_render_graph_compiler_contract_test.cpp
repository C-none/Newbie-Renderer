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
        nr::test::require(computeSample.stages == vk::PipelineStageFlagBits2::eComputeShader);
        nr::test::require(computeSample.access == vk::AccessFlagBits2::eShaderSampledRead);
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
    }};
} // namespace
