module;
#include <cstddef>
export module nr.renderPasses:uiNode;

import dependency;
import nr.app;
import nr.renderer;
import nr.rhi;
import nr.utils;
import std;
import :nodeType;

namespace
{
struct UiPushConstants
{
    glm::vec2 scale{1.0f, 1.0f};
    glm::vec2 translate{0.0f, 0.0f};
    std::uint32_t textureIndex = 0u;
    std::array<std::uint32_t, 3> padding{};
};

inline constexpr ImTextureID kInvalidTextureId = ImTextureID{};
inline constexpr std::uint32_t kInitialUiTextureDescriptorCapacity = 64u;
inline constexpr std::uint32_t kMaxUiTextureDescriptorCapacity = 4096u;

static_assert(sizeof(UiPushConstants) <= 128u, "UiNode push constants exceed 128 bytes.");

struct UiDrawCommand
{
    vk::Rect2D scissor{};
    std::uint32_t elementCount = 0u;
    std::uint32_t firstIndex = 0u;
    std::int32_t vertexOffset = 0;
    ImTextureID textureId = kInvalidTextureId;
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
    std::optional<nr::rhi::ops::ImageUploadTicket> pendingUpload{};
};

struct UiRetiredTexture
{
    nr::rhi::Image image{};
    std::uint32_t slot = 0u;
    std::uint64_t retiredFrameIndex = 0u;
};

struct UiRuntimeCache
{
    nr::rhi::PipelineState<nr::rhi::GraphicsPipeline> pipeline{};
    std::array<std::vector<nr::rhi::ShaderBindingSet>, nr::maxFrameInFlight> bindlessBindingSetsByFrame{};
    std::array<std::uint32_t, nr::maxFrameInFlight> bindlessDescriptorCapacityByFrame{};
    std::array<bool, nr::maxFrameInFlight> bindlessDescriptorsInitializedByFrame{};
    nr::rhi::SlangSampler textureSampler{};
    std::array<nr::rhi::Buffer, nr::maxFrameInFlight> vertexBuffers{};
    std::array<nr::rhi::Buffer, nr::maxFrameInFlight> indexBuffers{};
    std::map<ImTextureID, UiTextureEntry> textures{};
    std::map<ImTextureID, std::uint32_t> textureSlotById{};
    std::vector<ImTextureID> textureIdsBySlot{};
    std::vector<std::uint32_t> freeTextureSlots{};
    std::vector<UiRetiredTexture> retiredTextures{};
    std::uint32_t textureDescriptorCapacity = 1u;
    std::uint32_t maxTextureDescriptorCount = 1u;
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
            static_cast<std::uint32_t>(offsetof(ImDrawVert, pos)),
        },
        vk::VertexInputAttributeDescription{
            1u,
            0u,
            vk::Format::eR32G32Sfloat,
            static_cast<std::uint32_t>(offsetof(ImDrawVert, uv)),
        },
        vk::VertexInputAttributeDescription{
            2u,
            0u,
            vk::Format::eR8G8B8A8Unorm,
            static_cast<std::uint32_t>(offsetof(ImDrawVert, col)),
        },
    };
}

[[nodiscard]] std::shared_ptr<UiRuntimeCache> ensureUiRuntime(
    nr::rhi::Device& device,
    vk::Format colorFormat)
{
    auto& shaderService = nr::rhi::ShaderService::instance();
    auto program = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
        .sourcePath = std::filesystem::path("renderer/appUi"),
    });
    nr::nrAssert(program.valid(), "UiNode failed to compile shader module renderer/appUi.");

    auto pipelineDesc = nr::rhi::GraphicsPipelineDesc{};
    pipelineDesc.entryPointNames = {"vertexMain", "fragmentMain"};
    pipelineDesc.colorAttachmentFormats = {colorFormat};
    pipelineDesc.cullMode = vk::CullModeFlagBits::eNone;
    pipelineDesc.depthTestEnable = false;
    pipelineDesc.depthWriteEnable = false;
    pipelineDesc.vertexBindings = makeUiVertexBindings();
    pipelineDesc.vertexAttributes = makeUiVertexAttributes();
    pipelineDesc.colorBlendAttachments = {makeUiBlendAttachment()};
    pipelineDesc.descriptorBindingPolicy.defaultRuntimeDescriptorCount = std::max(
        1u,
        std::min(
            kMaxUiTextureDescriptorCapacity,
            device.descriptorIndexingCapabilities().maxDescriptorSetUpdateAfterBindSampledImages));

    auto runtime = std::make_shared<UiRuntimeCache>();
    runtime->pipeline = device.pipeline().createGraphicsPipeline(program, pipelineDesc);
    nr::nrAssert(runtime->pipeline.pipeline.valid(), "UiNode failed to create graphics pipeline.");
    runtime->maxTextureDescriptorCount = pipelineDesc.descriptorBindingPolicy.defaultRuntimeDescriptorCount;
    runtime->textureDescriptorCapacity = std::min(runtime->maxTextureDescriptorCount, kInitialUiTextureDescriptorCapacity);

    runtime->textureSampler = device.pipeline().createSampler(nr::rhi::SlangSamplerDesc{
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eLinear,
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW = vk::SamplerAddressMode::eClampToEdge,
    },
                                                          "Ui.TextureSampler");
    nr::nrAssert(runtime->textureSampler.valid(), "UiNode failed to create texture sampler.");

    return runtime;
}

