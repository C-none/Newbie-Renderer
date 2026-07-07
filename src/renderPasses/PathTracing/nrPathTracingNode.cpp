module nr.renderPasses;
import dependency.vulkan;

import :pathTracing;
import :rtHitSbtPlan;
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

struct PathTracingRuntimeKey
{
    PathTracingVariantKey variant{};
    std::uint64_t hitPermutationSetHash = 0u;
    std::uint64_t hitRecordPlanHash = 0u;

    [[nodiscard]] friend bool operator<(const PathTracingRuntimeKey& lhs, const PathTracingRuntimeKey& rhs) noexcept
    {
        return std::tie(lhs.variant, lhs.hitPermutationSetHash, lhs.hitRecordPlanHash) <
               std::tie(rhs.variant, rhs.hitPermutationSetHash, rhs.hitRecordPlanHash);
    }
};

struct PathTracingRuntimeCache
{
    std::map<PathTracingRuntimeKey, PathTracingVariantRuntime> variants{};
};

[[nodiscard]] PathTracingVariantKey normalizePathTracingVariantKey(PathTracingVariantKey key) noexcept;

[[nodiscard]] std::array<nr::renderer::VariantItemDesc, 2> makePathTracingVariantItems(
    PathTracingVariantKey defaults)
{
    defaults = normalizePathTracingVariantKey(defaults);
    return std::array{
        nr::renderer::VariantItemDesc{
            .shader = nr::rhi::ShaderVariantItemDesc{
                .id = "maxSurfaceBounces",
                .label = "Max Bounces",
                .kind = nr::rhi::ShaderVariantValueKind::UInt32,
                .defaultValue = defaults.maxSurfaceBounces,
                .numericRange = nr::rhi::ShaderVariantNumericRange{
                    .minValue = static_cast<double>(kPathTracingMinSurfaceBounces),
                    .maxValue = static_cast<double>(kPathTracingMaxSurfaceBouncesLimit),
                    .step = 1.0,
                    .bounded = true,
                },
                .slangBinding = nr::rhi::ShaderVariantSlangBinding{
                    .kind = nr::rhi::ShaderVariantSlangBindingKind::Constant,
                    .constant = nr::rhi::ShaderVariantSlangConstantBinding{
                        .name = "kPathTracingMaxSurfaceBounces",
                        .type = nr::rhi::SlangVariantConstantType::UInt32,
                    },
                },
            },
            .effect = nr::renderer::VariantItemEffect::SlangLinkTime,
        },
        nr::renderer::VariantItemDesc{
            .shader = nr::rhi::ShaderVariantItemDesc{
                .id = "enableRussianRoulette",
                .label = "Russian Roulette",
                .kind = nr::rhi::ShaderVariantValueKind::Bool,
                .defaultValue = defaults.enableRussianRoulette,
                .slangBinding = nr::rhi::ShaderVariantSlangBinding{
                    .kind = nr::rhi::ShaderVariantSlangBindingKind::TypeAlias,
                    .typeAlias = nr::rhi::ShaderVariantSlangTypeAliasBinding{
                        .exportedTypeName = "PathTracingRussianRoulettePolicy",
                        .interfaceName = "IPathTracingRussianRoulettePolicy",
                        .concreteTypeNameByChoice = {
                            {"true", "PathTracingRussianRouletteEnabledPolicy"},
                            {"false", "PathTracingRussianRouletteDisabledPolicy"},
                        },
                    },
                },
            },
            .effect = nr::renderer::VariantItemEffect::SlangLinkTime,
        },
    };
}

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

[[nodiscard]] std::string pathTracingHashHex(std::uint64_t value)
{
    auto chars = nr::hash::toHexChars(value);
    return std::string(nr::hash::toHexView(chars));
}

[[nodiscard]] std::string describePathTracingRuntimeKey(const PathTracingRuntimeKey& key)
{
    return std::format(
        "{},hitPermutations={},hitRecords={}",
        describePathTracingVariantKey(key.variant),
        pathTracingHashHex(key.hitPermutationSetHash),
        pathTracingHashHex(key.hitRecordPlanHash));
}

[[nodiscard]] nr::rhi::SlangProgramVariantDesc makePathTracingVariantDesc(const PathTracingVariantKey& key)
{
    auto normalizedKey = normalizePathTracingVariantKey(key);
    auto items = makePathTracingVariantItems(PathTracingVariantKey{});
    auto valueSet = nr::rhi::ShaderVariantValueSet{
        .values = {
            {"maxSurfaceBounces", normalizedKey.maxSurfaceBounces},
            {"enableRussianRoulette", normalizedKey.enableRussianRoulette},
        },
    };
    auto shaderItems = items |
                       std::views::transform([](const nr::renderer::VariantItemDesc& item) {
                           return item.shader;
                       }) |
                       std::ranges::to<std::vector>();
    return nr::rhi::makeSlangProgramVariantDesc(
        describePathTracingVariantKey(normalizedKey),
        std::span<const nr::rhi::ShaderVariantItemDesc>{shaderItems.data(), shaderItems.size()},
        valueSet);
}

