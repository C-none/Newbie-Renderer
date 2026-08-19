module nr.renderPasses;
import dependency.vulkan;
import dependency.shaderShare;

import :pathTracing;
import :rtHitSbtPlan;
import :sceneTextureTableBinding;
import nr.options;
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

struct PathTracingProgramBatch
{
    nr::rhi::SlangProgram raygen{};
    nr::rhi::SlangProgram materialMiss{};
    nr::rhi::SlangProgram shadowMiss{};
    nr::rhi::SlangProgram shadowAnyHit{};
    std::optional<nr::rhi::SlangProgram> materialAnyHit{};
    std::vector<PathTracingHitProgram> hitPrograms{};
};

struct PathTracingPipelineKey
{
    PathTracingVariantKey variant{};
    std::uint64_t chsPermutationSetHash = 0u;
    std::uint64_t shaderSessionGeneration = 0u;

    [[nodiscard]] friend auto operator<=>(const PathTracingPipelineKey &,
                                          const PathTracingPipelineKey &) noexcept = default;
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

enum class PathTracingGuideResource : std::size_t
{
    color,
    depth,
    diffuseAlbedo,
    specularAlbedo,
    normalRoughness,
    motionVectors,
    specularHitDistance,
    count,
};

inline constexpr auto kPathTracingGuideResourceCount = static_cast<std::size_t>(PathTracingGuideResource::count);

struct PathTracingGuideSpec
{
    std::string_view debugName{};
    std::string_view shaderBinding{};
    vk::Format format = vk::Format::eUndefined;
    nr::rhi::DlssRayReconstructionResourceSlot dlssSlot{};
    std::array<float, 4u> unavailableClear{};
};

struct PathTracingGuideFrameSlot
{
    std::array<nr::rhi::Image, kPathTracingGuideResourceCount> images{};
    std::array<nr::renderer::RetainedImageState, kPathTracingGuideResourceCount> states{};
    vk::Extent2D allocatedExtent{};
    vk::Format allocatedColorFormat = vk::Format::eUndefined;
};

struct PathTracingRuntimeCache
{
    std::map<PathTracingPipelineKey, std::shared_ptr<nr::renderer::PipelineRuntime<nr::rhi::RayTracingPipeline>>>
        pipelines{};
    std::map<PathTracingSbtKey, nr::rhi::ShaderBindingTable> shaderBindingTables{};
    std::array<PathTracingGuideFrameSlot, nr::maxFrameInFlight> guideFrameSlots{};
};

[[nodiscard]] constexpr std::size_t guideIndex(PathTracingGuideResource resource) noexcept
{
    return static_cast<std::size_t>(resource);
}

[[nodiscard]] std::array<PathTracingGuideSpec, kPathTracingGuideResourceCount> makePathTracingGuideSpecs(
    vk::Format colorFormat)
{
    using DlssSlot = nr::rhi::DlssRayReconstructionResourceSlot;
    return {
        PathTracingGuideSpec{
            .debugName = "Color",
            .shaderBinding = "outputImage",
            .format = colorFormat,
            .dlssSlot = DlssSlot::Color,
            .unavailableClear = {0.0f, 0.0f, 0.0f, 1.0f},
        },
        PathTracingGuideSpec{
            .debugName = "Depth",
            .shaderBinding = "depthImage",
            .format = vk::Format::eR32Sfloat,
            .dlssSlot = DlssSlot::Depth,
            .unavailableClear = {1.0f, 0.0f, 0.0f, 0.0f},
        },
        PathTracingGuideSpec{
            .debugName = "DiffuseAlbedo",
            .shaderBinding = "diffuseAlbedoImage",
            .format = vk::Format::eR16G16B16A16Sfloat,
            .dlssSlot = DlssSlot::DiffuseAlbedo,
            .unavailableClear = {0.0f, 0.0f, 0.0f, 1.0f},
        },
        PathTracingGuideSpec{
            .debugName = "SpecularAlbedo",
            .shaderBinding = "specularAlbedoImage",
            .format = vk::Format::eR16G16B16A16Sfloat,
            .dlssSlot = DlssSlot::SpecularAlbedo,
            .unavailableClear = {0.5f, 0.5f, 0.5f, 1.0f},
        },
        PathTracingGuideSpec{
            .debugName = "NormalRoughness",
            .shaderBinding = "normalRoughnessImage",
            .format = vk::Format::eR16G16B16A16Sfloat,
            .dlssSlot = DlssSlot::Normals,
            .unavailableClear = {0.0f, 0.0f, 1.0f, 1.0f},
        },
        PathTracingGuideSpec{
            .debugName = "MotionVectors",
            .shaderBinding = "motionVectorsImage",
            .format = vk::Format::eR16G16Sfloat,
            .dlssSlot = DlssSlot::MotionVectors,
        },
        PathTracingGuideSpec{
            .debugName = "SpecularHitDistance",
            .shaderBinding = "specularHitDistanceImage",
            .format = vk::Format::eR16Sfloat,
            .dlssSlot = DlssSlot::SpecularHitDistance,
        },
    };
}

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
    std::shared_ptr<const SceneRtHitSbtPlan> hitSbtPlan{};
};

inline constexpr std::string_view kMaxSurfaceBouncesVariantName = "kMaxSurfaceBounces";
inline constexpr std::string_view kFilterAfterShadingVariantName = "kEnableFilterAfterShading";
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
    key.maxSurfaceBounces =
        std::clamp(key.maxSurfaceBounces, kPathTracingMinSurfaceBounces, kPathTracingMaxSurfaceBouncesLimit);
    return key;
}