[[nodiscard]] ImTextureID makeManagedTextureId(const ImTextureData& textureData) noexcept
{
    return static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(&textureData));
}

void invalidateBindlessTextureTables(UiRuntimeCache& runtime)
{
    std::ranges::fill(runtime.bindlessDescriptorsInitializedByFrame, false);
}

[[nodiscard]] std::uint32_t growUiDescriptorCapacity(
    std::uint32_t currentCapacity,
    std::uint32_t requiredCapacity,
    std::uint32_t maxCapacity)
{
    auto capacity = std::max(currentCapacity, 1u);
    while (capacity < requiredCapacity && capacity < maxCapacity)
    {
        capacity = std::min(maxCapacity, capacity * 2u);
    }
    return capacity;
}

[[nodiscard]] std::uint32_t acquireUiTextureSlot(
    UiRuntimeCache& runtime,
    ImTextureID textureId)
{
    if (auto existing = runtime.textureSlotById.find(textureId); existing != runtime.textureSlotById.end())
    {
        return existing->second;
    }

    std::uint32_t slot = 0u;
    if (!runtime.freeTextureSlots.empty())
    {
        slot = runtime.freeTextureSlots.back();
        runtime.freeTextureSlots.pop_back();
        runtime.textureIdsBySlot[slot] = textureId;
    }
    else
    {
        slot = static_cast<std::uint32_t>(runtime.textureIdsBySlot.size());
        runtime.textureIdsBySlot.push_back(textureId);
    }

    runtime.textureSlotById.insert_or_assign(textureId, slot);
    runtime.textureDescriptorCapacity = growUiDescriptorCapacity(
        runtime.textureDescriptorCapacity,
        slot + 1u,
        runtime.maxTextureDescriptorCount);
    nr::nrAssert(
        slot < runtime.textureDescriptorCapacity,
        std::format(
            "UiNode bindless texture slot {} exceeds descriptor capacity {}.",
            slot,
            runtime.textureDescriptorCapacity));
    invalidateBindlessTextureTables(runtime);
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

[[nodiscard]] nr::rhi::Image createUiTextureImage(
    nr::rhi::Device& device,
    vk::Extent2D textureExtent,
    std::string_view debugName)
{
    auto imageCreateInfo = vk::ImageCreateInfo{};
    imageCreateInfo.imageType = vk::ImageType::e2D;
    imageCreateInfo.format = vk::Format::eR8G8B8A8Unorm;
    imageCreateInfo.extent = vk::Extent3D{textureExtent.width, textureExtent.height, 1u};
    imageCreateInfo.mipLevels = 1u;
    imageCreateInfo.arrayLayers = 1u;
    imageCreateInfo.samples = vk::SampleCountFlagBits::e1;
    imageCreateInfo.tiling = vk::ImageTiling::eOptimal;
    imageCreateInfo.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
    imageCreateInfo.sharingMode = vk::SharingMode::eExclusive;
    imageCreateInfo.initialLayout = vk::ImageLayout::eUndefined;

    auto image = device.resourceFactory.createImage(imageCreateInfo, nr::rhi::MemoryUsage::GpuOnly, debugName);
    nr::nrAssert(image.valid(), std::format("UiNode failed to create texture image '{}'.", debugName));
    return image;
}

[[nodiscard]] nr::rhi::ops::BufferUploadOwnershipPlan makeUiTextureUploadOwnershipPlan(
    nr::rhi::Device& device)
{
    auto const transferQueueFamily = device.queueManager.transfer().queueFamilyIndex();
    auto const graphicsQueueFamily = device.queueManager.graphics().queueFamilyIndex();
    
    if (transferQueueFamily == graphicsQueueFamily)
    {
        // Same queue family, no ownership transfer needed - use simple exclusive plan
        return nr::rhi::ops::BufferUploadOwnershipPlan{
            .acquireToTransfer = std::nullopt,
            .releaseToDestination = nr::rhi::ops::QueueOwnershipTransfer{
                .release = nr::rhi::ops::QueueOwnershipRequest{
                    .srcQueueFamilyIndex = transferQueueFamily,
                    .dstQueueFamilyIndex = transferQueueFamily,
                    .stages = vk::PipelineStageFlagBits2::eTransfer,
                    .access = vk::AccessFlagBits2::eTransferWrite,
                },
                .acquire = nr::rhi::ops::QueueOwnershipRequest{
                    .srcQueueFamilyIndex = transferQueueFamily,
                    .dstQueueFamilyIndex = transferQueueFamily,
                    .stages = vk::PipelineStageFlagBits2::eFragmentShader,
                    .access = vk::AccessFlagBits2::eShaderRead,
                },
            },
        };
    }

    auto ownershipBundle = nr::rhi::ops::makeUploadQueueOwnershipBundle(
        transferQueueFamily,
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
    return ownershipBundle.uploadPlan;
}

[[nodiscard]] std::optional<nr::rhi::ops::ImageUploadTicket> uploadUiTextureAsync(
    nr::rhi::Device& device,
    nr::rhi::Image& image,
    std::span<const std::byte> uploadBytes,
    vk::ImageLayout oldLayout)
{
    nr::nrAssert(image.valid(), "UiNode texture upload requires a valid destination image.");
    nr::nrAssert(!uploadBytes.empty(), "UiNode texture upload requires non-empty upload bytes.");

    auto& uploadContext = device.uploadReadback();
    nr::nrAssert(uploadContext.valid(), "UiNode requires a valid upload context for async texture uploads.");

    auto ownership = makeUiTextureUploadOwnershipPlan(device);
    auto ticket = uploadContext.uploadImage(
        uploadBytes,
        image,
        oldLayout,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        ownership);
    
    if (!ticket.valid())
    {
        nr::nrInfo("UiNode: async texture upload failed, ticket invalid.");
        return std::nullopt;
    }

    return ticket;
}

void waitForPendingUiTextureUploads(
    nr::rhi::Device& device,
    UiRuntimeCache& runtime)
{
    auto& uploadContext = device.uploadReadback();
    std::uint64_t maxSignalValue = 0u;
    
    std::ranges::for_each(runtime.textures, [&](auto& pair) {
        if (pair.second.pendingUpload.has_value() && pair.second.pendingUpload->valid())
        {
            maxSignalValue = std::max(maxSignalValue, pair.second.pendingUpload->signalValue);
        }
    });

    if (maxSignalValue == 0u)
    {
        return;
    }

    uploadContext.waitUploadComplete(maxSignalValue);
}

void recordUiTextureAcquireBarriers(
    nr::rhi::Device& device,
    UiRuntimeCache& runtime,
    vk::CommandBuffer commandBuffer)
{
    auto& uploadContext = device.uploadReadback();
    auto const transferQueueFamily = device.queueManager.transfer().queueFamilyIndex();
    auto const graphicsQueueFamily = device.queueManager.graphics().queueFamilyIndex();
    
    auto barriers = nr::rhi::ops::BarrierBatch{};
    
    if (transferQueueFamily == graphicsQueueFamily)
    {
        // Same queue family: no ownership transfer, but still need layout transition
        std::ranges::for_each(runtime.textures, [&](auto& pair) {
            if (!pair.second.pendingUpload.has_value() || !pair.second.pendingUpload->valid())
            {
                return;
            }
            
            barriers.add(nr::rhi::ops::makeImageTransferDstToShaderReadBarrier(
                pair.second.pendingUpload->image->get(),
                vk::PipelineStageFlagBits2::eFragmentShader));
            pair.second.pendingUpload.reset();
        });
    }
    else
    {
        // Cross-queue: record ownership acquire barriers (which also handle layout transition)
        std::ranges::for_each(runtime.textures, [&](auto& pair) {
            if (!pair.second.pendingUpload.has_value() || !pair.second.pendingUpload->valid())
            {
                return;
            }
            
            barriers.add(uploadContext.makeImageAcquireBarrier(pair.second.pendingUpload.value()));
            pair.second.pendingUpload.reset();
        });
    }

    if (!barriers.empty())
    {
        nr::rhi::ops::pipelineBarrier(commandBuffer, barriers);
    }
}

void createOrUpdateUiTexture(
    nr::rhi::Device& device,
    UiRuntimeCache& runtime,
    ImTextureData& textureData)
{
    auto const textureId = makeManagedTextureId(textureData);
    auto const textureSlot = acquireUiTextureSlot(runtime, textureId);
    auto const textureExtent = makeTextureExtent(textureData);
    auto uploadBytes = makeTextureUploadBytes(textureData);

    auto existingTexture = runtime.textures.find(textureId);
    auto needsCreate = existingTexture == runtime.textures.end();
    if (!needsCreate)
    {
        auto const currentExtent = existingTexture->second.image.extent();
        needsCreate = currentExtent.width != textureExtent.width || currentExtent.height != textureExtent.height;
    }

    if (needsCreate)
    {
        auto newImage = createUiTextureImage(
            device,
            textureExtent,
            std::format("Ui.Texture[{}:{}]", textureData.UniqueID, textureSlot));
        
        auto ticket = uploadUiTextureAsync(
            device,
            newImage,
            std::span<const std::byte>{uploadBytes.data(), uploadBytes.size()},
            vk::ImageLayout::eUndefined);
        
        auto entry = UiTextureEntry{
            .image = std::move(newImage),
            .pendingUpload = ticket,
        };
        
        if (ticket.has_value())
        {
            entry.pendingUpload->image = std::cref(entry.image);
        }
        
        runtime.textures.insert_or_assign(textureId, std::move(entry));
        invalidateBindlessTextureTables(runtime);
    }
    else
    {
        auto ticket = uploadUiTextureAsync(
            device,
            existingTexture->second.image,
            std::span<const std::byte>{uploadBytes.data(), uploadBytes.size()},
            vk::ImageLayout::eShaderReadOnlyOptimal);
        existingTexture->second.pendingUpload = ticket;
    }

    textureData.SetTexID(textureId);
    textureData.SetStatus(ImTextureStatus_OK);
}

void destroyUiTexture(
    UiRuntimeCache& runtime,
    ImTextureData& textureData,
    std::uint64_t currentFrameIndex)
{
    auto textureId = textureData.GetTexID();
    if (textureId == kInvalidTextureId)
    {
        textureId = makeManagedTextureId(textureData);
    }

    auto textureIt = runtime.textures.find(textureId);
    if (textureIt != runtime.textures.end())
    {
        auto slotIt = runtime.textureSlotById.find(textureId);
        auto const slot = slotIt != runtime.textureSlotById.end() ? slotIt->second : 0u;
        
        runtime.retiredTextures.push_back(UiRetiredTexture{
            .image = std::move(textureIt->second.image),
            .slot = slot,
            .retiredFrameIndex = currentFrameIndex,
        });
        runtime.textures.erase(textureIt);
    }

    if (auto slotIt = runtime.textureSlotById.find(textureId); slotIt != runtime.textureSlotById.end())
    {
        auto const slot = slotIt->second;
        runtime.textureSlotById.erase(slotIt);
        if (slot < runtime.textureIdsBySlot.size())
        {
            runtime.textureIdsBySlot[slot] = kInvalidTextureId;
        }
        invalidateBindlessTextureTables(runtime);
    }
    textureData.BackendUserData = nullptr;
    textureData.SetTexID(kInvalidTextureId);
    textureData.SetStatus(ImTextureStatus_Destroyed);
}

void cleanupRetiredTextures(
    UiRuntimeCache& runtime,
    std::uint64_t currentFrameIndex)
{
    constexpr auto kRetirementFrameCount = static_cast<std::uint64_t>(nr::maxFrameInFlight + 1u);
    
    std::erase_if(runtime.retiredTextures, [&](const UiRetiredTexture& retired) {
        if (currentFrameIndex >= retired.retiredFrameIndex + kRetirementFrameCount)
        {
            if (retired.slot < runtime.freeTextureSlots.size() ||
                std::ranges::find(runtime.freeTextureSlots, retired.slot) == runtime.freeTextureSlots.end())
            {
                if (retired.slot < runtime.textureIdsBySlot.size() &&
                    runtime.textureIdsBySlot[retired.slot] == kInvalidTextureId)
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
        case ImTextureStatus_WantCreate:
        case ImTextureStatus_WantUpdates:
            createOrUpdateUiTexture(device, runtime, *textureData);
            break;
        case ImTextureStatus_WantDestroy:
            destroyUiTexture(runtime, *textureData, currentFrameIndex);
            break;
        default:
            break;
        }
    });
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

    auto bufferInfo = vk::BufferCreateInfo{};
    bufferInfo.size = capacity;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = vk::SharingMode::eExclusive;

    buffer = device.resourceFactory.createBuffer(
        bufferInfo,
        nr::rhi::MemoryUsage::CpuToGpu,
        debugName);
    nr::nrAssert(buffer.valid(), std::format("UiNode failed to create upload buffer '{}'.", debugName));
}

[[nodiscard]] std::optional<std::reference_wrapper<nr::app::UiSystem>> tryGetUiSystem(
    const nr::renderer::NodeFrameParameters& frameParameters)
{
    if (!frameParameters.frameServices.has_value())
    {
        return std::nullopt;
    }

    return frameParameters.frameServices->get().tryGet<nr::app::UiSystem>();
}

[[nodiscard]] std::vector<nr::rhi::ShaderBindingSet>& ensureBindlessTextureBindingSetsForFrame(
    UiRuntimeCache& runtime,
    std::size_t frameSlot)
{
    auto const requiredCapacity = std::max(runtime.textureDescriptorCapacity, 1u);
    auto& bindingSets = runtime.bindlessBindingSetsByFrame[frameSlot];
    if (!bindingSets.empty() && runtime.bindlessDescriptorCapacityByFrame[frameSlot] >= requiredCapacity)
    {
        return bindingSets;
    }

    auto root = runtime.pipeline.descriptorLayout.rootCursor();
    nr::nrAssert(root.valid(), "UiNode requires a valid root shader cursor for bindless descriptor allocation.");
    auto textureCursor = root["gUiTextures"];
    auto textureBinding = textureCursor.descriptorBinding();
    nr::nrAssert(
        textureBinding.has_value() && textureBinding->supportsVariableDescriptorCount(),
        "UiNode requires gUiTextures to be a runtime-sized descriptor array.");
    nr::nrAssert(
        requiredCapacity <= runtime.maxTextureDescriptorCount,
        std::format(
            "UiNode requested bindless descriptor capacity {} beyond pipeline maximum {}.",
            requiredCapacity,
            runtime.maxTextureDescriptorCount));

    auto newSets = nr::rhi::allocateBindingSetsForLayout(
        runtime.pipeline.layout,
        runtime.pipeline.bindingPool,
        {{textureBinding->set, requiredCapacity}});
    nr::nrAssert(
        !newSets.empty(),
        std::format(
            "UiNode failed to allocate bindless descriptor sets in frame slot {} with capacity {}.",
            frameSlot,
            requiredCapacity));
    bindingSets = std::move(newSets);
    runtime.bindlessDescriptorCapacityByFrame[frameSlot] = requiredCapacity;
    runtime.bindlessDescriptorsInitializedByFrame[frameSlot] = false;
    return bindingSets;
}

void updateBindlessTextureBindingsForFrame(
    UiRuntimeCache& runtime,
    std::size_t frameSlot)
{
    auto& bindingSets = ensureBindlessTextureBindingSetsForFrame(runtime, frameSlot);
    if (runtime.bindlessDescriptorsInitializedByFrame[frameSlot])
    {
        return;
    }

    auto root = runtime.pipeline.descriptorLayout.rootCursor();
    nr::nrAssert(root.valid(), "UiNode requires a valid root shader cursor for descriptor updates.");

    auto texturesCursor = root["gUiTextures"];
    auto samplerCursor = root["gUiSampler"];
    nr::nrAssert(texturesCursor.valid(), "UiNode requires gUiTextures cursor for descriptor updates.");
    nr::nrAssert(samplerCursor.valid(), "UiNode requires gUiSampler cursor for descriptor updates.");

    auto samplerBindOk = samplerCursor.setObject(runtime.textureSampler.raw());
    nr::nrAssert(samplerBindOk, "UiNode failed to write gUiSampler descriptor.");

    auto slotRange = std::views::iota(std::size_t{0}, runtime.textureIdsBySlot.size());
    std::ranges::for_each(slotRange, [&](std::size_t slot) {
        auto const textureId = runtime.textureIdsBySlot[slot];
        if (textureId == kInvalidTextureId)
        {
            return;
        }

        auto textureIt = runtime.textures.find(textureId);
        nr::nrAssert(
            textureIt != runtime.textures.end(),
            std::format(
                "UiNode bindless descriptor update could not resolve ImGui texture id {} for slot {}.",
                static_cast<unsigned long long>(textureId),
                slot));

        auto textureElementCursor = texturesCursor[slot];
        auto textureBinding = textureElementCursor.descriptorBinding();
        auto textureBindOk = textureElementCursor.setObject(textureIt->second.image, vk::ImageLayout::eShaderReadOnlyOptimal);
        nr::nrAssert(
            textureBindOk,
            std::format(
                "UiNode failed to write gUiTextures[{}] descriptor for ImGui texture id {}. cursorValid={}, kind={}, type='{}', bindingPresent={}, descriptorType={}, bindingArrayIndex={}, bindingRangeIndex={}.",
                slot,
                static_cast<unsigned long long>(textureId),
                textureElementCursor.valid(),
                static_cast<int>(textureElementCursor.kind()),
                textureElementCursor.typeName(),
                textureBinding.has_value(),
                textureBinding.has_value() ? vk::to_string(textureBinding->descriptorType) : std::string("<none>"),
                textureElementCursor.address().bindingArrayIndex,
                textureElementCursor.address().bindingRangeIndex));
    });

    auto bindingSnapshot = root.snapshot();
    root.clearSnapshot();

    auto writeRequests = nr::rhi::resolveDescriptorWriteRequests(bindingSnapshot, {});
    if (writeRequests.empty())
    {
        return;
    }

    auto requestsBySet = std::map<uint32_t, std::vector<nr::rhi::DescriptorWriteRequest>>{};
    std::ranges::for_each(writeRequests, [&](const nr::rhi::DescriptorWriteRequest& request) {
        requestsBySet[request.binding.set].push_back(request);
    });

    std::ranges::for_each(bindingSets, [&](const nr::rhi::ShaderBindingSet& bindingSet) {
        auto it = requestsBySet.find(bindingSet.setIndex());
        if (it == requestsBySet.end())
        {
            return;
        }

        runtime.pipeline.bindingPool.update(bindingSet, std::span<const nr::rhi::DescriptorWriteRequest>{it->second.data(), it->second.size()});
        requestsBySet.erase(it);
    });

    nr::nrAssert(
        requestsBySet.empty(),
        std::format(
            "UiNode could not resolve bindless descriptor set writes in frame slot {}.",
            frameSlot));

    runtime.bindlessDescriptorsInitializedByFrame[frameSlot] = true;
}

void pushUiConstantsToCommandBuffer(
    const vk::raii::CommandBuffer& commandBuffer,
    const nr::rhi::PipelineState<nr::rhi::GraphicsPipeline>& pipeline,
    const UiPushConstants& pushConstants)
{
    auto root = pipeline.descriptorLayout.rootCursor();
    nr::nrAssert(root.valid(), "UiNode requires a valid root shader cursor for push constants.");

    auto pushCursor = root["gUiPush"];
    nr::nrAssert(pushCursor.valid(), "UiNode requires gUiPush cursor for push constants.");

    auto pushBindOk = pushCursor.setData(pushConstants);
    nr::nrAssert(pushBindOk, "UiNode failed to write gUiPush.");

    auto bindingSnapshot = root.snapshot();
    root.clearSnapshot();
    nr::rhi::pushConstantsToCommandBuffer(commandBuffer, pipeline.layout, bindingSnapshot);
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
                "UiNode vertex offset exceeds int32_t range.");

            auto scissor = vk::Rect2D{};
            scissor.offset = vk::Offset2D{offsetX, offsetY};
            scissor.extent = vk::Extent2D{extentWidth, extentHeight};

            output.commands.push_back(UiDrawCommand{
                .scissor = scissor,
                .elementCount = static_cast<std::uint32_t>(command.ElemCount),
                .firstIndex = firstIndex,
                .vertexOffset = static_cast<std::int32_t>(vertexOffset),
                .textureId = command.GetTexID(),
            });
        });

        globalVertexOffset += static_cast<std::uint32_t>(commandList->VtxBuffer.Size);
        globalIndexOffset += static_cast<std::uint32_t>(commandList->IdxBuffer.Size);
    });

    return output;
}
} // namespace

