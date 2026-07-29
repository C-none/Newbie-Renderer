import std;
import dependency.vulkan;
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

[[nodiscard]] bool hasBufferFields(const nr::renderer::PassResourceUseDesc &use) noexcept
{
    return use.bufferUsage.has_value() &&
           use.bufferAccess.has_value();
}

[[nodiscard]] bool hasAccelerationStructureFields(const nr::renderer::PassResourceUseDesc &use) noexcept
{
    return use.accelerationStructureUsage.has_value() &&
           use.accelerationStructureAccess.has_value();
}

void requireContiguousCoverage(
    const nr::renderer::PassParallelRecordPlan& plan,
    std::size_t itemCount)
{
    auto expectedBegin = std::size_t{0};
    std::ranges::for_each(plan.ranges, [&](const nr::renderer::ParallelRecordRange& range) {
        nr::test::requireEqual(range.begin, expectedBegin, "parallel ranges should be contiguous");
        nr::test::require(range.end >= range.begin, "parallel ranges should be non-inverted");
        expectedBegin = range.end;
    });
    nr::test::requireEqual(expectedBegin, itemCount, "parallel ranges should cover the item domain exactly");
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

        auto orderedColor = nr::renderer::use::orderedAfterPrevious(color);
        nr::test::require(orderedColor.requiresPreviousUseBarrier, "ordered use should request a previous-use barrier");
        nr::test::requireEqual(orderedColor.resource, color.resource);
        nr::test::require(orderedColor.imageLayout == color.imageLayout);

        auto colorReadWrite = nr::renderer::use::colorReadWrite(handle);
        nr::test::require(colorReadWrite.imageUsage == nr::renderer::ImageUsageIntent::ColorAttachment);
        nr::test::require(colorReadWrite.imageAccess == nr::renderer::ImageAccessIntent::ColorAttachmentReadWrite);
        nr::test::require(!colorReadWrite.readOnly, "color read-write should not be read-only");

        auto depthReadWrite = nr::renderer::use::depthReadWrite(handle);
        nr::test::require(depthReadWrite.imageUsage == nr::renderer::ImageUsageIntent::DepthStencilAttachment);
        nr::test::require(depthReadWrite.imageAccess == nr::renderer::ImageAccessIntent::DepthStencilReadWrite);
        nr::test::require(depthReadWrite.imageAspect == nr::renderer::ImageAspectIntent::Depth);

        auto sampled = nr::renderer::use::sampledRead(handle);
        nr::test::require(sampled.imageUsage == nr::renderer::ImageUsageIntent::Sampled);
        nr::test::require(sampled.imageAccess == nr::renderer::ImageAccessIntent::SampledRead);
        nr::test::require(sampled.imageLayout == nr::renderer::ImageLayoutIntent::ShaderReadOnly);
        nr::test::require(sampled.readOnly, "sampled read should be read-only");

        auto fragmentSampled = nr::renderer::use::withShaderStages(
            sampled,
            nr::renderer::ShaderStageIntent::Fragment);
        nr::test::require(fragmentSampled.shaderStages == vk::PipelineStageFlagBits2::eFragmentShader);
        nr::test::require(fragmentSampled.imageAccess == sampled.imageAccess);

        auto graphicsStageIntents = std::array{
            nr::renderer::ShaderStageIntent::Vertex,
            nr::renderer::ShaderStageIntent::Fragment,
        };
        auto graphicsShaderStages = nr::renderer::use::shaderStageScope(graphicsStageIntents);
        nr::test::require(graphicsShaderStages ==
                          (vk::PipelineStageFlagBits2::eVertexShader |
                           vk::PipelineStageFlagBits2::eFragmentShader));

        auto sampledDepth = nr::renderer::use::make<nr::renderer::use::spec::SampledRead>(
            handle,
            nr::renderer::use::ImageUseOptions{
                .aspect = nr::renderer::ImageAspectIntent::Depth,
            });
        nr::test::require(sampledDepth.imageAspect == nr::renderer::ImageAspectIntent::Depth);
        nr::test::require(sampledDepth.readOnly, "customized sampled read should preserve read-only intent");

        auto storageReadWrite = nr::renderer::use::storageReadWrite(handle);
        nr::test::require(storageReadWrite.imageUsage == nr::renderer::ImageUsageIntent::StorageReadWrite);
        nr::test::require(storageReadWrite.imageAccess == nr::renderer::ImageAccessIntent::StorageReadWrite);
        nr::test::require(!storageReadWrite.readOnly, "storage read-write should not be read-only");

        auto present = nr::renderer::use::presentRead(handle);
        nr::test::require(present.imageUsage == nr::renderer::ImageUsageIntent::PresentSource);
        nr::test::require(present.ownershipDomain == nr::renderer::ResourceOwnershipDomain::Undefined);
        nr::test::require(present.readOnly, "present read should be read-only");

        auto computePresent = nr::renderer::use::presentRead(handle, nr::renderer::ResourceOwnershipDomain::Compute);
        nr::test::require(computePresent.ownershipDomain == nr::renderer::ResourceOwnershipDomain::Compute);

        auto uniform = nr::renderer::use::uniformRead(handle);
        nr::test::require(hasBufferFields(uniform), "uniform read should fill buffer fields");
        nr::test::require(uniform.bufferUsage == nr::renderer::BufferUsageIntent::Uniform);
        nr::test::require(uniform.bufferAccess == nr::renderer::BufferAccessIntent::UniformRead);
        nr::test::require(uniform.readOnly, "uniform read should be read-only");

        auto bufferUpload = nr::renderer::use::bufferTransferSrc(handle);
        nr::test::require(bufferUpload.bufferUsage == nr::renderer::BufferUsageIntent::TransferSrc);
        nr::test::require(bufferUpload.bufferAccess == nr::renderer::BufferAccessIntent::TransferRead);
        nr::test::require(bufferUpload.readOnly, "buffer transfer source should be read-only");

        auto shaderBindingTable = nr::renderer::use::shaderBindingTableRead(handle);
        nr::test::require(shaderBindingTable.bufferUsage == nr::renderer::BufferUsageIntent::ShaderBindingTable);
        nr::test::require(shaderBindingTable.bufferAccess == nr::renderer::BufferAccessIntent::ShaderBindingTableRead);
        nr::test::require(shaderBindingTable.readOnly, "shader binding table read should be read-only");

        auto accelerationStructureTrace = nr::renderer::use::accelerationStructureTraceRead(handle);
        nr::test::require(hasAccelerationStructureFields(accelerationStructureTrace), "AS trace read should fill acceleration-structure fields");
        nr::test::require(accelerationStructureTrace.accelerationStructureUsage == nr::renderer::AccelerationStructureUsageIntent::TraceInput);
        nr::test::require(accelerationStructureTrace.accelerationStructureAccess == nr::renderer::AccelerationStructureAccessIntent::TraceRead);
        nr::test::require(accelerationStructureTrace.readOnly, "AS trace read should be read-only");

        auto accelerationStructureStorage = nr::renderer::use::accelerationStructureStorageWrite(handle);
        nr::test::require(hasBufferFields(accelerationStructureStorage), "AS storage write should remain a buffer use");
        nr::test::require(accelerationStructureStorage.bufferUsage == nr::renderer::BufferUsageIntent::AccelerationStructureStorage);
        nr::test::require(accelerationStructureStorage.bufferAccess == nr::renderer::BufferAccessIntent::AccelerationStructureWrite);
    }};

