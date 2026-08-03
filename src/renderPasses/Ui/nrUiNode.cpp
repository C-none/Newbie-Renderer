module nr.renderPasses;
import dependency.math;
import dependency.ui;
import dependency.vulkan;

import :uiNode;
import nr.app;
import nr.renderer;
import nr.rhi;
import nr.utils;
import std;
import :nodeType;

namespace nr::renderPasses::detail
{
struct UiPushConstants
{
    glm::vec2 scale{1.0f, 1.0f};
    glm::vec2 translate{0.0f, 0.0f};
    std::uint32_t textureIndex = 0u;
    std::array<std::uint32_t, 3> padding{};
};

inline constexpr std::uint32_t kUiTextureDescriptorCapacity = 1024u;

/// Initial vertex buffer capacity per frame slot (64 KiB = ~1.5k ImDrawVert)
/// Pre-allocating avoids per-frame vkAllocateMemory calls in typical UI scenarios.
inline constexpr vk::DeviceSize kInitialUiVertexBufferCapacity = 64u * 1024u;

/// Initial index buffer capacity per frame slot (32 KiB = ~10k ImDrawIdx)
inline constexpr vk::DeviceSize kInitialUiIndexBufferCapacity = 32u * 1024u;

static_assert(sizeof(UiPushConstants) <= nr::rhi::kMaxPushConstantBytes, "UiNode push constants exceed 128 bytes.");

struct UiDrawCommand
{
    vk::Rect2D scissor{};
    std::uint32_t elementCount = 0u;
    std::uint32_t firstIndex = 0u;
    std::int32_t vertexOffset = 0;
    std::uint32_t textureSlot = 0u;
};

struct UiFrameDrawData
{
    std::vector<ImDrawVert> vertices{};
    std::vector<ImDrawIdx> indices{};
    std::vector<UiDrawCommand> commands{};
    vk::Extent2D framebufferExtent{1u, 1u};
    UiPushConstants pushConstants{};
};

struct UiTextureEntry
{
    nr::rhi::Image image{};
    nr::renderer::RetainedImageState state{};
    std::uint64_t textureKey = 0u;
};

struct UiRetiredTexture
{
    nr::rhi::Image image{};
    std::uint32_t slot = 0u;
    std::uint64_t retiredFrameIndex = 0u;
    bool releaseSlot = true;
};

struct UiRuntimeCache
{
    std::shared_ptr<nr::renderer::PipelineRuntime<nr::rhi::GraphicsPipeline>> pipeline{};
    nr::rhi::SlangSampler textureSampler{};
    std::uint64_t textureTableRevision = 1u;
    std::array<nr::rhi::Buffer, nr::maxFrameInFlight> vertexBuffers{};
    std::array<nr::rhi::Buffer, nr::maxFrameInFlight> indexBuffers{};
    // Per-frame GPU-only UI overlay images avoid cross-frame read/write overlap.
    std::array<nr::rhi::Image, nr::maxFrameInFlight> uiBuffers{};
    vk::Extent2D allocatedUiExtent{0, 0};
    vk::Format allocatedUiFormat = vk::Format::eUndefined;
    std::vector<UiTextureEntry> texturesBySlot{};
    std::map<std::uint64_t, std::uint32_t> textureSlotByKey{};
    std::vector<std::uint32_t> freeTextureSlots{};
    std::vector<UiRetiredTexture> retiredTextures{};
    std::optional<UiFrameDrawData> preparedDrawFrame{};
};

[[nodiscard]] vk::PipelineColorBlendAttachmentState makeUiBlendAttachment()
{
    auto blendAttachment = vk::PipelineColorBlendAttachmentState{};
    blendAttachment.blendEnable = vk::True;
    blendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
    blendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    blendAttachment.colorBlendOp = vk::BlendOp::eAdd;
    blendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
    blendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    blendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
    blendAttachment.colorWriteMask =
        vk::ColorComponentFlagBits::eR |
        vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB |
        vk::ColorComponentFlagBits::eA;
    return blendAttachment;
}

[[nodiscard]] std::vector<vk::VertexInputBindingDescription> makeUiVertexBindings()
{
    return {
        vk::VertexInputBindingDescription{
            0u,
            static_cast<std::uint32_t>(sizeof(ImDrawVert)),
            vk::VertexInputRate::eVertex,
        },
    };
}

[[nodiscard]] std::vector<vk::VertexInputAttributeDescription> makeUiVertexAttributes()
{
    return {
        vk::VertexInputAttributeDescription{
            0u,
            0u,
            vk::Format::eR32G32Sfloat,
            static_cast<std::uint32_t>(imgui::drawVertPosOffset),
        },
        vk::VertexInputAttributeDescription{
            1u,
            0u,
            vk::Format::eR32G32Sfloat,
            static_cast<std::uint32_t>(imgui::drawVertUvOffset),
        },
        vk::VertexInputAttributeDescription{
            2u,
            0u,
            vk::Format::eR8G8B8A8Unorm,
            static_cast<std::uint32_t>(imgui::drawVertColorOffset),
        },
    };
}

void ensureUiBufferImage(
    nr::rhi::Device& device,
    UiRuntimeCache& runtime,
    vk::Extent2D extent,
    vk::Format format)
{
    auto const needsRealloc = runtime.allocatedUiExtent != extent ||
                              runtime.allocatedUiFormat != format ||
                              std::ranges::any_of(runtime.uiBuffers, [](const nr::rhi::Image& image) {
                                  return !image.valid();
                              });

    if (!needsRealloc)
    {
        return;
    }

    auto imageInfo = nr::rhi::makeImageCreateInfo(
        format,
        extent,
        vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled);

    auto frameSlots = std::views::iota(std::size_t{0}, runtime.uiBuffers.size());
    std::ranges::for_each(frameSlots, [&](std::size_t frameSlot) {
        runtime.uiBuffers[frameSlot] = device.resourceFactory.createImage(
            imageInfo,
            nr::rhi::MemoryUsage::GpuOnly,
            std::format("Ui.Buffer[{}]", frameSlot));
        nr::nrAssert(runtime.uiBuffers[frameSlot].valid(), "UiNode failed to allocate Ui.Buffer image.");
    });

    runtime.allocatedUiExtent = extent;
    runtime.allocatedUiFormat = format;
}

[[nodiscard]] std::shared_ptr<UiRuntimeCache> ensureUiRuntime(
    nr::rhi::Device& device,
    std::span<const nr::rhi::SlangProgram> programs,
    vk::Format colorFormat)
{
    auto pipelineDesc = nr::rhi::GraphicsPipelineDesc{};
    pipelineDesc.colorAttachmentFormats = {colorFormat};
    pipelineDesc.cullMode = vk::CullModeFlagBits::eNone;
    pipelineDesc.vertexBindings = makeUiVertexBindings();
    pipelineDesc.vertexAttributes = makeUiVertexAttributes();
    pipelineDesc.colorBlendAttachments = {makeUiBlendAttachment()};
    auto const& descriptorCaps = device.descriptorIndexingCapabilities();
    nr::nrAssert(
        descriptorCaps.maxDescriptorSetUpdateAfterBindSampledImages >= kUiTextureDescriptorCapacity,
        std::format(
            "UiNode requires at least {} update-after-bind sampled/combined image descriptors per set; device reports {}.",
            kUiTextureDescriptorCapacity,
            descriptorCaps.maxDescriptorSetUpdateAfterBindSampledImages));
    nr::nrAssert(
        descriptorCaps.maxPerStageDescriptorUpdateAfterBindSampledImages >= kUiTextureDescriptorCapacity,
        std::format(
            "UiNode requires at least {} update-after-bind sampled/combined image descriptors per stage; device reports {}.",
            kUiTextureDescriptorCapacity,
            descriptorCaps.maxPerStageDescriptorUpdateAfterBindSampledImages));
    pipelineDesc.descriptorBindingPolicy.defaultRuntimeDescriptorCount = kUiTextureDescriptorCapacity;

    auto runtime = std::make_shared<UiRuntimeCache>();
    runtime->pipeline = std::make_shared<nr::renderer::PipelineRuntime<nr::rhi::GraphicsPipeline>>();
    runtime->pipeline->initializeDeferred(device.pipeline().createGraphicsPipeline(programs, pipelineDesc));
    nr::nrAssert(runtime->pipeline->valid(), "UiNode failed to create graphics pipeline.");

    runtime->textureSampler = device.pipeline().createSampler(nr::rhi::SlangSamplerDesc{
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW = vk::SamplerAddressMode::eClampToEdge,
    },
                                                          "Ui.TextureSampler");
    nr::nrAssert(runtime->textureSampler.valid(), "UiNode failed to create texture sampler.");

    // Pre-allocate vertex and index buffers per frame slot to avoid per-frame vkAllocateMemory calls.
    // This amortizes the initial allocation cost and prevents runtime allocations during typical UI rendering.
    auto frameSlots = std::views::iota(std::size_t{0u}, nr::maxFrameInFlight);
    std::ranges::for_each(frameSlots, [&](std::size_t frameSlot) {
        auto vertexBufferInfo = nr::rhi::makeBufferCreateInfo(
            kInitialUiVertexBufferCapacity,
            vk::BufferUsageFlagBits::eVertexBuffer);
        runtime->vertexBuffers[frameSlot] = device.resourceFactory.createBuffer(
            vertexBufferInfo,
            nr::rhi::MemoryUsage::CpuToGpu,
            std::format("Ui.VertexBuffer[{}]", frameSlot));

        auto indexBufferInfo = nr::rhi::makeBufferCreateInfo(
            kInitialUiIndexBufferCapacity,
            vk::BufferUsageFlagBits::eIndexBuffer);
        runtime->indexBuffers[frameSlot] = device.resourceFactory.createBuffer(
            indexBufferInfo,
            nr::rhi::MemoryUsage::CpuToGpu,
            std::format("Ui.IndexBuffer[{}]", frameSlot));
    });

    return runtime;
}

[[nodiscard]] std::uint64_t makeManagedTextureKey(const ImTextureData& textureData) noexcept
{
    return static_cast<std::uint64_t>(textureData.UniqueID);
}

[[nodiscard]] ImTextureID makeTextureIdFromSlot(std::uint32_t slot) noexcept
{
    return reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(slot) + 1u);
}