export namespace nr::renderPasses
{
struct UiNodeInput
{
    vk::Format bufferFormat = vk::Format::eR8G8B8A8Unorm;
};

struct UiNodeOutput
{
    nr::renderer::GraphResourceHandle uiBuffer{};
};

class UiNode final : public Node
{
  public:
    UiNodeInput input{};
    UiNodeOutput output{};

    [[nodiscard]] NodeDescription describe() const override
    {
        return NodeDescription{
            .name = "Ui",
            .outputPorts = {
                NodePort{.name = "uiBuffer"},
            },
        };
    }

    void initialize(NodeInitContext& context) override
    {
        device_ = context.device;

        auto bufferFormat = input.bufferFormat == vk::Format::eUndefined
                                ? vk::Format::eR8G8B8A8Unorm
                                : input.bufferFormat;

        runtime_ = ensureUiRuntime(context.device.get(), bufferFormat);
    }

    void build(NodeBuildContext& context, const NodeFrameParameters& frameParameters) override
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

        auto uiBuffer = context.addResource(nr::renderer::GraphTransientImageDesc{
            .debugName = "Ui.Buffer",
            .lifetime = nr::renderer::ResourceLifetime::GraphTransient,
            .extent = vk::Extent3D{bufferExtent.width, bufferExtent.height, 1u},
            .format = bufferFormat,
            .usageIntents = {
                nr::renderer::ImageUsageIntent::ColorAttachment,
                nr::renderer::ImageUsageIntent::Sampled,
            },
            .initialLayout = nr::renderer::ImageLayoutIntent::ColorAttachment,
            .aspect = nr::renderer::ImageAspectIntent::Color,
        });

