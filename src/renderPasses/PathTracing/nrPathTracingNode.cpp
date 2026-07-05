module nr.renderPasses;
import dependency.vulkan;

import :pathTracing;
import :sceneTextureTableBinding;
import nr.renderer;
import nr.rhi;
import nr.utils;
import std;
import :nodeType;

namespace nr::renderPasses::detail
{
struct PathTracingVariantRuntime
{
    std::shared_ptr<nr::renderer::PipelineRuntime<nr::rhi::RayTracingPipeline>> pipeline{};
    nr::rhi::ShaderBindingTable shaderBindingTable{};
};

struct PathTracingRuntimeCache
{
    std::map<PathTracingVariantKey, PathTracingVariantRuntime> variants{};
};

[[nodiscard]] PathTracingVariantKey normalizePathTracingVariantKey(PathTracingVariantKey key) noexcept
{
    key.maxSurfaceBounces = std::clamp(
        key.maxSurfaceBounces,
        kPathTracingMinSurfaceBounces,
        kPathTracingMaxSurfaceBouncesLimit);
    return key;
}

[[nodiscard]] std::string describePathTracingVariantKey(const PathTracingVariantKey& key)
{
    auto normalizedKey = normalizePathTracingVariantKey(key);
    return std::format(
        "PathTracing[maxBounces={},russianRoulette={}]",
        normalizedKey.maxSurfaceBounces,
        normalizedKey.enableRussianRoulette ? "enabled" : "disabled");
}

[[nodiscard]] nr::rhi::SlangProgramVariantDesc makePathTracingVariantDesc(const PathTracingVariantKey& key)
{
    auto normalizedKey = normalizePathTracingVariantKey(key);

    auto variant = nr::rhi::SlangProgramVariantDesc{};
    variant.debugName = describePathTracingVariantKey(normalizedKey);

    variant.constants.try_emplace(
        "kPathTracingMaxSurfaceBounces",
        nr::rhi::SlangVariantConstant::fromUInt32(normalizedKey.maxSurfaceBounces));

    variant.typeAliases.try_emplace(
        "PathTracingRussianRoulettePolicy",
        nr::rhi::SlangVariantTypeAlias{
            .typeName = "PathTracingRussianRoulettePolicy",
            .interfaceName = "IPathTracingRussianRoulettePolicy",
            .concreteTypeName = normalizedKey.enableRussianRoulette ? "PathTracingRussianRouletteEnabledPolicy"
                                                                    : "PathTracingRussianRouletteDisabledPolicy",
        });

    return variant;
}

[[nodiscard]] nr::rhi::RayTracingPipelineDesc makePathTracingPipelineDesc()
{
    auto raygenGroup = nr::rhi::RayTracingShaderGroupDesc{};
    raygenGroup.generalEntryPoint = "rgMain";

    auto missGroup = nr::rhi::RayTracingShaderGroupDesc{};
    missGroup.generalEntryPoint = "msMain";

    auto hitGroup = nr::rhi::RayTracingShaderGroupDesc{};
    hitGroup.type = vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup;
    hitGroup.closestHitEntryPoint = "chMain";
    hitGroup.anyHitEntryPoint = "ahMain";

    auto pipelineDesc = nr::rhi::RayTracingPipelineDesc{};
    pipelineDesc.entryPointNames = {
        "rgMain",
        "msMain",
        "chMain",
        "ahMain",
    };
    pipelineDesc.groups = {
        std::move(raygenGroup),
        std::move(missGroup),
        std::move(hitGroup),
    };
    pipelineDesc.descriptorBindingPolicy.defaultRuntimeDescriptorCount = nr::renderer::kSceneTextureDescriptorCapacity;
    return pipelineDesc;
}

[[nodiscard]] PathTracingVariantRuntime createPathTracingVariantRuntime(nr::rhi::Device& device, const PathTracingVariantKey& key)
{
    auto& shaderService = nr::rhi::ShaderService::instance();
    auto baselineVariantDesc = makePathTracingVariantDesc(PathTracingVariantKey{});
    auto baselineProgram = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
        .sourcePath = std::filesystem::path("renderer/pathTracing"),
        .variant = baselineVariantDesc,
    });
    nr::nrAssert(baselineProgram.valid(), "Path tracing pass failed to compile baseline shader module renderer/pathTracing.");

    auto variantDesc = makePathTracingVariantDesc(key);
    auto program = baselineProgram;
    if (variantDesc.hashValue() != baselineVariantDesc.hashValue())
    {
        program = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path("renderer/pathTracing"),
            .variant = variantDesc,
        });
        nr::nrAssert(program.valid(), "Path tracing pass failed to compile variant shader module renderer/pathTracing.");
    }

    auto pipelineDesc = makePathTracingPipelineDesc();
    if (variantDesc.hashValue() != baselineVariantDesc.hashValue())
    {
        nr::rhi::assertShaderLayoutAbiStable(
            baselineProgram,
            program,
            pipelineDesc.descriptorBindingPolicy,
            variantDesc.debugName);
    }

    auto runtime = PathTracingVariantRuntime{};
    runtime.pipeline = std::make_shared<nr::renderer::PipelineRuntime<nr::rhi::RayTracingPipeline>>();
    auto sceneTextureImmutableSamplers = std::array{sceneTextureTableImmutableSamplerBinding()};
    runtime.pipeline->initializeDeferred(device.pipeline().createRayTracingPipeline(
        program,
        pipelineDesc,
        64u,
        sceneTextureImmutableSamplers));
    nr::nrAssert(runtime.pipeline->valid(), "Path tracing pass failed to create ray tracing pipeline.");
    nr::rhi::setPipelineDebugName(
        device.device,
        runtime.pipeline->pipeline().raw(),
        describePathTracingVariantKey(key) + ".Pipeline");

    runtime.shaderBindingTable = nr::rhi::ShaderBindingTable::create(
        device.resourceFactory,
        nr::rhi::ShaderBindingTableBuildDesc{
            .pipeline = runtime.pipeline->pipeline(),
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
            .debugName = "PathTracing.SBT",
        });
    nr::nrAssert(runtime.shaderBindingTable.valid(), "Path tracing pass failed to create SBT.");

    return runtime;
}