[[nodiscard]] std::uint32_t textureSlotFromId(ImTextureID textureId)
{
    auto const encodedSlot = reinterpret_cast<std::uintptr_t>(textureId);
    nr::nrAssert(encodedSlot > 0u, "UiNode texture id cannot be null.");
    auto const slot = encodedSlot - 1u;
    nr::nrAssert(
        slot <= static_cast<std::uintptr_t>(std::numeric_limits<std::uint32_t>::max()),
        std::format("UiNode texture id {} exceeds texture slot range.", slot));
    return static_cast<std::uint32_t>(slot);
}

void markBindlessTextureTableDirty(UiRuntimeCache& runtime) noexcept
{
    ++runtime.textureTableRevision;
    if (runtime.textureTableRevision == 0u)
    {
        runtime.textureTableRevision = 1u;
    }
}

[[nodiscard]] void* uiTextureBackendMarker(UiRuntimeCache& runtime) noexcept
{
    return std::addressof(runtime);
}

[[nodiscard]] const void* uiTextureBackendMarker(const UiRuntimeCache& runtime) noexcept
{
    return std::addressof(runtime);
}

[[nodiscard]] bool textureOwnedByRuntime(
    const UiRuntimeCache& runtime,
    const ImTextureData& textureData) noexcept
{
    return static_cast<const void*>(textureData.BackendUserData) == uiTextureBackendMarker(runtime);
}

[[nodiscard]] std::uint32_t acquireUiTextureSlot(
    UiRuntimeCache& runtime,
    std::uint64_t textureKey)
{
    if (auto existing = runtime.textureSlotByKey.find(textureKey); existing != runtime.textureSlotByKey.end())
    {
        return existing->second;
    }

    std::uint32_t slot = 0u;
    if (!runtime.freeTextureSlots.empty())
    {
        slot = runtime.freeTextureSlots.back();
        runtime.freeTextureSlots.pop_back();
    }
    else
    {
        slot = static_cast<std::uint32_t>(runtime.texturesBySlot.size());
        nr::nrAssert(
            slot < kUiTextureDescriptorCapacity,
            std::format(
                "UiNode texture slot allocation exceeded fixed descriptor capacity {}.",
                kUiTextureDescriptorCapacity));
        runtime.texturesBySlot.emplace_back();
    }

    nr::nrAssert(slot < runtime.texturesBySlot.size(), "UiNode acquired texture slot outside texture table.");
    nr::nrAssert(
        slot < kUiTextureDescriptorCapacity,
        std::format(
            "UiNode bindless texture slot {} exceeds fixed descriptor capacity {}.",
            slot,
            kUiTextureDescriptorCapacity));

    runtime.texturesBySlot[slot].textureKey = textureKey;
    runtime.textureSlotByKey.insert_or_assign(textureKey, slot);
    markBindlessTextureTableDirty(runtime);
    return slot;
}

[[nodiscard]] vk::Extent2D makeTextureExtent(const ImTextureData& textureData)
{
    nr::nrAssert(textureData.Width > 0, "UiNode requires textures with positive width.");
    nr::nrAssert(textureData.Height > 0, "UiNode requires textures with positive height.");

    return vk::Extent2D{
        static_cast<std::uint32_t>(textureData.Width),
        static_cast<std::uint32_t>(textureData.Height),
    };
}

[[nodiscard]] std::vector<std::byte> makeTextureUploadBytes(const ImTextureData& textureData)
{
    nr::nrAssert(textureData.Pixels != nullptr, "UiNode requires CPU-visible ImGui texture pixels.");

    auto const textureExtent = makeTextureExtent(textureData);
    auto const pixelCount = static_cast<std::size_t>(textureExtent.width) * static_cast<std::size_t>(textureExtent.height);

    if (textureData.Format == ImTextureFormat_RGBA32)
    {
        auto const sourceBytes = static_cast<std::size_t>(textureData.GetSizeInBytes());
        auto const* sourceFirst = reinterpret_cast<const std::byte*>(textureData.Pixels);
        return std::vector<std::byte>{sourceFirst, sourceFirst + sourceBytes};
    }

    nr::nrAssert(
        textureData.Format == ImTextureFormat_Alpha8,
        std::format("UiNode encountered unsupported ImGui texture format {}.", static_cast<int>(textureData.Format)));

    auto uploadBytes = std::vector<std::byte>{};
    uploadBytes.resize(pixelCount * 4u);

    auto const* sourceAlpha = textureData.Pixels;
    auto destinationIndex = std::size_t{0u};
    auto sourceIndexRange = std::views::iota(std::size_t{0u}, pixelCount);
    std::ranges::for_each(sourceIndexRange, [&](std::size_t sourceIndex) {
        uploadBytes[destinationIndex + 0u] = static_cast<std::byte>(0xff);
        uploadBytes[destinationIndex + 1u] = static_cast<std::byte>(0xff);
        uploadBytes[destinationIndex + 2u] = static_cast<std::byte>(0xff);
        uploadBytes[destinationIndex + 3u] = static_cast<std::byte>(sourceAlpha[sourceIndex]);
        destinationIndex += 4u;
    });

    return uploadBytes;
}

