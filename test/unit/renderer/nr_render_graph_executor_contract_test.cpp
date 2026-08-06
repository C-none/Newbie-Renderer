import std;
import dependency.vulkan;
import nr.rhi;
import nr.renderer;
import nr.test;

namespace
{
const nr::test::CaseRegistrar prepareCapabilityCase{
    "render graph prepare resolvers enforce declared capabilities", [] {
        auto builder = nr::renderer::RenderGraphBuilder{};
        auto const node = builder.addNode("ExecutorCapability.Prepare", nr::renderer::QueueDomain::Compute);
        auto const constants = builder.addResource(nr::renderer::GraphImportedBufferDesc{
            .debugName = "ExecutorCapability.Constants",
            .lifetime = nr::renderer::ResourceLifetime::FrameLocal,
            .initialOwnership = nr::renderer::ResourceOwnershipDomain::Compute,
            .size = 256u,
            .usageIntents = {nr::renderer::BufferUsageIntent::Uniform},
        });
        auto const frameData = builder.addFrameData("ExecutorCapability.FrameData", std::uint32_t{42u});
        auto const resourceUses = std::array{nr::renderer::use::uniformRead(constants)};
        auto const frameDataUses = std::array{frameData};
        auto prepareInvoked = false;

        static_cast<void>(builder.addPass(
            "ExecutorCapability.PreparePass", node, resourceUses, [](const nr::renderer::PassRecordContext &) {},
            [constants, frameData, &prepareInvoked](const nr::renderer::PassPrepareContext &context) {
                auto const buffer = context.resolveBuffer(constants);
                nr::test::require(buffer.has_value(), "declared prepare buffer capability should resolve");
                nr::test::requireEqual(buffer->size, vk::DeviceSize{256u});
                nr::test::require(!context.resolveImage(constants).has_value(),
                                  "declared resource resolved as the wrong type should remain empty");
                nr::test::requireEqual(context.frameData<std::uint32_t>(frameData), std::uint32_t{42u});

                auto logicalResolver = nr::renderer::makeDefaultLogicalDescriptorResolver(context);
                auto descriptor = logicalResolver(
                    nr::rhi::LogicalResourceDescriptorWrite{
                        .logicalResourceId = constants.value,
                        .debugName = "ExecutorCapability.Constants",
                    },
                    nr::rhi::DescriptorBindingInfo{
                        .descriptorType = vk::DescriptorType::eUniformBuffer,
                        .debugPath = "gConstants",
                    },
                    0u);
                nr::test::require(descriptor.has_value(),
                                  "default logical resolver should resolve a declared prepare capability");
                nr::test::require(std::holds_alternative<nr::rhi::BufferDescriptorWrite>(*descriptor));
                nr::test::requireEqual(std::get<nr::rhi::BufferDescriptorWrite>(*descriptor).range,
                                       vk::DeviceSize{256u});
                prepareInvoked = true;
            },
            false, vk::PipelineStageFlags2{}, frameDataUses));

        auto compiled = nr::renderer::RenderGraphCompiler{}.compile(builder.build());
        auto device = nr::rhi::Device{};
        auto prepared = nr::renderer::RenderGraphExecutor{}.prepareFrame(
            std::move(compiled), nr::renderer::RenderGraphExecutor::ExecuteContext{
                                     .device = device,
                                 });
        nr::test::require(prepareInvoked, "prepare callback should run through the executor");
        nr::test::requireEqual(prepared.invokedPassPrepareCount, std::size_t{1u});
    }};
} // namespace
