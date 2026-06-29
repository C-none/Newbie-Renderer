module nr.renderPasses;
import dependency.vulkan;

import :rayTraceInstanceHash;
import nr.renderer;
import nr.rhi;
import nr.utils;
import std;
import :nodeType;

namespace nr::renderPasses::detail
{
using GraphImportedBufferDesc = nr::renderer::GraphImportedBufferDesc;
using GraphResourceHandle = nr::renderer::GraphResourceHandle;
using GraphTransientImageDesc = nr::renderer::GraphTransientImageDesc;
using ResourceLifetime = nr::renderer::ResourceLifetime;
using BufferUsageIntent = nr::renderer::BufferUsageIntent;
using ImageUsageIntent = nr::renderer::ImageUsageIntent;
using ImageLayoutIntent = nr::renderer::ImageLayoutIntent;
namespace use = nr::renderer::use;

struct RayTraceInstanceHashRuntimeCache
{
    std::shared_ptr<nr::renderer::PipelineRuntime<nr::rhi::RayTracingPipeline>> pipeline{};
    nr::rhi::ShaderBindingTable shaderBindingTable{};
};

[[nodiscard]] std::shared_ptr<RayTraceInstanceHashRuntimeCache> ensureRayTraceInstanceHashRuntime(nr::rhi::Device& device)
{
    auto& shaderService = nr::rhi::ShaderService::instance();
    auto program = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
        .sourcePath = std::filesystem::path("renderer/rtInstanceHash"),
    });
    nr::nrAssert(program.valid(), "RT instance hash pass failed to compile shader module renderer/rtInstanceHash.");

    auto raygenGroup = nr::rhi::RayTracingShaderGroupDesc{};
    raygenGroup.generalEntryPoint = "rgMain";

    auto missGroup = nr::rhi::RayTracingShaderGroupDesc{};
    missGroup.generalEntryPoint = "msMain";

    auto hitGroup = nr::rhi::RayTracingShaderGroupDesc{};
    hitGroup.type = vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup;
    hitGroup.closestHitEntryPoint = "chMain";

    auto pipelineDesc = nr::rhi::RayTracingPipelineDesc{};
    pipelineDesc.entryPointNames = {
        "rgMain",
        "msMain",
        "chMain",
    };
    pipelineDesc.groups = {
        std::move(raygenGroup),
        std::move(missGroup),
        std::move(hitGroup),
    };

    auto runtime = std::make_shared<RayTraceInstanceHashRuntimeCache>();
    runtime->pipeline = std::make_shared<nr::renderer::PipelineRuntime<nr::rhi::RayTracingPipeline>>();
    runtime->pipeline->initialize(device.pipeline().createRayTracingPipeline(program, pipelineDesc));
    nr::nrAssert(runtime->pipeline->valid(), "RT instance hash pass failed to create ray tracing pipeline.");

    runtime->shaderBindingTable = nr::rhi::ShaderBindingTable::create(
        device.resourceFactory,
        nr::rhi::ShaderBindingTableBuildDesc{
            .pipeline = runtime->pipeline->pipeline(),
            .capabilities = device.rayTracingCapabilities(),
            .raygen = nr::rhi::ShaderBindingTableSectionDesc{
                .firstGroup = 0u,
                .groupCount = 1u,
            },
            .miss = nr::rhi::ShaderBindingTableSectionDesc{
                .firstGroup = 1u,
                .groupCount = 1u,
            },
            .hit = nr::rhi::ShaderBindingTableSectionDesc{
                .firstGroup = 2u,
                .groupCount = 1u,
            },
            .debugName = "RTInstanceHash.SBT",
        });
    nr::nrAssert(runtime->shaderBindingTable.valid(), "RT instance hash pass failed to create SBT.");

    return runtime;
}

[[nodiscard]] GraphResourceHandle importRayTraceBuffer(
    nr::renderer::NodeBuildContext& context,
    const nr::rhi::Buffer& buffer,
    std::string_view debugName,
    ResourceLifetime lifetime,
    std::initializer_list<BufferUsageIntent> usageIntents)
{
    nr::nrAssert(buffer.valid(), std::format("{} buffer is invalid.", debugName));
    return context.addResource(GraphImportedBufferDesc{
        .debugName = std::string(debugName),
        .lifetime = lifetime,
        .initialOwnership = nr::renderer::ownershipDomainFromQueue(context.queue),
        .size = buffer.size(),
        .usageIntents = std::vector<BufferUsageIntent>{usageIntents},
        .importedResource = std::cref(buffer),
    });
}

[[nodiscard]] GraphResourceHandle addOutputImage(
    nr::renderer::NodeBuildContext& context,
    std::string_view debugName,
    vk::Extent2D extent,
    vk::Format format)
{
    return context.addResource(GraphTransientImageDesc{
        .debugName = std::string(debugName),
        .extent = vk::Extent3D{extent.width, extent.height, 1u},
        .format = format,
        .usageIntents = {
            ImageUsageIntent::StorageWrite,
            ImageUsageIntent::Sampled,
            ImageUsageIntent::TransferDst,
            ImageUsageIntent::TransferSrc,
        },
        .initialLayout = ImageLayoutIntent::Undefined,
    });
}