const nr::test::CaseRegistrar parallelPlannerCase{
    "render graph parallel planner splits contiguous unordered ranges",
    [] {
        auto zero = nr::renderer::ParallelRecordPlanner::planContiguousRanges(0, 4);
        nr::test::requireEqual(zero.itemCount, std::size_t{0});
        nr::test::requireEqual(zero.assignedThreadCount, std::uint32_t{0});
        nr::test::require(zero.ranges.empty(), "zero-item parallel plan should not create ranges");

        auto one = nr::renderer::ParallelRecordPlanner::planContiguousRanges(1, 4);
        nr::test::requireEqual(one.assignedThreadCount, std::uint32_t{1});
        requireContiguousCoverage(one, 1);

        auto sixtyThree = nr::renderer::ParallelRecordPlanner::planContiguousRanges(63, 4);
        nr::test::requireEqual(sixtyThree.assignedThreadCount, std::uint32_t{4});
        requireContiguousCoverage(sixtyThree, 63);

        auto sixtyFour = nr::renderer::ParallelRecordPlanner::planContiguousRanges(64, 4);
        nr::test::requireEqual(sixtyFour.assignedThreadCount, std::uint32_t{4});
        requireContiguousCoverage(sixtyFour, 64);

        auto sixtyFive = nr::renderer::ParallelRecordPlanner::planContiguousRanges(65, 4);
        nr::test::requireEqual(sixtyFive.assignedThreadCount, std::uint32_t{4});
        requireContiguousCoverage(sixtyFive, 65);

        auto twenty = nr::renderer::ParallelRecordPlanner::planContiguousRanges(20, 10);
        nr::test::requireEqual(twenty.assignedThreadCount, std::uint32_t{10});
        nr::test::require(std::ranges::all_of(twenty.ranges, [](const nr::renderer::ParallelRecordRange& range) {
            return range.size() == 2u;
        }));
        requireContiguousCoverage(twenty, 20);

        auto large = nr::renderer::ParallelRecordPlanner::planContiguousRanges(1000, 4);
        nr::test::requireEqual(large.assignedThreadCount, std::uint32_t{4});
        requireContiguousCoverage(large, 1000);
    }};

