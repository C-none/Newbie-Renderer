export module nr.renderPasses:sceneTextureTableBinding;
import dependency.vulkan;

import nr.renderer;
import nr.rhi;
import nr.scene;
import nr.utils;
import std;

export namespace nr::renderPasses::detail
{
inline constexpr std::uint32_t kSceneTextureTableSet = 1u;
inline constexpr std::uint32_t kSceneTextureTableBinding = 0u;

enum class SceneTextureTableBindingRequirement
{
    required,
    optional,
};

struct SceneTextureTableBindingInput
{
    std::map<std::uint32_t, nr::renderer::SceneTextureDescriptorBinding> descriptorsById{};
    std::uint32_t descriptorCapacity = nr::renderer::kSceneTextureDescriptorCapacity;
    std::uint64_t tableVersion = 0;
};

[[nodiscard]] inline nr::rhi::SlangSamplerDesc sceneTextureTableLinearSamplerDesc() noexcept
{
    return nr::rhi::SlangSamplerDesc{
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eLinear,
    };
}

[[nodiscard]] inline nr::rhi::SlangImmutableSamplerBinding sceneTextureTableImmutableSamplerBinding() noexcept
{
    return nr::rhi::SlangImmutableSamplerBinding{
        .set = kSceneTextureTableSet,
        .binding = kSceneTextureTableBinding,
        .descriptorCount = nr::renderer::kSceneTextureDescriptorCapacity,
        .samplerDesc = sceneTextureTableLinearSamplerDesc(),
    };
}

[[nodiscard]] inline SceneTextureTableBindingInput makeSceneTextureTableBindingInput(
    const nr::renderer::FrameGlobalResources& globalResources)
{
    return SceneTextureTableBindingInput{
        .descriptorsById = globalResources.sceneTextureDescriptorsById,
        .descriptorCapacity = globalResources.sceneTextureDescriptorCapacity,
        .tableVersion = globalResources.sceneTextureDescriptorVersion,
    };
}

[[nodiscard]] inline nr::renderer::BindlessImageTableRequirement rendererRequirement(
    SceneTextureTableBindingRequirement requirement) noexcept
{
    return requirement == SceneTextureTableBindingRequirement::required
               ? nr::renderer::BindlessImageTableRequirement::required
               : nr::renderer::BindlessImageTableRequirement::optional;
}

[[nodiscard]] inline nr::renderer::BindlessImageTableRequest makeSceneTextureTableBindingRequest(
    const SceneTextureTableBindingInput& bindingInput,
    SceneTextureTableBindingRequirement requirement = SceneTextureTableBindingRequirement::required)
{
    nr::nrAssert(
        bindingInput.descriptorCapacity == nr::renderer::kSceneTextureDescriptorCapacity,
        "Scene texture table descriptor capacity must match renderer ABI.");

    auto fallbackIt = bindingInput.descriptorsById.find(nr::scene::kDefaultSceneTextureId);
    nr::nrAssert(
        fallbackIt != bindingInput.descriptorsById.end(),
        "Scene texture table requires default texture id 0.");

    auto descriptorsById = std::map<std::uint32_t, nr::renderer::BindlessImageDescriptor>{};
    std::ranges::for_each(bindingInput.descriptorsById, [&](const auto& entry) {
        auto const& descriptor = entry.second;
        descriptorsById.insert_or_assign(
            entry.first,
            nr::renderer::BindlessImageDescriptor{
                .image = std::cref(descriptor.image.get()),
                .layout = descriptor.layout,
            });
    });

    return nr::renderer::BindlessImageTableRequest{
        .tableKey = "scene.gSceneTextures",
        .shaderSymbol = "gSceneTextures",
        .expectedSet = kSceneTextureTableSet,
        .expectedBinding = kSceneTextureTableBinding,
        .expectedDescriptorType = vk::DescriptorType::eCombinedImageSampler,
        .descriptorCapacity = bindingInput.descriptorCapacity,
        .usesImmutableSampler = true,
        .tableVersion = bindingInput.tableVersion,
        .descriptorsById = std::move(descriptorsById),
        .fallbackDescriptor = nr::renderer::BindlessImageDescriptor{
            .image = std::cref(fallbackIt->second.image.get()),
            .layout = fallbackIt->second.layout,
        },
        .requirement = rendererRequirement(requirement),
    };
}

template <typename TPipeline, std::size_t FrameSlotCount>
void prepareSceneTextureTableBindingForFrame(
    nr::renderer::PipelineRuntime<TPipeline, FrameSlotCount>& pipeline,
    nr::renderer::BindlessImageTableCache& cache,
    std::uint32_t frameIndex,
    const SceneTextureTableBindingInput& bindingInput,
    SceneTextureTableBindingRequirement requirement = SceneTextureTableBindingRequirement::required)
{
    cache.ensureTableForFrame(
        pipeline,
        frameIndex,
        makeSceneTextureTableBindingRequest(bindingInput, requirement));
}

template <typename TPipeline, std::size_t FrameSlotCount>
[[nodiscard]] nr::rhi::ShaderBindingSnapshot makeSceneTextureTableBindingSnapshot(
    nr::renderer::PipelineRuntime<TPipeline, FrameSlotCount>& pipeline,
    nr::renderer::BindlessImageTableCache& cache,
    std::uint32_t frameIndex,
    const SceneTextureTableBindingInput& bindingInput,
    SceneTextureTableBindingRequirement requirement = SceneTextureTableBindingRequirement::required)
{
    return cache.makeSnapshotForFrame(
        pipeline,
        frameIndex,
        makeSceneTextureTableBindingRequest(bindingInput, requirement));
}
} // namespace nr::renderPasses::detail