[[nodiscard]] nr::rhi::ops::BufferUploadOwnershipPlan makeUiTextureUploadPlan(
    const nr::rhi::Device& device)
{
    auto const transferQueueFamily = device.queueManager.transfer().queueFamilyIndex();
    auto const graphicsQueueFamily = device.queueManager.graphics().queueFamilyIndex();
    nr::nrAssert(
        transferQueueFamily != graphicsQueueFamily,
        "UiNode texture upload requires distinct transfer and graphics queue families.");

    auto plan = nr::rhi::ops::BufferUploadOwnershipPlan{};
    plan.releaseToDestination = nr::rhi::ops::makeQueueOwnershipTransfer(
        transferQueueFamily,
        graphicsQueueFamily,
        nr::rhi::ops::QueueAccessScope{
            .stages = vk::PipelineStageFlagBits2::eTransfer,
            .access = vk::AccessFlagBits2::eTransferWrite,
        },
        nr::rhi::ops::QueueAccessScope{
            .stages = vk::PipelineStageFlagBits2::eFragmentShader,
            .access = vk::AccessFlagBits2::eShaderRead,
        });
    return plan;
}

void submitAndWaitUiTextureUploadSync(
    nr::rhi::Device& device,
    nr::rhi::ops::UploadReadbackContext& uploadContext,
    const nr::rhi::ops::ImageUploadTicket& uploadTicket)
{
    nr::nrAssert(uploadTicket.valid(), "UiNode texture upload synchronization requires a valid upload ticket.");

    auto syncPool = nr::rhi::CommandPool{
        device.device,
        device.queueManager.graphics().queueFamilyIndex(),
        vk::CommandPoolCreateFlagBits::eTransient,
    };
    auto syncCommandBuffers = syncPool.allocatePrimary(1);
    auto& syncCommandBuffer = syncCommandBuffers.front();

    nr::rhi::CommandRecorder::beginPrimary(syncCommandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    uploadContext.recordImageAcquireBarrier(syncCommandBuffer, uploadTicket);
    nr::rhi::CommandRecorder::end(syncCommandBuffer);

    auto syncSubmission = nr::rhi::CommandBatch{};
    syncSubmission.addWait(
        uploadContext.uploadTimelineSemaphore(),
        vk::PipelineStageFlagBits2::eAllCommands,
        uploadTicket.signalValue);
    syncSubmission.addCommandBuffer(syncCommandBuffer);

    auto syncFence = vk::raii::Fence{device.device, vk::FenceCreateInfo{}};
    device.queueManager.graphics().submit(std::move(syncSubmission), std::cref(syncFence));

    auto const waitResult = device.device.waitForFences(
        *syncFence,
        vk::True,
        std::numeric_limits<std::uint64_t>::max());
    nr::nrAssert(waitResult == vk::Result::eSuccess, "UiNode failed waiting for texture upload graphics synchronization.");
    uploadContext.reclaimCompletedUploads();
}

void uploadUiTextureThroughRing(
    nr::rhi::Device& device,
    const nr::rhi::Image& image,
    std::span<const std::byte> uploadBytes)
{
    auto& uploadContext = device.uploadReadback();
    auto uploadTicket = uploadContext.uploadImage(
        uploadBytes,
        image,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        makeUiTextureUploadPlan(device));
    submitAndWaitUiTextureUploadSync(device, uploadContext, uploadTicket);
}

void createOrUpdateUiTexture(
    nr::rhi::Device& device,
    UiRuntimeCache& runtime,
    ImTextureData& textureData,
    std::uint64_t currentFrameIndex)
{
    auto const wasOwnedByRuntime = textureOwnedByRuntime(runtime, textureData);
    auto const textureKey = makeManagedTextureKey(textureData);
    auto const hadRuntimeSlot = runtime.textureSlotByKey.contains(textureKey);
    auto const textureSlot = acquireUiTextureSlot(runtime, textureKey);
    auto const textureExtent = makeTextureExtent(textureData);
    auto uploadBytes = makeTextureUploadBytes(textureData);

    auto& textureEntry = runtime.texturesBySlot[textureSlot];
    auto needsReplacement = !textureEntry.image.valid() ||
                             textureData.Status == ImTextureStatus_WantCreate ||
                             textureData.Status == ImTextureStatus_WantUpdates;
    if (!needsReplacement)
    {
        auto const currentExtent = textureEntry.image.extent();
        needsReplacement = currentExtent.width != textureExtent.width || currentExtent.height != textureExtent.height;
    }

    if (needsReplacement)
    {
        if (textureEntry.image.valid())
        {
            runtime.retiredTextures.push_back(UiRetiredTexture{
                .image = std::move(textureEntry.image),
                .slot = textureSlot,
                .retiredFrameIndex = currentFrameIndex,
                .releaseSlot = false,
            });
        }

        auto const debugName = std::format("Ui.Texture[{}:{}]", textureData.UniqueID, textureSlot);
        auto imageCreateInfo = nr::rhi::makeImageCreateInfo(
            vk::Format::eR8G8B8A8Unorm,
            textureExtent,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled);
        auto textureImage = device.resourceFactory.createImage(
            imageCreateInfo,
            nr::rhi::MemoryUsage::GpuOnly,
            debugName);
        nr::nrAssert(textureImage.valid(), std::format("UiNode failed to create texture image '{}'.", debugName));

        textureEntry = UiTextureEntry{
            .image = std::move(textureImage),
            .textureKey = textureKey,
        };
        markBindlessTextureTableDirty(runtime);
    }

    uploadUiTextureThroughRing(
        device,
        textureEntry.image,
        std::span<const std::byte>{uploadBytes.data(), uploadBytes.size()});
    textureEntry.state.common.initialized = true;
    textureEntry.state.layout = nr::renderer::ImageLayoutIntent::ShaderReadOnly;
    textureEntry.state.common.ownership = nr::renderer::ResourceOwnershipDomain::Graphics;
    textureEntry.state.common.access = nr::renderer::AccessScope{
        .stages = vk::PipelineStageFlagBits2::eFragmentShader,
        .access = vk::AccessFlagBits2::eShaderSampledRead,
    };

    if (!wasOwnedByRuntime && hadRuntimeSlot)
    {
        markBindlessTextureTableDirty(runtime);
    }
    textureData.BackendUserData = uiTextureBackendMarker(runtime);
    textureData.SetTexID(makeTextureIdFromSlot(textureSlot));
    textureData.SetStatus(ImTextureStatus_OK);
}

void destroyUiTexture(
    UiRuntimeCache& runtime,
    ImTextureData& textureData,
    std::uint64_t currentFrameIndex)
{
    auto const ownedByRuntime = textureOwnedByRuntime(runtime, textureData);
    auto const textureKey = makeManagedTextureKey(textureData);
    auto slotIt = runtime.textureSlotByKey.find(textureKey);
    if (slotIt != runtime.textureSlotByKey.end())
    {
        auto const slot = slotIt->second;
        if (slot < runtime.texturesBySlot.size())
        {
            auto& textureEntry = runtime.texturesBySlot[slot];
            if (textureEntry.image.valid())
            {
                runtime.retiredTextures.push_back(UiRetiredTexture{
                    .image = std::move(textureEntry.image),
                    .slot = slot,
                    .retiredFrameIndex = currentFrameIndex,
                });
            }
            textureEntry = UiTextureEntry{};
        }
        runtime.textureSlotByKey.erase(slotIt);
        markBindlessTextureTableDirty(runtime);
    }
    if (ownedByRuntime)
    {
        textureData.BackendUserData = nullptr;
        textureData.SetTexID(ImTextureID{});
        textureData.SetStatus(ImTextureStatus_Destroyed);
    }
}

[[nodiscard]] bool runtimeHasValidTextureFor(
    const UiRuntimeCache& runtime,
    const ImTextureData& textureData) noexcept
{
    if (!textureOwnedByRuntime(runtime, textureData))
    {
        return false;
    }

    auto const textureKey = makeManagedTextureKey(textureData);
    auto const slotIt = runtime.textureSlotByKey.find(textureKey);
    if (slotIt == runtime.textureSlotByKey.end())
    {
        return false;
    }

    auto const slot = static_cast<std::size_t>(slotIt->second);
    return slot < runtime.texturesBySlot.size() && runtime.texturesBySlot[slot].image.valid();
}

void cleanupRetiredTextures(
    UiRuntimeCache& runtime,
    std::uint64_t currentFrameIndex)
{
    constexpr auto kRetirementFrameCount = static_cast<std::uint64_t>(nr::maxFrameInFlight + 1u);

    std::erase_if(runtime.retiredTextures, [&](const UiRetiredTexture& retired) {
        auto const frameLatencySatisfied = currentFrameIndex >= retired.retiredFrameIndex + kRetirementFrameCount;
        if (frameLatencySatisfied)
        {
            if (retired.releaseSlot &&
                std::ranges::find(runtime.freeTextureSlots, retired.slot) == runtime.freeTextureSlots.end())
            {
                if (retired.slot < runtime.texturesBySlot.size() &&
                    !runtime.texturesBySlot[retired.slot].image.valid())
                {
                    runtime.freeTextureSlots.push_back(retired.slot);
                }
            }
            return true;
        }
        return false;
    });
}

void synchronizeUiTextures(
    nr::rhi::Device& device,
    UiRuntimeCache& runtime,
    const ImDrawData& drawData,
    std::uint64_t currentFrameIndex)
{
    cleanupRetiredTextures(runtime, currentFrameIndex);
    
    if (drawData.Textures == nullptr)
    {
        return;
    }

    std::ranges::for_each(*drawData.Textures, [&](ImTextureData* textureData) {
        if (textureData == nullptr)
        {
            return;
        }

        switch (textureData->Status)
        {
        case ImTextureStatus_OK:
            if (!runtimeHasValidTextureFor(runtime, *textureData))
            {
                createOrUpdateUiTexture(device, runtime, *textureData, currentFrameIndex);
            }
            break;
        case ImTextureStatus_WantCreate:
        case ImTextureStatus_WantUpdates:
            createOrUpdateUiTexture(device, runtime, *textureData, currentFrameIndex);
            break;
        case ImTextureStatus_WantDestroy:
            destroyUiTexture(runtime, *textureData, currentFrameIndex);
            break;
        default:
            break;
        }
    });
}

[[nodiscard]] std::map<std::uint32_t, nr::renderer::GraphResourceHandle> registerUiTextureImageResources(
    nr::renderer::NodeBuildContext& context,
    UiRuntimeCache& runtime)
{
    auto graphResourceBySlot = std::map<std::uint32_t, nr::renderer::GraphResourceHandle>{};
    auto slotRange = std::views::iota(std::size_t{0}, runtime.texturesBySlot.size());
    std::ranges::for_each(slotRange, [&](std::size_t slot) {
        auto& textureEntry = runtime.texturesBySlot[slot];
        if (!textureEntry.image.valid())
        {
            return;
        }

        nr::nrAssert(
            slot < kUiTextureDescriptorCapacity,
            std::format(
                "UiNode graph resource registration found slot {} beyond descriptor capacity {}.",
                slot,
                kUiTextureDescriptorCapacity));

        auto const textureSlot = static_cast<std::uint32_t>(slot);
        auto resource = context.addResource(nr::renderer::GraphImportedImageDesc{
            .debugName = std::format("Ui.TextureResource[{}]", slot),
            .lifetime = nr::renderer::ResourceLifetime::RendererPersistent,
            .initialOwnership = nr::renderer::ResourceOwnershipDomain::Graphics,
            .extent = textureEntry.image.extent(),
            .format = vk::Format::eR8G8B8A8Unorm,
            .usageIntents = {
                nr::renderer::ImageUsageIntent::Sampled,
            },
            .initialLayout = textureEntry.state.common.initialized
                                 ? textureEntry.state.layout
                                 : nr::renderer::ImageLayoutIntent::Undefined,
            .initialAccessScope = textureEntry.state.common.initialized
                                      ? textureEntry.state.common.access
                                      : nr::renderer::AccessScope{},
            .importedResource = std::cref(textureEntry.image),
            .retainedState = std::ref(textureEntry.state),
        });

        graphResourceBySlot.insert_or_assign(textureSlot, resource);
    });

    return graphResourceBySlot;
}

[[nodiscard]] std::string uiSkeletonBranchKey(const UiRuntimeCache& runtime, vk::Format format)
{
    auto key = std::format(
        "overlay;format={};revision={};slots={}",
        static_cast<std::uint32_t>(format),
        runtime.textureTableRevision,
        runtime.texturesBySlot.size());
    auto const slots = std::views::iota(std::size_t{0u}, runtime.texturesBySlot.size());
    std::ranges::for_each(slots, [&](std::size_t slot) {
        auto const& entry = runtime.texturesBySlot[slot];
        key += std::format(
            ";{}:{}:{}",
            slot,
            entry.textureKey,
            entry.image.valid() ? 1u : 0u);
    });
    return key;
}

void ensureFrameUploadBuffer(
    nr::rhi::Device& device,
    nr::rhi::Buffer& buffer,
    vk::DeviceSize requiredSize,
    vk::BufferUsageFlags usage,
    std::string_view debugName)
{
    if (requiredSize == 0u)
    {
        return;
    }

    if (buffer.valid() && buffer.size() >= requiredSize)
    {
        return;
    }

    auto capacity = std::max<vk::DeviceSize>(buffer.valid() ? buffer.size() : 1u, 1u);
    while (capacity < requiredSize)
    {
        capacity = std::max(requiredSize, capacity * 2u);
    }

    auto bufferInfo = nr::rhi::makeBufferCreateInfo(capacity, usage);

    buffer = device.resourceFactory.createBuffer(
        bufferInfo,
        nr::rhi::MemoryUsage::CpuToGpu,
        debugName);
    nr::nrAssert(buffer.valid(), std::format("UiNode failed to create upload buffer '{}'.", debugName));
}

[[nodiscard]] std::optional<std::reference_wrapper<nr::app::UiSystem>> tryGetUiOverlaySystem(
    const nr::renderer::NodeFrameParameters& frameParameters)
{
    if (!frameParameters.frameServices.has_value())
    {
        return std::nullopt;
    }

    return frameParameters.frameServices->get().tryGet<nr::app::UiSystem>();
}

void drawCpuTimingLine(nr::app::UiSystem& ui, std::string_view label, double milliseconds)
{
    ui.textFmt("{}: {:.3f} ms", label, milliseconds);
}

template <typename Statistics>
[[nodiscard]] bool hasPerformanceSample(const Statistics& statistics) noexcept
{
    return statistics.valid;
}

[[nodiscard]] std::string_view queueDomainLabel(nr::renderer::QueueDomain queue) noexcept
{
    if (queue == nr::renderer::QueueDomain::Graphics)
    {
        return "Graphics";
    }
    if (queue == nr::renderer::QueueDomain::Compute)
    {
        return "Compute";
    }
    return "Transfer";
}

void drawGpuPassTimingLine(
    nr::app::UiSystem& ui,
    const nr::renderer::RendererGpuPassAverage& timing,
    std::uint32_t averagedFrameCount)
{
    auto const passName = timing.debugName.empty()
                              ? std::format("Pass {}", timing.pass.value)
                              : timing.debugName;
    auto const passKind = timing.isCopyPass ? std::string_view{"Copy"} : queueDomainLabel(timing.queue);
    if (timing.sampleCount == averagedFrameCount)
    {
        ui.textFmt("{} [{}]: {:.3f} ms", passName, passKind, timing.milliseconds);
        return;
    }

    ui.textFmt(
        "{} [{}]: {:.3f} ms ({} samples)",
        passName,
        passKind,
        timing.milliseconds,
        timing.sampleCount);
}

void drawCpuPerformanceSection(nr::app::UiSystem& ui)
{
    auto const& statistics = ui.cpuStatistics();
    if (!hasPerformanceSample(statistics))
    {
        return;
    }

    auto const& average = statistics.average;
    drawCpuTimingLine(ui, "CPU Wait GPU", average.cpuWaitGpuMilliseconds);
    drawCpuTimingLine(ui, "Frame Setup", average.frameSetupMilliseconds);
    drawCpuTimingLine(ui, "Scene", average.sceneMilliseconds);
    drawCpuTimingLine(ui, "Post Scene", average.postSceneMilliseconds);
    drawCpuTimingLine(ui, "Build", average.buildMilliseconds);
    drawCpuTimingLine(ui, "Compile", average.compileMilliseconds);
    drawCpuTimingLine(ui, "Prepare", average.prepareMilliseconds);
    drawCpuTimingLine(ui, "Execute", average.executeMilliseconds);
    drawCpuTimingLine(ui, "Present", average.presentMilliseconds);
    drawCpuTimingLine(ui, "Total", average.totalMilliseconds);
}

void drawGpuPassPerformanceSection(nr::app::UiSystem& ui)
{
    auto const& statistics = ui.gpuPassStatistics();
    if (!hasPerformanceSample(statistics))
    {
        return;
    }

    if (statistics.averages.empty())
    {
        ui.text("No addPass samples");
        return;
    }

    std::ranges::for_each(statistics.averages, [&](const nr::renderer::RendererGpuPassAverage& timing) {
        drawGpuPassTimingLine(ui, timing, statistics.averagedFrameCount);
    });
}

template <std::size_t N>
using UiSectionArray = std::array<nr::app::UiSection, N>;

template <std::size_t N>
[[nodiscard]] std::span<const nr::app::UiSection> sectionSpan(const UiSectionArray<N>& sections) noexcept
{
    return std::span<const nr::app::UiSection>{sections.data(), sections.size()};
}

[[nodiscard]] UiSectionArray<2> makeTrailingPerformanceUiSections()
{
    return {
        nr::app::UiSection{
            .id = "cpu.performance",
            .title = "CPU Performance",
            .draw = drawCpuPerformanceSection,
        },
        nr::app::UiSection{
            .id = "gpu.performance",
            .title = "GPU Performance",
            .draw = drawGpuPassPerformanceSection,
        },
    };
}

[[nodiscard]] std::map<std::uint32_t, nr::renderer::BindlessImageDescriptor> makeUiTextureDescriptors(
    const UiRuntimeCache& runtime)
{
    auto descriptorsById = std::map<std::uint32_t, nr::renderer::BindlessImageDescriptor>{};
    auto slotRange = std::views::iota(std::size_t{0}, runtime.texturesBySlot.size());
    std::ranges::for_each(slotRange, [&](std::size_t slot) {
        auto const& textureEntry = runtime.texturesBySlot[slot];
        if (!textureEntry.image.valid())
        {
            return;
        }

        nr::nrAssert(
            slot < kUiTextureDescriptorCapacity,
            std::format(
                "UiNode bindless descriptor update found slot {} beyond descriptor capacity {}.",
                slot,
                kUiTextureDescriptorCapacity));
        descriptorsById.insert_or_assign(
            static_cast<std::uint32_t>(slot),
            nr::renderer::BindlessImageDescriptor{
                .image = std::cref(textureEntry.image),
                .layout = vk::ImageLayout::eShaderReadOnlyOptimal,
            });
    });
    return descriptorsById;
}

[[nodiscard]] nr::renderer::BindlessImageTableRequest makeBindlessTextureTableRequest(UiRuntimeCache& runtime)
{
    nr::nrAssert(runtime.textureSampler.valid(), "UiNode bindless texture table requires a valid sampler.");

    auto descriptorsById = makeUiTextureDescriptors(runtime);
    auto fallbackDescriptor = std::optional<nr::renderer::BindlessImageDescriptor>{};
    if (!descriptorsById.empty())
    {
        fallbackDescriptor = descriptorsById.begin()->second;
    }

    return nr::renderer::BindlessImageTableRequest{
        .tableKey = "ui.gUiTextures",
        .shaderSymbol = "gUiTextures",
        .expectedSet = 1u,
        .expectedBinding = 0u,
        .expectedDescriptorType = vk::DescriptorType::eCombinedImageSampler,
        .descriptorCapacity = kUiTextureDescriptorCapacity,
        .sampler = runtime.textureSampler.raw(),
        .tableVersion = runtime.textureTableRevision,
        .refreshActiveDescriptorsOnCacheHit = true,
        .descriptorsById = std::move(descriptorsById),
        .fallbackDescriptor = fallbackDescriptor,
    };
}

void prepareBindlessTextureTableForFrame(
    UiRuntimeCache& runtime,
    nr::renderer::BindlessImageTableCache& cache,
    std::uint32_t frameIndex)
{
    nr::nrAssert(static_cast<bool>(runtime.pipeline), "UiNode bindless texture table requires an initialized pipeline runtime.");

    cache.ensureTableForFrame(
        *runtime.pipeline,
        frameIndex,
        makeBindlessTextureTableRequest(runtime));
}

[[nodiscard]] nr::rhi::ShaderBindingSnapshot makeBindlessTextureBindingSnapshotForFrame(
    UiRuntimeCache& runtime,
    nr::renderer::BindlessImageTableCache& cache,
    std::uint32_t frameIndex)
{
    return cache.makeSnapshotForFrame(
        *runtime.pipeline,
        frameIndex,
        makeBindlessTextureTableRequest(runtime));
}

[[nodiscard]] UiFrameDrawData copyUiDrawData(
    const ImDrawData& drawData,
    vk::Extent2D fallbackExtent)
{
    auto output = UiFrameDrawData{};
    output.framebufferExtent = vk::Extent2D{
        std::max(1u, fallbackExtent.width),
        std::max(1u, fallbackExtent.height),
    };

    if (drawData.TotalVtxCount <= 0 || drawData.TotalIdxCount <= 0)
    {
        return output;
    }

    auto const framebufferWidth = static_cast<int>(drawData.DisplaySize.x * drawData.FramebufferScale.x);
    auto const framebufferHeight = static_cast<int>(drawData.DisplaySize.y * drawData.FramebufferScale.y);
    if (framebufferWidth <= 0 || framebufferHeight <= 0)
    {
        return output;
    }

    auto const effectiveWidth = std::max(1u, std::min(fallbackExtent.width, static_cast<std::uint32_t>(framebufferWidth)));
    auto const effectiveHeight = std::max(1u, std::min(fallbackExtent.height, static_cast<std::uint32_t>(framebufferHeight)));
    output.framebufferExtent = vk::Extent2D{effectiveWidth, effectiveHeight};
    output.pushConstants = UiPushConstants{
        .scale = glm::vec2{
            2.0f / drawData.DisplaySize.x,
            2.0f / drawData.DisplaySize.y,
        },
        .translate = glm::vec2{
            -1.0f - drawData.DisplayPos.x * (2.0f / drawData.DisplaySize.x),
            -1.0f - drawData.DisplayPos.y * (2.0f / drawData.DisplaySize.y),
        },
    };

    output.vertices.reserve(static_cast<std::size_t>(drawData.TotalVtxCount));
    output.indices.reserve(static_cast<std::size_t>(drawData.TotalIdxCount));

    auto globalVertexOffset = std::uint32_t{0u};
    auto globalIndexOffset = std::uint32_t{0u};

    auto commandListIndices = std::views::iota(0, drawData.CmdListsCount);
    std::ranges::for_each(commandListIndices, [&](int commandListIndex) {
        auto* commandList = drawData.CmdLists[commandListIndex];
        if (commandList == nullptr)
        {
            return;
        }

        output.vertices.insert(
            output.vertices.end(),
            commandList->VtxBuffer.Data,
            commandList->VtxBuffer.Data + commandList->VtxBuffer.Size);
        output.indices.insert(
            output.indices.end(),
            commandList->IdxBuffer.Data,
            commandList->IdxBuffer.Data + commandList->IdxBuffer.Size);

        auto commandIndices = std::views::iota(0, commandList->CmdBuffer.Size);
        std::ranges::for_each(commandIndices, [&](int commandIndex) {
            auto const& command = commandList->CmdBuffer[commandIndex];
            if (command.UserCallback != nullptr)
            {
                return;
            }

            auto const clipMinX = std::max(0.0f, (command.ClipRect.x - drawData.DisplayPos.x) * drawData.FramebufferScale.x);
            auto const clipMinY = std::max(0.0f, (command.ClipRect.y - drawData.DisplayPos.y) * drawData.FramebufferScale.y);
            auto const clipMaxX = std::min(
                static_cast<float>(effectiveWidth),
                (command.ClipRect.z - drawData.DisplayPos.x) * drawData.FramebufferScale.x);
            auto const clipMaxY = std::min(
                static_cast<float>(effectiveHeight),
                (command.ClipRect.w - drawData.DisplayPos.y) * drawData.FramebufferScale.y);

            if (clipMaxX <= clipMinX || clipMaxY <= clipMinY)
            {
                return;
            }

            auto const offsetX = static_cast<std::int32_t>(clipMinX);
            auto const offsetY = static_cast<std::int32_t>(clipMinY);
            auto const extentWidth = static_cast<std::uint32_t>(clipMaxX - clipMinX);
            auto const extentHeight = static_cast<std::uint32_t>(clipMaxY - clipMinY);
            auto const firstIndex = globalIndexOffset + static_cast<std::uint32_t>(command.IdxOffset);
            auto const vertexOffset = globalVertexOffset + static_cast<std::uint32_t>(command.VtxOffset);

            nr::nrAssert(
                vertexOffset <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()),
                "UiNode vertex offset exceeds std::int32_t range.");

            auto scissor = vk::Rect2D{};
            scissor.offset = vk::Offset2D{offsetX, offsetY};
            scissor.extent = vk::Extent2D{extentWidth, extentHeight};

            output.commands.push_back(UiDrawCommand{
                .scissor = scissor,
                .elementCount = static_cast<std::uint32_t>(command.ElemCount),
                .firstIndex = firstIndex,
                .vertexOffset = static_cast<std::int32_t>(vertexOffset),
                .textureSlot = textureSlotFromId(command.GetTexID()),
            });
        });

        globalVertexOffset += static_cast<std::uint32_t>(commandList->VtxBuffer.Size);
        globalIndexOffset += static_cast<std::uint32_t>(commandList->IdxBuffer.Size);
    });

    return output;
}