[[nodiscard]] std::string describePathTracingVariantKey(const PathTracingVariantKey &key)
{
    auto normalizedKey = normalizePathTracingVariantKey(key);
    return std::format("PathTracing[maxBounces={},russianRoulette={},filterAfterShading={}]",
                       normalizedKey.maxSurfaceBounces, normalizedKey.enableRussianRoulette ? "enabled" : "disabled",
                       normalizedKey.enableFilterAfterShading ? "enabled" : "disabled");
}

[[nodiscard]] std::string describePathTracingPipelineKey(const PathTracingPipelineKey &key)
{
    return std::format("{},chsPermutations={},shaderSession={}", describePathTracingVariantKey(key.variant),
                       nr::hash::toHexString(key.chsPermutationSetHash), key.shaderSessionGeneration);
}

[[nodiscard]] std::string describePathTracingSbtKey(const PathTracingSbtKey &key)
{
    return std::format("{},hitRecords={}", describePathTracingPipelineKey(key.pipeline),
                       nr::hash::toHexString(key.hitRecordPlanHash));
}

[[nodiscard]] nr::rhi::SlangProgramVariantDesc makePathTracingRaygenVariantDesc(const PathTracingVariantKey &key)
{
    auto normalizedKey = normalizePathTracingVariantKey(key);
    auto variant = nr::rhi::SlangProgramVariantDesc{};
    variant.assign(kMaxSurfaceBouncesVariantName, "uint", normalizedKey.maxSurfaceBounces)
        .assign(kRussianRoulettePolicyVariantName, kRussianRoulettePolicyType,
                std::string{pathTracingRussianRoulettePolicy(normalizedKey.enableRussianRoulette)});
    return variant;
}

[[nodiscard]] nr::rhi::SlangProgramVariantDesc makePathTracingClosestHitVariantDesc(
    const PathTracingVariantKey &variantKey, const PathTracingChsVariantKey &chsKey)
{
    auto variant = nr::rhi::SlangProgramVariantDesc{};
    variant
        .assign(kFilterAfterShadingVariantName, "bool",
                normalizePathTracingVariantKey(variantKey).enableFilterAfterShading)
        .assign("CHS", "ICHS",
                std::format("MaterialCHS<RtMaterialLayerFlag({}u)>", static_cast<std::uint32_t>(chsKey.layerFlags)));
    return variant;
}

inline constexpr std::string_view kPathTracingRaygenGroupName = "raygen";
inline constexpr std::string_view kPathTracingMaterialMissGroupName = "miss_material";
inline constexpr std::string_view kPathTracingShadowMissGroupName = "miss_shadow";
inline constexpr std::string_view kPathTracingShadowHitGroupName = "hit_shadow";

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
    const PathTracingProgramBatch &programs, const SceneRtHitSbtPlan &hitSbtPlan)
{
    auto assembly = nr::rhi::RayTracingProgramAssemblyDesc{};
    assembly.stages.reserve(4u + (programs.materialAnyHit.has_value() ? 1u : 0u) + programs.hitPrograms.size());
    assembly.stages.push_back(nr::rhi::RayTracingPipelineStageSelection{
        .program = std::cref(programs.raygen),
        .logicalEntryPointName = "rgMain",
    });
    assembly.stages.push_back(nr::rhi::RayTracingPipelineStageSelection{
        .program = std::cref(programs.materialMiss),
        .logicalEntryPointName = "msMaterial",
    });
    assembly.stages.push_back(nr::rhi::RayTracingPipelineStageSelection{
        .program = std::cref(programs.shadowMiss),
        .logicalEntryPointName = "msShadow",
    });
    assembly.stages.push_back(nr::rhi::RayTracingPipelineStageSelection{
        .program = std::cref(programs.shadowAnyHit),
        .logicalEntryPointName = "ahShadow",
    });
    if (programs.materialAnyHit.has_value())
    {
        assembly.stages.push_back(nr::rhi::RayTracingPipelineStageSelection{
            .program = std::cref(*programs.materialAnyHit),
            .logicalEntryPointName = "ahMaterialPolicy",
        });
    }
    std::ranges::for_each(programs.hitPrograms, [&](const PathTracingHitProgram &hitProgram) {
        assembly.stages.push_back(nr::rhi::RayTracingPipelineStageSelection{
            .program = std::cref(hitProgram.program),
            .logicalEntryPointName = rtHitClosestHitEntryPointName(hitProgram.key),
        });
    });

    assembly.groups.reserve(4u + hitSbtPlan.permutations.size());
    assembly.groups.push_back(nr::rhi::RayTracingShaderGroupDesc{
        .name = std::string{kPathTracingRaygenGroupName},
        .generalEntryPoint = "rgMain",
    });
    assembly.groups.push_back(nr::rhi::RayTracingShaderGroupDesc{
        .name = std::string{kPathTracingMaterialMissGroupName},
        .generalEntryPoint = "msMaterial",
    });
    assembly.groups.push_back(nr::rhi::RayTracingShaderGroupDesc{
        .name = std::string{kPathTracingShadowMissGroupName},
        .generalEntryPoint = "msShadow",
    });
    assembly.groups.push_back(nr::rhi::RayTracingShaderGroupDesc{
        .name = std::string{kPathTracingShadowHitGroupName},
        .type = vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup,
        .anyHitEntryPoint = "ahShadow",
    });
    std::ranges::for_each(hitSbtPlan.permutations, [&](const SceneRtHitSbtPermutation &permutation) {
        auto hitGroup = nr::rhi::RayTracingShaderGroupDesc{
            .name = pathTracingHitGroupName(permutation.key),
            .type = vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup,
            .closestHitEntryPoint = rtHitClosestHitEntryPointName(permutation.key.bsdf),
        };
        if (rtHitPermutationUsesAnyHit(permutation.key))
        {
            hitGroup.anyHitEntryPoint = "ahMaterialPolicy";
        }
        assembly.groups.push_back(std::move(hitGroup));
    });
    return assembly;
}

