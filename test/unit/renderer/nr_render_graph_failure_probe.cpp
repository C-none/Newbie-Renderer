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

    nr::nrAssert(false, "nr_render_graph_failure_probe received an unknown scenario.");
}