[[nodiscard]] UiFrameDrawData prepareUiDrawFrame(
    nr::rhi::Device& device,
    UiRuntimeCache& runtime,
    const nr::renderer::NodeFrameParameters& frameParameters)
{
    auto const bufferExtent = vk::Extent2D{
        std::max(1u, frameParameters.swapchainExtent.width),
        std::max(1u, frameParameters.swapchainExtent.height),
    };
    auto drawFrame = UiFrameDrawData{};
    drawFrame.framebufferExtent = bufferExtent;
    auto uiSystem = tryGetUiOverlaySystem(frameParameters);
    if (!uiSystem.has_value())
    {
        return drawFrame;
    }

    auto trailingSections = makeTrailingPerformanceUiSections();
    uiSystem->get().renderSections(
        std::span<const nr::app::UiSection>{},
        sectionSpan(trailingSections));
    uiSystem->get().finalizeFrame();
    auto drawData = uiSystem->get().drawData();
    if (!drawData.has_value())
    {
        return drawFrame;
    }

    synchronizeUiTextures(
        device,
        runtime,
        drawData->get(),
        static_cast<std::uint64_t>(frameParameters.frameIndex));
    return copyUiDrawData(drawData->get(), bufferExtent);
}
} // namespace nr::renderPasses::detail