[[nodiscard]] nr::rhi::SlangProgramVariantDesc makePathTracingChsVariantDesc(const RtHitPermutationKey& key)
{
    nr::nrAssert(
        key.shadingModel == RtHitShadingModel::gltfPbr,
        "PathTracing CHS v1 only supports glTF PBR hit permutations.");

    auto variant = nr::rhi::SlangProgramVariantDesc{};
    variant.debugName = std::format(
        "PathTracing.CHS[features={},alpha={}]",
        key.materialFeatureMask,
        static_cast<std::uint32_t>(key.alphaPolicy));
    variant.typeAliases.try_emplace(
        "CHS",
        nr::rhi::SlangVariantTypeAlias{
            .typeName = "CHS",
            .interfaceName = "ICHS",
            .concreteTypeName = std::format(
                "DefaultLitCHS<{}u, {}u>",
                key.materialFeatureMask,
                static_cast<std::uint32_t>(key.alphaPolicy)),
        });
    return variant;
}

[[nodiscard]] nr::rhi::RayTracingPipelineDesc makePathTracingPipelineDesc(const SceneRtHitSbtPlan& hitSbtPlan)
{
    auto raygenGroup = nr::rhi::RayTracingShaderGroupDesc{};
    raygenGroup.generalEntryPoint = "rgMain";

    auto missGroup = nr::rhi::RayTracingShaderGroupDesc{};
    missGroup.generalEntryPoint = "msMain";

    auto pipelineDesc = nr::rhi::RayTracingPipelineDesc{};
    pipelineDesc.entryPointNames = {
        "rgMain",
        "msMain",
    };
    auto const usesAnyHit = std::ranges::any_of(hitSbtPlan.permutations, [](const SceneRtHitSbtPermutation& permutation) {
        return rtHitPermutationUsesAnyHit(permutation.key);
    });
    if (usesAnyHit)
    {
        pipelineDesc.entryPointNames.push_back("ahAlphaMask");
    }

    pipelineDesc.groups.reserve(2u + hitSbtPlan.permutations.size());
    pipelineDesc.groups.push_back(std::move(raygenGroup));
    pipelineDesc.groups.push_back(std::move(missGroup));
    std::ranges::for_each(hitSbtPlan.permutations, [&](const SceneRtHitSbtPermutation& permutation) {
        auto hitGroup = nr::rhi::RayTracingShaderGroupDesc{};
        hitGroup.type = vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup;
        hitGroup.closestHitEntryPoint = rtHitClosestHitEntryPointName(permutation.key);
        if (rtHitPermutationUsesAnyHit(permutation.key))
        {
            hitGroup.anyHitEntryPoint = "ahAlphaMask";
        }
        pipelineDesc.entryPointNames.push_back(hitGroup.closestHitEntryPoint);
        pipelineDesc.groups.push_back(std::move(hitGroup));
    });
    pipelineDesc.descriptorBindingPolicy.defaultRuntimeDescriptorCount = nr::renderer::kSceneTextureDescriptorCapacity;
    return pipelineDesc;
}

[[nodiscard]] std::vector<nr::rhi::RayTracingPipelineStageSelection> makePathTracingPipelineStageSelections(
    const nr::rhi::SlangProgram& rootProgram,
    const SceneRtHitSbtPlan& hitSbtPlan,
    std::span<const nr::rhi::SlangProgram> hitPrograms)
{
    auto const usesAnyHit = std::ranges::any_of(hitSbtPlan.permutations, [](const SceneRtHitSbtPermutation& permutation) {
        return rtHitPermutationUsesAnyHit(permutation.key);
    });

    auto selections = std::vector<nr::rhi::RayTracingPipelineStageSelection>{};
    selections.reserve(2u + (usesAnyHit ? 1u : 0u) + hitSbtPlan.permutations.size());
    selections.push_back(nr::rhi::RayTracingPipelineStageSelection{
        .program = std::cref(rootProgram),
        .entryPointName = "rgMain",
        .logicalEntryPointName = "rgMain",
    });
    selections.push_back(nr::rhi::RayTracingPipelineStageSelection{
        .program = std::cref(rootProgram),
        .entryPointName = "msMain",
        .logicalEntryPointName = "msMain",
    });
    if (usesAnyHit)
    {
        selections.push_back(nr::rhi::RayTracingPipelineStageSelection{
            .program = std::cref(rootProgram),
            .entryPointName = "ahAlphaMask",
            .logicalEntryPointName = "ahAlphaMask",
        });
    }

    std::ranges::for_each(hitSbtPlan.permutations, [&](const SceneRtHitSbtPermutation& permutation) {
        nr::nrAssert(permutation.permutationIndex < hitPrograms.size(), "PathTracing CHS program index is out of range.");
        selections.push_back(nr::rhi::RayTracingPipelineStageSelection{
            .program = std::cref(hitPrograms[permutation.permutationIndex]),
            .entryPointName = "chMain",
            .logicalEntryPointName = rtHitClosestHitEntryPointName(permutation.key),
        });
    });
    return selections;
}

