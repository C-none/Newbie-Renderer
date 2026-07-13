module nr.renderPasses;
import dependency.vulkan;
import dependency.shaderShare;

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
using PathTracingChsVariantKey = RtBsdfVariantKey;

struct PathTracingHitProgram
{
    PathTracingChsVariantKey key{};
    nr::rhi::SlangProgram program{};
};

struct PathTracingPipelineKey
{
    PathTracingVariantKey rootVariant{};
    std::uint64_t chsPermutationSetHash = 0u;

    [[nodiscard]] friend auto operator<=>(const PathTracingPipelineKey &, const PathTracingPipelineKey &) noexcept = default;
};

struct PathTracingSbtKey
{
    PathTracingPipelineKey pipeline{};
    std::uint64_t hitRecordPlanHash = 0u;

    [[nodiscard]] friend auto operator<=>(const PathTracingSbtKey &, const PathTracingSbtKey &) noexcept = default;
};

struct PathTracingFrameRuntime
{
    std::shared_ptr<nr::renderer::PipelineRuntime<nr::rhi::RayTracingPipeline>> pipeline{};
    std::reference_wrapper<nr::rhi::ShaderBindingTable> shaderBindingTable;
};

struct PathTracingRuntimeCache
{
    std::map<PathTracingPipelineKey, std::shared_ptr<nr::renderer::PipelineRuntime<nr::rhi::RayTracingPipeline>>> pipelines{};
    std::map<PathTracingSbtKey, nr::rhi::ShaderBindingTable> shaderBindingTables{};
};

enum class PathTracingUnavailableReason
{
    missingTlas,
    incompleteRtSideband,
    invalidHitSbtPlan,
};

struct PathTracingFrameInputs
{
    nr::renderer::GraphResourceHandle sceneTlas{};
    nr::renderer::GraphResourceHandle rtInstanceMetadata{};
    nr::renderer::GraphResourceHandle rtGeometryMetadata{};
    nr::renderer::GraphResourceHandle rtMaterialHeaders{};
    nr::renderer::GraphResourceHandle rtMaterialLayers{};
    nr::renderer::GraphResourceHandle rtMaterialTextureRefs{};
    nr::renderer::GraphResourceHandle rtVertexAtlas{};
    nr::renderer::GraphResourceHandle rtIndexAtlas{};
    nr::renderer::GraphResourceHandle sceneLightHeader{};
    nr::renderer::GraphResourceHandle sceneLights{};
    nr::renderer::GraphResourceHandle sceneLightAliasTable{};
    std::reference_wrapper<const SceneRtHitSbtPlan> hitSbtPlan;
};

inline constexpr std::string_view kMaxSurfaceBouncesVariantName = "kMaxSurfaceBounces";
inline constexpr std::string_view kRussianRoulettePolicyVariantName = "RussianRoulettePolicy";
inline constexpr std::string_view kRussianRoulettePolicyType = "IRussianRoulettePolicy";
inline constexpr std::string_view kRussianRouletteEnabledPolicy = "RussianRouletteEnabledPolicy";
inline constexpr std::string_view kRussianRouletteDisabledPolicy = "RussianRouletteDisabledPolicy";

[[nodiscard]] std::string_view pathTracingRussianRoulettePolicy(bool enabled) noexcept
{
    return enabled ? kRussianRouletteEnabledPolicy : kRussianRouletteDisabledPolicy;
}

[[nodiscard]] PathTracingVariantKey normalizePathTracingVariantKey(PathTracingVariantKey key) noexcept
{
    key.maxSurfaceBounces = std::clamp(key.maxSurfaceBounces, kPathTracingMinSurfaceBounces, kPathTracingMaxSurfaceBouncesLimit);
    return key;
}

[[nodiscard]] std::string describePathTracingVariantKey(const PathTracingVariantKey &key)
{
    auto normalizedKey = normalizePathTracingVariantKey(key);
    return std::format("PathTracing[maxBounces={},russianRoulette={}]", normalizedKey.maxSurfaceBounces, normalizedKey.enableRussianRoulette ? "enabled" : "disabled");
}