namespace nr::renderPasses
{
using namespace detail;

UiNode::~UiNode() = default;

[[nodiscard]] std::vector<nr::rhi::SlangProgramCompileFileRequest> UiNode::shaderRequests() const
{
        return {
            nr::rhi::SlangProgramCompileFileRequest{
                .sourcePath = std::filesystem::path{"renderer/appUi/vertex"},
            },
            nr::rhi::SlangProgramCompileFileRequest{
                .sourcePath = std::filesystem::path{"renderer/appUi/fragment"},
            },
        };
}

void UiNode::initialize(NodeInitContext& context)
{
        nr::nrAssert(
            context.shaderPrograms.size() == 2u &&
                context.shaderPrograms[0].entryPoint() != nullptr &&
                context.shaderPrograms[0].entryPoint()->stage == SLANG_STAGE_VERTEX &&
                context.shaderPrograms[1].entryPoint() != nullptr &&
                context.shaderPrograms[1].entryPoint()->stage == SLANG_STAGE_FRAGMENT,
            "Ui initialization requires ordered vertex and fragment shaders.");
        device_ = context.device;

        auto bufferFormat = input.bufferFormat == vk::Format::eUndefined
                                ? vk::Format::eR8G8B8A8Unorm
                                : input.bufferFormat;

        runtime_ = ensureUiRuntime(context.device.get(), context.shaderPrograms, bufferFormat);
        nr::rhi::setPipelineDebugName(
            context.device.get().device,
            runtime_->pipeline->pipeline().raw(),
            context.runtimeName + ".Pipeline");
    }

void UiNode::build(NodeBuildContext& context, const NodeFrameParameters& frameParameters)
{
        materializeCurrentFrame(context, frameParameters);
    }

[[nodiscard]] std::optional<nr::renderer::NodeRuntime::StructuralSnapshot>
UiNode::structuralSnapshot(const NodeFrameParameters& frameParameters) const
{
        if (!runtime_ || !device_.has_value())
        {
            return std::nullopt;
        }
        runtime_->preparedDrawFrame = detail::prepareUiDrawFrame(
            device_->get(), *runtime_, frameParameters);
        auto const bufferFormat = input.bufferFormat == vk::Format::eUndefined
                                      ? vk::Format::eR8G8B8A8Unorm
                                      : input.bufferFormat;
        auto branch = detail::uiSkeletonBranchKey(*runtime_, bufferFormat);
        return StructuralSnapshot{
            .configurationRevision = std::max<std::uint64_t>(1u, std::hash<std::string>{}(branch)),
            .branchKey = std::move(branch),
        };
    }

bool UiNode::materializeRenderGraphSkeleton(
    nr::renderer::RenderGraphSkeletonPatchContext& context,
    const NodeFrameParameters& frameParameters,
    const StructuralSnapshot& snapshot)
{
        nr::nrAssert(static_cast<bool>(runtime_) && device_.has_value(), "UiNode Skeleton patch requires initialized state.");
        auto const bufferExtent = vk::Extent2D{
            std::max(1u, frameParameters.swapchainExtent.width),
            std::max(1u, frameParameters.swapchainExtent.height),
        };
        auto const bufferFormat = input.bufferFormat == vk::Format::eUndefined
                                      ? vk::Format::eR8G8B8A8Unorm
                                      : input.bufferFormat;
        detail::ensureUiBufferImage(device_->get(), *runtime_, bufferExtent, bufferFormat);
        auto const frameSlot = static_cast<std::size_t>(frameParameters.frameIndex % nr::maxFrameInFlight);
        context.patchResource(0u, nr::renderer::GraphImportedImageDesc{
            .debugName = std::format("Ui.Buffer[{}]", frameSlot),
            .lifetime = nr::renderer::ResourceLifetime::FrameLocal,
            .extent = vk::Extent3D{bufferExtent.width, bufferExtent.height, 1u},
            .format = bufferFormat,
            .usageIntents = {
                nr::renderer::ImageUsageIntent::Sampled,
                nr::renderer::ImageUsageIntent::ColorAttachment,
            },
            .initialLayout = nr::renderer::ImageLayoutIntent::Undefined,
            .importedResource = std::cref(runtime_->uiBuffers[frameSlot]),
        });

        if (!runtime_->preparedDrawFrame.has_value())
        {
            runtime_->preparedDrawFrame = detail::prepareUiDrawFrame(
                device_->get(), *runtime_, frameParameters);
        }
        auto const drawFrame = *runtime_->preparedDrawFrame;
        if (detail::uiSkeletonBranchKey(*runtime_, bufferFormat) != snapshot.branchKey)
        {
            return false;
        }

        auto resourceSlot = std::size_t{1u};
        auto const textureSlots = std::views::iota(std::size_t{0u}, runtime_->texturesBySlot.size());
        std::ranges::for_each(textureSlots, [&](std::size_t slot) {
            auto& entry = runtime_->texturesBySlot[slot];
            if (!entry.image.valid())
            {
                return;
            }
            context.patchResource(resourceSlot++, nr::renderer::GraphImportedImageDesc{
                .debugName = std::format("Ui.TextureResource[{}]", slot),
                .lifetime = nr::renderer::ResourceLifetime::RendererPersistent,
                .initialOwnership = nr::renderer::ResourceOwnershipDomain::Graphics,
                .extent = entry.image.extent(),
                .format = vk::Format::eR8G8B8A8Unorm,
                .usageIntents = {nr::renderer::ImageUsageIntent::Sampled},
                .initialLayout = entry.state.common.initialized
                                     ? entry.state.layout
                                     : nr::renderer::ImageLayoutIntent::Undefined,
                .initialAccessScope = entry.state.common.initialized
                                          ? entry.state.common.access
                                          : nr::renderer::AccessScope{},
                .importedResource = std::cref(entry.image),
                .retainedState = std::ref(entry.state),
            });
        });

        auto runtime = runtime_;
        auto& bindlessCache = context.globalResources().bindlessImageTableCache.get();
        auto patch = nr::renderer::RasterPassPatchBuilder{
            context, 0u, "Ui.Overlay", runtime_->pipeline};
        patch.viewport(drawFrame.framebufferExtent)
            .colorAttachment(
                context.resource(0u),
                vk::ClearValue{vk::ClearColorValue{
                    std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}}})
            .rasterState(nr::rhi::MeshRasterState{
                .cullMode = vk::CullModeFlagBits::eNone,
                .depthCompareOp = vk::CompareOp::eAlways,
            })
            .prepare([runtime, drawFrame, cache = std::ref(bindlessCache)](
                         const nr::renderer::PassPrepareContext& prepareContext) {
                detail::prepareBindlessTextureTableForFrame(
                    *runtime, cache.get(), prepareContext.frameIndex);
                auto& device = prepareContext.device->get();
                auto const frameSlot = static_cast<std::size_t>(
                    prepareContext.frameIndex % runtime->vertexBuffers.size());
                auto const vertexBytes = static_cast<vk::DeviceSize>(
                    drawFrame.vertices.size() * sizeof(ImDrawVert));
                auto const indexBytes = static_cast<vk::DeviceSize>(
                    drawFrame.indices.size() * sizeof(ImDrawIdx));
                detail::ensureFrameUploadBuffer(
                    device, runtime->vertexBuffers[frameSlot], vertexBytes,
                    vk::BufferUsageFlagBits::eVertexBuffer,
                    std::format("Ui.VertexBuffer[{}]", frameSlot));
                detail::ensureFrameUploadBuffer(
                    device, runtime->indexBuffers[frameSlot], indexBytes,
                    vk::BufferUsageFlagBits::eIndexBuffer,
                    std::format("Ui.IndexBuffer[{}]", frameSlot));
                if (vertexBytes > 0u)
                {
                    runtime->vertexBuffers[frameSlot].writeMappedAndFlush(
                        std::span<const ImDrawVert>{drawFrame.vertices});
                }
                if (indexBytes > 0u)
                {
                    runtime->indexBuffers[frameSlot].writeMappedAndFlush(
                        std::span<const ImDrawIdx>{drawFrame.indices});
                }
            })
            .dynamicBindingSnapshot(
                [runtime, cache = std::ref(bindlessCache)](
                    const nr::renderer::PassPrepareContext& prepareContext) {
                    return detail::makeBindlessTextureBindingSnapshotForFrame(
                        *runtime, cache.get(), prepareContext.frameIndex);
                })
            .record([runtime, drawFrame](const nr::renderer::RasterPassRecordContext& rasterContext) {
                if (drawFrame.commands.empty())
                {
                    return;
                }
                auto const frameSlot = static_cast<std::size_t>(
                    rasterContext.pass.frameIndex % runtime->vertexBuffers.size());
                auto& commandBuffer = rasterContext.commandBuffer;
                auto const vertexBuffers = std::array{
                    runtime->vertexBuffers[frameSlot].handle()};
                auto const vertexOffsets = std::array<vk::DeviceSize, 1>{0u};
                commandBuffer.bindVertexBuffers(0u, vertexBuffers, vertexOffsets);
                commandBuffer.bindIndexBuffer(
                    runtime->indexBuffers[frameSlot].handle(), 0u,
                    sizeof(ImDrawIdx) == 2u
                        ? vk::IndexType::eUint16
                        : vk::IndexType::eUint32);
                auto constants = drawFrame.pushConstants;
                auto lastTexture = std::optional<std::uint32_t>{};
                std::ranges::for_each(drawFrame.commands, [&](const detail::UiDrawCommand& command) {
                    if (command.elementCount == 0u ||
                        command.scissor.extent.width == 0u ||
                        command.scissor.extent.height == 0u)
                    {
                        return;
                    }
                    nr::nrAssert(
                        command.textureSlot < runtime->texturesBySlot.size() &&
                            runtime->texturesBySlot[command.textureSlot].image.valid(),
                        std::format(
                            "UiNode Skeleton record could not resolve texture slot {}.",
                            command.textureSlot));
                    if (!lastTexture.has_value() || *lastTexture != command.textureSlot)
                    {
                        constants.textureIndex = command.textureSlot;
                        rasterContext.pushConstants("gUiPush", constants);
                        lastTexture = command.textureSlot;
                    }
                    commandBuffer.setScissor(0u, {command.scissor});
                    commandBuffer.drawIndexed(
                        command.elementCount, 1u, command.firstIndex,
                        command.vertexOffset, 0u);
                });
            });
        patch.patch();
        runtime_->preparedDrawFrame.reset();
        return true;
}