[[nodiscard]] PathTracingVariantRuntime createPathTracingVariantRuntime(
    nr::rhi::Device& device,
    const PathTracingRuntimeKey& runtimeKey,
    const SceneRtHitSbtPlan& hitSbtPlan)
{
    auto& shaderService = nr::rhi::ShaderService::instance();
    auto baselineVariantDesc = makePathTracingVariantDesc(PathTracingVariantKey{});
    auto baselineChsVariantDesc = makePathTracingChsVariantDesc(RtHitPermutationKey{});
    auto baselineProgram = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
        .sourcePath = std::filesystem::path{"renderer/pathTracing"},
        .variant = baselineVariantDesc,
        .linkVariants = {baselineChsVariantDesc},
    });
    nr::nrAssert(baselineProgram.valid(), "Path tracing pass failed to compile baseline shader module.");

    auto variantDesc = makePathTracingVariantDesc(runtimeKey.variant);
    auto rootProgram = baselineProgram;
    if (variantDesc.hashValue() != baselineVariantDesc.hashValue())
    {
        rootProgram = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/pathTracing"},
            .variant = variantDesc,
            .linkVariants = {baselineChsVariantDesc},
        });
        nr::nrAssert(rootProgram.valid(), "Path tracing pass failed to compile variant shader module.");
    }

    auto pipelineDesc = makePathTracingPipelineDesc(hitSbtPlan);
    if (variantDesc.hashValue() != baselineVariantDesc.hashValue())
    {
        nr::rhi::assertShaderLayoutAbiStable(
            baselineProgram,
            rootProgram,
            pipelineDesc.descriptorBindingPolicy,
            variantDesc.debugName);
    }

    auto hitPrograms = std::vector<nr::rhi::SlangProgram>{};
    hitPrograms.reserve(hitSbtPlan.permutations.size());
    std::ranges::for_each(hitSbtPlan.permutations, [&](const SceneRtHitSbtPermutation& permutation) {
        auto chsVariantDesc = makePathTracingChsVariantDesc(permutation.key);
        auto chsProgram = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/pathTracing"},
            .variant = variantDesc,
            .linkVariants = {chsVariantDesc},
        });
        nr::nrAssert(chsProgram.valid(), "Path tracing pass failed to compile CHS-specialized shader module.");
        if (chsVariantDesc.hashValue() != baselineChsVariantDesc.hashValue())
        {
            nr::rhi::assertShaderLayoutAbiStable(
                rootProgram,
                chsProgram,
                pipelineDesc.descriptorBindingPolicy,
                chsVariantDesc.debugName);
        }
        hitPrograms.push_back(std::move(chsProgram));
    });
    auto pipelineStageSelections = makePathTracingPipelineStageSelections(rootProgram, hitSbtPlan, hitPrograms);

    auto runtime = PathTracingVariantRuntime{};
    runtime.pipeline = std::make_shared<nr::renderer::PipelineRuntime<nr::rhi::RayTracingPipeline>>();
    auto sceneTextureImmutableSamplers = std::array{sceneTextureTableImmutableSamplerBinding()};
    runtime.pipeline->initializeDeferred(device.pipeline().createRayTracingPipeline(
        rootProgram,
        pipelineStageSelections,
        pipelineDesc,
        64u,
        sceneTextureImmutableSamplers));
    nr::nrAssert(runtime.pipeline->valid(), "Path tracing pass failed to create ray tracing pipeline.");
    nr::rhi::setPipelineDebugName(
        device.device,
        runtime.pipeline->pipeline().raw(),
        describePathTracingRuntimeKey(runtimeKey) + ".Pipeline");

    auto hitRecords = hitSbtPlan.records |
                      std::views::transform([](const SceneRtHitSbtRecord& record) {
                          return nr::rhi::ShaderBindingTableRecordDesc{
                              .groupIndex = 2u + record.permutationIndex,
                          };
                      }) |
                      std::ranges::to<std::vector>();

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
                .records = std::span<const nr::rhi::ShaderBindingTableRecordDesc>{hitRecords.data(), hitRecords.size()},
            },
            .debugName = "PathTracing.SBT",
        });
    nr::nrAssert(runtime.shaderBindingTable.valid(), "Path tracing pass failed to create SBT.");

    return runtime;
}