[[nodiscard]] PathTracingProgramBatch compilePathTracingPrograms(nr::rhi::ShaderService &shaderService,
                                                                 const nr::rhi::RayTracingPipelineDesc &pipelineDesc,
                                                                 const PathTracingVariantKey &variantKey,
                                                                 const SceneRtHitSbtPlan &hitSbtPlan)
{
    auto chsKeys =
        hitSbtPlan.permutations |
        std::views::transform([](const SceneRtHitSbtPermutation &permutation) { return permutation.key.bsdf; }) |
        std::ranges::to<std::vector>();
    std::ranges::sort(chsKeys);
    chsKeys.erase(std::ranges::unique(chsKeys).begin(), chsKeys.end());

    auto requests = std::vector<nr::rhi::SlangProgramCompileFileRequest>{};
    requests.reserve(6u + chsKeys.size());
    auto appendRequest = [&](std::filesystem::path sourcePath, nr::rhi::SlangProgramVariantDesc variant = {}) {
        auto const index = requests.size();
        requests.push_back(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::move(sourcePath),
            .variant = std::move(variant),
        });
        return index;
    };

    auto const baselineRaygenVariant = makePathTracingRaygenVariantDesc(PathTracingVariantKey{});
    auto const raygenVariant = makePathTracingRaygenVariantDesc(variantKey);
    auto const baselineRaygenIndex =
        appendRequest(std::filesystem::path{"renderer/pathTracing/raygen"}, baselineRaygenVariant);
    auto const raygenIndex = raygenVariant.hashValue() == baselineRaygenVariant.hashValue()
                                 ? baselineRaygenIndex
                                 : appendRequest(std::filesystem::path{"renderer/pathTracing/raygen"}, raygenVariant);
    auto const materialMissIndex = appendRequest(std::filesystem::path{"renderer/pathTracing/miss"});
    auto const shadowMissIndex = appendRequest(std::filesystem::path{"renderer/pathTracing/shadowMiss"});
    auto const shadowAnyHitIndex = appendRequest(std::filesystem::path{"renderer/pathTracing/shadowAnyHit"});
    auto const usesMaterialAnyHit =
        std::ranges::any_of(hitSbtPlan.permutations, [](const SceneRtHitSbtPermutation &permutation) {
            return rtHitPermutationUsesAnyHit(permutation.key);
        });
    auto const materialAnyHitIndex =
        usesMaterialAnyHit ? std::optional{appendRequest(std::filesystem::path{"renderer/pathTracing/anyHit"})}
                           : std::optional<std::size_t>{};
    auto const firstClosestHitIndex = requests.size();
    std::ranges::for_each(chsKeys, [&](const PathTracingChsVariantKey &chsKey) {
        appendRequest(std::filesystem::path{"renderer/pathTracing/closestHit"},
                      makePathTracingClosestHitVariantDesc(variantKey, chsKey));
    });

    auto compiledPrograms = shaderService.compileProgramsByFile(requests);
    nr::nrAssert(compiledPrograms.size() == requests.size() &&
                     std::ranges::all_of(compiledPrograms, &nr::rhi::SlangProgram::valid),
                 "Path tracing pass failed to compile its shader batch.");
    nr::nrAssert(compiledPrograms[baselineRaygenIndex].entryPoint()->stage == SLANG_STAGE_RAY_GENERATION &&
                     compiledPrograms[raygenIndex].entryPoint()->stage == SLANG_STAGE_RAY_GENERATION,
                 "Path tracing ray-generation files must each define one ray-generation entrypoint.");
    nr::nrAssert(compiledPrograms[materialMissIndex].entryPoint()->stage == SLANG_STAGE_MISS,
                 "Path tracing material miss file must define one miss entrypoint.");
    nr::nrAssert(compiledPrograms[shadowMissIndex].entryPoint()->stage == SLANG_STAGE_MISS,
                 "Path tracing shadow miss file must define one miss entrypoint.");
    nr::nrAssert(compiledPrograms[shadowAnyHitIndex].entryPoint()->stage == SLANG_STAGE_ANY_HIT,
                 "Path tracing shadow any-hit file must define one any-hit entrypoint.");
    if (materialAnyHitIndex.has_value())
    {
        nr::nrAssert(compiledPrograms[*materialAnyHitIndex].entryPoint()->stage == SLANG_STAGE_ANY_HIT,
                     "Path tracing material any-hit file must define one any-hit entrypoint.");
    }
    nr::nrAssert(std::ranges::all_of(std::views::iota(firstClosestHitIndex, compiledPrograms.size()),
                                     [&](std::size_t index) {
                                         return compiledPrograms[index].entryPoint()->stage == SLANG_STAGE_CLOSEST_HIT;
                                     }),
                 "Path tracing closest-hit files must each define one closest-hit entrypoint.");

    if (raygenIndex != baselineRaygenIndex)
    {
        nr::rhi::assertShaderLayoutAbiStable(compiledPrograms[baselineRaygenIndex], compiledPrograms[raygenIndex],
                                             pipelineDesc.descriptorBindingPolicy,
                                             describePathTracingVariantKey(variantKey));
    }
    if (!chsKeys.empty())
    {
        auto const &baselineClosestHit = compiledPrograms[firstClosestHitIndex];
        auto const variantIndices = std::views::iota(firstClosestHitIndex + 1u, compiledPrograms.size());
        std::ranges::for_each(variantIndices, [&](std::size_t index) {
            auto const keyIndex = index - firstClosestHitIndex;
            nr::rhi::assertShaderLayoutAbiStable(baselineClosestHit, compiledPrograms[index],
                                                 pipelineDesc.descriptorBindingPolicy,
                                                 rtHitClosestHitEntryPointName(chsKeys[keyIndex]));
        });
    }

    auto result = PathTracingProgramBatch{
        .raygen = compiledPrograms[raygenIndex],
        .materialMiss = compiledPrograms[materialMissIndex],
        .shadowMiss = compiledPrograms[shadowMissIndex],
        .shadowAnyHit = compiledPrograms[shadowAnyHitIndex],
    };
    if (materialAnyHitIndex.has_value())
    {
        result.materialAnyHit.emplace(compiledPrograms[*materialAnyHitIndex]);
    }
    result.hitPrograms = std::views::iota(std::size_t{0u}, chsKeys.size()) |
                         std::views::transform([&](std::size_t index) {
                             return PathTracingHitProgram{
                                 .key = chsKeys[index],
                                 .program = compiledPrograms[firstClosestHitIndex + index],
                             };
                         }) |
                         std::ranges::to<std::vector>();
    return result;
}