void UiNode::materializeCurrentFrame(NodeBuildContext& context, const NodeFrameParameters& frameParameters)
{
        nr::nrAssert(static_cast<bool>(runtime_), "UiNode build stage requires initialized runtime state.");
        nr::nrAssert(device_.has_value(), "UiNode build stage requires initialize() device reference.");

        auto const bufferExtent = vk::Extent2D{
            std::max(1u, frameParameters.swapchainExtent.width),
            std::max(1u, frameParameters.swapchainExtent.height),
        };
        auto const bufferFormat = input.bufferFormat == vk::Format::eUndefined
                                      ? vk::Format::eR8G8B8A8Unorm
                                      : input.bufferFormat;

        ensureUiBufferImage(device_->get(), *runtime_, bufferExtent, bufferFormat);

        auto const frameSlot = static_cast<std::size_t>(frameParameters.frameIndex % nr::maxFrameInFlight);
        auto uiBuffer = context.importSampledColor(
            runtime_->uiBuffers[frameSlot],
            std::format("Ui.Buffer[{}]", frameSlot),
            bufferExtent,
            bufferFormat,
            nr::renderer::ResourceLifetime::FrameLocal);

        context.publishFrameResource(nr::renderer::frameResource::uiColor, uiBuffer);

        if (!runtime_->preparedDrawFrame.has_value())
        {
            runtime_->preparedDrawFrame = prepareUiDrawFrame(
                device_->get(), *runtime_, frameParameters);
        }
        auto drawFrame = std::move(*runtime_->preparedDrawFrame);
        runtime_->preparedDrawFrame.reset();

        auto graphResourceBySlot = registerUiTextureImageResources(
            context,
            *runtime_);

        auto runtime = runtime_;
        auto& bindlessImageTableCache = context.globalResources.get().bindlessImageTableCache.get();
        auto overlayPass = nr::renderer::RasterPassBuilder{
            context,
            "Ui.Overlay",
            runtime_->pipeline};
        overlayPass
            .viewport(drawFrame.framebufferExtent)
            .colorAttachment(
                uiBuffer,
                vk::ClearValue{vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}}})
            .rasterState(nr::rhi::MeshRasterState{
                .cullMode = vk::CullModeFlagBits::eNone,
                .depthCompareOp = vk::CompareOp::eAlways,
            })
            .prepare([runtime, drawFrame, cache = std::ref(bindlessImageTableCache)](const nr::renderer::PassPrepareContext& prepareContext) {
                nr::nrAssert(prepareContext.device.has_value(), "UiNode prepare stage requires device access.");
                nr::nrAssert(static_cast<bool>(runtime), "UiNode prepare stage requires initialized runtime state.");

                prepareBindlessTextureTableForFrame(*runtime, cache.get(), prepareContext.frameIndex);

                auto& device = prepareContext.device->get();
                auto const frameSlot = static_cast<std::size_t>(prepareContext.frameIndex % runtime->vertexBuffers.size());

                auto const vertexBytes = static_cast<vk::DeviceSize>(drawFrame.vertices.size() * sizeof(ImDrawVert));
                auto const indexBytes = static_cast<vk::DeviceSize>(drawFrame.indices.size() * sizeof(ImDrawIdx));

                ensureFrameUploadBuffer(
                    device,
                    runtime->vertexBuffers[frameSlot],
                    vertexBytes,
                    vk::BufferUsageFlagBits::eVertexBuffer,
                    std::format("Ui.VertexBuffer[{}]", frameSlot));
                ensureFrameUploadBuffer(
                    device,
                    runtime->indexBuffers[frameSlot],
                    indexBytes,
                    vk::BufferUsageFlagBits::eIndexBuffer,
                    std::format("Ui.IndexBuffer[{}]", frameSlot));

                if (vertexBytes > 0u)
                {
                    auto& vertexBuffer = runtime->vertexBuffers[frameSlot];
                    vertexBuffer.writeMappedAndFlush(std::span<const ImDrawVert>{drawFrame.vertices.data(), drawFrame.vertices.size()});
                }

                if (indexBytes > 0u)
                {
                    auto& indexBuffer = runtime->indexBuffers[frameSlot];
                    indexBuffer.writeMappedAndFlush(std::span<const ImDrawIdx>{drawFrame.indices.data(), drawFrame.indices.size()});
                }
            })
            .dynamicBindingSnapshot([runtime, cache = std::ref(bindlessImageTableCache)](const nr::renderer::PassPrepareContext& prepareContext) {
                return makeBindlessTextureBindingSnapshotForFrame(*runtime, cache.get(), prepareContext.frameIndex);
            })
            .record([runtime, drawFrame](const nr::renderer::RasterPassRecordContext& rasterContext) {
                if (drawFrame.commands.empty())
                {
                    return;
                }

                auto const frameSlot = static_cast<std::size_t>(rasterContext.pass.frameIndex % runtime->vertexBuffers.size());
                auto const& vertexBuffer = runtime->vertexBuffers[frameSlot];
                auto const& indexBuffer = runtime->indexBuffers[frameSlot];
                nr::nrAssert(vertexBuffer.valid(), "UiNode requires a valid per-frame vertex buffer.");
                nr::nrAssert(indexBuffer.valid(), "UiNode requires a valid per-frame index buffer.");

                auto& commandBuffer = rasterContext.commandBuffer;
                auto vertexBuffers = std::array{vertexBuffer.handle()};
                auto vertexOffsets = std::array<vk::DeviceSize, 1>{0u};
                commandBuffer.bindVertexBuffers(0u, vertexBuffers, vertexOffsets);
                commandBuffer.bindIndexBuffer(
                    indexBuffer.handle(),
                    0u,
                    sizeof(ImDrawIdx) == 2u ? vk::IndexType::eUint16 : vk::IndexType::eUint32);

                auto drawPushConstants = drawFrame.pushConstants;
                std::optional<std::uint32_t> lastTextureIndex{};
                std::ranges::for_each(drawFrame.commands, [&](const UiDrawCommand& command) {
                    if (command.elementCount == 0u || command.scissor.extent.width == 0u || command.scissor.extent.height == 0u)
                    {
                        return;
                    }

                    nr::nrAssert(
                        command.textureSlot < runtime->texturesBySlot.size() &&
                            runtime->texturesBySlot[command.textureSlot].image.valid(),
                        std::format(
                            "UiNode record stage could not resolve texture slot {}.",
                            command.textureSlot));

                    auto const textureIndex = command.textureSlot;
                    if (!lastTextureIndex.has_value() || *lastTextureIndex != textureIndex)
                    {
                        drawPushConstants.textureIndex = textureIndex;
                        rasterContext.pushConstants("gUiPush", drawPushConstants);
                        lastTextureIndex = textureIndex;
                    }

                    commandBuffer.setScissor(0u, {command.scissor});
                    commandBuffer.drawIndexed(
                        command.elementCount,
                        1u,
                        command.firstIndex,
                        command.vertexOffset,
                        0u);
                });
            });

        std::ranges::for_each(graphResourceBySlot, [&](const auto& pair) {
            overlayPass.resourceUse(nr::renderer::use::withShaderStages(
                nr::renderer::use::sampledRead(pair.second),
                nr::renderer::ShaderStageIntent::Fragment));
        });

        [[maybe_unused]] auto overlayPassHandle = overlayPass.build();
    }

void UiNode::shutdown(NodeShutdownContext&)
{
        if (runtime_ && runtime_->pipeline)
        {
            runtime_->pipeline->clearBindingSets();
        }

        runtime_.reset();
        device_.reset();
    }
} // namespace nr::renderPasses
