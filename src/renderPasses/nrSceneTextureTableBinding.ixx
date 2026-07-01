export module nr.renderPasses:sceneTextureTableBinding;
import dependency.vulkan;

import nr.renderer;
import nr.rhi;
import nr.scene;
import nr.utils;
import std;

export namespace nr::renderPasses::detail
{
enum class SceneTextureTableBindingRequirement
{
    required,
    optional,
};

struct SceneTextureTableBindingInput
{
    std::map<std::uint32_t, nr::renderer::SceneTextureDescriptorBinding> descriptorsById{};
    vk::Sampler sampler{};
    std::uint32_t descriptorCapacity = nr::renderer::kSceneTextureDescriptorCapacity;
    std::uint64_t tableVersion = 0;
};

struct SceneTextureTableBindingCache
{
    std::array<bool, nr::maxFrameInFlight> initialized{};
    std::array<std::uint64_t, nr::maxFrameInFlight> versions{};
    std::array<std::set<std::uint32_t>, nr::maxFrameInFlight> textureIdsByFrame{};
};

[[nodiscard]] inline SceneTextureTableBindingInput makeSceneTextureTableBindingInput(
    const nr::renderer::FrameGlobalResources& globalResources)
{
    return SceneTextureTableBindingInput{
        .descriptorsById = globalResources.sceneTextureDescriptorsById,
        .sampler = globalResources.sceneTextureSampler,
        .descriptorCapacity = globalResources.sceneTextureDescriptorCapacity,
        .tableVersion = globalResources.sceneTextureDescriptorVersion,
    };
}

inline void resetSceneTextureTableFrameCache(
    SceneTextureTableBindingCache& cache,
    std::uint32_t frameIndex) noexcept
{
    auto const frameSlot = static_cast<std::size_t>(frameIndex % nr::maxFrameInFlight);
    cache.initialized[frameSlot] = false;
    cache.versions[frameSlot] = 0;
    cache.textureIdsByFrame[frameSlot].clear();
}

template <typename TPipeline, std::size_t FrameSlotCount>
void ensureSceneTextureTableBindingSetsForFrame(
    nr::renderer::PipelineRuntime<TPipeline, FrameSlotCount>& pipeline,
    SceneTextureTableBindingCache& cache,
    std::uint32_t frameIndex,
    SceneTextureTableBindingRequirement requirement = SceneTextureTableBindingRequirement::required)
{
    auto root = pipeline.rootCursor();
    auto texturesCursor = root["gSceneTextures"];
    if (!texturesCursor.valid())
    {
        nr::nrAssert(
            requirement == SceneTextureTableBindingRequirement::optional,
            "Scene texture table binding requires shader symbol gSceneTextures.");
        if (requirement == SceneTextureTableBindingRequirement::required)
        {
            return;
        }

        auto const reallocated = pipeline.ensureBindingSetsForFrame(frameIndex, {});
        if (reallocated)
        {
            resetSceneTextureTableFrameCache(cache, frameIndex);
        }
        return;
    }

    auto textureBinding = texturesCursor.descriptorBinding();
    nr::nrAssert(
        textureBinding.has_value() &&
            textureBinding->supportsVariableDescriptorCount() &&
            textureBinding->set == 1u &&
            textureBinding->binding == 0u &&
            textureBinding->descriptorType == vk::DescriptorType::eCombinedImageSampler,
        "gSceneTextures must be a runtime-sized combined image sampler at set 1 binding 0.");

    auto const reallocated = pipeline.ensureBindingSetsForFrame(
        frameIndex,
        {{textureBinding->set, nr::renderer::kSceneTextureDescriptorCapacity}});
    if (reallocated)
    {
        resetSceneTextureTableFrameCache(cache, frameIndex);
    }
}

template <typename TPipeline, std::size_t FrameSlotCount>
[[nodiscard]] nr::rhi::ShaderBindingSnapshot makeSceneTextureTableBindingSnapshot(
    nr::renderer::PipelineRuntime<TPipeline, FrameSlotCount>& pipeline,
    SceneTextureTableBindingCache& cache,
    std::uint32_t frameIndex,
    const SceneTextureTableBindingInput& bindingInput,
    SceneTextureTableBindingRequirement requirement = SceneTextureTableBindingRequirement::required)
{
    auto root = pipeline.rootCursor();
    auto texturesCursor = root["gSceneTextures"];
    if (!texturesCursor.valid())
    {
        nr::nrAssert(
            requirement == SceneTextureTableBindingRequirement::optional,
            "Scene texture table snapshot requires shader symbol gSceneTextures.");
        return {};
    }

    auto textureBinding = texturesCursor.descriptorBinding();
    nr::nrAssert(
        textureBinding.has_value() &&
            textureBinding->supportsVariableDescriptorCount() &&
            textureBinding->set == 1u &&
            textureBinding->binding == 0u &&
            textureBinding->descriptorType == vk::DescriptorType::eCombinedImageSampler,
        "gSceneTextures must be a runtime-sized combined image sampler at set 1 binding 0.");
    nr::nrAssert(bindingInput.sampler != vk::Sampler{}, "Scene texture table binding requires a valid sampler.");
    nr::nrAssert(
        bindingInput.descriptorCapacity == nr::renderer::kSceneTextureDescriptorCapacity,
        "Scene texture table descriptor capacity must match renderer ABI.");

    auto fallbackIt = bindingInput.descriptorsById.find(nr::scene::kDefaultSceneTextureId);
    nr::nrAssert(
        fallbackIt != bindingInput.descriptorsById.end(),
        "Scene texture table requires default texture id 0.");

    auto const frameSlot = static_cast<std::size_t>(frameIndex % nr::maxFrameInFlight);
    auto& initialized = cache.initialized[frameSlot];
    auto& cachedVersion = cache.versions[frameSlot];
    auto& previousTextureIds = cache.textureIdsByFrame[frameSlot];

    if (initialized && cachedVersion == bindingInput.tableVersion)
    {
        return {};
    }

    auto currentTextureIds = std::set<std::uint32_t>{};
    std::ranges::for_each(bindingInput.descriptorsById, [&](const auto& entry) {
        currentTextureIds.insert(entry.first);
    });

    auto writeFallback = [&](std::uint32_t textureId) {
        nr::nrAssert(
            textureId < bindingInput.descriptorCapacity,
            std::format(
                "Scene texture fallback id {} exceeds descriptor capacity {}.",
                textureId,
                bindingInput.descriptorCapacity));
        auto textureElementCursor = texturesCursor[textureId];
        static_cast<void>(textureElementCursor.setObject(
            fallbackIt->second.image.get(),
            bindingInput.sampler,
            fallbackIt->second.layout));
    };

    if (!initialized)
    {
        auto descriptorSlots = std::views::iota(std::uint32_t{0}, bindingInput.descriptorCapacity);
        std::ranges::for_each(descriptorSlots, writeFallback);
    }
    else
    {
        std::ranges::for_each(previousTextureIds, [&](std::uint32_t previousTextureId) {
            if (!currentTextureIds.contains(previousTextureId))
            {
                writeFallback(previousTextureId);
            }
        });
    }

    std::ranges::for_each(bindingInput.descriptorsById, [&](const auto& entry) {
        auto const textureId = entry.first;
        auto const& descriptor = entry.second;
        nr::nrAssert(
            textureId < bindingInput.descriptorCapacity,
            std::format(
                "Scene texture id {} exceeds descriptor capacity {}.",
                textureId,
                bindingInput.descriptorCapacity));

        auto textureElementCursor = texturesCursor[textureId];
        static_cast<void>(textureElementCursor.setObject(
            descriptor.image.get(),
            bindingInput.sampler,
            descriptor.layout));
    });

    auto snapshot = root.snapshot();
    root.clearSnapshot();
    initialized = true;
    cachedVersion = bindingInput.tableVersion;
    previousTextureIds = std::move(currentTextureIds);
    return snapshot;
}
} // namespace nr::renderPasses::detail