[[nodiscard]] std::shared_ptr<nr::renderer::PipelineRuntime<nr::rhi::RayTracingPipeline>>
createPathTracingPipelineRuntime(nr::rhi::Device &device, const PathTracingPipelineKey &pipelineKey,
                                 const SceneRtHitSbtPlan &hitSbtPlan)
{
    nr::nrAssert(pipelineKey.chsPermutationSetHash == hitSbtPlan.permutationSetHash,
                 "Path tracing pipeline key must match the hit SBT permutation set.");
    nr::nrAssert(!hitSbtPlan.permutations.empty(),
                 "Path tracing pipeline creation requires at least one active hit permutation.");

    auto pipelineDesc = makePathTracingPipelineDesc();
    auto programs =
        compilePathTracingPrograms(nr::rhi::ShaderService::instance(), pipelineDesc, pipelineKey.variant, hitSbtPlan);
    auto programAssembly = makePathTracingProgramAssembly(programs, hitSbtPlan);

    auto descriptorLayout =
        nr::rhi::ShaderDescriptorLayout::create(programs.raygen, pipelineDesc.descriptorBindingPolicy);
    nr::nrAssert(descriptorLayout.valid(), "Path tracing environment sampler reflection failed.");
    auto environmentSampler =
        descriptorLayout.rootCursor()["gEnvironmentMap"].makeImmutableSamplerBinding(nr::rhi::SlangSamplerDesc{
            .magFilter = vk::Filter::eLinear,
            .minFilter = vk::Filter::eLinear,
            .mipmapMode = vk::SamplerMipmapMode::eLinear,
            .addressModeU = vk::SamplerAddressMode::eRepeat,
            .addressModeV = vk::SamplerAddressMode::eClampToEdge,
            .addressModeW = vk::SamplerAddressMode::eClampToEdge,
            .minLod = 0.0f,
            .maxLod = 0.0f,
        });
    nr::nrAssert(environmentSampler.has_value(), "Path tracing gEnvironmentMap must support an immutable sampler.");

    auto pipelineRuntime = std::make_shared<nr::renderer::PipelineRuntime<nr::rhi::RayTracingPipeline>>();
    auto immutableSamplers = std::array{
        sceneTextureTableImmutableSamplerBinding(),
        *environmentSampler,
    };
    pipelineRuntime->initializeDeferred(device.pipeline().createRayTracingPipeline(
        programs.raygen, programAssembly, pipelineDesc, 64u, immutableSamplers,
        describePathTracingPipelineKey(pipelineKey) + ".Pipeline"));

    return pipelineRuntime;
}

