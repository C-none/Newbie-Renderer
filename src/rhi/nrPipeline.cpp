module nr.rhi;
import :pipeline;
import dependency.slang;
import dependency.vulkan;
import :type;
import nr.utils;
import :descriptor;
import :slang;
import :pipelineBinary;
import std;

namespace nr::rhi
{
namespace
{
[[nodiscard]] constexpr bool isStageInSet(SlangStage stage, std::initializer_list<SlangStage> stages)
{
    return std::ranges::any_of(stages, [stage](SlangStage candidate) { return candidate == stage; });
}

[[nodiscard]] bool isGraphicsStage(SlangStage stage)
{
    return isStageInSet(stage, {SLANG_STAGE_VERTEX, SLANG_STAGE_FRAGMENT, SLANG_STAGE_GEOMETRY, SLANG_STAGE_HULL,
                                SLANG_STAGE_DOMAIN});
}

[[nodiscard]] bool isRayTracingStage(SlangStage stage)
{
    return isStageInSet(stage, {SLANG_STAGE_RAY_GENERATION, SLANG_STAGE_MISS, SLANG_STAGE_CLOSEST_HIT,
                                SLANG_STAGE_ANY_HIT, SLANG_STAGE_INTERSECTION, SLANG_STAGE_CALLABLE});
}

std::atomic<std::uint64_t> nextRayTracingPipelineIdentity{1u};

[[nodiscard]] RayTracingPipelineIdentity allocateRayTracingPipelineIdentity()
{
    auto const value = nextRayTracingPipelineIdentity.fetch_add(1u, std::memory_order_relaxed);
    nrAssert(value != 0u, "Ray tracing pipeline identity space exhausted.");
    return RayTracingPipelineIdentity{.value = value};
}

[[nodiscard]] vk::PipelineCreateFlags2 capturePipelineFlags(vk::PipelineCreateFlags flags) noexcept
{
    auto const legacyFlags = static_cast<std::uint64_t>(static_cast<vk::PipelineCreateFlags::MaskType>(flags));
    return vk::PipelineCreateFlags2{legacyFlags} | vk::PipelineCreateFlagBits2::eCaptureDataKHR;
}

// loadOrCreateAndCapturePipeline requires createInfo.pNext to be the caller-owned chain
// head (nullptr when the create info has no extension chain) on entry. It relinks the
// chain while attaching pipeline binaries or capture flags and restores the original
// chain head before returning. When deferredRetry is true, a failed immediate recreation
// falls back to returning a null pipeline instead of propagating the error, so the caller
// can retry through a deferred host operation.
template <bool deferredRetry = false, typename CreateInfo, typename CreatePipeline,
          std::convertible_to<std::string_view> PipelineKind>
    requires std::invocable<CreatePipeline &, const CreateInfo &> &&
             std::same_as<std::invoke_result_t<CreatePipeline &, const CreateInfo &>, vk::raii::Pipeline>
[[nodiscard]] vk::raii::Pipeline loadOrCreateAndCapturePipeline(PipelineBinaryStore &binaryStore,
                                                                std::uint64_t contentFingerprint,
                                                                CreateInfo &createInfo, PipelineKind &&pipelineKind,
                                                                CreatePipeline &&createPipeline,
                                                                PipelineBinaryCacheKey *keyOut = nullptr,
                                                                bool *loadedFromCacheOut = nullptr)
{
    auto *const originalNext = createInfo.pNext;
    auto pipelineCreateInfo = vk::PipelineCreateInfoKHR{};
    pipelineCreateInfo.pNext = &createInfo;
    auto const pipelineKey = binaryStore.pipelineKey(pipelineCreateInfo, contentFingerprint);
    if (keyOut != nullptr)
    {
        *keyOut = pipelineKey;
    }
    auto const kindText = std::string_view{pipelineKind};
    if (auto binaries = binaryStore.load(pipelineKey))
    {
        auto binaryInfo = vk::PipelineBinaryInfoKHR{};
        binaryInfo.pNext = originalNext;
        binaryInfo.binaryCount = static_cast<std::uint32_t>(binaries->handles.size());
        binaryInfo.pPipelineBinaries = binaries->handles.data();
        createInfo.pNext = &binaryInfo;
        try
        {
            auto pipeline = std::invoke(createPipeline, createInfo);
            binaryStore.markLoadAccepted();
            if (loadedFromCacheOut != nullptr)
            {
                *loadedFromCacheOut = true;
            }
            return pipeline;
        }
        catch (const vk::SystemError &error)
        {
            nrLog<LogLevel::warning>("{} rejected persisted PSO binaries and will rebuild: {}", kindText, error.what());
            binaryStore.invalidate(pipelineKey);
        }
    }
    if (loadedFromCacheOut != nullptr)
    {
        *loadedFromCacheOut = false;
    }

    auto captureInfo = vk::PipelineCreateFlags2CreateInfoKHR{};
    captureInfo.pNext = originalNext;
    captureInfo.flags = capturePipelineFlags(createInfo.flags);
    createInfo.pNext = &captureInfo;
    try
    {
        auto pipeline = std::invoke(createPipeline, createInfo);
        binaryStore.capture(pipelineKey, *pipeline);
        createInfo.pNext = originalNext;
        return pipeline;
    }
    catch (const vk::SystemError &error)
    {
        if constexpr (deferredRetry)
        {
            nrLog<LogLevel::warning>("{} immediate PSO creation failed and will retry deferred: {}", kindText,
                                     error.what());
            createInfo.pNext = originalNext;
            return vk::raii::Pipeline{nullptr};
        }
        else
        {
            throw;
        }
    }
}

template <typename TEnum>
    requires std::is_enum_v<TEnum>
void appendPsoEnum(std::uint64_t &fingerprint, TEnum value) noexcept
{
    nr::hash::hashAppend(fingerprint, static_cast<std::underlying_type_t<TEnum>>(value));
}

template <typename TFlags> void appendPsoFlags(std::uint64_t &fingerprint, TFlags value) noexcept
{
    nr::hash::hashAppend(fingerprint, static_cast<typename TFlags::MaskType>(value));
}

template <typename TRange> void appendPsoRangeSize(std::uint64_t &fingerprint, const TRange &range) noexcept
{
    nr::hash::hashAppend(fingerprint, static_cast<std::uint64_t>(std::ranges::size(range)));
}

[[nodiscard]] std::uint64_t finalizePsoContentFingerprint(std::uint64_t fingerprint) noexcept
{
    return fingerprint == 0u ? nr::hash::fnv1a64OffsetBasis : fingerprint;
}

void appendProgramFingerprint(std::uint64_t &fingerprint, const SlangProgram &program,
                              std::string_view logicalEntryPointName = {})
{
    auto const *entryPoint = program.entryPoint();
    nrAssert(entryPoint != nullptr && entryPoint->valid() && entryPoint->spirv,
             "PSO content fingerprint requires a valid compiled Slang entry point.");
    appendPsoEnum(fingerprint, entryPoint->stage);
    nr::hash::hashAppendString(fingerprint, entryPoint->entryPointName);
    nr::hash::hashAppendString(fingerprint, logicalEntryPointName);
    nr::hash::hashAppend(fingerprint, static_cast<std::uint64_t>(entryPoint->spirv->size()));
    auto const words = std::span<const std::uint32_t>{entryPoint->spirv->data(), entryPoint->spirv->size()};
    nr::hash::hashAppend(fingerprint, nr::hash::fnv1a64(std::as_bytes(words)));
}

void appendShaderLayoutFingerprint(std::uint64_t &fingerprint, const ShaderLayoutAbiSignature &signature)
{
    auto descriptorBindings = signature.descriptorBindings;
    std::ranges::sort(descriptorBindings);
    appendPsoRangeSize(fingerprint, descriptorBindings);
    std::ranges::for_each(descriptorBindings, [&](const ShaderDescriptorAbiBinding &binding) {
        nr::hash::hashAppend(fingerprint, binding.set);
        nr::hash::hashAppend(fingerprint, binding.binding);
        nr::hash::hashAppend(fingerprint, binding.descriptorCount);
        nr::hash::hashAppend(fingerprint, binding.isRuntimeSized);
        appendPsoEnum(fingerprint, binding.descriptorType);
        appendPsoFlags(fingerprint, binding.stageFlags);
        appendPsoFlags(fingerprint, binding.bindingFlags);
    });

    auto pushConstantRanges = signature.pushConstantRanges;
    std::ranges::sort(pushConstantRanges);
    appendPsoRangeSize(fingerprint, pushConstantRanges);
    std::ranges::for_each(pushConstantRanges, [&](const ShaderPushConstantAbiRange &range) {
        nr::hash::hashAppend(fingerprint, range.offset);
        nr::hash::hashAppend(fingerprint, range.size);
        appendPsoFlags(fingerprint, range.stageFlags);
    });
}

void appendSamplerDescFingerprint(std::uint64_t &fingerprint, const SlangSamplerDesc &desc) noexcept
{
    appendPsoEnum(fingerprint, desc.magFilter);
    appendPsoEnum(fingerprint, desc.minFilter);
    appendPsoEnum(fingerprint, desc.mipmapMode);
    appendPsoEnum(fingerprint, desc.addressModeU);
    appendPsoEnum(fingerprint, desc.addressModeV);
    appendPsoEnum(fingerprint, desc.addressModeW);
    nr::hash::hashAppend(fingerprint, desc.mipLodBias);
    nr::hash::hashAppend(fingerprint, desc.anisotropyEnable);
    nr::hash::hashAppend(fingerprint, desc.maxAnisotropy);
    nr::hash::hashAppend(fingerprint, desc.compareEnable);
    appendPsoEnum(fingerprint, desc.compareOp);
    nr::hash::hashAppend(fingerprint, desc.minLod);
    nr::hash::hashAppend(fingerprint, desc.maxLod);
    appendPsoEnum(fingerprint, desc.borderColor);
    nr::hash::hashAppend(fingerprint, desc.unnormalizedCoordinates);
}

void appendImmutableSamplersFingerprint(std::uint64_t &fingerprint,
                                        std::span<const SlangImmutableSamplerBinding> immutableSamplers)
{
    auto indices = std::views::iota(std::size_t{0}, immutableSamplers.size()) | std::ranges::to<std::vector>();
    std::ranges::sort(indices, {}, [&](std::size_t index) {
        auto const &binding = immutableSamplers[index];
        return std::tuple{binding.set, binding.binding};
    });
    appendPsoRangeSize(fingerprint, indices);
    std::ranges::for_each(indices, [&](std::size_t index) {
        auto const &binding = immutableSamplers[index];
        nr::hash::hashAppend(fingerprint, binding.set);
        nr::hash::hashAppend(fingerprint, binding.binding);
        nr::hash::hashAppend(fingerprint, binding.descriptorCount);
        appendSamplerDescFingerprint(fingerprint, binding.samplerDesc);
    });
}

[[nodiscard]] std::vector<vk::PipelineColorBlendAttachmentState> normalizedColorBlendAttachments(
    const GraphicsPipelineDesc &desc)
{
    if (!desc.colorBlendAttachments.empty())
    {
        return desc.colorBlendAttachments;
    }
    return desc.colorAttachmentFormats | std::views::transform([](vk::Format) {
               return vk::PipelineColorBlendAttachmentState{
                   vk::False,
                   vk::BlendFactor::eOne,
                   vk::BlendFactor::eZero,
                   vk::BlendOp::eAdd,
                   vk::BlendFactor::eOne,
                   vk::BlendFactor::eZero,
                   vk::BlendOp::eAdd,
                   vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB |
                       vk::ColorComponentFlagBits::eA,
               };
           }) |
           std::ranges::to<std::vector>();
}

void appendDescriptorPolicyFingerprint(std::uint64_t &fingerprint, const DescriptorBindingPolicy &policy) noexcept
{
    nr::hash::hashAppend(fingerprint, policy.defaultRuntimeDescriptorCount);
}

[[nodiscard]] std::uint64_t graphicsPsoContentFingerprint(
    std::span<const SlangProgram> programs, const GraphicsPipelineDesc &desc,
    const ShaderLayoutAbiSignature &layoutSignature, std::span<const SlangImmutableSamplerBinding> immutableSamplers)
{
    auto fingerprint = nr::hash::fnv1a64OffsetBasis;
    nr::hash::hashAppendString(fingerprint, "nr.pso.graphics.v1");
    appendPsoRangeSize(fingerprint, programs);
    std::ranges::for_each(programs,
                          [&](const SlangProgram &program) { appendProgramFingerprint(fingerprint, program); });

    appendPsoRangeSize(fingerprint, desc.colorAttachmentFormats);
    std::ranges::for_each(desc.colorAttachmentFormats, [&](vk::Format format) { appendPsoEnum(fingerprint, format); });
    nr::hash::hashAppend(fingerprint, desc.depthAttachmentFormat.has_value());
    if (desc.depthAttachmentFormat)
    {
        appendPsoEnum(fingerprint, *desc.depthAttachmentFormat);
    }
    nr::hash::hashAppend(fingerprint, desc.stencilAttachmentFormat.has_value());
    if (desc.stencilAttachmentFormat)
    {
        appendPsoEnum(fingerprint, *desc.stencilAttachmentFormat);
    }
    appendPsoEnum(fingerprint, desc.topology);
    appendPsoFlags(fingerprint, desc.cullMode);
    appendPsoEnum(fingerprint, desc.frontFace);
    appendPsoEnum(fingerprint, desc.polygonMode);
    appendPsoEnum(fingerprint, desc.sampleCount);
    nr::hash::hashAppend(fingerprint, desc.depthTestEnable);
    nr::hash::hashAppend(fingerprint, desc.depthWriteEnable);
    appendPsoEnum(fingerprint, desc.depthCompareOp);

    auto const colorBlendAttachments = normalizedColorBlendAttachments(desc);
    appendPsoRangeSize(fingerprint, colorBlendAttachments);
    std::ranges::for_each(colorBlendAttachments, [&](const vk::PipelineColorBlendAttachmentState &attachment) {
        nr::hash::hashAppend(fingerprint, attachment.blendEnable);
        appendPsoEnum(fingerprint, attachment.srcColorBlendFactor);
        appendPsoEnum(fingerprint, attachment.dstColorBlendFactor);
        appendPsoEnum(fingerprint, attachment.colorBlendOp);
        appendPsoEnum(fingerprint, attachment.srcAlphaBlendFactor);
        appendPsoEnum(fingerprint, attachment.dstAlphaBlendFactor);
        appendPsoEnum(fingerprint, attachment.alphaBlendOp);
        appendPsoFlags(fingerprint, attachment.colorWriteMask);
    });

    appendPsoRangeSize(fingerprint, desc.vertexBindings);
    std::ranges::for_each(desc.vertexBindings, [&](const vk::VertexInputBindingDescription &binding) {
        nr::hash::hashAppend(fingerprint, binding.binding);
        nr::hash::hashAppend(fingerprint, binding.stride);
        appendPsoEnum(fingerprint, binding.inputRate);
    });
    appendPsoRangeSize(fingerprint, desc.vertexAttributes);
    std::ranges::for_each(desc.vertexAttributes, [&](const vk::VertexInputAttributeDescription &attribute) {
        nr::hash::hashAppend(fingerprint, attribute.location);
        nr::hash::hashAppend(fingerprint, attribute.binding);
        appendPsoEnum(fingerprint, attribute.format);
        nr::hash::hashAppend(fingerprint, attribute.offset);
    });
    appendDescriptorPolicyFingerprint(fingerprint, desc.descriptorBindingPolicy);
    appendShaderLayoutFingerprint(fingerprint, layoutSignature);
    appendImmutableSamplersFingerprint(fingerprint, immutableSamplers);
    return finalizePsoContentFingerprint(fingerprint);
}

[[nodiscard]] std::uint64_t computePsoContentFingerprint(
    const SlangProgram &program, const ComputePipelineDesc &desc, const ShaderLayoutAbiSignature &layoutSignature,
    std::span<const SlangImmutableSamplerBinding> immutableSamplers)
{
    auto fingerprint = nr::hash::fnv1a64OffsetBasis;
    nr::hash::hashAppendString(fingerprint, "nr.pso.compute.v1");
    appendProgramFingerprint(fingerprint, program);
    appendDescriptorPolicyFingerprint(fingerprint, desc.descriptorBindingPolicy);
    appendShaderLayoutFingerprint(fingerprint, layoutSignature);
    appendImmutableSamplersFingerprint(fingerprint, immutableSamplers);
    return finalizePsoContentFingerprint(fingerprint);
}

[[nodiscard]] std::uint64_t rayTracingPsoContentFingerprint(
    const RayTracingProgramAssemblyDesc &assembly, const RayTracingPipelineDesc &desc,
    const ShaderLayoutAbiSignature &layoutSignature, std::span<const SlangImmutableSamplerBinding> immutableSamplers)
{
    auto fingerprint = nr::hash::fnv1a64OffsetBasis;
    nr::hash::hashAppendString(fingerprint, "nr.pso.ray_tracing.v1");
    appendPsoRangeSize(fingerprint, assembly.stages);
    std::ranges::for_each(assembly.stages, [&](const RayTracingPipelineStageSelection &selection) {
        appendProgramFingerprint(fingerprint, selection.program.get(), selection.logicalEntryPointName);
    });
    appendPsoRangeSize(fingerprint, assembly.groups);
    std::ranges::for_each(assembly.groups, [&](const RayTracingShaderGroupDesc &group) {
        nr::hash::hashAppendString(fingerprint, group.name);
        appendPsoEnum(fingerprint, group.type);
        nr::hash::hashAppendString(fingerprint, group.generalEntryPoint);
        nr::hash::hashAppendString(fingerprint, group.closestHitEntryPoint);
        nr::hash::hashAppendString(fingerprint, group.anyHitEntryPoint);
        nr::hash::hashAppendString(fingerprint, group.intersectionEntryPoint);
    });
    nr::hash::hashAppend(fingerprint, desc.maxRayRecursionDepth);
    appendDescriptorPolicyFingerprint(fingerprint, desc.descriptorBindingPolicy);
    appendPsoFlags(fingerprint, desc.flags);
    appendShaderLayoutFingerprint(fingerprint, layoutSignature);
    appendImmutableSamplersFingerprint(fingerprint, immutableSamplers);
    return finalizePsoContentFingerprint(fingerprint);
}
} // namespace

[[nodiscard]] std::optional<std::string> validateRayTracingPipelineDesc(const RayTracingPipelineDesc &desc)
{
    if ((desc.flags & vk::PipelineCreateFlagBits::eEarlyReturnOnFailure) != vk::PipelineCreateFlags{})
    {
        return std::string{"RayTracingPipelineDesc cannot use eEarlyReturnOnFailure with deferred host creation."};
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<std::string> validateRayTracingProgramAssemblyDesc(
    const RayTracingProgramAssemblyDesc &desc)
{
    if (desc.stages.empty())
    {
        return std::string{"RayTracingProgramAssemblyDesc requires at least one shader stage."};
    }
    if (desc.groups.empty())
    {
        return std::string{"RayTracingProgramAssemblyDesc requires at least one shader group."};
    }

    auto logicalEntryPointNames = std::set<std::string>{};
    auto logicalEntryPointStages = std::map<std::string, SlangStage>{};
    auto stageValidation = std::optional<std::string>{};
    std::ranges::for_each(desc.stages, [&](const RayTracingPipelineStageSelection &stage) {
        if (stageValidation.has_value())
        {
            return;
        }
        if (stage.logicalEntryPointName.empty())
        {
            stageValidation = "RayTracingProgramAssemblyDesc stages require non-empty logical entry-point names.";
            return;
        }
        if (!logicalEntryPointNames.insert(stage.logicalEntryPointName).second)
        {
            stageValidation = std::format("RayTracingProgramAssemblyDesc has duplicate logical entry point '{}'.",
                                          stage.logicalEntryPointName);
            return;
        }
        auto const *entryPointData = stage.program.get().entryPoint();
        if (entryPointData == nullptr)
        {
            stageValidation = std::format(
                "RayTracingProgramAssemblyDesc logical entry point '{}' references an invalid single-entry program.",
                stage.logicalEntryPointName);
            return;
        }
        if (!isRayTracingStage(entryPointData->stage))
        {
            stageValidation =
                std::format("RayTracingProgramAssemblyDesc logical entry point '{}' is not a ray-tracing stage.",
                            stage.logicalEntryPointName);
            return;
        }
        logicalEntryPointStages.emplace(stage.logicalEntryPointName, entryPointData->stage);
    });
    if (stageValidation.has_value())
    {
        return stageValidation;
    }

    auto groupNames = std::set<std::string>{};
    auto groupValidation = std::optional<std::string>{};
    std::ranges::for_each(desc.groups, [&](const RayTracingShaderGroupDesc &group) {
        if (groupValidation.has_value())
        {
            return;
        }
        if (group.name.empty())
        {
            groupValidation = "RayTracingProgramAssemblyDesc groups require non-empty names.";
            return;
        }
        if (!groupNames.insert(group.name).second)
        {
            groupValidation = std::format("RayTracingProgramAssemblyDesc has duplicate group '{}'.", group.name);
            return;
        }

        auto validateEntryPointStage = [&](std::string_view entryPointName,
                                           std::initializer_list<SlangStage> expectedStages,
                                           std::string_view role) -> std::optional<std::string> {
            if (entryPointName.empty())
            {
                return std::nullopt;
            }
            auto const found = logicalEntryPointStages.find(std::string{entryPointName});
            if (found == logicalEntryPointStages.end())
            {
                return std::format(
                    "RayTracingProgramAssemblyDesc group '{}' references unknown logical entry point '{}'.", group.name,
                    entryPointName);
            }
            if (std::ranges::none_of(expectedStages, [&](SlangStage stage) { return stage == found->second; }))
            {
                return std::format(
                    "RayTracingProgramAssemblyDesc group '{}' {} entry point '{}' has an incompatible shader stage.",
                    group.name, role, entryPointName);
            }
            return std::nullopt;
        };

        auto validateGroupShape = [&]() -> std::optional<std::string> {
            switch (group.type)
            {
            case vk::RayTracingShaderGroupTypeKHR::eGeneral:
                if (group.generalEntryPoint.empty() || !group.closestHitEntryPoint.empty() ||
                    !group.anyHitEntryPoint.empty() || !group.intersectionEntryPoint.empty())
                {
                    return std::format(
                        "RayTracingProgramAssemblyDesc general group '{}' must contain only a general entry point.",
                        group.name);
                }
                return validateEntryPointStage(group.generalEntryPoint,
                                               {SLANG_STAGE_RAY_GENERATION, SLANG_STAGE_MISS, SLANG_STAGE_CALLABLE},
                                               "general");
            case vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup:
                if (!group.generalEntryPoint.empty() || !group.intersectionEntryPoint.empty() ||
                    (group.closestHitEntryPoint.empty() && group.anyHitEntryPoint.empty()))
                {
                    return std::format("RayTracingProgramAssemblyDesc triangles hit group '{}' requires closest-hit "
                                       "and/or any-hit stages only.",
                                       group.name);
                }
                if (auto validation =
                        validateEntryPointStage(group.closestHitEntryPoint, {SLANG_STAGE_CLOSEST_HIT}, "closest-hit");
                    validation.has_value())
                {
                    return validation;
                }
                return validateEntryPointStage(group.anyHitEntryPoint, {SLANG_STAGE_ANY_HIT}, "any-hit");
            case vk::RayTracingShaderGroupTypeKHR::eProceduralHitGroup:
                if (!group.generalEntryPoint.empty() || group.intersectionEntryPoint.empty())
                {
                    return std::format("RayTracingProgramAssemblyDesc procedural hit group '{}' requires an "
                                       "intersection stage and no general stage.",
                                       group.name);
                }
                if (auto validation =
                        validateEntryPointStage(group.closestHitEntryPoint, {SLANG_STAGE_CLOSEST_HIT}, "closest-hit");
                    validation.has_value())
                {
                    return validation;
                }
                if (auto validation = validateEntryPointStage(group.anyHitEntryPoint, {SLANG_STAGE_ANY_HIT}, "any-hit");
                    validation.has_value())
                {
                    return validation;
                }
                return validateEntryPointStage(group.intersectionEntryPoint, {SLANG_STAGE_INTERSECTION},
                                               "intersection");
            default:
                return std::format("RayTracingProgramAssemblyDesc group '{}' has an unsupported shader group type.",
                                   group.name);
            }
        };
        groupValidation = validateGroupShape();
    });
    if (groupValidation.has_value())
    {
        return groupValidation;
    }

    return std::nullopt;
}

namespace
{
[[nodiscard]] bool shaderStageFlagsCover(vk::ShaderStageFlags available, vk::ShaderStageFlags required) noexcept
{
    return (available & required) == required;
}

[[nodiscard]] std::string_view programDebugName(const SlangProgram &program) noexcept
{
    auto const *entryPoint = program.entryPoint();
    return entryPoint ? std::string_view{entryPoint->debugName} : std::string_view{"<invalid>"};
}

[[nodiscard]] std::optional<std::string> validateReflectionLayoutCoverage(const ShaderLayoutAbiSignature &owner,
                                                                          const ShaderLayoutAbiSignature &required,
                                                                          std::string_view ownerName,
                                                                          std::string_view requiredName)
{
    auto error = std::optional<std::string>{};
    std::ranges::for_each(required.descriptorBindings, [&](const ShaderDescriptorAbiBinding &requiredBinding) {
        if (error.has_value())
        {
            return;
        }

        auto ownerBinding =
            std::ranges::find_if(owner.descriptorBindings, [&](const ShaderDescriptorAbiBinding &candidate) {
                return candidate.set == requiredBinding.set && candidate.binding == requiredBinding.binding;
            });
        if (ownerBinding == owner.descriptorBindings.end())
        {
            error = std::format("Reflection root '{}' does not expose descriptor set={}, binding={} required by '{}'.",
                                ownerName, requiredBinding.set, requiredBinding.binding, requiredName);
            return;
        }

        if (ownerBinding->descriptorCount != requiredBinding.descriptorCount ||
            ownerBinding->isRuntimeSized != requiredBinding.isRuntimeSized ||
            ownerBinding->descriptorType != requiredBinding.descriptorType ||
            ownerBinding->bindingFlags != requiredBinding.bindingFlags ||
            !shaderStageFlagsCover(ownerBinding->stageFlags, requiredBinding.stageFlags))
        {
            error = std::format(
                "Reflection root '{}' has an incompatible descriptor at set={}, binding={} required by '{}'.",
                ownerName, requiredBinding.set, requiredBinding.binding, requiredName);
        }
    });
    if (error.has_value())
    {
        return error;
    }

    std::ranges::for_each(required.pushConstantRanges, [&](const ShaderPushConstantAbiRange &requiredRange) {
        if (error.has_value())
        {
            return;
        }

        auto const requiredEnd = static_cast<std::uint64_t>(requiredRange.offset) + requiredRange.size;
        auto ownerRange =
            std::ranges::find_if(owner.pushConstantRanges, [&](const ShaderPushConstantAbiRange &candidate) {
                auto const candidateEnd = static_cast<std::uint64_t>(candidate.offset) + candidate.size;
                return candidate.offset <= requiredRange.offset && candidateEnd >= requiredEnd &&
                       shaderStageFlagsCover(candidate.stageFlags, requiredRange.stageFlags);
            });
        if (ownerRange == owner.pushConstantRanges.end())
        {
            error = std::format("Reflection root '{}' does not cover push-constant bytes [{}, {}) required by '{}'.",
                                ownerName, requiredRange.offset, requiredEnd, requiredName);
        }
    });
    return error;
}

void assertReflectionLayoutCoverage(const ShaderLayoutAbiSignature &ownerSignature, const SlangProgram &ownerProgram,
                                    const SlangProgram &requiredProgram, const DescriptorBindingPolicy &policy)
{
    auto requiredLayout = ShaderDescriptorLayout::create(requiredProgram, policy);
    nrAssert(requiredLayout.valid(), "Pipeline reflection coverage validation requires a valid required-stage layout.");
    auto validation =
        validateReflectionLayoutCoverage(ownerSignature, requiredLayout.abiSignature(), programDebugName(ownerProgram),
                                         programDebugName(requiredProgram));
    nrAssert(!validation.has_value(), "{}", validation.value_or(std::string{}));
}
} // namespace

[[nodiscard]] CursorPipelineLayout CursorPipelineLayout::create(
    const vk::raii::Device &device, const ShaderDescriptorLayout &descriptorLayout,
    std::uint32_t maxBoundDescriptorSets,
    std::span<const SlangImmutableSamplerBinding> immutableSamplers)
{
    nrAssert(*device != nullptr, "CursorPipelineLayout::create requires a valid Vulkan device.");
    nrAssert(descriptorLayout.valid(), "CursorPipelineLayout::create requires a valid descriptor layout.");
    nrAssert(maxBoundDescriptorSets > 0u,
             "CursorPipelineLayout::create requires a non-zero maxBoundDescriptorSets limit.");
    CursorPipelineLayout layout;

    struct ImmutableSamplerBindingBuildState
    {
        std::uint32_t set = 0;
        std::uint32_t binding = 0;
        std::vector<vk::Sampler> rawSamplers;
        bool isApplied = false;
    };
    auto immutableSamplerBindings = std::vector<ImmutableSamplerBindingBuildState>{};

    auto setLayouts = descriptorLayout.descriptorSets();
    layout.immutableSamplers_.reserve(immutableSamplers.size());
    immutableSamplerBindings.reserve(immutableSamplers.size());

    auto findDescriptorBinding = [setLayouts](std::uint32_t setIndex,
                                              std::uint32_t bindingIndex) -> const DescriptorBindingInfo * {
        auto setIt = std::ranges::find_if(
            setLayouts, [setIndex](const DescriptorSetLayoutInfo &setInfo) { return setInfo.set == setIndex; });
        if (setIt == std::ranges::end(setLayouts))
        {
            return nullptr;
        }

        auto bindingIt =
            std::ranges::find_if(setIt->bindings, [bindingIndex](const DescriptorBindingInfo &bindingInfo) {
                return bindingInfo.binding == bindingIndex;
            });
        if (bindingIt == std::ranges::end(setIt->bindings))
        {
            return nullptr;
        }

        return &(*bindingIt);
    };

    std::ranges::for_each(immutableSamplers, [&](const SlangImmutableSamplerBinding &immutableSamplerBinding) {
        nrAssert(std::ranges::none_of(immutableSamplerBindings,
                                      [&](const ImmutableSamplerBindingBuildState &state) {
                                          return state.set == immutableSamplerBinding.set &&
                                                 state.binding == immutableSamplerBinding.binding;
                                      }),
                 "CursorPipelineLayout::create duplicate immutable sampler binding at set={}, binding={}",
                 immutableSamplerBinding.set, immutableSamplerBinding.binding);

        nrAssert(immutableSamplerBinding.descriptorCount > 0,
                 "CursorPipelineLayout::create immutable sampler binding must have descriptorCount > 0 at "
                 "set={}, binding={}",
                 immutableSamplerBinding.set, immutableSamplerBinding.binding);

        auto const *bindingInfo = findDescriptorBinding(immutableSamplerBinding.set, immutableSamplerBinding.binding);
        nrAssert(bindingInfo != nullptr,
                 "CursorPipelineLayout::create immutable sampler target not found at set={}, binding={}",
                 immutableSamplerBinding.set, immutableSamplerBinding.binding);

        nrAssert(bindingInfo->descriptorType == vk::DescriptorType::eSampler ||
                     bindingInfo->descriptorType == vk::DescriptorType::eCombinedImageSampler,
                 "CursorPipelineLayout::create immutable sampler target must be "
                 "sampler/combined-image-sampler at set={}, binding={}, descriptorType={}",
                 immutableSamplerBinding.set, immutableSamplerBinding.binding,
                 vk::to_string(bindingInfo->descriptorType));

        nrAssert(bindingInfo->descriptorCount == immutableSamplerBinding.descriptorCount,
                 "CursorPipelineLayout::create immutable sampler descriptorCount mismatch at set={}, "
                 "binding={}, layoutCount={}, immutableCount={}",
                 immutableSamplerBinding.set, immutableSamplerBinding.binding, bindingInfo->descriptorCount,
                 immutableSamplerBinding.descriptorCount);

        ImmutableSamplerBindingBuildState state{};
        state.set = immutableSamplerBinding.set;
        state.binding = immutableSamplerBinding.binding;

        auto samplerDebugName = std::format("immutable_sampler_s{}_b{}", state.set, state.binding);
        layout.immutableSamplers_.push_back(
            SlangSampler::create(device, immutableSamplerBinding.samplerDesc, samplerDebugName));
        nrAssert(layout.immutableSamplers_.back().valid(),
                 "CursorPipelineLayout::create failed to create immutable sampler '{}'.", samplerDebugName);
        state.rawSamplers.resize(immutableSamplerBinding.descriptorCount, layout.immutableSamplers_.back().raw());

        immutableSamplerBindings.push_back(std::move(state));
    });

    auto setInfoByIndex = std::map<std::uint32_t, std::reference_wrapper<const DescriptorSetLayoutInfo>>{};
    std::ranges::for_each(setLayouts, [&](const DescriptorSetLayoutInfo &setInfo) {
        auto const [_, inserted] = setInfoByIndex.emplace(setInfo.set, std::cref(setInfo));
        nrAssert(inserted, "CursorPipelineLayout::create found duplicate descriptor set {}.", setInfo.set);
    });

    auto maxSetIndex = setInfoByIndex.empty() ? std::uint32_t{0} : setInfoByIndex.rbegin()->first;
    nrAssert(setInfoByIndex.empty() || maxSetIndex < maxBoundDescriptorSets,
             "CursorPipelineLayout::create descriptor set {} exceeds maxBoundDescriptorSets {}.", maxSetIndex,
             maxBoundDescriptorSets);
    auto pipelineSetLayoutCount = setInfoByIndex.empty() ? std::size_t{0} : static_cast<std::size_t>(maxSetIndex) + 1u;
    layout.setLayouts_.reserve(pipelineSetLayoutCount);

    std::vector<vk::DescriptorSetLayout> pipelineSetLayouts;
    pipelineSetLayouts.reserve(pipelineSetLayoutCount);

    auto makeSetLayout = [&](std::uint32_t setIndex, const DescriptorSetLayoutInfo *setInfo) {
        auto bindings = setInfo ? descriptorLayout.makeVkSetLayoutBindings(setIndex)
                                : std::vector<vk::DescriptorSetLayoutBinding>{};
        auto bindingFlags = setInfo ? descriptorLayout.makeVkSetLayoutBindingFlags(setIndex)
                                    : std::vector<vk::DescriptorBindingFlags>{};
        std::ranges::for_each(bindings, [&](vk::DescriptorSetLayoutBinding &binding) {
            auto immutableSamplerIt =
                std::ranges::find_if(immutableSamplerBindings, [&](ImmutableSamplerBindingBuildState &state) {
                    return state.set == setIndex && state.binding == binding.binding;
                });
            if (immutableSamplerIt == std::ranges::end(immutableSamplerBindings))
            {
                return;
            }

            nrAssert(binding.descriptorCount == immutableSamplerIt->rawSamplers.size(),
                     "CursorPipelineLayout::create immutable sampler descriptorCount mismatch at set={}, "
                     "binding={}, layoutCount={}, immutableCount={}",
                     setIndex, binding.binding, binding.descriptorCount, immutableSamplerIt->rawSamplers.size());

            binding.pImmutableSamplers = immutableSamplerIt->rawSamplers.data();
            immutableSamplerIt->isApplied = true;
        });

        vk::DescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{};
        vk::DescriptorSetLayoutCreateInfo setLayoutInfo{};
        nrAssert(std::in_range<std::uint32_t>(bindings.size()),
                 "Descriptor set {} has too many bindings for the Vulkan uint32 ABI.", setIndex);
        setLayoutInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
        setLayoutInfo.pBindings = bindings.data();
        if (!bindingFlags.empty())
        {
            bindingFlagsInfo.bindingCount = static_cast<std::uint32_t>(bindingFlags.size());
            bindingFlagsInfo.pBindingFlags = bindingFlags.data();
            setLayoutInfo.pNext = &bindingFlagsInfo;
            if (std::ranges::any_of(bindingFlags, [](vk::DescriptorBindingFlags flags) {
                    return (flags & vk::DescriptorBindingFlagBits::eUpdateAfterBind) ==
                           vk::DescriptorBindingFlagBits::eUpdateAfterBind;
                }))
            {
                setLayoutInfo.flags |= vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;
            }
        }

        auto support = device.getDescriptorSetLayoutSupport<vk::DescriptorSetLayoutSupport,
                                                            vk::DescriptorSetVariableDescriptorCountLayoutSupport>(
            setLayoutInfo);
        nrAssert(support.get<vk::DescriptorSetLayoutSupport>().supported == vk::True,
                 "Vulkan does not support reflected descriptor set layout {}.", setIndex);
        auto variableBinding = std::ranges::find_if(bindingFlags, [](vk::DescriptorBindingFlags flags) {
            return (flags & vk::DescriptorBindingFlagBits::eVariableDescriptorCount) != vk::DescriptorBindingFlags{};
        });
        if (variableBinding != bindingFlags.end())
        {
            auto bindingIndex = static_cast<std::size_t>(std::distance(bindingFlags.begin(), variableBinding));
            auto maxVariableDescriptorCount =
                support.get<vk::DescriptorSetVariableDescriptorCountLayoutSupport>().maxVariableDescriptorCount;
            nrAssert(bindings[bindingIndex].descriptorCount <= maxVariableDescriptorCount,
                     "Descriptor set {} variable binding {} requests {} descriptors, but Vulkan supports "
                     "at most {} for this layout.",
                     setIndex, bindings[bindingIndex].binding, bindings[bindingIndex].descriptorCount,
                     maxVariableDescriptorCount);
        }

        layout.setLayouts_.push_back(DescriptorSetLayoutHandle{
            .set = setIndex,
            .layout = vk::raii::DescriptorSetLayout(device, setLayoutInfo),
            .isPlaceholder = setInfo == nullptr,
        });
        pipelineSetLayouts.push_back(*layout.setLayouts_.back().layout);
    };

    if (!setInfoByIndex.empty())
    {
        std::ranges::for_each(std::views::iota(std::uint32_t{0}, maxSetIndex + 1u), [&](std::uint32_t setIndex) {
            auto setIt = setInfoByIndex.find(setIndex);
            makeSetLayout(setIndex, setIt != setInfoByIndex.end() ? std::addressof(setIt->second.get()) : nullptr);
        });
    }

    std::ranges::for_each(immutableSamplerBindings, [](const ImmutableSamplerBindingBuildState &state) {
        nrAssert(state.isApplied,
                 "CursorPipelineLayout::create immutable sampler binding was not used by descriptor layout at set={}, "
                 "binding={}",
                 state.set, state.binding);
    });

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.setLayoutCount = static_cast<std::uint32_t>(pipelineSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts = pipelineSetLayouts.data();
    auto pushConstantRanges = descriptorLayout.makeVkPushConstantRanges();
    std::ranges::for_each(pushConstantRanges, [](const vk::PushConstantRange &range) {
        nrAssert(range.size <= kMaxPushConstantBytes,
                 "CursorPipelineLayout::create push constant range exceeds hard limit. size={} max={}", range.size,
                 kMaxPushConstantBytes);
    });
    pipelineLayoutInfo.pushConstantRangeCount = static_cast<std::uint32_t>(pushConstantRanges.size());
    pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges.data();
    layout.pipelineLayout_ = vk::raii::PipelineLayout(device, pipelineLayoutInfo);
    return layout;
}

[[nodiscard]] bool CursorPipelineLayout::valid() const noexcept
{
    return *pipelineLayout_ != nullptr;
}

[[nodiscard]] vk::PipelineLayout CursorPipelineLayout::raw() const noexcept
{
    return valid() ? *pipelineLayout_ : vk::PipelineLayout{};
}

[[nodiscard]] std::optional<vk::DescriptorSetLayout> CursorPipelineLayout::descriptorSetLayout(
    std::uint32_t setIndex) const noexcept
{
    auto it = std::ranges::find_if(setLayouts_, [setIndex](const DescriptorSetLayoutHandle &handle) {
        return handle.set == setIndex && !handle.isPlaceholder;
    });
    if (it == std::ranges::end(setLayouts_))
    {
        return std::nullopt;
    }
    return *it->layout;
}

[[nodiscard]] std::vector<std::uint32_t> CursorPipelineLayout::setIndices() const
{
    return setLayouts_ |
           std::views::filter([](const DescriptorSetLayoutHandle &handle) { return !handle.isPlaceholder; }) |
           std::views::transform([](const DescriptorSetLayoutHandle &handle) { return handle.set; }) |
           std::ranges::to<std::vector>();
}

void CursorPipelineLayout::pushConstants(const vk::raii::CommandBuffer &commandBuffer, vk::ShaderStageFlags stageFlags,
                                         std::uint32_t offset, std::span<const std::uint8_t> bytes) const
{
    nrAssert(valid(), "CursorPipelineLayout::pushConstants requires a valid pipeline layout.");
    nrAssert(*commandBuffer != nullptr, "CursorPipelineLayout::pushConstants requires a valid command buffer.");
    if (bytes.empty())
    {
        return;
    }
    nrAssert(bytes.size() <= std::numeric_limits<std::uint32_t>::max(),
             "CursorPipelineLayout::pushConstants payload too large: {} bytes", bytes.size());
    nrAssert(static_cast<std::uint64_t>(offset) + static_cast<std::uint64_t>(bytes.size()) <= kMaxPushConstantBytes,
             "CursorPipelineLayout::pushConstants write exceeds hard limit. offset={}, size={}, max={}", offset,
             bytes.size(), kMaxPushConstantBytes);
    commandBuffer.pushConstants(
        raw(), stageFlags, offset,
        vk::ArrayProxy<const std::uint8_t>(static_cast<std::uint32_t>(bytes.size()), bytes.data()));
}

void CursorPipelineLayout::bindDescriptorSets(const vk::raii::CommandBuffer &commandBuffer,
                                              vk::PipelineBindPoint bindPoint, std::span<const ShaderBindingSet> sets,
                                              std::span<const std::uint32_t> dynamicOffsets) const
{
    nrAssert(valid(), "CursorPipelineLayout::bindDescriptorSets requires a valid pipeline layout.");
    nrAssert(*commandBuffer != nullptr, "CursorPipelineLayout::bindDescriptorSets requires a valid command buffer.");
    nrAssert(dynamicOffsets.empty(), "CursorPipelineLayout::bindDescriptorSets with multiple sets does not accept "
                                     "shared dynamic offsets. Bind per-set when using dynamic offsets.");

    auto runFirstSetIndex = std::optional<std::uint32_t>{};
    auto runDescriptorSets = std::vector<vk::DescriptorSet>{};
    runDescriptorSets.reserve(sets.size());

    auto flushRun = [&]() {
        if (!runFirstSetIndex.has_value() || runDescriptorSets.empty())
        {
            return;
        }

        commandBuffer.bindDescriptorSets(bindPoint, raw(), *runFirstSetIndex, runDescriptorSets, {});
        runFirstSetIndex.reset();
        runDescriptorSets.clear();
    };

    std::ranges::for_each(sets, [&](const ShaderBindingSet &set) {
        if (!set.valid())
        {
            flushRun();
            return;
        }

        auto const setIndex = set.setIndex();
        if (!runFirstSetIndex.has_value())
        {
            runFirstSetIndex = setIndex;
        }
        else
        {
            auto const expectedSetIndex = *runFirstSetIndex + static_cast<std::uint32_t>(runDescriptorSets.size());
            if (setIndex != expectedSetIndex)
            {
                flushRun();
                runFirstSetIndex = setIndex;
            }
        }

        runDescriptorSets.push_back(set.raw());
    });
    flushRun();
}

std::vector<ShaderBindingSet> allocateBindingSetsForLayout(
    const CursorPipelineLayout &layout, ShaderBindingPool &pool,
    const std::map<std::uint32_t, std::uint32_t> &variableDescriptorCountsBySet)
{
    nrAssert(layout.valid(), "allocateBindingSetsForLayout requires a valid cursor pipeline layout.");

    auto setIndices = layout.setIndices();
    nrAssert(std::ranges::all_of(variableDescriptorCountsBySet, [&](const auto &entry) {
                 return std::ranges::find(setIndices, entry.first) != setIndices.end();
             }),
             "allocateBindingSetsForLayout received a variable descriptor count for an unknown set.");
    auto sets = std::vector<ShaderBindingSet>{};
    sets.reserve(setIndices.size());

    std::ranges::for_each(setIndices, [&](std::uint32_t setIndex) {
        auto descriptorSetLayout = layout.descriptorSetLayout(setIndex);
        nrAssert(descriptorSetLayout.has_value(),
                 "allocateBindingSetsForLayout missing descriptor set layout for set {}.", setIndex);

        auto requestedVariableCount = variableDescriptorCountsBySet.find(setIndex);
        auto set = pool.allocate(*descriptorSetLayout, setIndex,
                                 requestedVariableCount != variableDescriptorCountsBySet.end()
                                     ? std::optional<std::uint32_t>(requestedVariableCount->second)
                                     : std::nullopt);
        nrAssert(set.valid(), "allocateBindingSetsForLayout failed to allocate descriptor set for set {}.", setIndex);
        sets.push_back(std::move(set));
    });

    return sets;
}

std::vector<ShaderBindingSet> allocateBindingSetsForLayout(const CursorPipelineLayout &layout, ShaderBindingPool &pool)
{
    return allocateBindingSetsForLayout(layout, pool, {});
}

void updateResourcesForBindingSnapshot(ShaderBindingPool &pool, std::span<const ShaderBindingSet> sets,
                                       DescriptorWriteCache &descriptorWriteCache,
                                       const ShaderBindingSnapshot &snapshot, LogicalDescriptorResolver logicalResolver)
{
    auto writeRequests = resolveDescriptorWriteRequests(snapshot, std::move(logicalResolver));
    auto changedWriteRequests = descriptorWriteCache.filterChanged(writeRequests);
    if (!changedWriteRequests.empty())
    {
        auto requestsBySet = std::map<std::uint32_t, std::vector<DescriptorWriteRequest>>{};
        std::ranges::for_each(changedWriteRequests, [&](const DescriptorWriteRequest &request) {
            requestsBySet[request.binding.set].push_back(request);
        });

        std::ranges::for_each(sets, [&](const ShaderBindingSet &set) {
            if (!set.valid())
            {
                return;
            }

            auto it = requestsBySet.find(set.setIndex());
            if (it == requestsBySet.end())
            {
                return;
            }

            pool.update(set, it->second);
            requestsBySet.erase(it);
        });

        nrAssert(requestsBySet.empty(),
                 "updateResourcesForBindingSnapshot could not find descriptor sets for one or more snapshot writes.");
        descriptorWriteCache.commit(changedWriteRequests);
    }
}

void bindPreparedResourcesToCommandBuffer(const vk::raii::CommandBuffer &commandBuffer, vk::PipelineBindPoint bindPoint,
                                          const CursorPipelineLayout &layout, std::span<const ShaderBindingSet> sets)
{
    nrAssert(layout.valid(), "bindPreparedResourcesToCommandBuffer requires a valid cursor pipeline layout.");
    nrAssert(*commandBuffer != nullptr, "bindPreparedResourcesToCommandBuffer requires a valid command buffer.");

    if (!sets.empty())
    {
        layout.bindDescriptorSets(commandBuffer, bindPoint, sets);
    }
}

void pushConstantsToCommandBuffer(const vk::raii::CommandBuffer &commandBuffer, const CursorPipelineLayout &layout,
                                  const ShaderBindingSnapshot &snapshot)
{
    nrAssert(layout.valid(), "pushConstantsToCommandBuffer requires a valid cursor pipeline layout.");
    nrAssert(*commandBuffer != nullptr, "pushConstantsToCommandBuffer requires a valid command buffer.");

    std::ranges::for_each(snapshot.pushConstantWrites(), [&](const PushConstantWriteRecord &record) {
        if (record.data.empty())
        {
            return;
        }

        layout.pushConstants(commandBuffer, record.range.stageFlags, record.offset,
                             std::span<const std::uint8_t>{record.data.data(), record.data.size()});
    });
}

template <vk::ObjectType ObjectType, typename Handle>
void setDebugObjectNameChecked(const vk::raii::Device &device, const Handle &handle, std::string_view name)
{
    if constexpr (gpuDebugNamesEnabled)
    {
        try
        {
            nr::rhi::setDebugObjectName<ObjectType>(device, handle, name);
        }
        catch (const vk::SystemError &error)
        {
            auto errorText = std::string_view{error.what()};
            nrLog<LogLevel::warning>("Failed to set Vulkan debug name '{}': {}", name, errorText);
            nrAssert(false, "Failed to set a Vulkan debug object name.");
        }
    }
}

void setShaderModuleDebugName(const vk::raii::Device &device, const vk::raii::ShaderModule &shaderModule,
                              std::string_view name)
{
    setDebugObjectNameChecked<vk::ObjectType::eShaderModule>(device, shaderModule, name);
}

void setPipelineDebugName(const vk::raii::Device &device, vk::Pipeline pipeline, std::string_view name)
{
    setDebugObjectNameChecked<vk::ObjectType::ePipeline>(device, pipeline, name);
}

[[nodiscard]] std::string makeShaderModuleDebugName(const SlangEntryPointData &entryPoint)
{
    if (entryPoint.debugName.empty())
    {
        return entryPoint.entryPointName.empty() ? std::string{"shader"} : entryPoint.entryPointName;
    }

    return entryPoint.debugName;
}

void VkShaderProgram::appendStage(VkShaderProgram &program, const vk::raii::Device &device,
                                  const SlangEntryPointData &entryPoint, std::string logicalEntryPointName)
{
    nrAssert(entryPoint.valid(), "Entry point '{}' has no valid SPIR-V artifact.", entryPoint.entryPointName);

    vk::ShaderModuleCreateInfo moduleInfo{};
    moduleInfo.codeSize = entryPoint.spirv->size() * sizeof(std::uint32_t);
    moduleInfo.pCode = entryPoint.spirv->data();
    program.modules_.emplace_back(device, moduleInfo);
    setShaderModuleDebugName(device, program.modules_.back(), makeShaderModuleDebugName(entryPoint));

    program.shaderEntryPointNames_.push_back(entryPoint.entryPointName);
    program.logicalEntryPointNames_.push_back(std::move(logicalEntryPointName));
    program.stages_.push_back(entryPoint.stage);

    auto stageInfo = vk::PipelineShaderStageCreateInfo{};
    stageInfo.stage = toVkShaderStage(entryPoint.stage);
    stageInfo.module = *program.modules_.back();
    stageInfo.pName = program.shaderEntryPointNames_.back().c_str();
    program.stageCreateInfos_.push_back(stageInfo);
}

[[nodiscard]] VkShaderProgram VkShaderProgram::create(const vk::raii::Device &device,
                                                      std::span<const SlangProgram> programs)
{
    nrAssert(!programs.empty(), "VkShaderProgram::create requires at least one single-entry program.");

    VkShaderProgram result;
    result.modules_.reserve(programs.size());
    result.shaderEntryPointNames_.reserve(programs.size());
    result.logicalEntryPointNames_.reserve(programs.size());
    result.stages_.reserve(programs.size());
    result.stageCreateInfos_.reserve(programs.size());
    std::ranges::for_each(programs, [&](const SlangProgram &program) {
        auto const *entryPoint = program.entryPoint();
        nrAssert(entryPoint != nullptr, "VkShaderProgram::create received an invalid single-entry program.");
        appendStage(result, device, *entryPoint, entryPoint->entryPointName);
    });
    return result;
}

[[nodiscard]] VkShaderProgram VkShaderProgram::create(
    const vk::raii::Device &device, std::span<const RayTracingPipelineStageSelection> selectedEntryPoints)
{
    nrAssert(!selectedEntryPoints.empty(), "VkShaderProgram::create requires at least one selected entrypoint.");

    VkShaderProgram result;
    result.modules_.reserve(selectedEntryPoints.size());
    result.shaderEntryPointNames_.reserve(selectedEntryPoints.size());
    result.logicalEntryPointNames_.reserve(selectedEntryPoints.size());
    result.stages_.reserve(selectedEntryPoints.size());
    result.stageCreateInfos_.reserve(selectedEntryPoints.size());
    std::ranges::for_each(selectedEntryPoints, [&](const RayTracingPipelineStageSelection &selection) {
        auto const *entryPoint = selection.program.get().entryPoint();
        nrAssert(entryPoint != nullptr, "VkShaderProgram::create received an invalid single-entry RT program.");
        appendStage(result, device, *entryPoint, selection.logicalEntryPointName);
    });
    return result;
}

[[nodiscard]] bool VkShaderProgram::valid() const noexcept
{
    return !stageCreateInfos_.empty();
}

[[nodiscard]] const vk::PipelineShaderStageCreateInfo &VkShaderProgram::stageCreateInfo(
    std::uint32_t index) const noexcept
{
    return stageCreateInfos_[index];
}

[[nodiscard]] std::span<const SlangStage> VkShaderProgram::stages() const noexcept
{
    return stages_;
}

[[nodiscard]] std::span<const std::string> VkShaderProgram::logicalEntryPointNames() const noexcept
{
    return logicalEntryPointNames_;
}

[[nodiscard]] GraphicsPipeline GraphicsPipeline::create(
    const vk::raii::Device &device, const CursorPipelineLayout &layout, const VkShaderProgram &shaderProgram,
    PipelineBinaryStore &binaryStore, std::uint64_t contentFingerprint, const GraphicsPipelineDesc &desc)
{
    nrAssert(!desc.colorAttachmentFormats.empty() || desc.depthAttachmentFormat.has_value() ||
                 desc.stencilAttachmentFormat.has_value(),
             "GraphicsPipeline::create requires at least one attachment format when using dynamic rendering.");
    nrAssert(layout.valid(), "GraphicsPipeline::create requires a valid pipeline layout.");
    nrAssert(shaderProgram.valid(), "GraphicsPipeline::create requires a valid shader program.");

    auto graphicsStageIndices =
        std::views::iota(std::uint32_t{0}, static_cast<std::uint32_t>(shaderProgram.stages().size())) |
        std::views::filter([&](std::uint32_t index) { return isGraphicsStage(shaderProgram.stages()[index]); }) |
        std::ranges::to<std::vector>();
    nrAssert(!graphicsStageIndices.empty(), "GraphicsPipeline::create requires at least one graphics shader stage.");

    auto stageCreateInfos =
        graphicsStageIndices |
        std::views::transform([&](std::uint32_t stageIndex) { return shaderProgram.stageCreateInfo(stageIndex); }) |
        std::ranges::to<std::vector>();

    nrAssert(std::ranges::any_of(
                 graphicsStageIndices,
                 [&](std::uint32_t stageIndex) { return shaderProgram.stages()[stageIndex] == SLANG_STAGE_VERTEX; }),
             "GraphicsPipeline::create requires a vertex shader stage.");

    auto colorBlendAttachments = normalizedColorBlendAttachments(desc);
    nrAssert(colorBlendAttachments.size() == desc.colorAttachmentFormats.size(),
             "GraphicsPipeline::create color blend attachment count mismatch. formats={}, blends={}",
             desc.colorAttachmentFormats.size(), colorBlendAttachments.size());

    auto dynamicStates = std::vector{
        vk::DynamicState::eViewport,           vk::DynamicState::eScissor,
        vk::DynamicState::eCullModeEXT,        vk::DynamicState::eFrontFaceEXT,
        vk::DynamicState::eDepthTestEnableEXT, vk::DynamicState::eDepthWriteEnableEXT,
        vk::DynamicState::eDepthCompareOpEXT,  vk::DynamicState::ePrimitiveTopologyEXT,
    };

    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<std::uint32_t>(desc.vertexBindings.size());
    vertexInputInfo.pVertexBindingDescriptions = desc.vertexBindings.empty() ? nullptr : desc.vertexBindings.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(desc.vertexAttributes.size());
    vertexInputInfo.pVertexAttributeDescriptions =
        desc.vertexAttributes.empty() ? nullptr : desc.vertexAttributes.data();
    vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo{};
    inputAssemblyInfo.topology = desc.topology;
    inputAssemblyInfo.primitiveRestartEnable = vk::False;

    vk::PipelineViewportStateCreateInfo viewportStateInfo{};
    viewportStateInfo.viewportCount = 1;
    viewportStateInfo.scissorCount = 1;

    vk::PipelineRasterizationStateCreateInfo rasterizationInfo{};
    rasterizationInfo.depthClampEnable = vk::False;
    rasterizationInfo.rasterizerDiscardEnable = vk::False;
    rasterizationInfo.polygonMode = desc.polygonMode;
    rasterizationInfo.cullMode = desc.cullMode;
    rasterizationInfo.frontFace = desc.frontFace;
    rasterizationInfo.depthBiasEnable = vk::False;
    rasterizationInfo.lineWidth = 1.0f;

    vk::PipelineMultisampleStateCreateInfo multisampleInfo{};
    multisampleInfo.rasterizationSamples = desc.sampleCount;
    multisampleInfo.sampleShadingEnable = vk::False;

    vk::PipelineDepthStencilStateCreateInfo depthStencilInfo{};
    depthStencilInfo.depthTestEnable = desc.depthTestEnable ? vk::True : vk::False;
    depthStencilInfo.depthWriteEnable = desc.depthWriteEnable ? vk::True : vk::False;
    depthStencilInfo.depthCompareOp = desc.depthCompareOp;
    depthStencilInfo.depthBoundsTestEnable = vk::False;
    depthStencilInfo.stencilTestEnable = desc.stencilAttachmentFormat.has_value() ? vk::True : vk::False;

    vk::PipelineColorBlendStateCreateInfo colorBlendInfo{};
    colorBlendInfo.logicOpEnable = vk::False;
    colorBlendInfo.attachmentCount = static_cast<std::uint32_t>(colorBlendAttachments.size());
    colorBlendInfo.pAttachments = colorBlendAttachments.data();

    vk::PipelineDynamicStateCreateInfo dynamicStateInfo{};
    dynamicStateInfo.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
    dynamicStateInfo.pDynamicStates = dynamicStates.data();

    auto renderingColorFormats = desc.colorAttachmentFormats;
    vk::PipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.colorAttachmentCount = static_cast<std::uint32_t>(renderingColorFormats.size());
    renderingInfo.pColorAttachmentFormats = renderingColorFormats.data();
    renderingInfo.depthAttachmentFormat = desc.depthAttachmentFormat.value_or(vk::Format::eUndefined);
    renderingInfo.stencilAttachmentFormat = desc.stencilAttachmentFormat.value_or(vk::Format::eUndefined);

    vk::GraphicsPipelineCreateInfo createInfo{};
    createInfo.stageCount = static_cast<std::uint32_t>(stageCreateInfos.size());
    createInfo.pStages = stageCreateInfos.data();
    createInfo.pVertexInputState = &vertexInputInfo;
    createInfo.pInputAssemblyState = &inputAssemblyInfo;
    createInfo.pViewportState = &viewportStateInfo;
    createInfo.pRasterizationState = &rasterizationInfo;
    createInfo.pMultisampleState = &multisampleInfo;
    createInfo.pDepthStencilState = &depthStencilInfo;
    createInfo.pColorBlendState = &colorBlendInfo;
    createInfo.pDynamicState = &dynamicStateInfo;
    createInfo.layout = layout.raw();
    createInfo.pNext = &renderingInfo;

    GraphicsPipeline pipeline;
    try
    {
        pipeline.pipeline_ = loadOrCreateAndCapturePipeline(
            binaryStore, contentFingerprint, createInfo, "GraphicsPipeline",
            [&device](const vk::GraphicsPipelineCreateInfo &info) {
                return vk::raii::Pipeline(device, nullptr, info);
            });
    }
    catch (const vk::SystemError &error)
    {
        nrLog<LogLevel::error>("GraphicsPipeline creation failed: {}", error.what());
        return {};
    }
    return pipeline;
}

[[nodiscard]] bool GraphicsPipeline::valid() const noexcept
{
    return *pipeline_ != nullptr;
}

[[nodiscard]] vk::Pipeline GraphicsPipeline::raw() const noexcept
{
    return valid() ? *pipeline_ : vk::Pipeline{};
}

[[nodiscard]] ComputePipeline ComputePipeline::create(const vk::raii::Device &device,
                                                      const CursorPipelineLayout &layout,
                                                      const VkShaderProgram &shaderProgram,
                                                      PipelineBinaryStore &binaryStore,
                                                      std::uint64_t contentFingerprint)
{
    nrAssert(layout.valid(), "ComputePipeline::create requires a valid pipeline layout.");
    nrAssert(shaderProgram.valid(), "ComputePipeline::create requires a valid shader program.");

    auto indices = std::views::iota(std::uint32_t{0}, static_cast<std::uint32_t>(shaderProgram.stages().size()));
    auto it = std::ranges::find_if(
        indices, [&](std::uint32_t index) { return shaderProgram.stages()[index] == SLANG_STAGE_COMPUTE; });
    nrAssert(it != std::ranges::end(indices), "ComputePipeline::create requires at least one compute entry point.");
    auto stageIndex = *it;

    vk::ComputePipelineCreateInfo createInfo{};
    createInfo.stage = shaderProgram.stageCreateInfo(stageIndex);
    createInfo.layout = layout.raw();

    ComputePipeline pipeline;
    try
    {
        pipeline.pipeline_ = loadOrCreateAndCapturePipeline(
            binaryStore, contentFingerprint, createInfo, "ComputePipeline",
            [&device](const vk::ComputePipelineCreateInfo &info) { return vk::raii::Pipeline(device, nullptr, info); });
    }
    catch (const vk::SystemError &error)
    {
        nrLog<LogLevel::error>("ComputePipeline creation failed: {}", error.what());
        return {};
    }
    return pipeline;
}

[[nodiscard]] bool ComputePipeline::valid() const noexcept
{
    return *pipeline_ != nullptr;
}

[[nodiscard]] vk::Pipeline ComputePipeline::raw() const noexcept
{
    return valid() ? *pipeline_ : vk::Pipeline{};
}

namespace
{
struct DeferredHostJoinResult
{
    vk::Result status = vk::Result::eErrorUnknown;
    std::string error{};

    [[nodiscard]] bool valid() const noexcept
    {
        return error.empty();
    }
};

[[nodiscard]] DeferredHostJoinResult joinDeferredHostOperation(const vk::raii::DeferredOperationKHR &operation)
{
    try
    {
        auto result = operation.join();
        while (result == vk::Result::eThreadIdleKHR)
        {
            std::this_thread::yield();
            result = operation.join();
        }
        return {.status = result};
    }
    catch (const vk::SystemError &error)
    {
        auto message = std::format("RayTracingPipeline deferred host worker failed while joining: {}", error.what());
        nrLog<LogLevel::warning, "LOG">("{}", message);
        return {.error = std::move(message)};
    }
}
} // namespace

[[nodiscard]] RayTracingPipeline RayTracingPipeline::create(
    const vk::raii::Device &device, const CursorPipelineLayout &layout, const VkShaderProgram &shaderProgram,
    threading::StaticThreadPool &deferredHostPool, const RayTracingCapabilitySnapshot &capabilities,
    PipelineBinaryStore &binaryStore, std::uint64_t contentFingerprint, const RayTracingPipelineDesc &desc,
    std::span<const RayTracingShaderGroupDesc> groupDescs)
{
    nrAssert(layout.valid(), "RayTracingPipeline::create requires a valid pipeline layout.");
    nrAssert(shaderProgram.valid(), "RayTracingPipeline::create requires a valid shader program.");
    nrAssert(desc.maxRayRecursionDepth > 0u, "RayTracingPipeline::create requires maxRayRecursionDepth > 0.");
    nrAssert(!groupDescs.empty(), "RayTracingPipeline::create requires at least one named shader group.");
    nrAssert(groupDescs.size() <= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()),
             "RayTracingPipeline::create shader group count exceeds uint32 ABI.");
    auto descValidation = validateRayTracingPipelineDesc(desc);
    nrAssert(!descValidation.has_value(), "RayTracingPipeline::create invalid desc: {}",
             descValidation.value_or(std::string{}));

    auto rtStageIndices =
        std::views::iota(std::uint32_t{0}, static_cast<std::uint32_t>(shaderProgram.stages().size())) |
        std::views::filter([&](std::uint32_t index) { return isRayTracingStage(shaderProgram.stages()[index]); }) |
        std::ranges::to<std::vector>();
    nrAssert(!rtStageIndices.empty(), "RayTracingPipeline::create requires at least one ray tracing shader stage.");

    auto stageCreateInfos =
        rtStageIndices |
        std::views::transform([&](std::uint32_t stageIndex) { return shaderProgram.stageCreateInfo(stageIndex); }) |
        std::ranges::to<std::vector>();

    constexpr std::uint32_t shaderUnused = std::numeric_limits<std::uint32_t>::max();
    auto groups = std::vector<vk::RayTracingShaderGroupCreateInfoKHR>{};

    auto findLocalStageIndex = [&](std::string_view entryPointName, std::initializer_list<SlangStage> expectedStages) {
        if (entryPointName.empty())
        {
            return shaderUnused;
        }
        auto it = std::ranges::find(shaderProgram.logicalEntryPointNames(), entryPointName);
        nrAssert(it != std::ranges::end(shaderProgram.logicalEntryPointNames()),
                 "RayTracingPipeline::create unknown logical entrypoint '{}' in custom group.", entryPointName);
        auto globalIndex =
            static_cast<std::uint32_t>(std::distance(std::ranges::begin(shaderProgram.logicalEntryPointNames()), it));
        auto localIt = std::ranges::find(rtStageIndices, globalIndex);
        nrAssert(localIt != std::ranges::end(rtStageIndices),
                 "RayTracingPipeline::create entrypoint '{}' is not a RT stage.", entryPointName);
        auto stage = shaderProgram.stages()[globalIndex];
        nrAssert(std::ranges::find(expectedStages, stage) != expectedStages.end(),
                 "RayTracingPipeline::create entrypoint '{}' stage mismatch for custom group.", entryPointName);
        return static_cast<std::uint32_t>(std::distance(std::ranges::begin(rtStageIndices), localIt));
    };

    groups.reserve(groupDescs.size());
    std::ranges::for_each(groupDescs, [&](const RayTracingShaderGroupDesc &groupDesc) {
        vk::RayTracingShaderGroupCreateInfoKHR group{};
        group.type = groupDesc.type;
        group.generalShader = findLocalStageIndex(groupDesc.generalEntryPoint,
                                                  {SLANG_STAGE_RAY_GENERATION, SLANG_STAGE_MISS, SLANG_STAGE_CALLABLE});
        group.closestHitShader = findLocalStageIndex(groupDesc.closestHitEntryPoint, {SLANG_STAGE_CLOSEST_HIT});
        group.anyHitShader = findLocalStageIndex(groupDesc.anyHitEntryPoint, {SLANG_STAGE_ANY_HIT});
        group.intersectionShader = findLocalStageIndex(groupDesc.intersectionEntryPoint, {SLANG_STAGE_INTERSECTION});
        groups.push_back(group);
    });

    vk::RayTracingPipelineCreateInfoKHR createInfo{};
    createInfo.flags = desc.flags;
    createInfo.stageCount = static_cast<std::uint32_t>(stageCreateInfos.size());
    createInfo.pStages = stageCreateInfos.data();
    createInfo.groupCount = static_cast<std::uint32_t>(groups.size());
    createInfo.pGroups = groups.data();
    createInfo.maxPipelineRayRecursionDepth = desc.maxRayRecursionDepth;
    createInfo.layout = layout.raw();

    auto const createStart = std::chrono::steady_clock::now();
    auto workerCount = std::uint32_t{0u};
    auto driverConcurrency = std::uint32_t{0u};
    auto creationMode = std::string_view{};
    auto deferredOperation = vk::raii::DeferredOperationKHR{nullptr};
    // Deferred commands may retain pointer parameters until completion. Keep the output
    // slot in this frame, then transfer the completed handle to RAII.
    auto rawPipeline = vk::Pipeline{};
    auto createWithDeferredOperation = [&device, &deferredOperation,
                                        &rawPipeline](const vk::RayTracingPipelineCreateInfoKHR &info) {
        rawPipeline = vk::Pipeline{};
        auto const result = (*device).createRayTracingPipelinesKHR(*deferredOperation, nullptr, 1u, &info, nullptr,
                                                                   &rawPipeline, *device.getDispatcher());
        nrAssert(result == vk::Result::eSuccess || result == vk::Result::eOperationNotDeferredKHR ||
                     result == vk::Result::eOperationDeferredKHR,
                 "RayTracingPipeline creation returned {}.", vk::to_string(result));
        if (result != vk::Result::eSuccess)
        {
            return vk::raii::Pipeline{nullptr};
        }
        return vk::raii::Pipeline{device, static_cast<VkPipeline>(rawPipeline)};
    };
    auto pipelineKey = PipelineBinaryCacheKey{};
    auto loadedFromCache = false;
    auto pipelineHandle = loadOrCreateAndCapturePipeline<true>(binaryStore, contentFingerprint, createInfo,
                                                               "RayTracingPipeline", createWithDeferredOperation,
                                                               &pipelineKey, &loadedFromCache);
    creationMode = loadedFromCache ? "binary" : "driver-immediate";

    if (*pipelineHandle == nullptr)
    {
        creationMode = "deferred-host";
        auto captureInfo = vk::PipelineCreateFlags2CreateInfoKHR{};
        captureInfo.pNext = createInfo.pNext;
        captureInfo.flags = capturePipelineFlags(createInfo.flags);
        createInfo.pNext = &captureInfo;
        try
        {
            deferredOperation = vk::raii::DeferredOperationKHR{device};
        }
        catch (const vk::SystemError &error)
        {
            nrLog<LogLevel::error>("RayTracingPipeline failed to create a deferred host operation: {}", error.what());
            return {};
        }

        rawPipeline = vk::Pipeline{};
        auto const initialResult = (*device).createRayTracingPipelinesKHR(
            *deferredOperation, nullptr, 1u, &createInfo, nullptr, &rawPipeline, *device.getDispatcher());

        if (initialResult == vk::Result::eOperationDeferredKHR)
        {
            driverConcurrency = deferredOperation.getMaxConcurrency();
            nrAssert(driverConcurrency > 0u,
                     "RayTracingPipeline received a deferred operation with zero available concurrency.");
            workerCount = threading::resolveWorkerCount(0u, driverConcurrency);
            auto const backgroundWorkerCount = workerCount - 1u;
            if (backgroundWorkerCount > 0u)
            {
                deferredHostPool.ensureWorkerCount(backgroundWorkerCount);
            }

            auto futures = std::vector<std::future<DeferredHostJoinResult>>{};
            futures.reserve(backgroundWorkerCount);
            std::ranges::for_each(std::views::iota(std::uint32_t{0u}, backgroundWorkerCount), [&](std::uint32_t) {
                futures.push_back(deferredHostPool.submit(
                    [&deferredOperation] { return joinDeferredHostOperation(deferredOperation); }));
            });
            auto joinResults = std::vector<DeferredHostJoinResult>{};
            joinResults.reserve(workerCount);
            joinResults.push_back(joinDeferredHostOperation(deferredOperation));
            std::ranges::for_each(futures, [&](auto &future) { joinResults.push_back(future.get()); });

            auto failedJoin = std::ranges::find_if(joinResults, [](const auto &result) { return !result.valid(); });
            if (failedJoin != std::ranges::end(joinResults))
            {
                nrAssert(false, "RayTracingPipeline deferred host join failed: {}", failedJoin->error);
            }
            nrAssert(std::ranges::all_of(joinResults,
                                         [](const auto &result) {
                                             return result.status == vk::Result::eSuccess ||
                                                    result.status == vk::Result::eThreadDoneKHR;
                                         }),
                     "RayTracingPipeline deferred host join returned an unexpected status.");
            nrAssert(std::ranges::any_of(joinResults,
                                         [](const auto &result) { return result.status == vk::Result::eSuccess; }),
                     "RayTracingPipeline deferred host workers completed without finalizing the operation.");

            auto finalResult = vk::Result::eErrorUnknown;
            try
            {
                finalResult = deferredOperation.getResult();
            }
            catch (const vk::SystemError &error)
            {
                nrLog<LogLevel::error>("RayTracingPipeline deferred host creation failed: {}", error.what());
                return {};
            }
            nrAssert(finalResult == vk::Result::eSuccess,
                     "RayTracingPipeline deferred host creation returned {}.", vk::to_string(finalResult));
        }
        else
        {
            nrAssert(initialResult == vk::Result::eSuccess || initialResult == vk::Result::eOperationNotDeferredKHR,
                     "RayTracingPipeline creation returned {}.", vk::to_string(initialResult));
        }

        if (initialResult != vk::Result::eOperationDeferredKHR)
        {
            creationMode = "driver-immediate";
        }
        pipelineHandle = vk::raii::Pipeline{device, static_cast<VkPipeline>(rawPipeline)};
        nrAssert(*pipelineHandle != nullptr,
                 "RayTracingPipeline creation completed without a valid pipeline handle.");
        binaryStore.capture(pipelineKey, *pipelineHandle);
    }

    auto const createElapsed =
        std::chrono::duration<double, std::milli>{std::chrono::steady_clock::now() - createStart};
    nrLog<LogLevel::info>(
        "RayTracingPipeline PSO creation completed: mode={}, workers={}, driverConcurrency={}, elapsedMs={:.3f}.",
        creationMode, workerCount, driverConcurrency, createElapsed.count());

    RayTracingPipeline pipeline;
    pipeline.pipeline_ = std::move(pipelineHandle);
    nrAssert(pipeline.valid(), "RayTracingPipeline creation completed without a valid pipeline handle.");
    pipeline.identity_ = allocateRayTracingPipelineIdentity();
    pipeline.capabilities_ = capabilities;
    pipeline.shaderGroupCount_ = static_cast<std::uint32_t>(groups.size());
    auto groupIndices = std::views::iota(std::size_t{0u}, groupDescs.size());
    std::ranges::for_each(groupIndices, [&](std::size_t groupIndex) {
        nrAssert(groupIndex <= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()),
                 "RayTracingPipeline::create group index exceeds uint32 ABI.");
        auto const [_, inserted] =
            pipeline.shaderGroupIndices_.emplace(groupDescs[groupIndex].name, static_cast<std::uint32_t>(groupIndex));
        nrAssert(inserted, "RayTracingPipeline::create requires unique shader group names.");
    });
    return pipeline;
}

[[nodiscard]] bool RayTracingPipeline::valid() const noexcept
{
    return *pipeline_ != nullptr;
}

[[nodiscard]] vk::Pipeline RayTracingPipeline::raw() const noexcept
{
    return valid() ? *pipeline_ : vk::Pipeline{};
}

[[nodiscard]] RayTracingPipelineIdentity RayTracingPipeline::identity() const noexcept
{
    return identity_;
}

[[nodiscard]] const RayTracingCapabilitySnapshot &RayTracingPipeline::capabilities() const noexcept
{
    return capabilities_;
}

[[nodiscard]] std::uint32_t RayTracingPipeline::shaderGroupCount() const noexcept
{
    return shaderGroupCount_;
}

[[nodiscard]] std::uint32_t RayTracingPipeline::shaderGroupIndex(std::string_view name) const
{
    auto const found = shaderGroupIndices_.find(std::string{name});
    nrAssert(found != shaderGroupIndices_.end(), "RayTracingPipeline::shaderGroupIndex unknown group '{}'.", name);
    return found->second;
}

[[nodiscard]] std::vector<std::uint8_t> RayTracingPipeline::shaderGroupHandles(std::uint32_t firstGroup,
                                                                               std::uint32_t groupCount) const
{
    nrAssert(valid(), "RayTracingPipeline::shaderGroupHandles requires a valid pipeline.");
    nrAssert(groupCount > 0u, "RayTracingPipeline::shaderGroupHandles requires groupCount > 0.");
    nrAssert(capabilities_.shaderGroupHandleSize > 0u,
             "RayTracingPipeline::shaderGroupHandles requires a physical shader group handle size.");
    nrAssert(firstGroup < shaderGroupCount_, "RayTracingPipeline::shaderGroupHandles firstGroup is out of range.");
    auto requestedEnd = static_cast<std::uint64_t>(firstGroup) + static_cast<std::uint64_t>(groupCount);
    nrAssert(requestedEnd <= static_cast<std::uint64_t>(shaderGroupCount_),
             "RayTracingPipeline::shaderGroupHandles range exceeds group count.");

    auto dataSize = static_cast<std::size_t>(capabilities_.shaderGroupHandleSize) *
                    static_cast<std::size_t>(groupCount);
    return pipeline_.getRayTracingShaderGroupHandlesKHR<std::uint8_t>(firstGroup, groupCount, dataSize);
}

PipelineService::PipelineService() = default;

PipelineService::~PipelineService() = default;

void PipelineService::bindDevice(const vk::raii::Device &device, std::uint32_t maxBoundDescriptorSets,
                                 const RayTracingCapabilitySnapshot &rtCapabilities,
                                 std::filesystem::path pipelineBinaryRoot)
{
    auto const buildWorkerCount = threading::resolveWorkerCount(0u, nr::maxThreads);
    pipelineBuildPool_.ensureWorkerCount(buildWorkerCount);
    nrLog<LogLevel::info>("PipelineService PSO build pool ready: workers={}, policy=host-default.", buildWorkerCount);
    device_ = std::cref(device);
    nrAssert(maxBoundDescriptorSets > 0u, "PipelineService::bindDevice requires maxBoundDescriptorSets > 0.");
    maxBoundDescriptorSets_ = maxBoundDescriptorSets;
    rtCapabilities_ = rtCapabilities;
    nrAssert(!pipelineBinaryRoot.empty(), "PipelineService requires a CMake-configured PSO cache root.");
    try
    {
        pipelineBinaryStore_ = std::make_unique<PipelineBinaryStore>(device, std::move(pipelineBinaryRoot));
    }
    catch (const vk::SystemError &error)
    {
        nrLog<LogLevel::warning>("PipelineService failed to initialize VK_KHR_pipeline_binary: {}", error.what());
        nrAssert(false, "PipelineService requires VK_KHR_pipeline_binary key queries.");
    }
    nrLog<LogLevel::info>("PipelineService PSO binary store ready.");
}

void PipelineService::waitForBuilds() const
{
    pipelineBuildPool_.waitIdle();
}

[[nodiscard]] std::uint64_t PipelineService::pipelineBinaryLoadCount() const noexcept
{
    return pipelineBinaryStore_ ? pipelineBinaryStore_->acceptedLoadCount() : 0u;
}

[[nodiscard]] std::uint64_t PipelineService::pipelineBinaryCaptureCount() const noexcept
{
    return pipelineBinaryStore_ ? pipelineBinaryStore_->persistedCaptureCount() : 0u;
}

[[nodiscard]] SlangSampler PipelineService::createSampler(SlangSamplerDesc desc, std::string_view debugName) const
{
    nrAssert(device_.has_value(), "PipelineService::createSampler requires a bound logical device.");
    return SlangSampler::create(device_->get(), std::move(desc), debugName);
}

[[nodiscard]] PipelineService::PipelineLayoutBundle PipelineService::createPipelineLayoutBundle(
    const SlangProgram &slangProgram, const DescriptorBindingPolicy &descriptorBindingPolicy,
    std::span<const SlangImmutableSamplerBinding> immutableSamplers) const
{
    const auto &device = device_->get();
    auto descriptorLayout =
        ShaderDescriptorLayout::create(slangProgram, descriptorBindingPolicy, immutableSamplers);
    auto layout =
        CursorPipelineLayout::create(device, descriptorLayout, maxBoundDescriptorSets_, immutableSamplers);
    return PipelineLayoutBundle{
        .descriptorLayout = std::move(descriptorLayout),
        .layout = std::move(layout),
    };
}

[[nodiscard]] PipelineBuild<GraphicsPipeline> PipelineService::createGraphicsPipeline(
    std::span<const SlangProgram> programs, const GraphicsPipelineDesc &desc, std::uint32_t descriptorMaxSets,
    std::span<const SlangImmutableSamplerBinding> immutableSamplers, std::string debugName) const
{
    nrAssert(device_.has_value(), "PipelineService::createGraphicsPipeline requires a bound logical device.");
    nrAssert(!programs.empty(), "PipelineService::createGraphicsPipeline requires at least one single-entry program.");
    nrAssert(std::ranges::all_of(programs,
                                 [](const SlangProgram &program) {
                                     auto const *entryPoint = program.entryPoint();
                                     return entryPoint && isGraphicsStage(entryPoint->stage);
                                 }),
             "PipelineService::createGraphicsPipeline requires valid graphics-stage programs.");
    auto uniqueStages = std::set<SlangStage>{};
    nrAssert(std::ranges::all_of(
                 programs,
                 [&](const SlangProgram &program) { return uniqueStages.insert(program.entryPoint()->stage).second; }),
             "PipelineService::createGraphicsPipeline requires at most one program for each graphics stage.");
    auto const &reflectionProgram = programs.front();

    const auto &device = device_->get();
    auto effectiveDesc = desc;
    auto layoutBundle =
        createPipelineLayoutBundle(reflectionProgram, effectiveDesc.descriptorBindingPolicy, immutableSamplers);
    auto const reflectionSignature = layoutBundle.descriptorLayout.abiSignature();
    std::ranges::for_each(programs | std::views::drop(1), [&](const SlangProgram &program) {
        assertReflectionLayoutCoverage(reflectionSignature, reflectionProgram, program,
                                       effectiveDesc.descriptorBindingPolicy);
    });
    auto const contentFingerprint =
        graphicsPsoContentFingerprint(programs, effectiveDesc, reflectionSignature, immutableSamplers);

    auto shaderProgram = VkShaderProgram::create(device, programs);
    return pipelineBuildPool_.submit([this, layoutBundle = std::move(layoutBundle),
                                      shaderProgram = std::move(shaderProgram),
                                      effectiveDesc = std::move(effectiveDesc), contentFingerprint, descriptorMaxSets,
                                      debugName = std::move(debugName)]() mutable {
        auto const createStart = std::chrono::steady_clock::now();
        auto const &buildDevice = device_->get();
        nrAssert(pipelineBinaryStore_ != nullptr,
                 "PipelineService::createGraphicsPipeline requires a bound PSO binary store.");
        auto pipeline = GraphicsPipeline::create(buildDevice, layoutBundle.layout, shaderProgram, *pipelineBinaryStore_,
                                                 contentFingerprint, effectiveDesc);
        nrAssert(pipeline.valid(), "PipelineService failed to build a valid graphics PSO.");
        setPipelineDebugName(buildDevice, pipeline.raw(), debugName);
        auto state = makePipelineState(std::move(layoutBundle), descriptorMaxSets, std::move(pipeline));
        state.graphicsDesc = std::move(effectiveDesc);
        auto const elapsed = std::chrono::duration<double, std::milli>{std::chrono::steady_clock::now() - createStart};
        nrLog<LogLevel::info>("PipelineService PSO build completed: type=graphics, name='{}', poolWorkers={}, elapsedMs={:.3f}.",
                debugName, pipelineBuildPool_.workerCount(), elapsed.count());
        return state;
    });
}

[[nodiscard]] PipelineBuild<ComputePipeline> PipelineService::createComputePipeline(
    const SlangProgram &slangProgram, const ComputePipelineDesc &desc, std::uint32_t descriptorMaxSets,
    std::span<const SlangImmutableSamplerBinding> immutableSamplers, std::string debugName) const
{
    nrAssert(device_.has_value(), "PipelineService::createComputePipeline requires a bound logical device.");
    nrAssert(slangProgram.valid(), "PipelineService::createComputePipeline requires a valid SlangProgram.");
    auto const *entryPoint = slangProgram.entryPoint();
    nrAssert(entryPoint && entryPoint->stage == SLANG_STAGE_COMPUTE,
             "PipelineService::createComputePipeline requires a compute-stage single-entry program.");

    const auto &device = device_->get();
    auto layoutBundle = createPipelineLayoutBundle(slangProgram, desc.descriptorBindingPolicy, immutableSamplers);
    auto const contentFingerprint = computePsoContentFingerprint(
        slangProgram, desc, layoutBundle.descriptorLayout.abiSignature(), immutableSamplers);

    auto programs = std::array{slangProgram};
    auto shaderProgram = VkShaderProgram::create(device, programs);
    return pipelineBuildPool_.submit([this, layoutBundle = std::move(layoutBundle),
                                      shaderProgram = std::move(shaderProgram), contentFingerprint, descriptorMaxSets,
                                      debugName = std::move(debugName)]() mutable {
        auto const createStart = std::chrono::steady_clock::now();
        auto const &buildDevice = device_->get();
        nrAssert(pipelineBinaryStore_ != nullptr,
                 "PipelineService::createComputePipeline requires a bound PSO binary store.");
        auto pipeline = ComputePipeline::create(buildDevice, layoutBundle.layout, shaderProgram, *pipelineBinaryStore_,
                                                contentFingerprint);
        nrAssert(pipeline.valid(), "PipelineService failed to build a valid compute PSO.");
        setPipelineDebugName(buildDevice, pipeline.raw(), debugName);
        auto state = makePipelineState(std::move(layoutBundle), descriptorMaxSets, std::move(pipeline));
        auto const elapsed = std::chrono::duration<double, std::milli>{std::chrono::steady_clock::now() - createStart};
        nrLog<LogLevel::info>("PipelineService PSO build completed: type=compute, name='{}', poolWorkers={}, elapsedMs={:.3f}.",
                debugName, pipelineBuildPool_.workerCount(), elapsed.count());
        return state;
    });
}

[[nodiscard]] PipelineBuild<RayTracingPipeline> PipelineService::createRayTracingPipeline(
    const SlangProgram &reflectionProgram, const RayTracingProgramAssemblyDesc &assembly,
    const RayTracingPipelineDesc &desc, std::uint32_t descriptorMaxSets,
    std::span<const SlangImmutableSamplerBinding> immutableSamplers, std::string debugName) const
{
    nrAssert(device_.has_value(), "PipelineService::createRayTracingPipeline requires a bound logical device.");
    nrAssert(reflectionProgram.valid(),
             "PipelineService::createRayTracingPipeline requires a valid reflection SlangProgram.");
    auto assemblyValidation = validateRayTracingProgramAssemblyDesc(assembly);
    nrAssert(!assemblyValidation.has_value(), "PipelineService::createRayTracingPipeline invalid program assembly: {}",
             assemblyValidation.value_or(std::string{}));

    const auto &device = device_->get();
    auto layoutBundle = createPipelineLayoutBundle(reflectionProgram, desc.descriptorBindingPolicy, immutableSamplers);
    auto const reflectionSignature = layoutBundle.descriptorLayout.abiSignature();
    std::ranges::for_each(assembly.stages, [&](const RayTracingPipelineStageSelection &selection) {
        assertReflectionLayoutCoverage(reflectionSignature, reflectionProgram, selection.program.get(),
                                       desc.descriptorBindingPolicy);
    });

    nrAssert(desc.maxRayRecursionDepth <= rtCapabilities_.maxRayRecursionDepth,
             "PipelineService::createRayTracingPipeline recursion depth {} exceeds device max {}.",
             desc.maxRayRecursionDepth, rtCapabilities_.maxRayRecursionDepth);
    auto const contentFingerprint =
        rayTracingPsoContentFingerprint(assembly, desc, reflectionSignature, immutableSamplers);

    auto shaderProgram = VkShaderProgram::create(device, assembly.stages);
    auto descCopy = desc;
    auto groups = assembly.groups;
    return pipelineBuildPool_.submit([this, layoutBundle = std::move(layoutBundle),
                                      shaderProgram = std::move(shaderProgram), capabilities = rtCapabilities_,
                                      desc = std::move(descCopy), groups = std::move(groups), contentFingerprint,
                                      descriptorMaxSets, debugName = std::move(debugName)]() mutable {
        auto const &buildDevice = device_->get();
        nrAssert(pipelineBinaryStore_ != nullptr,
                 "PipelineService::createRayTracingPipeline requires a bound PSO binary store.");
        auto pipeline =
            RayTracingPipeline::create(buildDevice, layoutBundle.layout, shaderProgram, rayTracingDeferredHostPool_,
                                       capabilities, *pipelineBinaryStore_, contentFingerprint, desc, groups);
        nrAssert(pipeline.valid(), "PipelineService failed to build a valid ray-tracing PSO.");
        setPipelineDebugName(buildDevice, pipeline.raw(), debugName);
        return makePipelineState(std::move(layoutBundle), descriptorMaxSets, std::move(pipeline));
    });
}
} // namespace nr::rhi

namespace nr::rhi
{
namespace mesh
{
void applyRasterState(const vk::raii::CommandBuffer &commandBuffer, const MeshRasterState &state)
{
    nrAssert(*commandBuffer != nullptr, "mesh::applyRasterState requires a valid command buffer.");
    commandBuffer.setCullMode(state.cullMode);
    commandBuffer.setFrontFace(state.frontFace);
    commandBuffer.setDepthTestEnable(state.depthTestEnable);
    commandBuffer.setDepthWriteEnable(state.depthWriteEnable);
    commandBuffer.setDepthCompareOp(state.depthCompareOp);
}

} // namespace mesh
} // namespace nr::rhi