        output.uiBuffer = uiBuffer;
        context.publishOutput("uiBuffer", uiBuffer);

        auto uiSystem = tryGetUiSystem(frameParameters);
        auto drawFrame = UiFrameDrawData{};
        drawFrame.framebufferExtent = bufferExtent;
        auto const currentFrameIndex = static_cast<std::uint64_t>(frameParameters.frameIndex);
        if (uiSystem.has_value())
        {
            uiSystem->get().finalizeFrame();
            auto drawData = uiSystem->get().drawData();
            if (drawData.has_value())
            {
                synchronizeUiTextures(device_->get(), *runtime_, drawData->get(), currentFrameIndex);
                waitForPendingUiTextureUploads(device_->get(), *runtime_);
                drawFrame = copyUiDrawData(drawData->get(), bufferExtent);
            }
        }

        auto passIntents = std::array{
            nr::renderer::PassResourceUseDesc{
                .resource = uiBuffer,
                .imageUsage = nr::renderer::ImageUsageIntent::ColorAttachment,
                .imageAccess = nr::renderer::ImageAccessIntent::ColorAttachmentWrite,
                .imageLayout = nr::renderer::ImageLayoutIntent::ColorAttachment,
                .imageAspect = nr::renderer::ImageAspectIntent::Color,
                .ownershipDomain = nr::renderer::ResourceOwnershipDomain::Undefined,
                .readOnly = false,
            },
        };