const nr::test::CaseRegistrar accelerationStructureResourceCase{
    "render graph builder accepts acceleration structure resources",
    [] {
        auto builder = nr::renderer::RenderGraphBuilder{};
        auto node = builder.addNode("RayTracing", nr::renderer::QueueDomain::Compute);

        auto tlas = builder.addResource(nr::renderer::GraphImportedAccelerationStructureDesc{
            .debugName = "Scene.TLAS",
            .type = vk::AccelerationStructureTypeKHR::eTopLevel,
            .size = 4096,
        });

        auto uses = std::array{nr::renderer::use::accelerationStructureTraceRead(tlas)};
        auto pass = builder.addPass(
            "RayTracing.Trace",
            node,
            uses,
            [](const nr::renderer::PassRecordContext &) {});

        auto frame = builder.build();
        nr::test::require(pass.valid(), "AS pass should be valid");
        nr::test::requireEqual(frame.resources.size(), std::size_t{1});
        nr::test::require(std::holds_alternative<nr::renderer::GraphImportedAccelerationStructureDesc>(frame.resources.front().desc));
        nr::test::require(frame.passes.front().resourceUses.front().accelerationStructureAccess ==
                          nr::renderer::AccelerationStructureAccessIntent::TraceRead);
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
        auto frameData = builder.addFrameData("SceneBridgeFrame", std::string{"frame payload"});

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
        nr::test::requireEqual(frame.frameData.size(), std::size_t{1});
        nr::test::requireEqual(frame.nodes.size(), std::size_t{2});
        nr::test::requireEqual(frame.passes.size(), std::size_t{2});
        nr::test::requireEqual(frame.submitBoundaries.size(), std::size_t{1});
        nr::test::requireEqual(frame.frameData.front().handle, frameData);
        auto const framePayload = std::any_cast<std::string>(&frame.frameData.front().payload);
        nr::test::require(framePayload != nullptr, "frame data should keep the requested payload type");
        nr::test::requireEqual(*framePayload, std::string{"frame payload"});
        nr::test::requireEqual(frame.passes[0].handle, graphicsPass);
        nr::test::requireEqual(frame.passes[1].handle, computePass);
        nr::test::require(std::holds_alternative<nr::renderer::GraphPassHandle>(frame.executionOrder[0]));
        nr::test::require(std::get<nr::renderer::GraphPassHandle>(frame.executionOrder[0]) == graphicsPass);
        nr::test::require(std::holds_alternative<nr::renderer::GraphSubmitHandle>(frame.executionOrder[1]));
        nr::test::require(std::get<nr::renderer::GraphSubmitHandle>(frame.executionOrder[1]) == submit);
        nr::test::require(std::holds_alternative<nr::renderer::GraphPassHandle>(frame.executionOrder[2]));

        builder.clear();
        nr::test::require(builder.frame().resources.empty(), "clear should remove resources");
        nr::test::require(builder.frame().frameData.empty(), "clear should remove frame data");
        nr::test::require(builder.frame().executionOrder.empty(), "clear should remove execution order");
    }};

const nr::test::CaseRegistrar builderParallelPassCase{
    "render graph builder accepts unordered parallel record passes",
    [] {
        auto builder = nr::renderer::RenderGraphBuilder{};
        auto node = builder.addNode("Parallel", nr::renderer::QueueDomain::Graphics);
        auto color = builder.addResource(nr::renderer::GraphTransientImageDesc{
            .debugName = "Parallel.Color",
            .extent = vk::Extent3D{64, 64, 1},
            .format = vk::Format::eR8G8B8A8Unorm,
        });

        auto uses = std::array{nr::renderer::use::colorWrite(color)};
        auto pass = builder.addPass(
            "Parallel.Draws",
            node,
            uses,
            nr::renderer::PassParallelRecordDesc{
                .itemCount = [](const nr::renderer::PassRecordContext&) {
                    return std::size_t{128};
                },
                .recordRange = [](const nr::renderer::PassRangeRecordContext&) {},
            },
            [](const nr::renderer::PassPrepareContext&) {});
        nr::test::require(pass.valid(), "parallel pass should be valid");

        auto frame = builder.build();
        nr::test::requireEqual(frame.passes.size(), std::size_t{1});
        nr::test::require(!frame.passes.front().record, "parallel pass should not retain a serial record callback");
        nr::test::require(frame.passes.front().parallelRecord.has_value(), "parallel pass should retain a parallel record desc");
        nr::test::require(static_cast<bool>(frame.passes.front().prepare), "parallel pass should retain prepare callback");
    }};

