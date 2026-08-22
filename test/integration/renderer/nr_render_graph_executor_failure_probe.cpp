import std;
import dependency.vulkan;
import nr.rhi;
import nr.renderer;
import nr.utils;

int main(int argc, char **argv)
{
    nr::nrAssert(argc == 2, "nr_render_graph_executor_failure_probe requires one scenario argument.");
    nr::nrAssert(std::string_view{argv[1]} == "record-undeclared-resource",
                 "nr_render_graph_executor_failure_probe received an unknown scenario.");

    auto device = nr::rhi::Device::create("nr_render_graph_executor_failure_probe", "NewbieRenderer");
    auto buffer = device.resourceFactory.createBuffer(
        nr::rhi::makeBufferCreateInfo(256u, vk::BufferUsageFlagBits::eStorageBuffer), nr::rhi::MemoryUsage::GpuOnly,
        "ExecutorFailureProbe.Buffer");

    auto builder = nr::renderer::RenderGraphBuilder{};
    auto const node = builder.addNode("ExecutorFailureProbe", nr::renderer::QueueDomain::Compute);
    auto importBuffer = [&](std::string_view debugName) {
        return builder.addResource(nr::renderer::GraphImportedBufferDesc{
            .debugName = std::string{debugName},
            .lifetime = nr::renderer::ResourceLifetime::FrameLocal,
            .initialOwnership = nr::renderer::ResourceOwnershipDomain::Compute,
            .size = buffer.size(),
            .usageIntents = {nr::renderer::BufferUsageIntent::StorageRead},
            .importedResource = std::cref(buffer),
        });
    };
    auto const declaredBuffer = importBuffer("ExecutorFailureProbe.Declared");
    auto const undeclaredBuffer = importBuffer("ExecutorFailureProbe.Undeclared");
    auto const resourceUses = std::array{nr::renderer::use::storageBufferRead(declaredBuffer)};
    static_cast<void>(builder.addPass("ExecutorFailureProbe.Record", node, resourceUses,
                                      [undeclaredBuffer](const nr::renderer::PassRecordContext &context) {
                                          static_cast<void>(context.resolveBuffer(undeclaredBuffer));
                                      }));

    auto compiled = nr::renderer::RenderGraphCompiler{}.compile(builder.build());
    auto const begin = device.beginFrame();
    auto executor = nr::renderer::RenderGraphExecutor{};
    auto const executeContext = nr::renderer::RenderGraphExecutor::ExecuteContext{
        .device = device,
        .frameIndex = begin.frameIndex,
    };
    auto prepared = executor.prepareFrame(std::move(compiled), executeContext);
    static_cast<void>(executor.executePrepared(prepared, executeContext));
    device.waitIdle();
    return 0;
}