[[nodiscard]] PathTracingVariantRuntime& ensurePathTracingVariantRuntime(
    PathTracingRuntimeCache& cache,
    nr::rhi::Device& device,
    const PathTracingVariantKey& key,
    const SceneRtHitSbtPlan& hitSbtPlan)
{
    auto runtimeKey = PathTracingRuntimeKey{
        .variant = normalizePathTracingVariantKey(key),
        .hitPermutationSetHash = hitSbtPlan.permutationSetHash,
        .hitRecordPlanHash = hitSbtPlan.recordPlanHash,
    };
    if (auto runtimeIt = cache.variants.find(runtimeKey); runtimeIt != cache.variants.end())
    {
        return runtimeIt->second;
    }

    auto [runtimeIt, inserted] = cache.variants.try_emplace(
        runtimeKey,
        createPathTracingVariantRuntime(device, runtimeKey, hitSbtPlan));
    nr::nrAssert(inserted, "Path tracing variant runtime cache insertion failed.");
    return runtimeIt->second;
}

[[nodiscard]] PathTracingVariantKey pathTracingVariantKeyFromRegistry(
    const nr::renderer::VariantStateRegistry& variants,
    std::string_view runtimeName,
    PathTracingVariantKey fallback)
{
    fallback = normalizePathTracingVariantKey(fallback);
    return normalizePathTracingVariantKey(PathTracingVariantKey{
        .maxSurfaceBounces = variants.valueOr<std::uint32_t>(
            runtimeName,
            "maxSurfaceBounces",
            fallback.maxSurfaceBounces),
        .enableRussianRoulette = variants.valueOr<bool>(
            runtimeName,
            "enableRussianRoulette",
            fallback.enableRussianRoulette),
    });
}

[[nodiscard]] std::shared_ptr<PathTracingRuntimeCache> makePathTracingRuntimeCache()
{
    auto cache = std::make_shared<PathTracingRuntimeCache>();
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
    auto items = detail::makePathTracingVariantItems(input.variant);
    context.variants.get().registerItems(
        context.runtimeName,
        std::span<const nr::renderer::VariantItemDesc>{items.data(), items.size()});
    runtime_ = detail::makePathTracingRuntimeCache();
}

void PathTracingNode::build(NodeBuildContext& context, const NodeFrameParameters& frameParameters)
{
    nr::nrAssert(static_cast<bool>(runtime_), "PathTracing build stage requires initialized runtime state.");
    nr::nrAssert(device_.has_value(), "PathTracing build stage requires device reference from initialize stage.");
    input.variant = detail::pathTracingVariantKeyFromRegistry(
        context.variants.get(),
        context.runtimeName,
        input.variant);

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
    auto rtHitSbtPlanHandle = context.resolveFrameData(nr::renderer::frameData::sceneRtHitSbtPlan);
    if (!rtInstanceMetadata.valid() ||
        !rtGeometryMetadata.valid() ||
        !rtMaterialHeaders.valid() ||
        !rtMaterialLayers.valid() ||
        !rtMaterialTextureRefs.valid() ||
        !rtVertexAtlas.valid() ||
        !rtIndexAtlas.valid() ||
        !sceneLightHeader.valid() ||
        !sceneLights.valid() ||
        !sceneLightAliasTable.valid() ||
        !rtHitSbtPlanHandle.valid())
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

    auto const& rtHitSbtPlan = context.buildFrameData<SceneRtHitSbtPlan>(rtHitSbtPlanHandle);
    if (!rtHitSbtPlan.valid())
    {
        auto clearUses = std::array{
            nr::renderer::use::imageTransferDst(output),
        };
        [[maybe_unused]] auto clearPass = context.addPass(
            std::span<const nr::renderer::PassResourceUseDesc>{clearUses.data(), clearUses.size()},
            "PathTracing.ClearInvalidHitSbtPlan",
            [output](const nr::renderer::PassRecordContext& recordContext) {
                nr::nrAssert(recordContext.commandBuffer.has_value(), "PathTracing hit SBT plan clear requires RAII command buffer access.");
                nr::nrAssert(static_cast<bool>(recordContext.resolveImage), "PathTracing hit SBT plan clear requires image resolver.");
                auto resolvedOutput = recordContext.resolveImage(output);
                nr::nrAssert(resolvedOutput.has_value(), "PathTracing hit SBT plan clear failed to resolve output image.");

                auto clearColor = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}};
                recordContext.commandBuffer->get().clearColorImage(
                    resolvedOutput->image,
                    vk::ImageLayout::eTransferDstOptimal,
                    clearColor,
                    resolvedOutput->subresourceRange);
            });
        return;
    }

    auto& activeRuntime = detail::ensurePathTracingVariantRuntime(
        *runtime_,
        device_->get(),
        input.variant,
        rtHitSbtPlan);

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
