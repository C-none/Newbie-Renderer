import std;
import dependency;
import nr.rhi;
import nr.renderer;
import nr.test;

namespace
{
[[nodiscard]] bool hasImageFields(const nr::renderer::PassResourceUseDesc &use) noexcept
{
    return use.imageUsage.has_value() &&
           use.imageAccess.has_value() &&
           use.imageLayout.has_value() &&
           use.imageAspect.has_value();
}

const nr::test::CaseRegistrar useFactoryCase{
    "render graph resource-use factories encode stable intents",
    [] {
        auto handle = nr::renderer::GraphResourceHandle{3u};

        auto color = nr::renderer::use::colorWrite(handle);
        nr::test::require(hasImageFields(color), "color write should fill image fields");
        nr::test::require(color.imageUsage == nr::renderer::ImageUsageIntent::ColorAttachment);
        nr::test::require(color.imageAccess == nr::renderer::ImageAccessIntent::ColorAttachmentWrite);
        nr::test::require(!color.readOnly, "color write should not be read-only");

        auto sampled = nr::renderer::use::sampledRead(handle);
        nr::test::require(sampled.imageUsage == nr::renderer::ImageUsageIntent::Sampled);
        nr::test::require(sampled.imageAccess == nr::renderer::ImageAccessIntent::SampledRead);
        nr::test::require(sampled.imageLayout == nr::renderer::ImageLayoutIntent::ShaderReadOnly);
        nr::test::require(sampled.readOnly, "sampled read should be read-only");

        auto present = nr::renderer::use::presentRead(handle);
        nr::test::require(present.imageUsage == nr::renderer::ImageUsageIntent::PresentSource);
        nr::test::require(present.ownershipDomain == nr::renderer::ResourceOwnershipDomain::Compute);
        nr::test::require(present.readOnly, "present read should be read-only");

        auto uniform = nr::renderer::use::uniformRead(handle);
        nr::test::require(uniform.bufferUsage == nr::renderer::BufferUsageIntent::Uniform);
        nr::test::require(uniform.bufferAccess == nr::renderer::BufferAccessIntent::UniformRead);
        nr::test::require(uniform.readOnly, "uniform read should be read-only");
    }};

const nr::test::CaseRegistrar builderFrameCase{
    "render graph builder records resources passes submits and execution order",
    [] {
        auto builder = nr::renderer::RenderGraphBuilder{};
        auto graphicsNode = builder.addNode("Geometry", nr::renderer::QueueDomain::Graphics);
        auto computeNode = builder.addNode("Compute", nr::renderer::QueueDomain::Compute);

        auto color = builder.addResource(nr::renderer::GraphTransientImageDesc{
            .debugName = "Color",
            .extent = vk::Extent3D{128, 64, 1},
            .format = vk::Format::eR16G16B16A16Sfloat,
            .usageIntents = {nr::renderer::ImageUsageIntent::ColorAttachment},
        });
        auto constants = builder.addResource(nr::renderer::GraphTransientBufferDesc{
            .debugName = "Constants",
            .size = 256,
            .usageIntents = {nr::renderer::BufferUsageIntent::Uniform},
            .memoryUsage = nr::rhi::MemoryUsage::CpuToGpu,
        });

        auto graphicsUses = std::array{
            nr::renderer::use::colorWrite(color),
            nr::renderer::use::uniformRead(constants),
        };
        auto graphicsPass = builder.addPass(
            "Geometry.Main",
            graphicsNode,
            graphicsUses,
            [](const nr::renderer::PassRecordContext &) {});

        auto submit = builder.addSubmitNode("GraphicsToCompute");

        auto computeUses = std::array{nr::renderer::use::sampledRead(color)};
        auto computePass = builder.addPass(
            "Compute.Sample",
            computeNode,
            computeUses,
            [](const nr::renderer::PassRecordContext &) {});

        auto frame = builder.build();
        nr::test::requireEqual(frame.resources.size(), std::size_t{2});
        nr::test::requireEqual(frame.nodes.size(), std::size_t{2});
        nr::test::requireEqual(frame.passes.size(), std::size_t{2});
        nr::test::requireEqual(frame.submitBoundaries.size(), std::size_t{1});
        nr::test::requireEqual(frame.passes[0].handle, graphicsPass);
        nr::test::requireEqual(frame.passes[1].handle, computePass);
        nr::test::require(std::holds_alternative<nr::renderer::GraphPassHandle>(frame.executionOrder[0]));
        nr::test::require(std::get<nr::renderer::GraphPassHandle>(frame.executionOrder[0]) == graphicsPass);
        nr::test::require(std::holds_alternative<nr::renderer::GraphSubmitHandle>(frame.executionOrder[1]));
        nr::test::require(std::get<nr::renderer::GraphSubmitHandle>(frame.executionOrder[1]) == submit);
        nr::test::require(std::holds_alternative<nr::renderer::GraphPassHandle>(frame.executionOrder[2]));

        builder.clear();
        nr::test::require(builder.frame().resources.empty(), "clear should remove resources");
        nr::test::require(builder.frame().executionOrder.empty(), "clear should remove execution order");
    }};
} // namespace