[[nodiscard]] std::string describePathTracingPipelineKey(const PathTracingPipelineKey &key)
{
    return std::format("{},chsPermutations={}", describePathTracingVariantKey(key.rootVariant), nr::hash::toHexString(key.chsPermutationSetHash));
}

[[nodiscard]] std::string describePathTracingSbtKey(const PathTracingSbtKey &key)
{
    return std::format("{},hitRecords={}", describePathTracingPipelineKey(key.pipeline), nr::hash::toHexString(key.hitRecordPlanHash));
}

[[nodiscard]] nr::rhi::SlangProgramVariantDesc makePathTracingVariantDesc(const PathTracingVariantKey &key)
{
    auto normalizedKey = normalizePathTracingVariantKey(key);
    auto variant = nr::rhi::SlangProgramVariantDesc{};
    variant.assign(kMaxSurfaceBouncesVariantName, "uint", normalizedKey.maxSurfaceBounces).assign(kRussianRoulettePolicyVariantName, kRussianRoulettePolicyType, std::string{pathTracingRussianRoulettePolicy(normalizedKey.enableRussianRoulette)});
    return variant;
}

[[nodiscard]] nr::rhi::SlangProgramVariantDesc makePathTracingChsVariantDesc(const PathTracingChsVariantKey &key)
{
    auto variant = nr::rhi::SlangProgramVariantDesc{};
    variant.assign("CHS", "ICHS", std::format("MaterialCHS<RtMaterialLayerFlag({}u)>", static_cast<std::uint32_t>(key.layerFlags)));
    return variant;
}

inline constexpr std::string_view kPathTracingRaygenGroupName = "raygen";
inline constexpr std::string_view kPathTracingMissGroupName = "miss";

[[nodiscard]] std::string pathTracingHitGroupName(const RtHitPermutationKey &key)
{
    return std::format("hit_{}", nr::hash::toHexString(hashRtHitPermutationKey(key)));
}

[[nodiscard]] nr::rhi::RayTracingPipelineDesc makePathTracingPipelineDesc()
{
    auto pipelineDesc = nr::rhi::RayTracingPipelineDesc{};
    pipelineDesc.descriptorBindingPolicy.defaultRuntimeDescriptorCount = nr::renderer::kSceneTextureDescriptorCapacity;
    return pipelineDesc;
}

[[nodiscard]] nr::rhi::RayTracingProgramAssemblyDesc makePathTracingProgramAssembly(
    const nr::rhi::SlangProgram &rootProgram,
    const SceneRtHitSbtPlan &hitSbtPlan,
    std::span<const PathTracingHitProgram> hitPrograms)
{
    auto const usesAnyHit = std::ranges::any_of(hitSbtPlan.permutations, [](const SceneRtHitSbtPermutation &permutation) { return rtHitPermutationUsesAnyHit(permutation.key); });

    auto assembly = nr::rhi::RayTracingProgramAssemblyDesc{};
    assembly.stages.reserve(2u + (usesAnyHit ? 1u : 0u) + hitPrograms.size());
    assembly.stages.push_back(nr::rhi::RayTracingPipelineStageSelection{
        .program = std::cref(rootProgram),
        .entryPointName = "rgMain",
        .logicalEntryPointName = "rgMain",
    });
    assembly.stages.push_back(nr::rhi::RayTracingPipelineStageSelection{
        .program = std::cref(rootProgram),
        .entryPointName = "msMain",
        .logicalEntryPointName = "msMain",
    });
    if (usesAnyHit)
    {
        assembly.stages.push_back(nr::rhi::RayTracingPipelineStageSelection{
            .program = std::cref(rootProgram),
            .entryPointName = "ahAlphaMask",
            .logicalEntryPointName = "ahAlphaMask",
        });
    }
    std::ranges::for_each(hitPrograms, [&](const PathTracingHitProgram &hitProgram) {
        assembly.stages.push_back(nr::rhi::RayTracingPipelineStageSelection{
            .program = std::cref(hitProgram.program),
            .entryPointName = "chMain",
            .logicalEntryPointName = rtHitClosestHitEntryPointName(hitProgram.key),
        });
    });

    assembly.groups.reserve(2u + hitSbtPlan.permutations.size());
    assembly.groups.push_back(nr::rhi::RayTracingShaderGroupDesc{
        .name = std::string{kPathTracingRaygenGroupName},
        .generalEntryPoint = "rgMain",
    });
    assembly.groups.push_back(nr::rhi::RayTracingShaderGroupDesc{
        .name = std::string{kPathTracingMissGroupName},
        .generalEntryPoint = "msMain",
    });
    std::ranges::for_each(hitSbtPlan.permutations, [&](const SceneRtHitSbtPermutation &permutation) {
        auto hitGroup = nr::rhi::RayTracingShaderGroupDesc{
            .name = pathTracingHitGroupName(permutation.key),
            .type = vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup,
            .closestHitEntryPoint = rtHitClosestHitEntryPointName(permutation.key.bsdf),
        };
        if (rtHitPermutationUsesAnyHit(permutation.key))
        {
            hitGroup.anyHitEntryPoint = "ahAlphaMask";
        }
        assembly.groups.push_back(std::move(hitGroup));
    });
    return assembly;
}

