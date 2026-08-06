import std;
import dependency.vulkan;
import nr.rhi;
import nr.renderer;
import nr.utils;

namespace
{
[[nodiscard]] nr::renderer::RenderGraphBuilder makeBuilderWithStorageBuffer(nr::renderer::GraphNodeHandle &node,
                                                                            nr::renderer::GraphResourceHandle &buffer)
{
    auto builder = nr::renderer::RenderGraphBuilder{};
    node = builder.addNode("FailureProbe", nr::renderer::QueueDomain::Compute);
    buffer = builder.addResource(nr::renderer::GraphImportedBufferDesc{
        .debugName = "FailureProbe.Buffer",
        .lifetime = nr::renderer::ResourceLifetime::FrameLocal,
        .initialOwnership = nr::renderer::ResourceOwnershipDomain::Compute,
        .size = 256,
        .usageIntents = {nr::renderer::BufferUsageIntent::StorageReadWrite},
    });
    return builder;
}
} // namespace

int main(int argc, char **argv)
{
    nr::nrAssert(argc == 2, "nr_render_graph_failure_probe requires one scenario argument.");
    auto const scenario = std::string_view{argv[1]};
    auto node = nr::renderer::GraphNodeHandle{};
    auto buffer = nr::renderer::GraphResourceHandle{};
    auto builder = makeBuilderWithStorageBuffer(node, buffer);

    if (scenario == "conflicting-uses")
    {
        auto uses = std::array{
            nr::renderer::use::storageBufferRead(buffer),
            nr::renderer::use::storageBufferWrite(buffer),
        };
        static_cast<void>(
            builder.addPass("FailureProbe.Conflict", node, uses, [](const nr::renderer::PassRecordContext &) {}));
        return 0;
    }

    if (scenario == "copy-self")
    {
        static_cast<void>(builder.addCopyPass("FailureProbe.CopySelf", node,
                                              nr::renderer::CopyBufferToBufferPassDesc{
                                                  .source = buffer,
                                                  .destination = buffer,
                                              }));
        return 0;
    }

    if (scenario == "compiler-same-pass")
    {
        auto uses = std::array{nr::renderer::use::storageBufferRead(buffer)};
        static_cast<void>(
            builder.addPass("FailureProbe.Compiler", node, uses, [](const nr::renderer::PassRecordContext &) {}));
        auto frame = builder.build();
        frame.passes.front().resourceUses.push_back(nr::renderer::use::storageBufferWrite(buffer));
        static_cast<void>(nr::renderer::RenderGraphCompiler{}.compile(frame));
        return 0;
    }

    if (scenario == "prepare-undeclared-resource" || scenario == "prepare-undeclared-frame-data" ||
        scenario == "prepare-logical-undeclared")
    {
        auto const undeclaredBuffer = builder.addResource(nr::renderer::GraphImportedBufferDesc{
            .debugName = "FailureProbe.UndeclaredBuffer",
            .lifetime = nr::renderer::ResourceLifetime::FrameLocal,
            .initialOwnership = nr::renderer::ResourceOwnershipDomain::Compute,
            .size = 256,
            .usageIntents = {nr::renderer::BufferUsageIntent::StorageReadWrite},
        });
        auto const declaredFrameData = builder.addFrameData("FailureProbe.DeclaredFrameData", std::uint32_t{1u});
        auto const undeclaredFrameData = builder.addFrameData("FailureProbe.UndeclaredFrameData", std::uint32_t{2u});
        auto const resourceUses = std::array{nr::renderer::use::storageBufferRead(buffer)};
        auto const frameDataUses = std::array{declaredFrameData};
        auto prepare = nr::renderer::PassPrepareCallback{};
        if (scenario == "prepare-undeclared-resource")
        {
            prepare = [undeclaredBuffer](const nr::renderer::PassPrepareContext &context) {
                static_cast<void>(context.resolveBuffer(undeclaredBuffer));
            };
        }
        else if (scenario == "prepare-undeclared-frame-data")
        {
            prepare = [undeclaredFrameData](const nr::renderer::PassPrepareContext &context) {
                static_cast<void>(context.frameData<std::uint32_t>(undeclaredFrameData));
            };
        }
        else
        {
            prepare = [undeclaredBuffer](const nr::renderer::PassPrepareContext &context) {
                auto resolver = nr::renderer::makeDefaultLogicalDescriptorResolver(context);
                static_cast<void>(resolver(
                    nr::rhi::LogicalResourceDescriptorWrite{
                        .logicalResourceId = undeclaredBuffer.value,
                        .debugName = "FailureProbe.UndeclaredBuffer",
                    },
                    nr::rhi::DescriptorBindingInfo{
                        .descriptorType = vk::DescriptorType::eUniformBuffer,
                        .debugPath = "gUndeclared",
                    },
                    0u));
            };
        }

        static_cast<void>(builder.addPass(
            "FailureProbe.PrepareCapability", node, resourceUses, [](const nr::renderer::PassRecordContext &) {},
            std::move(prepare), false, vk::PipelineStageFlags2{}, frameDataUses));
        auto compiled = nr::renderer::RenderGraphCompiler{}.compile(builder.build());
        auto device = nr::rhi::Device{};
        static_cast<void>(nr::renderer::RenderGraphExecutor{}.prepareFrame(
            std::move(compiled), nr::renderer::RenderGraphExecutor::ExecuteContext{
                                     .device = device,
                                 }));
        return 0;
    }

    nr::nrAssert(false, "nr_render_graph_failure_probe received an unknown scenario.");
}