[[nodiscard]] nr::rhi::ShaderBindingTable createPathTracingShaderBindingTable(
    nr::rhi::Device &device,
    const std::shared_ptr<nr::renderer::PipelineRuntime<nr::rhi::RayTracingPipeline>> &pipelineRuntime,
    const PathTracingSbtKey &sbtKey, const SceneRtHitSbtPlan &hitSbtPlan)
{
    nr::nrAssert(sbtKey.pipeline.chsPermutationSetHash == hitSbtPlan.permutationSetHash,
                 "Path tracing SBT key must match the hit SBT permutation set.");
    nr::nrAssert(sbtKey.hitRecordPlanHash == hitSbtPlan.recordPlanHash,
                 "Path tracing SBT key must match the hit SBT record plan.");
    nr::nrAssert(pipelineRuntime && pipelineRuntime->valid(),
                 "Path tracing SBT creation requires a valid ray tracing pipeline.");

    auto const &pipeline = pipelineRuntime->pipeline();
    nr::nrAssert(hitSbtPlan.records.size() <=
                     static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()),
                 "Path tracing logical hit SBT record count exceeds uint32 ABI.");
    auto const physicalHitRecordCount =
        rtPhysicalHitRecordCount(static_cast<std::uint32_t>(hitSbtPlan.records.size()));
    nr::nrAssert(physicalHitRecordCount <= std::numeric_limits<std::uint32_t>::max(),
                 "Path tracing physical hit SBT record count exceeds uint32 ABI.");

    auto hitRecords = std::vector<nr::rhi::ShaderBindingTableRecordDesc>{};
    hitRecords.reserve(static_cast<std::size_t>(physicalHitRecordCount));
    auto const shadowHitGroupIndex = pipeline.shaderGroupIndex(kPathTracingShadowHitGroupName);
    std::ranges::for_each(hitSbtPlan.records, [&](const SceneRtHitSbtRecord &record) {
        nr::nrAssert(record.permutationIndex < hitSbtPlan.permutations.size(),
                     "Path tracing SBT record references an invalid hit permutation.");
        hitRecords.push_back(nr::rhi::ShaderBindingTableRecordDesc{
            .groupIndex = pipeline.shaderGroupIndex(
                pathTracingHitGroupName(hitSbtPlan.permutations[record.permutationIndex].key)),
        });
        hitRecords.push_back(nr::rhi::ShaderBindingTableRecordDesc{
            .groupIndex = shadowHitGroupIndex,
        });
    });
    nr::nrAssert(hitRecords.size() == physicalHitRecordCount,
                 "Path tracing physical hit SBT record expansion is inconsistent with the ray-type ABI.");

    auto const missRecords = std::array{
        nr::rhi::ShaderBindingTableRecordDesc{
            .groupIndex = pipeline.shaderGroupIndex(kPathTracingMaterialMissGroupName),
        },
        nr::rhi::ShaderBindingTableRecordDesc{
            .groupIndex = pipeline.shaderGroupIndex(kPathTracingShadowMissGroupName),
        },
    };

    auto shaderBindingTable = nr::rhi::ShaderBindingTable::create(
        device.resourceFactory,
        nr::rhi::ShaderBindingTableBuildDesc{
            .pipeline = pipeline,
            .raygen =
                nr::rhi::ShaderBindingTableSectionDesc{
                    .firstGroup = pipeline.shaderGroupIndex(kPathTracingRaygenGroupName),
                    .groupCount = 1u,
                },
            .miss =
                nr::rhi::ShaderBindingTableSectionDesc{
                    .records = std::span<const nr::rhi::ShaderBindingTableRecordDesc>{missRecords},
                },
            .hit =
                nr::rhi::ShaderBindingTableSectionDesc{
                    .records =
                        std::span<const nr::rhi::ShaderBindingTableRecordDesc>{hitRecords.data(), hitRecords.size()},
                },
            .debugName = describePathTracingSbtKey(sbtKey) + ".SBT",
        });
    nr::nrAssert(shaderBindingTable.valid(), "Path tracing pass failed to create SBT.");

    return shaderBindingTable;
}

[[nodiscard]] std::shared_ptr<nr::renderer::PipelineRuntime<nr::rhi::RayTracingPipeline>> &
ensurePathTracingPipelineRuntime(PathTracingRuntimeCache &cache, nr::rhi::Device &device,
                                 const PathTracingPipelineKey &pipelineKey, const SceneRtHitSbtPlan &hitSbtPlan)
{
    if (auto runtimeIt = cache.pipelines.find(pipelineKey); runtimeIt != cache.pipelines.end())
    {
        return runtimeIt->second;
    }

    auto [runtimeIt, inserted] =
        cache.pipelines.try_emplace(pipelineKey, createPathTracingPipelineRuntime(device, pipelineKey, hitSbtPlan));
    nr::nrAssert(inserted, "Path tracing pipeline runtime cache insertion failed.");
    return runtimeIt->second;
}

[[nodiscard]] nr::rhi::ShaderBindingTable &ensurePathTracingShaderBindingTable(
    PathTracingRuntimeCache &cache, nr::rhi::Device &device, const PathTracingSbtKey &sbtKey,
    const std::shared_ptr<nr::renderer::PipelineRuntime<nr::rhi::RayTracingPipeline>> &pipelineRuntime,
    const SceneRtHitSbtPlan &hitSbtPlan)
{
    if (auto sbtIt = cache.shaderBindingTables.find(sbtKey); sbtIt != cache.shaderBindingTables.end())
    {
        return sbtIt->second;
    }

    auto [sbtIt, inserted] = cache.shaderBindingTables.try_emplace(
        sbtKey, createPathTracingShaderBindingTable(device, pipelineRuntime, sbtKey, hitSbtPlan));
    nr::nrAssert(inserted, "Path tracing SBT cache insertion failed.");
    return sbtIt->second;
}