[[nodiscard]] std::vector<PathTracingHitProgram> compilePathTracingHitPrograms(
    nr::rhi::ShaderService &shaderService,
    const nr::rhi::SlangProgram &rootProgram,
    const nr::rhi::SlangProgramVariantDesc &variantDesc,
    PathTracingChsVariantKey baselineChsKey,
    const nr::rhi::SlangProgramVariantDesc &baselineChsVariantDesc,
    const nr::rhi::RayTracingPipelineDesc &pipelineDesc,
    const SceneRtHitSbtPlan &hitSbtPlan)
{
    auto hitPrograms = std::vector<PathTracingHitProgram>{};
    auto programIndexByKey = std::map<PathTracingChsVariantKey, std::uint32_t>{};
    hitPrograms.reserve(hitSbtPlan.permutations.size());
    hitPrograms.push_back(PathTracingHitProgram{
        .key = baselineChsKey,
        .program = rootProgram,
    });
    programIndexByKey.emplace(baselineChsKey, 0u);

    std::ranges::for_each(hitSbtPlan.permutations, [&](const SceneRtHitSbtPermutation &permutation) {
        auto const &chsKey = permutation.key.bsdf;
        if (programIndexByKey.contains(chsKey))
        {
            return;
        }

        auto chsVariantDesc = makePathTracingChsVariantDesc(chsKey);
        auto chsProgram = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/pathTracing"},
            .variant = variantDesc,
            .linkVariants = {chsVariantDesc},
        });
        nr::nrAssert(chsProgram.valid(), "Path tracing pass failed to compile CHS-specialized shader module.");
        if (chsVariantDesc.hashValue() != baselineChsVariantDesc.hashValue())
        {
            nr::rhi::assertShaderLayoutAbiStable(rootProgram, chsProgram, pipelineDesc.descriptorBindingPolicy, rtHitClosestHitEntryPointName(chsKey));
        }

        nr::nrAssert(
            hitPrograms.size() < static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()),
            "Path tracing CHS program count exceeds uint32 ABI.");
        auto const programIndex = static_cast<std::uint32_t>(hitPrograms.size());
        hitPrograms.push_back(PathTracingHitProgram{
            .key = chsKey,
            .program = std::move(chsProgram),
        });
        programIndexByKey.emplace(chsKey, programIndex);
    });

    return hitPrograms;
}