[[nodiscard]] nr::rhi::QueueRole queueRoleFromDomain(nr::renderer::QueueDomain queue) noexcept
{
    if (queue == nr::renderer::QueueDomain::Graphics)
    {
        return nr::rhi::QueueRole::Graphics;
    }
    if (queue == nr::renderer::QueueDomain::Compute)
    {
        return nr::rhi::QueueRole::Compute;
    }
    return nr::rhi::QueueRole::Transfer;
}
} // namespace nr::renderPasses::detail

namespace nr::renderPasses
{
RayTraceInstanceHashNode::~RayTraceInstanceHashNode() = default;

NodeDescription RayTraceInstanceHashNode::describe() const
{
    return NodeDescription{
        .name = "RayTraceInstanceHash",
    };
}

void RayTraceInstanceHashNode::initialize(NodeInitContext& context)
{
    device_ = context.device;
    runtime_ = detail::ensureRayTraceInstanceHashRuntime(context.device.get());
    nr::rhi::setPipelineDebugName(
        context.device.get().device,
        runtime_->pipeline->pipeline().raw(),
        describe().name + ".Pipeline");
}

void RayTraceInstanceHashNode::build(NodeBuildContext& context, const NodeFrameParameters& frameParameters)
{
    nr::nrAssert(static_cast<bool>(runtime_), "RayTraceInstanceHash build stage requires initialized runtime state.");
    nr::nrAssert(device_.has_value(), "RayTraceInstanceHash build stage requires device reference from initialize stage.");

    auto viewportExtent = input.viewportExtent;
    if (viewportExtent.width == 1u && viewportExtent.height == 1u)
    {
        viewportExtent = frameParameters.swapchainExtent;
    }
    viewportExtent.width = std::max(1u, viewportExtent.width);
    viewportExtent.height = std::max(1u, viewportExtent.height);

    auto output = detail::addOutputImage(
        context,
        "RTInstanceHash.Output",
        viewportExtent,
        input.outputFormat);
    context.publishFrameResource(nr::renderer::frameResource::presentSourceColor, output);

    auto sceneTlas = context.resolveFrameResource(nr::renderer::frameResource::sceneTlas);
    if (!sceneTlas.valid())
    {
        auto clearUses = std::array{
            nr::renderer::use::transferDst(output),
        };
        [[maybe_unused]] auto clearPass = context.addPass(
            std::span<const nr::renderer::PassResourceUseDesc>{clearUses.data(), clearUses.size()},
            "RTInstanceHash.ClearNoTLAS",
            [output](const nr::renderer::PassRecordContext& recordContext) {
                nr::nrAssert(recordContext.commandBuffer.has_value(), "RTInstanceHash clear requires RAII command buffer access.");
                nr::nrAssert(static_cast<bool>(recordContext.resolveImage), "RTInstanceHash clear requires image resolver.");
                auto resolvedOutput = recordContext.resolveImage(output);
                nr::nrAssert(resolvedOutput.has_value(), "RTInstanceHash clear failed to resolve output image.");

                auto clearColor = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}};
                recordContext.commandBuffer->get().clearColorImage(
                    resolvedOutput->image,
                    vk::ImageLayout::eTransferDstOptimal,
                    clearColor,
                    resolvedOutput->subresourceRange);
            });
        return;
    }

    auto sbtResource = detail::importRayTraceBuffer(
        context,
        runtime_->shaderBindingTable.buffer(),
        "RTInstanceHash.SBT",
        nr::renderer::ResourceLifetime::RendererPersistent,
        {
            nr::renderer::BufferUsageIntent::ShaderBindingTable,
            nr::renderer::BufferUsageIntent::ShaderDeviceAddress,
        });

    auto dimensions = nr::rhi::TraceRaysDimensions{
        .width = viewportExtent.width,
        .height = viewportExtent.height,
        .depth = 1u,
    };
    auto queueRole = detail::queueRoleFromDomain(context.queue);
    nr::nrAssert(queueRole != nr::rhi::QueueRole::Transfer, "RayTraceInstanceHash cannot run on the transfer queue.");

    auto tracePass = nr::renderer::RayTracingPassBuilder{
        context,
        "RTInstanceHash.Trace",
        runtime_->pipeline};
    tracePass
        .accelerationStructure("scene", sceneTlas, "RTInstanceHash.SceneTLAS")
        .storageImage("outputImage", output, "RTInstanceHash.Output")
        .uniform("gFrame", context.globalResources.get().frameUniform, "Renderer.GlobalFrameUniforms")
        .resourceUse(nr::renderer::use::shaderBindingTableRead(sbtResource))
        .record([runtime = runtime_,
                 dimensions,
                 queueRole,
                 device = std::cref(device_->get())](const nr::renderer::RayTracingPassRecordContext& rayContext) {
            nr::rhi::traceRays(
                rayContext.commandBuffer,
                nr::rhi::TraceRaysDesc{
                    .pipeline = runtime->pipeline->pipeline(),
                    .shaderBindingTable = runtime->shaderBindingTable,
                    .dimensions = dimensions,
                    .recordingQueueRole = queueRole,
                },
                device.get().rayTracingCapabilities());
        });

    [[maybe_unused]] auto tracePassHandle = tracePass.build();
}

void RayTraceInstanceHashNode::shutdown(NodeShutdownContext&)
{
    if (runtime_ && runtime_->pipeline)
    {
        runtime_->pipeline->clearBindingSets();
    }
    runtime_.reset();
    device_.reset();
}
} // namespace nr::renderPasses