[[nodiscard]] PathTracingFrameRuntime ensurePathTracingFrameRuntime(PathTracingRuntimeCache &cache,
                                                                    nr::rhi::Device &device,
                                                                    const PathTracingVariantKey &key,
                                                                    const SceneRtHitSbtPlan &hitSbtPlan)
{
    auto pipelineKey = PathTracingPipelineKey{
        .variant = normalizePathTracingVariantKey(key),
        .chsPermutationSetHash = hitSbtPlan.permutationSetHash,
        .shaderSessionGeneration = nr::rhi::ShaderService::instance().sessionGeneration(),
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

void ensurePathTracingGuideImages(nr::rhi::Device &device, PathTracingGuideFrameSlot &frameSlot,
                                  std::size_t frameSlotIndex, vk::Extent2D requestedExtent, vk::Format colorFormat)
{
    nr::nrAssert(colorFormat != vk::Format::eUndefined, "PathTracing guide color format must be defined.");
    requestedExtent.width = std::max(1u, requestedExtent.width);
    requestedExtent.height = std::max(1u, requestedExtent.height);

    auto specs = makePathTracingGuideSpecs(colorFormat);
    auto guideIndices = std::views::iota(std::size_t{0u}, kPathTracingGuideResourceCount);
    auto resourcesValid = std::ranges::all_of(guideIndices, [&](std::size_t index) {
        return frameSlot.images[index].valid() && frameSlot.images[index].format() == specs[index].format;
    });
    auto extentFits = frameSlot.allocatedExtent.width >= requestedExtent.width &&
                      frameSlot.allocatedExtent.height >= requestedExtent.height;
    if (resourcesValid && extentFits && frameSlot.allocatedColorFormat == colorFormat)
    {
        return;
    }

    auto allocatedExtent = vk::Extent2D{
        std::max(requestedExtent.width, frameSlot.allocatedExtent.width),
        std::max(requestedExtent.height, frameSlot.allocatedExtent.height),
    };
    auto imageUsage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled |
                      vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc;

    std::ranges::for_each(guideIndices, [&](std::size_t guideResourceIndex) {
        auto const &spec = specs[guideResourceIndex];
        auto imageInfo = nr::rhi::makeImageCreateInfo(spec.format, allocatedExtent, imageUsage);
        frameSlot.images[guideResourceIndex] =
            device.resourceFactory.createImage(imageInfo, nr::rhi::MemoryUsage::GpuOnly,
                                               std::format("PathTracing.{}[{}]", spec.debugName, frameSlotIndex));
        nr::nrAssert(
            frameSlot.images[guideResourceIndex].valid(), "PathTracing failed to allocate {} guide for frame slot {}.", spec.debugName, frameSlotIndex);
        frameSlot.states[guideResourceIndex].reset();
    });

    frameSlot.allocatedExtent = allocatedExtent;
    frameSlot.allocatedColorFormat = colorFormat;
}

[[nodiscard]] std::array<nr::renderer::GraphResourceHandle, kPathTracingGuideResourceCount> importPathTracingGuides(
    NodeBuildContext &context, PathTracingGuideFrameSlot &frameSlot, vk::Extent2D extent,
    std::span<const PathTracingGuideSpec, kPathTracingGuideResourceCount> specs, std::size_t frameSlotIndex)
{
    auto resources = std::array<nr::renderer::GraphResourceHandle, kPathTracingGuideResourceCount>{};
    auto guideIndices = std::views::iota(std::size_t{0u}, kPathTracingGuideResourceCount);
    std::ranges::for_each(guideIndices, [&](std::size_t index) {
        auto &state = frameSlot.states[index];
        resources[index] = context.addResource(nr::renderer::GraphImportedImageDesc{
            .debugName = std::format("PathTracing.{}[{}]", specs[index].debugName, frameSlotIndex),
            .lifetime = nr::renderer::ResourceLifetime::RendererPersistent,
            .initialOwnership =
                state.common.initialized ? state.common.ownership : nr::renderer::ResourceOwnershipDomain::Undefined,
            .extent = vk::Extent3D{extent.width, extent.height, 1u},
            .format = specs[index].format,
            .usageIntents =
                {
                    nr::renderer::ImageUsageIntent::StorageWrite,
                    nr::renderer::ImageUsageIntent::Sampled,
                    nr::renderer::ImageUsageIntent::TransferDst,
                    nr::renderer::ImageUsageIntent::TransferSrc,
                },
            .initialLayout = state.common.initialized ? state.layout : nr::renderer::ImageLayoutIntent::Undefined,
            .initialAccessScope = state.common.initialized ? state.common.access : nr::renderer::AccessScope{},
            .importedResource = std::cref(frameSlot.images[index]),
            .retainedState = std::ref(state),
        });
    });
    return resources;
}


void publishPathTracingGuides(
    NodeBuildContext &context,
    std::span<const nr::renderer::GraphResourceHandle, kPathTracingGuideResourceCount> resources,
    std::span<const PathTracingGuideSpec, kPathTracingGuideResourceCount> specs)
{
    auto guideIndices = std::views::iota(std::size_t{0u}, kPathTracingGuideResourceCount);
    std::ranges::for_each(guideIndices, [&](std::size_t index) {
        context.publishFrameResource(
            std::format("dlss.rr.input.{}", nr::rhi::dlssResourceSlotName(specs[index].dlssSlot)), resources[index]);
    });
    context.publishFrameResource(nr::renderer::frameResource::presentSourceColor,
                                 resources[guideIndex(PathTracingGuideResource::color)]);
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
        rtInstanceMetadata,    rtGeometryMetadata, rtMaterialHeaders, rtMaterialLayers, rtMaterialTextureRefs,
        rtVertexAtlas,         rtIndexAtlas,       sceneLightHeader,  sceneLights,      sceneLightAliasTable,
    };
    if (!rtHitSbtPlanHandle.valid() ||
        std::ranges::any_of(sidebandResources,
                            [](nr::renderer::GraphResourceHandle resource) { return !resource.valid(); }))
    {
        return std::unexpected(PathTracingUnavailableReason::incompleteRtSideband);
    }

    auto rtHitSbtPlan = context.resolveBuildFrameData<std::shared_ptr<const SceneRtHitSbtPlan>>(rtHitSbtPlanHandle);
    if (!rtHitSbtPlan.has_value() || !rtHitSbtPlan->get())
    {
        return std::unexpected(PathTracingUnavailableReason::incompleteRtSideband);
    }
    if (!rtHitSbtPlan->get()->valid())
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
        .hitSbtPlan = rtHitSbtPlan->get(),
    };
}