[[nodiscard]] std::shared_ptr<nr::renderer::PipelineRuntime<nr::rhi::RayTracingPipeline>> createPathTracingPipelineRuntime(nr::rhi::Device &device, const PathTracingPipelineKey &pipelineKey, const SceneRtHitSbtPlan &hitSbtPlan)
{
    nr::nrAssert(pipelineKey.chsPermutationSetHash == hitSbtPlan.permutationSetHash, "Path tracing pipeline key must match the hit SBT permutation set.");
    nr::nrAssert(!hitSbtPlan.permutations.empty(), "Path tracing pipeline creation requires at least one active hit permutation.");

    auto &shaderService = nr::rhi::ShaderService::instance();
    auto baselineVariantDesc = makePathTracingVariantDesc(PathTracingVariantKey{});
    auto const baselineChsKey = hitSbtPlan.permutations.front().key.bsdf;
    auto baselineChsVariantDesc = makePathTracingChsVariantDesc(baselineChsKey);
    auto baselineProgram = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
        .sourcePath = std::filesystem::path{"renderer/pathTracing"},
        .variant = baselineVariantDesc,
        .linkVariants = {baselineChsVariantDesc},
    });
    nr::nrAssert(baselineProgram.valid(), "Path tracing pass failed to compile baseline shader module.");

    auto variantDesc = makePathTracingVariantDesc(pipelineKey.rootVariant);
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

    auto pipelineDesc = makePathTracingPipelineDesc();
    if (variantDesc.hashValue() != baselineVariantDesc.hashValue())
    {
        nr::rhi::assertShaderLayoutAbiStable(baselineProgram, rootProgram, pipelineDesc.descriptorBindingPolicy, describePathTracingVariantKey(pipelineKey.rootVariant));
    }

    auto hitPrograms = compilePathTracingHitPrograms(
        shaderService,
        rootProgram,
        variantDesc,
        baselineChsKey,
        baselineChsVariantDesc,
        pipelineDesc,
        hitSbtPlan);
    auto programAssembly = makePathTracingProgramAssembly(rootProgram, hitSbtPlan, hitPrograms);

    auto pipelineRuntime = std::make_shared<nr::renderer::PipelineRuntime<nr::rhi::RayTracingPipeline>>();
    auto sceneTextureImmutableSamplers = std::array{sceneTextureTableImmutableSamplerBinding()};
    pipelineRuntime->initializeDeferred(device.pipeline().createRayTracingPipeline(rootProgram, programAssembly, pipelineDesc, 64u, sceneTextureImmutableSamplers));
    nr::nrAssert(pipelineRuntime->valid(), "Path tracing pass failed to create ray tracing pipeline.");
    nr::rhi::setPipelineDebugName(device.device, pipelineRuntime->pipeline().raw(), describePathTracingPipelineKey(pipelineKey) + ".Pipeline");

    return pipelineRuntime;
}