const nr::test::CaseRegistrar transferOpsClearCase{
    "renderer transfer clear helpers encode stable pass uses",
    [] {
        auto builder = nr::renderer::RenderGraphBuilder{};
        auto node = builder.addNode("TransferOps", nr::renderer::QueueDomain::Compute);
        auto bindlessCache = nr::renderer::BindlessImageTableCache{};
        auto globals = nr::renderer::FrameGlobalResources{
            .bindlessImageTableCache = std::ref(bindlessCache),
        };
        auto frameResources = std::map<std::string, nr::renderer::GraphResourceHandle>{};
        auto frameDataResources = std::map<std::string, nr::renderer::GraphFrameDataHandle>{};
        auto context = nr::renderer::NodeBuildContext{
            .graphBuilder = std::ref(builder),
            .nodeHandle = node,
            .queue = nr::renderer::QueueDomain::Compute,
            .runtimeName = "TransferOps",
            .globalResources = std::cref(globals),
            .frameResources = std::ref(frameResources),
            .frameDataResources = std::ref(frameDataResources),
        };

        auto buffer = context.addResource(nr::renderer::GraphTransientBufferDesc{
            .debugName = "Clear.Buffer",
            .size = 256,
        });
        auto color = context.addResource(nr::renderer::GraphTransientImageDesc{
            .debugName = "Clear.Color",
            .extent = vk::Extent3D{32, 16, 1},
            .format = vk::Format::eR8G8B8A8Unorm,
        });
        auto depthStencil = context.addResource(nr::renderer::GraphTransientImageDesc{
            .debugName = "Clear.DepthStencil",
            .extent = vk::Extent3D{32, 16, 1},
            .format = vk::Format::eD32SfloatS8Uint,
            .aspect = nr::renderer::ImageAspectIntent::DepthStencil,
        });

        static_cast<void>(nr::renderer::ops::clearBuffer(
            context,
            "Clear.Buffer",
            nr::renderer::ops::ClearBufferPassDesc{.buffer = buffer}));
        static_cast<void>(nr::renderer::ops::clearColorImage(
            context,
            "Clear.Color",
            nr::renderer::ops::ClearColorImagePassDesc{.image = color}));
        static_cast<void>(nr::renderer::ops::clearDepthStencilImage(
            context,
            "Clear.DepthStencil",
            nr::renderer::ops::ClearDepthStencilImagePassDesc{.image = depthStencil}));

        auto frame = builder.build();
        nr::test::requireEqual(frame.passes.size(), std::size_t{3});
        nr::test::require(!frame.passes[0].isCopyPass, "clear buffer should be a recorded transfer pass");
        nr::test::require(static_cast<bool>(frame.passes[0].record), "clear buffer should retain record callback");
        nr::test::require(frame.passes[0].resourceUses.front().bufferUsage == nr::renderer::BufferUsageIntent::TransferDst);
        nr::test::require(frame.passes[1].resourceUses.front().imageUsage == nr::renderer::ImageUsageIntent::TransferDst);
        nr::test::require(frame.passes[1].resourceUses.front().imageAspect == nr::renderer::ImageAspectIntent::Color);
        nr::test::require(frame.passes[2].resourceUses.front().imageUsage == nr::renderer::ImageUsageIntent::TransferDst);
        nr::test::require(frame.passes[2].resourceUses.front().imageAspect == nr::renderer::ImageAspectIntent::DepthStencil);
    }};