        auto runtime = runtime_;
        [[maybe_unused]] auto overlayPassHandle = context.addPass(
            std::span<const nr::renderer::PassResourceUseDesc>{passIntents.data(), passIntents.size()},
            "Ui.Overlay",
            [runtime,
             uiBuffer,
             drawFrame](const nr::renderer::PassRecordContext& recordContext) {
                nr::nrAssert(recordContext.commandBuffer.has_value(), "UiNode record stage requires RAII command buffer access.");
                nr::nrAssert(static_cast<bool>(recordContext.resolveImage), "UiNode record stage requires image resolver callback.");
                nr::nrAssert(static_cast<bool>(runtime), "UiNode record stage requires initialized runtime state.");
                nr::nrAssert(recordContext.device.has_value(), "UiNode record stage requires device reference.");

                auto uiBufferImage = recordContext.resolveImage(uiBuffer);
                nr::nrAssert(uiBufferImage.has_value(), "UiNode failed to resolve uiBuffer image.");
                nr::nrAssert(uiBufferImage->view != vk::ImageView{}, "UiNode requires a valid uiBuffer image view.");

                auto targetExtent = vk::Extent2D{
                    std::max(1u, std::min(drawFrame.framebufferExtent.width, uiBufferImage->extent.width)),
                    std::max(1u, std::min(drawFrame.framebufferExtent.height, uiBufferImage->extent.height)),
                };

                auto colorAttachment = nr::rhi::ops::RenderingAttachmentDesc{
                    .imageView = uiBufferImage->view,
                    .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
                    .resolveMode = vk::ResolveModeFlagBits::eNone,
                    .resolveImageView = {},
                    .resolveImageLayout = vk::ImageLayout::eUndefined,
                    .loadOp = vk::AttachmentLoadOp::eClear,
                    .storeOp = vk::AttachmentStoreOp::eStore,
                    .clearValue = vk::ClearValue{vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}}},
                };