[[nodiscard]] nr::rhi::ShaderBindingTable createPathTracingShaderBindingTable(nr::rhi::Device &device, const std::shared_ptr<nr::renderer::PipelineRuntime<nr::rhi::RayTracingPipeline>> &pipelineRuntime, const PathTracingSbtKey &sbtKey, const SceneRtHitSbtPlan &hitSbtPlan)
{
    nr::nrAssert(sbtKey.pipeline.chsPermutationSetHash == hitSbtPlan.permutationSetHash, "Path tracing SBT key must match the hit SBT permutation set.");
    nr::nrAssert(sbtKey.hitRecordPlanHash == hitSbtPlan.recordPlanHash, "Path tracing SBT key must match the hit SBT record plan.");
    nr::nrAssert(pipelineRuntime && pipelineRuntime->valid(), "Path tracing SBT creation requires a valid ray tracing pipeline.");

    auto const &pipeline = pipelineRuntime->pipeline();
    auto hitRecords = hitSbtPlan.records | std::views::transform([&](const SceneRtHitSbtRecord &record) {
                          nr::nrAssert(
                              record.permutationIndex < hitSbtPlan.permutations.size(),
                              "Path tracing SBT record references an invalid hit permutation.");
                          return nr::rhi::ShaderBindingTableRecordDesc{
                              .groupIndex = pipeline.shaderGroupIndex(pathTracingHitGroupName(hitSbtPlan.permutations[record.permutationIndex].key)),
                          };
                      }) |
                      std::ranges::to<std::vector>();

    auto shaderBindingTable = nr::rhi::ShaderBindingTable::create(device.resourceFactory, nr::rhi::ShaderBindingTableBuildDesc{
                                                                                              .pipeline = pipeline,
                                                                                              .capabilities = device.rayTracingCapabilities(),
                                                                                              .raygen =
                                                                                                  nr::rhi::ShaderBindingTableSectionDesc{
                                                                                                      .firstGroup = pipeline.shaderGroupIndex(kPathTracingRaygenGroupName),
                                                                                                      .groupCount = 1u,
                                                                                                  },
                                                                                              .miss =
                                                                                                  nr::rhi::ShaderBindingTableSectionDesc{
                                                                                                      .firstGroup = pipeline.shaderGroupIndex(kPathTracingMissGroupName),
                                                                                                      .groupCount = 1u,
                                                                                                  },
                                                                                              .hit =
                                                                                                  nr::rhi::ShaderBindingTableSectionDesc{
                                                                                                      .records = std::span<const nr::rhi::ShaderBindingTableRecordDesc>{hitRecords.data(), hitRecords.size()},
                                                                                                  },
                                                                                              .debugName = describePathTracingSbtKey(sbtKey) + ".SBT",
                                                                                          });
    nr::nrAssert(shaderBindingTable.valid(), "Path tracing pass failed to create SBT.");

    return shaderBindingTable;
}

[[nodiscard]] std::shared_ptr<nr::renderer::PipelineRuntime<nr::rhi::RayTracingPipeline>> &ensurePathTracingPipelineRuntime(PathTracingRuntimeCache &cache, nr::rhi::Device &device, const PathTracingPipelineKey &pipelineKey, const SceneRtHitSbtPlan &hitSbtPlan)
{
    if (auto runtimeIt = cache.pipelines.find(pipelineKey); runtimeIt != cache.pipelines.end())
    {
        return runtimeIt->second;
    }

    auto [runtimeIt, inserted] = cache.pipelines.try_emplace(pipelineKey, createPathTracingPipelineRuntime(device, pipelineKey, hitSbtPlan));
    nr::nrAssert(inserted, "Path tracing pipeline runtime cache insertion failed.");
    return runtimeIt->second;
}

[[nodiscard]] nr::rhi::ShaderBindingTable &ensurePathTracingShaderBindingTable(PathTracingRuntimeCache &cache, nr::rhi::Device &device, const PathTracingSbtKey &sbtKey, const std::shared_ptr<nr::renderer::PipelineRuntime<nr::rhi::RayTracingPipeline>> &pipelineRuntime, const SceneRtHitSbtPlan &hitSbtPlan)
{
    if (auto sbtIt = cache.shaderBindingTables.find(sbtKey); sbtIt != cache.shaderBindingTables.end())
    {
        return sbtIt->second;
    }

    auto [sbtIt, inserted] = cache.shaderBindingTables.try_emplace(sbtKey, createPathTracingShaderBindingTable(device, pipelineRuntime, sbtKey, hitSbtPlan));
    nr::nrAssert(inserted, "Path tracing SBT cache insertion failed.");
    return sbtIt->second;
}