[[nodiscard]] PathTracingVariantRuntime& ensurePathTracingVariantRuntime(
    PathTracingRuntimeCache& cache,
    nr::rhi::Device& device,
    const PathTracingVariantKey& key)
{
    auto normalizedKey = normalizePathTracingVariantKey(key);
    if (auto runtimeIt = cache.variants.find(normalizedKey); runtimeIt != cache.variants.end())
    {
        return runtimeIt->second;
    }

    auto [runtimeIt, inserted] = cache.variants.try_emplace(normalizedKey, createPathTracingVariantRuntime(device, normalizedKey));
    nr::nrAssert(inserted, "Path tracing variant runtime cache insertion failed.");
    return runtimeIt->second;
}

[[nodiscard]] std::shared_ptr<PathTracingRuntimeCache> makePathTracingRuntimeCache(nr::rhi::Device& device, const PathTracingVariantKey& initialVariant)
{
    auto cache = std::make_shared<PathTracingRuntimeCache>();
    [[maybe_unused]] auto& initialRuntime = ensurePathTracingVariantRuntime(*cache, device, initialVariant);
    return cache;
}
} // namespace nr::renderPasses::detail

namespace nr::renderPasses
{
PathTracingNode::~PathTracingNode() = default;

NodeDescription PathTracingNode::describe() const
{
    return NodeDescription{
        .name = "PathTracing",
    };
}

void PathTracingNode::initialize(NodeInitContext& context)
{
    device_ = context.device;
    input.variant = detail::normalizePathTracingVariantKey(input.variant);
    runtime_ = detail::makePathTracingRuntimeCache(context.device.get(), input.variant);
}

void PathTracingNode::build(NodeBuildContext& context, const NodeFrameParameters& frameParameters)
{
    nr::nrAssert(static_cast<bool>(runtime_), "PathTracing build stage requires initialized runtime state.");
    nr::nrAssert(device_.has_value(), "PathTracing build stage requires device reference from initialize stage.");
    input.variant = detail::normalizePathTracingVariantKey(input.variant);

    auto viewportExtent = input.viewportExtent;
    if (viewportExtent.width == 1u && viewportExtent.height == 1u)
    {
        viewportExtent = frameParameters.swapchainExtent;
    }
    viewportExtent.width = std::max(1u, viewportExtent.width);
    viewportExtent.height = std::max(1u, viewportExtent.height);

    auto output = context.addResource(nr::renderer::GraphTransientImageDesc{
        .debugName = "PathTracing.Output",
        .extent = vk::Extent3D{viewportExtent.width, viewportExtent.height, 1u},
        .format = input.outputFormat,
        .usageIntents = {
            nr::renderer::ImageUsageIntent::StorageWrite,
            nr::renderer::ImageUsageIntent::Sampled,
            nr::renderer::ImageUsageIntent::TransferDst,
            nr::renderer::ImageUsageIntent::TransferSrc,
        },
        .initialLayout = nr::renderer::ImageLayoutIntent::Undefined,
    });
    context.publishFrameResource(nr::renderer::frameResource::presentSourceColor, output);

    auto sceneTlas = context.resolveFrameResource(nr::renderer::frameResource::sceneTlas);
    if (!sceneTlas.valid())
    {
        auto clearUses = std::array{
            nr::renderer::use::imageTransferDst(output),
        };
        [[maybe_unused]] auto clearPass = context.addPass(
            std::span<const nr::renderer::PassResourceUseDesc>{clearUses.data(), clearUses.size()},
            "PathTracing.ClearNoTLAS",
            [output](const nr::renderer::PassRecordContext& recordContext) {
                nr::nrAssert(recordContext.commandBuffer.has_value(), "PathTracing clear requires RAII command buffer access.");
                nr::nrAssert(static_cast<bool>(recordContext.resolveImage), "PathTracing clear requires image resolver.");
                auto resolvedOutput = recordContext.resolveImage(output);
                nr::nrAssert(resolvedOutput.has_value(), "PathTracing clear failed to resolve output image.");

                auto clearColor = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}};
                recordContext.commandBuffer->get().clearColorImage(
                    resolvedOutput->image,
                    vk::ImageLayout::eTransferDstOptimal,
                    clearColor,
                    resolvedOutput->subresourceRange);
        });
        return;
    }

    auto rtInstanceMetadata = context.resolveFrameResource(nr::renderer::frameResource::sceneRtInstanceMetadata);
    auto rtGeometryMetadata = context.resolveFrameResource(nr::renderer::frameResource::sceneRtGeometryMetadata);
    auto rtMaterialHeaders = context.resolveFrameResource(nr::renderer::frameResource::sceneRtMaterialHeaders);
    auto rtMaterialLayers = context.resolveFrameResource(nr::renderer::frameResource::sceneRtMaterialLayers);
    auto rtMaterialTextureRefs = context.resolveFrameResource(nr::renderer::frameResource::sceneRtMaterialTextureRefs);
    auto rtVertexAtlas = context.resolveFrameResource(nr::renderer::frameResource::sceneRtVertexAtlas);
    auto rtIndexAtlas = context.resolveFrameResource(nr::renderer::frameResource::sceneRtIndexAtlas);
    auto sceneLightHeader = context.resolveFrameResource(nr::renderer::frameResource::sceneLightHeader);
    auto sceneLights = context.resolveFrameResource(nr::renderer::frameResource::sceneLights);
    auto sceneLightAliasTable = context.resolveFrameResource(nr::renderer::frameResource::sceneLightAliasTable);
    if (!rtInstanceMetadata.valid() ||
        !rtGeometryMetadata.valid() ||
        !rtMaterialHeaders.valid() ||
        !rtMaterialLayers.valid() ||
        !rtMaterialTextureRefs.valid() ||
        !rtVertexAtlas.valid() ||
        !rtIndexAtlas.valid() ||
        !sceneLightHeader.valid() ||
        !sceneLights.valid() ||
        !sceneLightAliasTable.valid())
    {
        auto clearUses = std::array{
            nr::renderer::use::imageTransferDst(output),
        };
        [[maybe_unused]] auto clearPass = context.addPass(
            std::span<const nr::renderer::PassResourceUseDesc>{clearUses.data(), clearUses.size()},
            "PathTracing.ClearNoRTSideband",
            [output](const nr::renderer::PassRecordContext& recordContext) {
                nr::nrAssert(recordContext.commandBuffer.has_value(), "PathTracing sideband clear requires RAII command buffer access.");
                nr::nrAssert(static_cast<bool>(recordContext.resolveImage), "PathTracing sideband clear requires image resolver.");
                auto resolvedOutput = recordContext.resolveImage(output);
                nr::nrAssert(resolvedOutput.has_value(), "PathTracing sideband clear failed to resolve output image.");

                auto clearColor = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}};
                recordContext.commandBuffer->get().clearColorImage(
                    resolvedOutput->image,
                    vk::ImageLayout::eTransferDstOptimal,
                    clearColor,
                    resolvedOutput->subresourceRange);
            });
        return;
    }

    auto& activeRuntime = detail::ensurePathTracingVariantRuntime(*runtime_, device_->get(), input.variant);

    auto sbtResource = context.importBuffer(
        activeRuntime.shaderBindingTable.buffer(),
        "PathTracing.SBT",
        nr::renderer::ResourceLifetime::RendererPersistent,
        {
            nr::renderer::BufferUsageIntent::ShaderBindingTable,
            nr::renderer::BufferUsageIntent::ShaderDeviceAddress,
        },
        nr::renderer::ownershipDomainFromQueue(context.queue));

    auto dimensions = nr::rhi::TraceRaysDimensions{
        .width = viewportExtent.width,
        .height = viewportExtent.height,
        .depth = 1u,
    };
    auto queueRole = nr::renderer::rhiQueueRoleFromDomain(context.queue);
    nr::nrAssert(queueRole != nr::rhi::QueueRole::Transfer, "PathTracing cannot run on the transfer queue.");
    auto sceneTextureTableBinding = detail::makeSceneTextureTableBindingInput(context.globalResources.get());
    auto& bindlessImageTableCache = context.globalResources.get().bindlessImageTableCache.get();

    auto tracePass = nr::renderer::RayTracingPassBuilder{
        context,
        "PathTracing.Trace",
        activeRuntime.pipeline};
    tracePass
        .accelerationStructure("scene", sceneTlas, "PathTracing.SceneTLAS")
        .storageImage("outputImage", output, "PathTracing.Output")
        .storageBuffer("rtInstanceMetadata", rtInstanceMetadata, "PathTracing.InstanceMetadata")
        .storageBuffer("rtGeometryMetadata", rtGeometryMetadata, "PathTracing.GeometryMetadata")
        .storageBuffer("rtMaterialHeaders", rtMaterialHeaders, "PathTracing.MaterialHeaders")
        .storageBuffer("rtMaterialLayers", rtMaterialLayers, "PathTracing.MaterialLayers")
        .storageBuffer("rtMaterialTextureRefs", rtMaterialTextureRefs, "PathTracing.MaterialTextureRefs")
        .storageBuffer("rtVertexData", rtVertexAtlas, "PathTracing.VertexAtlas")
        .storageBuffer("rtIndexData", rtIndexAtlas, "PathTracing.IndexAtlas")
        .uniform("gFrame", context.globalResources.get().frameUniform, "Renderer.GlobalFrameUniforms")
        .resourceUse(nr::renderer::use::shaderBindingTableRead(sbtResource))
        .uniform("gSceneLightHeader", sceneLightHeader, "PathTracing.SceneLightHeader")
        .storageBuffer("gSceneLights", sceneLights, "PathTracing.SceneLights")
        .storageBuffer("gSceneLightAliasTable", sceneLightAliasTable, "PathTracing.SceneLightAliasTable")
        .prepare(
            [runtime = std::ref(activeRuntime),
             sceneTextureTableBinding,
             cache = std::ref(bindlessImageTableCache)](const nr::renderer::PassPrepareContext& prepareContext) {
                detail::prepareSceneTextureTableBindingForFrame(
                    *runtime.get().pipeline,
                    cache.get(),
                    prepareContext.frameIndex,
                    sceneTextureTableBinding,
                    detail::SceneTextureTableBindingRequirement::optional);
            })
        .dynamicBindingSnapshot(
            [runtime = std::ref(activeRuntime),
             sceneTextureTableBinding,
             cache = std::ref(bindlessImageTableCache)](const nr::renderer::PassPrepareContext& prepareContext) {
                return detail::makeSceneTextureTableBindingSnapshot(
                    *runtime.get().pipeline,
                    cache.get(),
                    prepareContext.frameIndex,
                    sceneTextureTableBinding,
                    detail::SceneTextureTableBindingRequirement::optional);
            })
        .record([runtime = std::ref(activeRuntime),
                 dimensions,
                 queueRole,
                 device = std::cref(device_->get())](const nr::renderer::RayTracingPassRecordContext& rayContext) {
            auto& activeRuntime = runtime.get();
            nr::rhi::traceRays(
                rayContext.commandBuffer,
                nr::rhi::TraceRaysDesc{
                    .pipeline = activeRuntime.pipeline->pipeline(),
                    .shaderBindingTable = activeRuntime.shaderBindingTable,
                    .dimensions = dimensions,
                    .recordingQueueRole = queueRole,
                },
                device.get().rayTracingCapabilities());
        });

    [[maybe_unused]] auto tracePassHandle = tracePass.build();
}