                auto colorAttachments = std::array{colorAttachment};
                auto renderingScope = nr::rhi::ops::RenderingScopeDesc{
                    .renderArea = vk::Rect2D{vk::Offset2D{0, 0}, targetExtent},
                    .layerCount = 1u,
                    .viewMask = 0u,
                    .flags = {},
                    .colorAttachments = colorAttachments,
                    .depthAttachment = std::nullopt,
                    .stencilAttachment = std::nullopt,
                };

                auto& commandBuffer = recordContext.commandBuffer->get();
                
                // Record layout transitions for any textures with pending uploads before rendering
                recordUiTextureAcquireBarriers(recordContext.device->get(), *runtime, *commandBuffer);
                
                auto scopedRendering = nr::rhi::ops::ScopedRendering(commandBuffer, renderingScope);

                if (drawFrame.commands.empty())
                {
                    return;
                }

                commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, runtime->pipeline.pipeline.raw());

                auto const frameSlot = static_cast<std::size_t>(recordContext.frameIndex % runtime->bindlessBindingSetsByFrame.size());

                auto viewport = vk::Viewport{
                    0.0f,
                    0.0f,
                    static_cast<float>(targetExtent.width),
                    static_cast<float>(targetExtent.height),
                    0.0f,
                    1.0f,
                };
                commandBuffer.setViewport(0u, {viewport});
                commandBuffer.setPrimitiveTopology(vk::PrimitiveTopology::eTriangleList);