[[nodiscard]] PathTracingFrameRuntime ensurePathTracingFrameRuntime(PathTracingRuntimeCache &cache, nr::rhi::Device &device, const PathTracingVariantKey &key, const SceneRtHitSbtPlan &hitSbtPlan)
{
    auto pipelineKey = PathTracingPipelineKey{
        .rootVariant = normalizePathTracingVariantKey(key),
        .chsPermutationSetHash = hitSbtPlan.permutationSetHash,
    };
    auto &pipelineRuntime = ensurePathTracingPipelineRuntime(cache, device, pipelineKey, hitSbtPlan);
    auto sbtKey = PathTracingSbtKey{
        .pipeline = pipelineKey,
        .hitRecordPlanHash = hitSbtPlan.recordPlanHash,
    };
    auto &shaderBindingTable = ensurePathTracingShaderBindingTable(cache, device, sbtKey, pipelineRuntime, hitSbtPlan);

    return PathTracingFrameRuntime{
        .pipeline = pipelineRuntime,
        .shaderBindingTable = std::ref(shaderBindingTable),
    };
}

[[nodiscard]] std::shared_ptr<PathTracingRuntimeCache> makePathTracingRuntimeCache()
{
    auto cache = std::make_shared<PathTracingRuntimeCache>();
    return cache;
}

[[nodiscard]] std::string_view pathTracingUnavailableReasonName(PathTracingUnavailableReason reason) noexcept
{
    switch (reason)
    {
    case PathTracingUnavailableReason::missingTlas:
        return "MissingTLAS";
    case PathTracingUnavailableReason::incompleteRtSideband:
        return "IncompleteRTSideband";
    case PathTracingUnavailableReason::invalidHitSbtPlan:
        return "InvalidHitSbtPlan";
    }
    nr::nrAssert(false, "Path tracing unavailable reason is unrecognized.");
    return "Unknown";
}

[[nodiscard]] std::expected<PathTracingFrameInputs, PathTracingUnavailableReason> resolvePathTracingFrameInputs(
    const NodeBuildContext &context)
{
    auto sceneTlas = context.resolveFrameResource(nr::renderer::frameResource::sceneTlas);
    if (!sceneTlas.valid())
    {
        return std::unexpected(PathTracingUnavailableReason::missingTlas);
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
    auto sidebandResources = std::array{
        rtInstanceMetadata,
        rtGeometryMetadata,
        rtMaterialHeaders,
        rtMaterialLayers,
        rtMaterialTextureRefs,
        rtVertexAtlas,
        rtIndexAtlas,
        sceneLightHeader,
        sceneLights,
        sceneLightAliasTable,
    };
    if (!rtHitSbtPlanHandle.valid() || std::ranges::any_of(sidebandResources, [](nr::renderer::GraphResourceHandle resource) { return !resource.valid(); }))
    {
        return std::unexpected(PathTracingUnavailableReason::incompleteRtSideband);
    }

    auto rtHitSbtPlan = context.resolveBuildFrameData<SceneRtHitSbtPlan>(rtHitSbtPlanHandle);
    if (!rtHitSbtPlan.has_value())
    {
        return std::unexpected(PathTracingUnavailableReason::incompleteRtSideband);
    }
    if (!rtHitSbtPlan->get().valid())
    {
        return std::unexpected(PathTracingUnavailableReason::invalidHitSbtPlan);
    }

    return PathTracingFrameInputs{
        .sceneTlas = sceneTlas,
        .rtInstanceMetadata = rtInstanceMetadata,
        .rtGeometryMetadata = rtGeometryMetadata,
        .rtMaterialHeaders = rtMaterialHeaders,
        .rtMaterialLayers = rtMaterialLayers,
        .rtMaterialTextureRefs = rtMaterialTextureRefs,
        .rtVertexAtlas = rtVertexAtlas,
        .rtIndexAtlas = rtIndexAtlas,
        .sceneLightHeader = sceneLightHeader,
        .sceneLights = sceneLights,
        .sceneLightAliasTable = sceneLightAliasTable,
        .hitSbtPlan = *rtHitSbtPlan,
    };
}

void clearUnavailableOutput(
    NodeBuildContext &context,
    nr::renderer::GraphResourceHandle output,
    PathTracingUnavailableReason reason)
{
    [[maybe_unused]] auto clearPass = nr::renderer::ops::clearColorImage(
        context,
        std::format("PathTracing.ClearUnavailable.{}", pathTracingUnavailableReasonName(reason)),
        nr::renderer::ops::ClearColorImagePassDesc{
            .image = output,
            .value = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}},
        });
}
} // namespace nr::renderPasses::detail