const nr::test::CaseRegistrar transferOpsCopyCase{
    "renderer transfer copy helpers encode buffer image combinations",
    [] {
        auto builder = nr::renderer::RenderGraphBuilder{};
        auto node = builder.addNode("TransferOps", nr::renderer::QueueDomain::Compute);
        auto bindlessCache = nr::renderer::BindlessImageTableCache{};
        auto globals = nr::renderer::FrameGlobalResources{
            .bindlessImageTableCache = std::ref(bindlessCache),
        };
        auto frameResources = std::map<std::string, nr::renderer::GraphResourceHandle>{};
        auto frameDataResources = std::map<std::string, nr::renderer::GraphFrameDataHandle>{};
        auto context = nr::renderer::NodeBuildContext{
            .graphBuilder = std::ref(builder),
            .nodeHandle = node,
            .queue = nr::renderer::QueueDomain::Compute,
            .runtimeName = "TransferOps",
            .globalResources = std::cref(globals),
            .frameResources = std::ref(frameResources),
            .frameDataResources = std::ref(frameDataResources),
        };

        auto sourceBuffer = context.addResource(nr::renderer::GraphTransientBufferDesc{.debugName = "Copy.SourceBuffer", .size = 256});
        auto destinationBuffer = context.addResource(nr::renderer::GraphTransientBufferDesc{.debugName = "Copy.DestinationBuffer", .size = 256});
        auto sourceImage = context.addResource(nr::renderer::GraphTransientImageDesc{
            .debugName = "Copy.SourceImage",
            .extent = vk::Extent3D{32, 16, 1},
            .format = vk::Format::eR8G8B8A8Unorm,
        });
        auto depthStencilImage = context.addResource(nr::renderer::GraphTransientImageDesc{
            .debugName = "Copy.DepthStencilImage",
            .extent = vk::Extent3D{32, 16, 1},
            .format = vk::Format::eD32SfloatS8Uint,
            .aspect = nr::renderer::ImageAspectIntent::DepthStencil,
        });
        auto destinationImage = context.addResource(nr::renderer::GraphTransientImageDesc{
            .debugName = "Copy.DestinationImage",
            .extent = vk::Extent3D{32, 16, 1},
            .format = vk::Format::eR8G8B8A8Unorm,
        });

        static_cast<void>(nr::renderer::ops::copyBufferToBuffer(
            context,
            "Copy.BufferToBuffer",
            nr::renderer::CopyBufferToBufferPassDesc{.source = sourceBuffer, .destination = destinationBuffer}));
        static_cast<void>(nr::renderer::ops::copyBufferToImage(
            context,
            "Copy.BufferToImage",
            nr::renderer::CopyBufferToImagePassDesc{.sourceBuffer = sourceBuffer, .destinationImage = destinationImage}));
        static_cast<void>(nr::renderer::ops::copyImageToBuffer(
            context,
            "Copy.ImageToReadback",
            nr::renderer::CopyImageToBufferPassDesc{
                .sourceImage = depthStencilImage,
                .destinationBuffer = destinationBuffer,
                .imageAspect = nr::renderer::ImageAspectIntent::Depth,
                .destinationIntent = nr::renderer::CopyBufferDestinationIntent::Readback,
                .destinationBufferRangeSize = 128,
            }));
        static_cast<void>(nr::renderer::ops::copyImageToImage(
            context,
            "Copy.ImageToPresent",
            nr::renderer::CopyImageToImagePassDesc{
                .source = sourceImage,
                .destination = destinationImage,
                .presentDestination = true,
            }));

        auto frame = builder.build();
        nr::test::requireEqual(frame.passes.size(), std::size_t{4});
        nr::test::require(std::ranges::all_of(frame.passes, [](const nr::renderer::PassExecutionDesc& pass) {
            return pass.isCopyPass && pass.copy.has_value() && !pass.record;
        }), "copy helpers should emit implicit copy passes");
        nr::test::require(frame.passes[0].resourceUses[0].bufferUsage == nr::renderer::BufferUsageIntent::TransferSrc);
        nr::test::require(frame.passes[0].resourceUses[1].bufferUsage == nr::renderer::BufferUsageIntent::TransferDst);
        nr::test::require(frame.passes[1].resourceUses[0].bufferUsage == nr::renderer::BufferUsageIntent::TransferSrc);
        nr::test::require(frame.passes[1].resourceUses[1].imageUsage == nr::renderer::ImageUsageIntent::CopyDestination);
        nr::test::require(frame.passes[1].resourceUses[1].imageAspect == nr::renderer::ImageAspectIntent::Color);
        nr::test::require(frame.passes[2].resourceUses[0].imageUsage == nr::renderer::ImageUsageIntent::CopySource);
        nr::test::require(frame.passes[2].resourceUses[0].imageAspect == nr::renderer::ImageAspectIntent::Depth);
        nr::test::require(frame.passes[2].resourceUses[1].bufferUsage == nr::renderer::BufferUsageIntent::Readback);
        nr::test::require(frame.passes[3].resourceUses.size() == 3u, "present image copy should add present use");
        nr::test::require(frame.passes[3].resourceUses[2].imageUsage == nr::renderer::ImageUsageIntent::PresentSource);
        nr::test::require(std::holds_alternative<nr::renderer::CopyImageToImagePassDesc>(*frame.passes[3].copy));
    }};
} // namespace