                auto rasterState = nr::rhi::MeshRasterState{
                    .cullMode = vk::CullModeFlagBits::eNone,
                    .frontFace = vk::FrontFace::eCounterClockwise,
                    .depthTestEnable = vk::False,
                    .depthWriteEnable = vk::False,
                    .depthCompareOp = vk::CompareOp::eAlways,
                    .polygonMode = vk::PolygonMode::eFill,
                    .rasterizationSamples = vk::SampleCountFlagBits::e1,
                };
                nr::rhi::mesh::applyRasterState(commandBuffer, rasterState);

                auto const& vertexBuffer = runtime->vertexBuffers[frameSlot];
                auto const& indexBuffer = runtime->indexBuffers[frameSlot];
                nr::nrAssert(vertexBuffer.valid(), "UiNode requires a valid per-frame vertex buffer.");
                nr::nrAssert(indexBuffer.valid(), "UiNode requires a valid per-frame index buffer.");

                auto vertexBuffers = std::array{vertexBuffer.handle()};
                auto vertexOffsets = std::array<vk::DeviceSize, 1>{0u};
                commandBuffer.bindVertexBuffers(0u, vertexBuffers, vertexOffsets);
                commandBuffer.bindIndexBuffer(
                    indexBuffer.handle(),
                    0u,
                    sizeof(ImDrawIdx) == 2u ? vk::IndexType::eUint16 : vk::IndexType::eUint32);