namespace nr::renderPasses
{
PathTracingNode::~PathTracingNode() = default;

void PathTracingNode::initialize(NodeInitContext &context)
{
    device_ = context.device;
    input.variant = detail::normalizePathTracingVariantKey(input.variant);
    variantUiDraft_ = input.variant;
    pendingVariant_.reset();
    runtime_ = detail::makePathTracingRuntimeCache();
}

void PathTracingNode::collectUi(NodeUiBuildContext &context, const NodeFrameParameters &)
{
    if (pendingVariant_.has_value())
    {
        input.variant = detail::normalizePathTracingVariantKey(*pendingVariant_);
        pendingVariant_.reset();
    }

    variantUiDraft_ = detail::normalizePathTracingVariantKey(input.variant);

    context.addSection(
        context.runtimeName(),
        [this](NodeUiWriter &ui) {
            auto changed = false;
            auto maxSurfaceBounces = variantUiDraft_.maxSurfaceBounces;
            if (ui.inputUInt("Max Bounces", maxSurfaceBounces, kPathTracingMinSurfaceBounces, kPathTracingMaxSurfaceBouncesLimit))
            {
                maxSurfaceBounces = std::clamp(maxSurfaceBounces, kPathTracingMinSurfaceBounces, kPathTracingMaxSurfaceBouncesLimit);
                variantUiDraft_.maxSurfaceBounces = maxSurfaceBounces;
                changed = true;
            }

            auto enableRussianRoulette = variantUiDraft_.enableRussianRoulette;
            if (ui.checkbox("Russian Roulette", enableRussianRoulette))
            {
                variantUiDraft_.enableRussianRoulette = enableRussianRoulette;
                changed = true;
            }

            if (changed)
            {
                pendingVariant_ = detail::normalizePathTracingVariantKey(variantUiDraft_);
            }
        },
        true, "controls");
}

void PathTracingNode::build(NodeBuildContext &context, const NodeFrameParameters &frameParameters)
{
    nr::nrAssert(static_cast<bool>(runtime_), "PathTracing build stage requires initialized runtime state.");
    nr::nrAssert(device_.has_value(), "PathTracing build stage requires device reference from initialize stage.");
    input.variant = detail::normalizePathTracingVariantKey(input.variant);

    auto viewportExtent = frameParameters.swapchainExtent;
    viewportExtent.width = std::max(1u, viewportExtent.width);
    viewportExtent.height = std::max(1u, viewportExtent.height);

    auto output = context.addResource(nr::renderer::GraphTransientImageDesc{
        .debugName = "PathTracing.Output",
        .extent = vk::Extent3D{viewportExtent.width, viewportExtent.height, 1u},
        .format = input.outputFormat,
        .usageIntents =
            {
                nr::renderer::ImageUsageIntent::StorageWrite,
                nr::renderer::ImageUsageIntent::Sampled,
                nr::renderer::ImageUsageIntent::TransferDst,
                nr::renderer::ImageUsageIntent::TransferSrc,
            },
        .initialLayout = nr::renderer::ImageLayoutIntent::Undefined,
    });
    context.publishFrameResource(nr::renderer::frameResource::presentSourceColor, output);

    auto frameInputs = detail::resolvePathTracingFrameInputs(context);
    if (!frameInputs.has_value())
    {
        detail::clearUnavailableOutput(context, output, frameInputs.error());
        return;
    }
    auto const &inputs = *frameInputs;
    auto activeRuntime = detail::ensurePathTracingFrameRuntime(*runtime_, device_->get(), input.variant, inputs.hitSbtPlan.get());

    auto sbtResource = context.importBuffer(activeRuntime.shaderBindingTable.get().buffer(), "PathTracing.SBT", nr::renderer::ResourceLifetime::RendererPersistent,
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
    auto &bindlessImageTableCache = context.globalResources.get().bindlessImageTableCache.get();

    auto tracePass = nr::renderer::RayTracingPassBuilder{context, "PathTracing.Trace", activeRuntime.pipeline};
    tracePass.accelerationStructure("scene", inputs.sceneTlas, "PathTracing.SceneTLAS")
        .storageImage("outputImage", output, "PathTracing.Output")
        .storageBuffer("rtInstanceMetadata", inputs.rtInstanceMetadata, "PathTracing.InstanceMetadata")
        .storageBuffer("rtGeometryMetadata", inputs.rtGeometryMetadata, "PathTracing.GeometryMetadata")
        .storageBuffer("rtMaterialHeaders", inputs.rtMaterialHeaders, "PathTracing.MaterialHeaders")
        .storageBuffer("rtMaterialLayers", inputs.rtMaterialLayers, "PathTracing.MaterialLayers")
        .storageBuffer("rtMaterialTextureRefs", inputs.rtMaterialTextureRefs, "PathTracing.MaterialTextureRefs")
        .storageBuffer("rtVertexData", inputs.rtVertexAtlas, "PathTracing.VertexAtlas")
        .storageBuffer("rtIndexData", inputs.rtIndexAtlas, "PathTracing.IndexAtlas")
        .uniform("gFrame", context.globalResources.get().frameUniform, "Renderer.GlobalFrameUniforms")
        .resourceUse(nr::renderer::use::shaderBindingTableRead(sbtResource))
        .uniform("gSceneLightHeader", inputs.sceneLightHeader, "PathTracing.SceneLightHeader")
        .storageBuffer("gSceneLights", inputs.sceneLights, "PathTracing.SceneLights")
        .storageBuffer("gSceneLightAliasTable", inputs.sceneLightAliasTable, "PathTracing.SceneLightAliasTable")
        .prepare([pipeline = activeRuntime.pipeline, sceneTextureTableBinding, cache = std::ref(bindlessImageTableCache)](const nr::renderer::PassPrepareContext &prepareContext) {
            detail::prepareSceneTextureTableBindingForFrame(*pipeline, cache.get(), prepareContext.frameIndex, sceneTextureTableBinding, detail::SceneTextureTableBindingRequirement::optional);
        })
        .dynamicBindingSnapshot([pipeline = activeRuntime.pipeline, sceneTextureTableBinding, cache = std::ref(bindlessImageTableCache)](const nr::renderer::PassPrepareContext &prepareContext) {
            return detail::makeSceneTextureTableBindingSnapshot(*pipeline, cache.get(), prepareContext.frameIndex, sceneTextureTableBinding, detail::SceneTextureTableBindingRequirement::optional);
        })
        .record([pipeline = activeRuntime.pipeline, shaderBindingTable = activeRuntime.shaderBindingTable, dimensions, queueRole, device = std::cref(device_->get())](const nr::renderer::RayTracingPassRecordContext &rayContext) {
            nr::rhi::traceRays(rayContext.commandBuffer,
                               nr::rhi::TraceRaysDesc{
                                   .pipeline = pipeline->pipeline(),
                                   .shaderBindingTable = shaderBindingTable.get(),
                                   .dimensions = dimensions,
                                   .recordingQueueRole = queueRole,
                               },
                               device.get().rayTracingCapabilities());
        });

    [[maybe_unused]] auto tracePassHandle = tracePass.build();
}

void PathTracingNode::shutdown(NodeShutdownContext &)
{
    if (runtime_)
    {
        std::ranges::for_each(runtime_->pipelines | std::views::values, [](std::shared_ptr<nr::renderer::PipelineRuntime<nr::rhi::RayTracingPipeline>> &pipelineRuntime) {
            if (pipelineRuntime)
            {
                pipelineRuntime->clearBindingSets();
            }
        });
    }
    runtime_.reset();
    device_.reset();
}
} // namespace nr::renderPasses