void clearUnavailableGuides(
    NodeBuildContext &context,
    std::span<const nr::renderer::GraphResourceHandle, kPathTracingGuideResourceCount> resources,
    std::span<const PathTracingGuideSpec, kPathTracingGuideResourceCount> specs, PathTracingUnavailableReason reason)
{
    auto guideIndices = std::views::iota(std::size_t{0u}, kPathTracingGuideResourceCount);
    std::ranges::for_each(guideIndices, [&](std::size_t index) {
        [[maybe_unused]] auto clearPass = nr::renderer::ops::clearColorImage(
            context,
            std::format("PathTracing.ClearUnavailable.{}.{}", pathTracingUnavailableReasonName(reason),
                        specs[index].debugName),
            nr::renderer::ops::ClearColorImagePassDesc{
                .image = resources[index],
                .value = vk::ClearColorValue{specs[index].unavailableClear},
            });
    });
}
} // namespace nr::renderPasses::detail

namespace nr::renderPasses
{
namespace
{
[[nodiscard]] PathTracingVariantKey pathTracingVariant(const nr::options::OptionFrameSnapshot &snapshot)
{
    auto const *maxBounces = snapshot.find(nr::options::keys::pathTracingMaxSurfaceBounces);
    auto const *russianRoulette = snapshot.find(nr::options::keys::pathTracingRussianRouletteEnabled);
    auto const *filterAfterShading = snapshot.find(nr::options::keys::pathTracingFilterAfterShadingEnabled);
    nrAssert(maxBounces != nullptr && russianRoulette != nullptr && filterAfterShading != nullptr,
             "PathTracing requires its option values in the frame snapshot.");
    return detail::normalizePathTracingVariantKey(PathTracingVariantKey{
        .maxSurfaceBounces = static_cast<std::uint32_t>(*maxBounces),
        .enableRussianRoulette = *russianRoulette,
        .enableFilterAfterShading = *filterAfterShading,
    });
}
} // namespace

PathTracingNode::~PathTracingNode() = default;

void PathTracingNode::declareOptions(nr::options::OptionCatalogBuilder &builder) const
{
    std::ranges::for_each(nr::options::makePathTracingDefinitions(), [&](nr::options::OptionDefinition definition) {
        static_cast<void>(builder.add(std::move(definition)));
    });
}

void PathTracingNode::collectOptionAvailability(const nr::options::OptionFrameSnapshot &,
                                                nr::options::OptionAvailabilityMap &availability) const
{
    auto const definitions = std::array{
        nr::options::optionId(nr::options::keys::pathTracingMaxSurfaceBounces),
        nr::options::optionId(nr::options::keys::pathTracingRussianRouletteEnabled),
        nr::options::optionId(nr::options::keys::pathTracingFilterAfterShadingEnabled),
    };
    std::ranges::for_each(definitions, [&](const nr::options::OptionId &id) {
        availability.insert_or_assign(id, nr::options::OptionAvailability{.available = true, .reason = {}});
    });
}

void PathTracingNode::initialize(NodeInitContext &context)
{
    device_ = context.device;
    nr::nrAssert(input.outputFormat != vk::Format::eUndefined, "PathTracing output format must be defined.");
    runtime_ = detail::makePathTracingRuntimeCache();
}

void PathTracingNode::build(NodeBuildContext &context, const NodeFrameParameters &frameParameters)
{
    materializeCurrentFrame(context, frameParameters);
}

void PathTracingNode::materializeCurrentFrame(NodeBuildContext &context, const NodeFrameParameters &frameParameters)
{
    nr::nrAssert(static_cast<bool>(runtime_), "PathTracing build stage requires initialized runtime state.");
    nr::nrAssert(device_.has_value(), "PathTracing build stage requires device reference from initialize stage.");
    auto const variant = pathTracingVariant(frameParameters.optionSnapshot.get());

    auto const renderExtent = frameParameters.resolutionPlan.renderExtent;
    nr::nrAssert(renderExtent.width > 0u && renderExtent.height > 0u,
                 "PathTracing requires a non-zero renderer frame render extent.");

    auto frameSlotIndex = static_cast<std::size_t>(frameParameters.frameIndex % nr::maxFrameInFlight);
    auto &guideFrameSlot = runtime_->guideFrameSlots[frameSlotIndex];
    detail::ensurePathTracingGuideImages(device_->get(), guideFrameSlot, frameSlotIndex, renderExtent,
                                         input.outputFormat);
    auto guideSpecs = detail::makePathTracingGuideSpecs(input.outputFormat);
    auto guideResources =
        detail::importPathTracingGuides(context, guideFrameSlot, renderExtent, guideSpecs, frameSlotIndex);
    detail::publishPathTracingGuides(context, guideResources, guideSpecs);

    auto frameInputs = detail::resolvePathTracingFrameInputs(context);
    if (!frameInputs.has_value())
    {
        detail::clearUnavailableGuides(context, guideResources, guideSpecs, frameInputs.error());
        return;
    }
    auto const &inputs = *frameInputs;
    auto activeRuntime = detail::ensurePathTracingFrameRuntime(*runtime_, device_->get(), variant, *inputs.hitSbtPlan);

    auto sbtResource = context.importBuffer(activeRuntime.shaderBindingTable.get().buffer(), "PathTracing.SBT",
                                            nr::renderer::ResourceLifetime::RendererPersistent,
                                            {
                                                nr::renderer::BufferUsageIntent::ShaderBindingTable,
                                                nr::renderer::BufferUsageIntent::ShaderDeviceAddress,
                                            },
                                            nr::renderer::ownershipDomainFromQueue(context.queue));

    auto dimensions = nr::rhi::TraceRaysDimensions{
        .width = renderExtent.width,
        .height = renderExtent.height,
        .depth = 1u,
    };
    auto queueRole = nr::renderer::rhiQueueRoleFromDomain(context.queue);
    nr::nrAssert(queueRole != nr::rhi::QueueRole::Transfer, "PathTracing cannot run on the transfer queue.");
    auto sceneTextureTableBinding = detail::makeSceneTextureTableBindingInput(context.globalResources.get());
    auto &bindlessImageTableCache = context.globalResources.get().bindlessImageTableCache.get();
    auto const environmentMap = context.globalResources.get().environmentMap;
    auto const environmentParameters = context.globalResources.get().environmentMapParameters;
    nr::nrAssert(environmentMap.valid(), "PathTracing requires the renderer-global environment map.");

    auto tracePass = nr::renderer::RayTracingPassBuilder{context, "PathTracing.Trace", activeRuntime.pipeline};
    tracePass.accelerationStructure("scene", inputs.sceneTlas, "PathTracing.SceneTLAS");
    auto guideIndices = std::views::iota(std::size_t{0u}, detail::kPathTracingGuideResourceCount);
    std::ranges::for_each(guideIndices, [&](std::size_t index) {
        tracePass.storageImage(guideSpecs[index].shaderBinding, guideResources[index],
                               std::format("PathTracing.{}", guideSpecs[index].debugName));
    });
    tracePass.storageBuffer("rtInstanceMetadata", inputs.rtInstanceMetadata, "PathTracing.InstanceMetadata")
        .storageBuffer("rtGeometryMetadata", inputs.rtGeometryMetadata, "PathTracing.GeometryMetadata")
        .storageBuffer("rtMaterialHeaders", inputs.rtMaterialHeaders, "PathTracing.MaterialHeaders")
        .storageBuffer("rtMaterialLayers", inputs.rtMaterialLayers, "PathTracing.MaterialLayers")
        .storageBuffer("rtMaterialTextureRefs", inputs.rtMaterialTextureRefs, "PathTracing.MaterialTextureRefs")
        .storageBuffer("rtVertexData", inputs.rtVertexAtlas, "PathTracing.VertexAtlas")
        .storageBuffer("rtIndexData", inputs.rtIndexAtlas, "PathTracing.IndexAtlas")
        .sampledImage("gEnvironmentMap", environmentMap, "Renderer.EnvironmentMap")
        .uniform("gFrame", context.globalResources.get().frameUniform, "Renderer.GlobalFrameUniforms")
        .pushConstants("gEnvironment", environmentParameters)
        .resourceUse(nr::renderer::use::shaderBindingTableRead(sbtResource))
        .uniform("gSceneLightHeader", inputs.sceneLightHeader, "PathTracing.SceneLightHeader")
        .storageBuffer("gSceneLights", inputs.sceneLights, "PathTracing.SceneLights")
        .storageBuffer("gSceneLightAliasTable", inputs.sceneLightAliasTable, "PathTracing.SceneLightAliasTable")
        .prepare([pipeline = activeRuntime.pipeline, sceneTextureTableBinding,
                  cache = std::ref(bindlessImageTableCache)](
                     const nr::renderer::PassPrepareContext &prepareContext,
                     nr::renderer::PipelineRuntime<nr::rhi::RayTracingPipeline>::PassBindingHandle passBinding) {
            detail::prepareSceneTextureTableBindingForFrame(*pipeline, passBinding, cache.get(),
                                                            prepareContext.frameIndex, sceneTextureTableBinding,
                                                            detail::SceneTextureTableBindingRequirement::optional);
        })
        .dynamicBindingSnapshot([pipeline = activeRuntime.pipeline, sceneTextureTableBinding,
                                 cache = std::ref(bindlessImageTableCache)](
                                    const nr::renderer::PassPrepareContext &prepareContext,
                                    nr::renderer::PipelineRuntime<
                                        nr::rhi::RayTracingPipeline>::PassBindingHandle passBinding) {
            return detail::makeSceneTextureTableBindingSnapshot(*pipeline, passBinding, cache.get(),
                                                                prepareContext.frameIndex, sceneTextureTableBinding,
                                                                detail::SceneTextureTableBindingRequirement::optional);
        })
        .record([pipeline = activeRuntime.pipeline, shaderBindingTable = activeRuntime.shaderBindingTable, dimensions,
                 queueRole](const nr::renderer::RayTracingPassRecordContext &rayContext) {
            nr::rhi::traceRays(rayContext.commandBuffer,
                               nr::rhi::TraceRaysDesc{
                                   .pipeline = pipeline->pipeline(),
                                   .shaderBindingTable = shaderBindingTable.get(),
                                   .dimensions = dimensions,
                                   .recordingQueueRole = queueRole,
                               });
        });

    [[maybe_unused]] auto tracePassHandle = tracePass.build();
}

void PathTracingNode::shutdown(NodeShutdownContext &)
{
    if (runtime_)
    {
        std::ranges::for_each(
            runtime_->pipelines | std::views::values,
            [](std::shared_ptr<nr::renderer::PipelineRuntime<nr::rhi::RayTracingPipeline>> &pipelineRuntime) {
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