                updateBindlessTextureBindingsForFrame(*runtime, frameSlot);
                auto& bindingSets = runtime->bindlessBindingSetsByFrame[frameSlot];
                nr::nrAssert(!bindingSets.empty(), "UiNode requires initialized bindless descriptor sets for the active frame slot.");
                runtime->pipeline.layout.bindDescriptorSets(
                    commandBuffer,
                    vk::PipelineBindPoint::eGraphics,
                    std::span<const nr::rhi::ShaderBindingSet>{bindingSets.data(), bindingSets.size()},
                    {});

                auto drawPushConstants = drawFrame.pushConstants;
                std::optional<std::uint32_t> lastTextureIndex{};
                std::ranges::for_each(drawFrame.commands, [&](const UiDrawCommand& command) {
                    if (command.elementCount == 0u || command.scissor.extent.width == 0u || command.scissor.extent.height == 0u)
                    {
                        return;
                    }

                    auto bindingSetsIt = runtime->textureSlotById.find(command.textureId);
                    nr::nrAssert(
                        bindingSetsIt != runtime->textureSlotById.end(),
                        std::format(
                            "UiNode record stage could not resolve bindless slot for ImGui texture id {}.",
                            static_cast<unsigned long long>(command.textureId)));

                    auto const textureIndex = bindingSetsIt->second;
                    if (!lastTextureIndex.has_value() || *lastTextureIndex != textureIndex)
                    {
                        drawPushConstants.textureIndex = textureIndex;
                        pushUiConstantsToCommandBuffer(commandBuffer, runtime->pipeline, drawPushConstants);
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
            },
            [runtime,
             drawFrame](const nr::renderer::PassPrepareContext& prepareContext) {
                nr::nrAssert(prepareContext.device.has_value(), "UiNode prepare stage requires device access.");
                nr::nrAssert(static_cast<bool>(runtime), "UiNode prepare stage requires initialized runtime state.");

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
                    vertexBuffer.write(std::span<const ImDrawVert>{drawFrame.vertices.data(), drawFrame.vertices.size()});
                    vertexBuffer.flush(0u, vertexBytes);
                }

                if (indexBytes > 0u)
                {
                    auto& indexBuffer = runtime->indexBuffers[frameSlot];
                    indexBuffer.write(std::span<const ImDrawIdx>{drawFrame.indices.data(), drawFrame.indices.size()});
                    indexBuffer.flush(0u, indexBytes);
                }
            });
    }

    void shutdown(NodeShutdownContext&) override
    {
        if (runtime_)
        {
            std::ranges::for_each(runtime_->bindlessBindingSetsByFrame, [](std::vector<nr::rhi::ShaderBindingSet>& bindingSets) {
                bindingSets.clear();
            });
        }

        runtime_.reset();
        device_.reset();
    }

  private:
    std::shared_ptr<UiRuntimeCache> runtime_{};
    std::optional<std::reference_wrapper<nr::rhi::Device>> device_{};
};
} // namespace nr::renderPasses