void PathTracingNode::collectUi(NodeUiBuildContext& context, const NodeFrameParameters&)
{
    if (pendingVariantValid_)
    {
        input.variant = detail::normalizePathTracingVariantKey(pendingVariant_);
        pendingVariantValid_ = false;
    }

    input.variant = detail::normalizePathTracingVariantKey(input.variant);
    variantDraft_ = input.variant;
    context.addSection(
        context.runtimeName(),
        [this](NodeUiWriter& ui) {
            auto maxBounces = variantDraft_.maxSurfaceBounces;
            if (ui.inputUInt(
                    "Max Bounces",
                    maxBounces,
                    kPathTracingMinSurfaceBounces,
                    kPathTracingMaxSurfaceBouncesLimit))
            {
                variantDraft_.maxSurfaceBounces = std::clamp(
                    maxBounces,
                    kPathTracingMinSurfaceBounces,
                    kPathTracingMaxSurfaceBouncesLimit);
                pendingVariant_ = detail::normalizePathTracingVariantKey(variantDraft_);
                pendingVariantValid_ = true;
            }

            auto enableRussianRoulette = variantDraft_.enableRussianRoulette;
            if (ui.checkbox("Russian Roulette", enableRussianRoulette))
            {
                variantDraft_.enableRussianRoulette = enableRussianRoulette;
                pendingVariant_ = detail::normalizePathTracingVariantKey(variantDraft_);
                pendingVariantValid_ = true;
            }
        },
        true,
        "controls");
}

void PathTracingNode::shutdown(NodeShutdownContext&)
{
    if (runtime_)
    {
        std::ranges::for_each(runtime_->variants | std::views::values, [](detail::PathTracingVariantRuntime& runtime) {
            if (runtime.pipeline)
            {
                runtime.pipeline->clearBindingSets();
            }
        });
    }
    runtime_.reset();
    device_.reset();
}
} // namespace nr::renderPasses
