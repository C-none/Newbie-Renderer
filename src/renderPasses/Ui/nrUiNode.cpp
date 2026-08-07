module;
#include <cstddef>

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

static_assert(std::is_standard_layout_v<UiPushConstants>);
static_assert(sizeof(UiPushConstants) == 32u);
static_assert(offsetof(UiPushConstants, scale) == 0u);
static_assert(offsetof(UiPushConstants, translate) == 8u);
static_assert(offsetof(UiPushConstants, textureIndex) == 16u);
static_assert(offsetof(UiPushConstants, padding) == 20u);
static_assert(sizeof(UiPushConstants) <= nr::rhi::kMaxPushConstantBytes,
              "UiNode push constants exceed the RHI maximum.");

static_assert(std::is_standard_layout_v<ImDrawVert>);
static_assert(sizeof(ImDrawVert) == 20u);
static_assert(imgui::drawVertPosOffset == 0u);
static_assert(imgui::drawVertUvOffset == 8u);
static_assert(imgui::drawVertColorOffset == 16u);
static_assert(offsetof(ImDrawVert, pos) == 0u);
static_assert(offsetof(ImDrawVert, uv) == 8u);
static_assert(offsetof(ImDrawVert, col) == 16u);

static_assert(std::is_unsigned_v<ImDrawIdx>);
static_assert(sizeof(ImDrawIdx) == 2u || sizeof(ImDrawIdx) == 4u);

inline constexpr vk::IndexType kUiIndexType =
    sizeof(ImDrawIdx) == 2u ? vk::IndexType::eUint16 : vk::IndexType::eUint32;
inline constexpr std::uint32_t kUiTextureDescriptorCapacity = 1024u;
inline constexpr std::size_t kUiRgbaBytesPerPixel = 4u;

[[nodiscard]] constexpr vk::Format resolveUiBufferFormat(const UiNodeInput &input) noexcept
{
    return input.bufferFormat == vk::Format::eUndefined ? vk::Format::eR8G8B8A8Unorm : input.bufferFormat;
}

/// Initial vertex buffer capacity per frame slot (64 KiB = ~1.5k ImDrawVert)
/// Pre-allocating avoids per-frame vkAllocateMemory calls in typical UI scenarios.
inline constexpr vk::DeviceSize kInitialUiVertexBufferCapacity = 64u * 1024u;

/// Initial index buffer capacity per frame slot (32 KiB = ~10k ImDrawIdx)
inline constexpr vk::DeviceSize kInitialUiIndexBufferCapacity = 32u * 1024u;

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

using UiDrawFramePayload = std::shared_ptr<const UiFrameDrawData>;

struct UiTextureEntry
{
    nr::rhi::Image image{};
    nr::renderer::RetainedImageState state{};
};

struct UiRetiredTexture
{
    nr::rhi::Image image{};
    std::uint32_t slot = 0u;
    std::bitset<nr::maxFrameInFlight> pendingFrameSlots{};
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
    vk::Format frozenBufferFormat = vk::Format::eUndefined;
    vk::Extent2D allocatedUiExtent{0, 0};
    std::vector<UiTextureEntry> texturesBySlot{};
    std::map<std::uint64_t, std::uint32_t> textureSlotByKey{};
    std::vector<std::uint32_t> freeTextureSlots{};
    std::vector<UiRetiredTexture> retiredTextures{};
};

[[nodiscard]] vk::Format validatedFrozenUiBufferFormat(const UiNodeInput &input, const UiRuntimeCache &runtime)
{
    auto const resolvedFormat = resolveUiBufferFormat(input);
    nr::nrAssert(
        resolvedFormat == runtime.frozenBufferFormat, "UiNode buffer format cannot change after initialization. frozen={} requested={}",
                    vk::to_string(runtime.frozenBufferFormat), vk::to_string(resolvedFormat));
    return runtime.frozenBufferFormat;
}

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
    blendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                     vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
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

void ensureUiBufferImage(nr::rhi::Device &device, UiRuntimeCache &runtime, vk::Extent2D extent)
{
    auto const needsRealloc =
        runtime.allocatedUiExtent != extent ||
        std::ranges::any_of(runtime.uiBuffers, [](const nr::rhi::Image &image) { return !image.valid(); });

    if (!needsRealloc)
    {
        return;
    }

    auto imageInfo = nr::rhi::makeImageCreateInfo(runtime.frozenBufferFormat, extent,
                                                  vk::ImageUsageFlagBits::eColorAttachment |
                                                      vk::ImageUsageFlagBits::eSampled);

    auto frameSlots = std::views::iota(std::size_t{0}, runtime.uiBuffers.size());
    std::ranges::for_each(frameSlots, [&](std::size_t frameSlot) {
        runtime.uiBuffers[frameSlot] = device.resourceFactory.createImage(imageInfo, nr::rhi::MemoryUsage::GpuOnly,
                                                                          std::format("Ui.Buffer[{}]", frameSlot));
        nr::nrAssert(runtime.uiBuffers[frameSlot].valid(), "UiNode failed to allocate Ui.Buffer image.");
    });

    runtime.allocatedUiExtent = extent;
}

[[nodiscard]] std::shared_ptr<UiRuntimeCache> ensureUiRuntime(nr::rhi::Device &device,
                                                              std::span<const nr::rhi::SlangProgram> programs,
                                                              vk::Format frozenBufferFormat, std::string debugName)
{
    auto runtime = std::make_shared<UiRuntimeCache>();
    runtime->frozenBufferFormat = frozenBufferFormat;

    auto pipelineDesc = nr::rhi::GraphicsPipelineDesc{};
    pipelineDesc.colorAttachmentFormats = {runtime->frozenBufferFormat};
    pipelineDesc.cullMode = vk::CullModeFlagBits::eNone;
    pipelineDesc.vertexBindings = makeUiVertexBindings();
    pipelineDesc.vertexAttributes = makeUiVertexAttributes();
    pipelineDesc.colorBlendAttachments = {makeUiBlendAttachment()};
    pipelineDesc.descriptorBindingPolicy.defaultRuntimeDescriptorCount = kUiTextureDescriptorCapacity;

    runtime->pipeline = std::make_shared<nr::renderer::PipelineRuntime<nr::rhi::GraphicsPipeline>>();
    runtime->pipeline->initializeDeferred(
        device.pipeline().createGraphicsPipeline(programs, pipelineDesc, 64u, {}, std::move(debugName)));

    runtime->textureSampler = device.pipeline().createSampler(
        nr::rhi::SlangSamplerDesc{
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
        auto vertexBufferInfo =
            nr::rhi::makeBufferCreateInfo(kInitialUiVertexBufferCapacity, vk::BufferUsageFlagBits::eVertexBuffer);
        runtime->vertexBuffers[frameSlot] = device.resourceFactory.createBuffer(
            vertexBufferInfo, nr::rhi::MemoryUsage::CpuToGpu, std::format("Ui.VertexBuffer[{}]", frameSlot));

        auto indexBufferInfo =
            nr::rhi::makeBufferCreateInfo(kInitialUiIndexBufferCapacity, vk::BufferUsageFlagBits::eIndexBuffer);
        runtime->indexBuffers[frameSlot] = device.resourceFactory.createBuffer(
            indexBufferInfo, nr::rhi::MemoryUsage::CpuToGpu, std::format("Ui.IndexBuffer[{}]", frameSlot));
    });

    return runtime;
}

[[nodiscard]] std::uint64_t makeManagedTextureKey(const ImTextureData &textureData) noexcept
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
    nr::nrAssert(slot <= static_cast<std::uintptr_t>(std::numeric_limits<std::uint32_t>::max()), "UiNode texture id {} exceeds texture slot range.", slot);
    return static_cast<std::uint32_t>(slot);
}

void markBindlessTextureTableDirty(UiRuntimeCache &runtime) noexcept
{
    ++runtime.textureTableRevision;
    if (runtime.textureTableRevision == 0u)
    {
        runtime.textureTableRevision = 1u;
    }
}

[[nodiscard]] void *uiTextureBackendMarker(UiRuntimeCache &runtime) noexcept
{
    return std::addressof(runtime);
}

[[nodiscard]] const void *uiTextureBackendMarker(const UiRuntimeCache &runtime) noexcept
{
    return std::addressof(runtime);
}

[[nodiscard]] bool textureOwnedByRuntime(const UiRuntimeCache &runtime, const ImTextureData &textureData) noexcept
{
    return static_cast<const void *>(textureData.BackendUserData) == uiTextureBackendMarker(runtime);
}

[[nodiscard]] std::uint32_t acquireUiTextureSlot(UiRuntimeCache &runtime, std::uint64_t textureKey)
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
        nr::nrAssert(slot < kUiTextureDescriptorCapacity, "UiNode texture slot allocation exceeded fixed descriptor capacity {}.",
                                 kUiTextureDescriptorCapacity);
        runtime.texturesBySlot.emplace_back();
    }

    nr::nrAssert(slot < runtime.texturesBySlot.size(), "UiNode acquired texture slot outside texture table.");
    nr::nrAssert(slot < kUiTextureDescriptorCapacity, "UiNode bindless texture slot {} exceeds fixed descriptor capacity {}.", slot,
                             kUiTextureDescriptorCapacity);

    runtime.textureSlotByKey.insert_or_assign(textureKey, slot);
    return slot;
}

struct UiTexturePayloadLayout
{
    vk::Extent2D extent{};
    std::size_t pixelCount = 0u;
    std::size_t rgbaByteCount = 0u;
};

struct UiTextureUploadPayload
{
    vk::Extent2D extent{};
    std::vector<std::byte> bytes{};
};

[[nodiscard]] UiTexturePayloadLayout checkedUiTexturePayloadLayout(const ImTextureData &textureData)
{
    nr::nrAssert(textureData.Width > 0, "UiNode requires textures with positive width.");
    nr::nrAssert(textureData.Height > 0, "UiNode requires textures with positive height.");
    nr::nrAssert(std::in_range<std::size_t>(textureData.Width),
                 "UiNode texture width cannot be represented as a host byte count.");
    nr::nrAssert(std::in_range<std::size_t>(textureData.Height),
                 "UiNode texture height cannot be represented as a host byte count.");

    auto const textureWidth = static_cast<std::size_t>(textureData.Width);
    auto const textureHeight = static_cast<std::size_t>(textureData.Height);
    auto constexpr maxByteCount = std::numeric_limits<std::size_t>::max();
    nr::nrAssert(textureHeight <= maxByteCount / textureWidth, "UiNode texture extent {}x{} overflows its pixel count.", textureWidth, textureHeight);
    auto const pixelCount = textureWidth * textureHeight;
    nr::nrAssert(pixelCount <= maxByteCount / kUiRgbaBytesPerPixel, "UiNode texture extent {}x{} overflows its RGBA upload byte count.", textureWidth,
                             textureHeight);
    nr::nrAssert(std::in_range<std::uint32_t>(textureWidth),
                 "UiNode texture width exceeds the Vulkan image extent range.");
    nr::nrAssert(std::in_range<std::uint32_t>(textureHeight),
                 "UiNode texture height exceeds the Vulkan image extent range.");

    return UiTexturePayloadLayout{
        .extent =
            vk::Extent2D{
                static_cast<std::uint32_t>(textureWidth),
                static_cast<std::uint32_t>(textureHeight),
            },
        .pixelCount = pixelCount,
        .rgbaByteCount = pixelCount * kUiRgbaBytesPerPixel,
    };
}

[[nodiscard]] UiTextureUploadPayload makeTextureUploadPayload(ImTextureData &textureData)
{
    auto const payloadLayout = checkedUiTexturePayloadLayout(textureData);
    nr::nrAssert(textureData.Format == ImTextureFormat_RGBA32 || textureData.Format == ImTextureFormat_Alpha8, "UiNode encountered unsupported ImGui texture format {}.",
                             static_cast<int>(textureData.Format));

    auto const rgbaSource = textureData.Format == ImTextureFormat_RGBA32;
    auto const expectedBytesPerPixel = rgbaSource ? static_cast<int>(kUiRgbaBytesPerPixel) : 1;
    auto const expectedSourceByteCount = rgbaSource ? payloadLayout.rgbaByteCount : payloadLayout.pixelCount;
    nr::nrAssert(textureData.BytesPerPixel == expectedBytesPerPixel, "UiNode ImGui texture format requires {} bytes per pixel, but the payload reports {}.",
                             expectedBytesPerPixel, textureData.BytesPerPixel);
    nr::nrAssert(expectedSourceByteCount <= static_cast<std::size_t>(std::numeric_limits<int>::max()), "UiNode ImGui texture payload requires {} source bytes, exceeding its API byte-count range.",
                             expectedSourceByteCount);

    auto const reportedSourceByteCount = textureData.GetSizeInBytes();
    nr::nrAssert(reportedSourceByteCount >= 0 &&
                     static_cast<std::size_t>(reportedSourceByteCount) == expectedSourceByteCount, "UiNode ImGui texture payload reports {} bytes; expected exactly {}.",
                             reportedSourceByteCount, expectedSourceByteCount);
    nr::nrAssert(textureData.Pixels != nullptr, "UiNode requires CPU-visible ImGui texture pixels.");
    auto const *sourcePixels = static_cast<const unsigned char *>(textureData.GetPixels());
    nr::nrAssert(sourcePixels != nullptr, "UiNode requires non-empty ImGui texture pixel storage.");

    if (rgbaSource)
    {
        auto const *sourceFirst = reinterpret_cast<const std::byte *>(sourcePixels);
        return UiTextureUploadPayload{
            .extent = payloadLayout.extent,
            .bytes = std::vector<std::byte>{sourceFirst, sourceFirst + payloadLayout.rgbaByteCount},
        };
    }

    auto uploadBytes = std::vector<std::byte>{};
    uploadBytes.resize(payloadLayout.rgbaByteCount);

    auto destinationIndex = std::size_t{0u};
    auto sourceIndexRange = std::views::iota(std::size_t{0u}, payloadLayout.pixelCount);
    std::ranges::for_each(sourceIndexRange, [&](std::size_t sourceIndex) {
        uploadBytes[destinationIndex + 0u] = static_cast<std::byte>(0xff);
        uploadBytes[destinationIndex + 1u] = static_cast<std::byte>(0xff);
        uploadBytes[destinationIndex + 2u] = static_cast<std::byte>(0xff);
        uploadBytes[destinationIndex + 3u] = static_cast<std::byte>(sourcePixels[sourceIndex]);
        destinationIndex += kUiRgbaBytesPerPixel;
    });

    return UiTextureUploadPayload{
        .extent = payloadLayout.extent,
        .bytes = std::move(uploadBytes),
    };
}

[[nodiscard]] nr::rhi::ops::BufferUploadOwnershipPlan makeUiTextureUploadPlan(const nr::rhi::Device &device)
{
    auto const transferQueueFamily = device.queueManager.transfer().queueFamilyIndex();
    auto const graphicsQueueFamily = device.queueManager.graphics().queueFamilyIndex();
    nr::nrAssert(transferQueueFamily != graphicsQueueFamily,
                 "UiNode texture upload requires distinct transfer and graphics queue families.");

    auto plan = nr::rhi::ops::BufferUploadOwnershipPlan{};
    plan.releaseToDestination =
        nr::rhi::ops::makeQueueOwnershipTransfer(transferQueueFamily, graphicsQueueFamily,
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

void submitAndWaitUiTextureUploadSync(nr::rhi::Device &device, nr::rhi::ops::UploadReadbackContext &uploadContext,
                                      const nr::rhi::ops::ImageUploadTicket &uploadTicket)
{
    nr::nrAssert(uploadTicket.valid(), "UiNode texture upload synchronization requires a valid upload ticket.");

    auto syncPool = nr::rhi::CommandPool{
        device.device,
        device.queueManager.graphics().queueFamilyIndex(),
        vk::CommandPoolCreateFlagBits::eTransient,
    };
    auto syncCommandBuffers = syncPool.allocatePrimary(1);
    auto &syncCommandBuffer = syncCommandBuffers.front();

    nr::rhi::CommandRecorder::beginPrimary(syncCommandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    uploadContext.recordImageAcquireBarrier(syncCommandBuffer, uploadTicket);
    nr::rhi::CommandRecorder::end(syncCommandBuffer);

    auto syncSubmission = nr::rhi::CommandBatch{};
    syncSubmission.addWait(uploadContext.uploadTimelineSemaphore(), vk::PipelineStageFlagBits2::eAllCommands,
                           uploadTicket.signalValue);
    syncSubmission.addCommandBuffer(syncCommandBuffer);

    auto syncFence = vk::raii::Fence{device.device, vk::FenceCreateInfo{}};
    device.queueManager.graphics().submit(std::move(syncSubmission), std::cref(syncFence));

    auto const waitResult =
        device.device.waitForFences(*syncFence, vk::True, std::numeric_limits<std::uint64_t>::max());
    nr::nrAssert(waitResult == vk::Result::eSuccess,
                 "UiNode failed waiting for texture upload graphics synchronization.");
    uploadContext.reclaimCompletedUploads();
}

void uploadUiTextureThroughRing(nr::rhi::Device &device, const nr::rhi::Image &image,
                                std::span<const std::byte> uploadBytes)
{
    auto &uploadContext = device.uploadReadback();
    auto uploadTicket =
        uploadContext.uploadImage(uploadBytes, image, vk::ImageLayout::eUndefined,
                                  vk::ImageLayout::eShaderReadOnlyOptimal, makeUiTextureUploadPlan(device));
    submitAndWaitUiTextureUploadSync(device, uploadContext, uploadTicket);
}

void retireUiTexture(UiRuntimeCache &runtime, nr::rhi::Image image, std::uint32_t slot,
                     std::size_t currentFrameSlot, bool releaseSlot)
{
    nr::nrAssert(image.valid(), "UiNode can retire only a valid texture image.");
    nr::nrAssert(currentFrameSlot < nr::maxFrameInFlight, "UiNode texture retirement requires a valid frame slot.");

    auto pendingFrameSlots = std::bitset<nr::maxFrameInFlight>{};
    pendingFrameSlots.set();
    pendingFrameSlots.reset(currentFrameSlot);
    runtime.retiredTextures.push_back(UiRetiredTexture{
        .image = std::move(image),
        .slot = slot,
        .pendingFrameSlots = pendingFrameSlots,
        .releaseSlot = releaseSlot,
    });
}

void createOrUpdateUiTexture(nr::rhi::Device &device, UiRuntimeCache &runtime, ImTextureData &textureData,
                             std::size_t currentFrameSlot)
{
    auto const textureKey = makeManagedTextureKey(textureData);
    auto const textureSlot = acquireUiTextureSlot(runtime, textureKey);
    auto uploadPayload = makeTextureUploadPayload(textureData);

    auto &textureEntry = runtime.texturesBySlot[textureSlot];
    auto needsReplacement = !textureEntry.image.valid() || textureData.Status == ImTextureStatus_WantCreate ||
                            textureData.Status == ImTextureStatus_WantUpdates;
    if (!needsReplacement)
    {
        auto const currentExtent = textureEntry.image.extent();
        needsReplacement = currentExtent.width != uploadPayload.extent.width ||
                           currentExtent.height != uploadPayload.extent.height;
    }

    if (needsReplacement)
    {
        if (textureEntry.image.valid())
        {
            retireUiTexture(runtime, std::move(textureEntry.image), textureSlot, currentFrameSlot, false);
        }

        auto const debugName = std::format("Ui.Texture[{}:{}]", textureData.UniqueID, textureSlot);
        auto imageCreateInfo =
            nr::rhi::makeImageCreateInfo(vk::Format::eR8G8B8A8Unorm, uploadPayload.extent,
                                         vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled);
        auto textureImage =
            device.resourceFactory.createImage(imageCreateInfo, nr::rhi::MemoryUsage::GpuOnly, debugName);
        nr::nrAssert(textureImage.valid(), "UiNode failed to create texture image '{}'.", debugName);

        textureEntry = UiTextureEntry{
            .image = std::move(textureImage),
        };
        markBindlessTextureTableDirty(runtime);
    }

    uploadUiTextureThroughRing(device, textureEntry.image,
                               std::span<const std::byte>{uploadPayload.bytes.data(), uploadPayload.bytes.size()});
    textureEntry.state.common.initialized = true;
    textureEntry.state.layout = nr::renderer::ImageLayoutIntent::ShaderReadOnly;
    textureEntry.state.common.ownership = nr::renderer::ResourceOwnershipDomain::Graphics;
    textureEntry.state.common.access = nr::renderer::AccessScope{
        .stages = vk::PipelineStageFlagBits2::eFragmentShader,
        .access = vk::AccessFlagBits2::eShaderSampledRead,
    };

    textureData.BackendUserData = uiTextureBackendMarker(runtime);
    textureData.SetTexID(makeTextureIdFromSlot(textureSlot));
    textureData.SetStatus(ImTextureStatus_OK);
}

void destroyUiTexture(UiRuntimeCache &runtime, ImTextureData &textureData, std::size_t currentFrameSlot)
{
    auto const ownedByRuntime = textureOwnedByRuntime(runtime, textureData);
    auto const textureKey = makeManagedTextureKey(textureData);
    auto slotIt = runtime.textureSlotByKey.find(textureKey);
    if (slotIt != runtime.textureSlotByKey.end())
    {
        auto const slot = slotIt->second;
        if (slot < runtime.texturesBySlot.size())
        {
            auto &textureEntry = runtime.texturesBySlot[slot];
            if (textureEntry.image.valid())
            {
                retireUiTexture(runtime, std::move(textureEntry.image), slot, currentFrameSlot, true);
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

[[nodiscard]] bool runtimeHasValidTextureFor(const UiRuntimeCache &runtime, const ImTextureData &textureData) noexcept
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

void cleanupRetiredTextures(UiRuntimeCache &runtime, std::size_t currentFrameSlot)
{
    nr::nrAssert(currentFrameSlot < nr::maxFrameInFlight, "UiNode texture cleanup requires a valid frame slot.");

    std::erase_if(runtime.retiredTextures, [&](UiRetiredTexture &retired) {
        retired.pendingFrameSlots.reset(currentFrameSlot);
        if (retired.pendingFrameSlots.any())
        {
            return false;
        }

        if (retired.releaseSlot && retired.slot < runtime.texturesBySlot.size() &&
            !runtime.texturesBySlot[retired.slot].image.valid() &&
            std::ranges::find(runtime.freeTextureSlots, retired.slot) == runtime.freeTextureSlots.end())
        {
            runtime.freeTextureSlots.push_back(retired.slot);
        }
        return true;
    });
}

void synchronizeUiTextures(nr::rhi::Device &device, UiRuntimeCache &runtime, const ImDrawData &drawData,
                           std::size_t currentFrameSlot)
{
    if (drawData.Textures == nullptr)
    {
        return;
    }

    std::ranges::for_each(*drawData.Textures, [&](ImTextureData *textureData) {
        if (textureData == nullptr)
        {
            return;
        }

        switch (textureData->Status)
        {
        case ImTextureStatus_OK:
            if (!runtimeHasValidTextureFor(runtime, *textureData))
            {
                createOrUpdateUiTexture(device, runtime, *textureData, currentFrameSlot);
            }
            break;
        case ImTextureStatus_WantCreate:
        case ImTextureStatus_WantUpdates:
            createOrUpdateUiTexture(device, runtime, *textureData, currentFrameSlot);
            break;
        case ImTextureStatus_WantDestroy:
            destroyUiTexture(runtime, *textureData, currentFrameSlot);
            break;
        default:
            break;
        }
    });
}

[[nodiscard]] std::vector<nr::renderer::GraphResourceHandle> registerUiTextureImageResources(
    nr::renderer::NodeBuildContext &context, UiRuntimeCache &runtime)
{
    auto graphResources = std::vector<nr::renderer::GraphResourceHandle>{};
    auto slotRange = std::views::iota(std::size_t{0}, runtime.texturesBySlot.size());
    std::ranges::for_each(slotRange, [&](std::size_t slot) {
        auto &textureEntry = runtime.texturesBySlot[slot];
        if (!textureEntry.image.valid())
        {
            return;
        }

        nr::nrAssert(slot < kUiTextureDescriptorCapacity, "UiNode graph resource registration found slot {} beyond descriptor capacity {}.",
                                 slot, kUiTextureDescriptorCapacity);

        auto resource = context.addResource(nr::renderer::GraphImportedImageDesc{
            .debugName = std::format("Ui.TextureResource[{}]", slot),
            .lifetime = nr::renderer::ResourceLifetime::RendererPersistent,
            .initialOwnership = nr::renderer::ResourceOwnershipDomain::Graphics,
            .extent = textureEntry.image.extent(),
            .format = vk::Format::eR8G8B8A8Unorm,
            .usageIntents =
                {
                    nr::renderer::ImageUsageIntent::Sampled,
                },
            .initialLayout = textureEntry.state.common.initialized ? textureEntry.state.layout
                                                                   : nr::renderer::ImageLayoutIntent::Undefined,
            .initialAccessScope =
                textureEntry.state.common.initialized ? textureEntry.state.common.access : nr::renderer::AccessScope{},
            .importedResource = std::cref(textureEntry.image),
            .retainedState = std::ref(textureEntry.state),
        });

        graphResources.push_back(resource);
    });

    return graphResources;
}

[[nodiscard]] vk::DeviceSize checkedUiUploadByteSize(std::size_t elementCount, std::size_t elementByteSize,
                                                     std::string_view payloadName)
{
    nr::nrAssert(elementByteSize > 0u, "UiNode {} upload requires a non-zero element byte size.", payloadName);
    nr::nrAssert(elementCount <= std::numeric_limits<std::size_t>::max() / elementByteSize, "UiNode {} upload byte size exceeds the host size range.", payloadName);
    auto const byteSize = elementCount * elementByteSize;
    nr::nrAssert(std::in_range<vk::DeviceSize>(byteSize), "UiNode {} upload byte size exceeds the Vulkan buffer size range.", payloadName);
    return static_cast<vk::DeviceSize>(byteSize);
}

void ensureFrameUploadBuffer(nr::rhi::Device &device, nr::rhi::Buffer &buffer, vk::DeviceSize requiredSize,
                             vk::BufferUsageFlags usage, std::string_view debugName)
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
    if (capacity < requiredSize)
    {
        auto constexpr maximumCapacity = std::numeric_limits<vk::DeviceSize>::max();
        auto const cappedDoubledCapacity =
            capacity > maximumCapacity / 2u ? maximumCapacity : capacity * 2u;
        capacity = std::max(requiredSize, cappedDoubledCapacity);
    }

    auto bufferInfo = nr::rhi::makeBufferCreateInfo(capacity, usage);

    buffer = device.resourceFactory.createBuffer(bufferInfo, nr::rhi::MemoryUsage::CpuToGpu, debugName);
    nr::nrAssert(buffer.valid(), "UiNode failed to create upload buffer '{}'.", debugName);
}

void uploadUiDrawFrameBuffers(nr::rhi::Device &device, UiRuntimeCache &runtime, std::size_t frameSlot,
                              const UiFrameDrawData &drawFrame)
{
    auto const vertexBytes = checkedUiUploadByteSize(drawFrame.vertices.size(), sizeof(ImDrawVert), "vertex");
    auto const indexBytes = checkedUiUploadByteSize(drawFrame.indices.size(), sizeof(ImDrawIdx), "index");

    ensureFrameUploadBuffer(device, runtime.vertexBuffers[frameSlot], vertexBytes,
                            vk::BufferUsageFlagBits::eVertexBuffer,
                            std::format("Ui.VertexBuffer[{}]", frameSlot));
    ensureFrameUploadBuffer(device, runtime.indexBuffers[frameSlot], indexBytes,
                            vk::BufferUsageFlagBits::eIndexBuffer,
                            std::format("Ui.IndexBuffer[{}]", frameSlot));

    if (vertexBytes > 0u)
    {
        runtime.vertexBuffers[frameSlot].writeMappedAndFlush(std::span<const ImDrawVert>{drawFrame.vertices});
    }
    if (indexBytes > 0u)
    {
        runtime.indexBuffers[frameSlot].writeMappedAndFlush(std::span<const ImDrawIdx>{drawFrame.indices});
    }
}

[[nodiscard]] std::optional<std::reference_wrapper<nr::app::UiSystem>> tryGetUiOverlaySystem(
    const nr::renderer::NodeFrameParameters &frameParameters)
{
    if (!frameParameters.frameServices.has_value())
    {
        return std::nullopt;
    }

    return frameParameters.frameServices->get().tryGet<nr::app::UiSystem>();
}

[[nodiscard]] std::map<std::uint32_t, nr::renderer::BindlessImageDescriptor> makeUiTextureDescriptors(
    const UiRuntimeCache &runtime)
{
    auto descriptorsById = std::map<std::uint32_t, nr::renderer::BindlessImageDescriptor>{};
    auto slotRange = std::views::iota(std::size_t{0}, runtime.texturesBySlot.size());
    std::ranges::for_each(slotRange, [&](std::size_t slot) {
        auto const &textureEntry = runtime.texturesBySlot[slot];
        if (!textureEntry.image.valid())
        {
            return;
        }

        nr::nrAssert(slot < kUiTextureDescriptorCapacity, "UiNode bindless descriptor update found slot {} beyond descriptor capacity {}.", slot,
                                 kUiTextureDescriptorCapacity);
        descriptorsById.insert_or_assign(static_cast<std::uint32_t>(slot),
                                         nr::renderer::BindlessImageDescriptor{
                                             .image = std::cref(textureEntry.image),
                                             .layout = vk::ImageLayout::eShaderReadOnlyOptimal,
                                         });
    });
    return descriptorsById;
}

[[nodiscard]] nr::renderer::BindlessImageTableRequest makeBindlessTextureTableRequest(UiRuntimeCache &runtime)
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

void prepareBindlessTextureTableForFrame(UiRuntimeCache &runtime, nr::renderer::BindlessImageTableCache &cache,
                                         nr::renderer::PipelineRuntime<
                                             nr::rhi::GraphicsPipeline>::PassBindingHandle passBinding,
                                         std::uint32_t frameIndex)
{
    nr::nrAssert(static_cast<bool>(runtime.pipeline),
                 "UiNode bindless texture table requires an initialized pipeline runtime.");

    cache.ensureTableForFrame(*runtime.pipeline, passBinding, frameIndex, makeBindlessTextureTableRequest(runtime));
}

[[nodiscard]] nr::rhi::ShaderBindingSnapshot makeBindlessTextureBindingSnapshotForFrame(
    UiRuntimeCache &runtime, nr::renderer::BindlessImageTableCache &cache,
    nr::renderer::PipelineRuntime<nr::rhi::GraphicsPipeline>::PassBindingHandle passBinding,
    std::uint32_t frameIndex)
{
    return cache.makeSnapshotForFrame(*runtime.pipeline, passBinding, frameIndex,
                                      makeBindlessTextureTableRequest(runtime));
}

struct UiDrawCommandRecorder
{
    std::shared_ptr<UiRuntimeCache> runtime{};
    std::reference_wrapper<const nr::renderer::RasterPassRecordContext> rasterContext;
    UiPushConstants pushConstants{};
    std::optional<std::uint32_t> lastTextureIndex{};

    void operator()(const UiDrawCommand &command)
    {
        if (command.elementCount == 0u || command.scissor.extent.width == 0u ||
            command.scissor.extent.height == 0u)
        {
            return;
        }

        nr::nrAssert(command.textureSlot < runtime->texturesBySlot.size() &&
                         runtime->texturesBySlot[command.textureSlot].image.valid(), "UiNode record stage could not resolve texture slot {}.", command.textureSlot);

        auto const &context = rasterContext.get();
        if (!lastTextureIndex.has_value() || *lastTextureIndex != command.textureSlot)
        {
            pushConstants.textureIndex = command.textureSlot;
            context.pushConstants("gUiPush", pushConstants);
            lastTextureIndex = command.textureSlot;
        }

        context.commandBuffer.setScissor(0u, {command.scissor});
        context.commandBuffer.drawIndexed(command.elementCount, 1u, command.firstIndex, command.vertexOffset, 0u);
    }
};

struct UiOverlayCallbacks
{
    nr::renderer::RasterPassPrepareCallback prepare{};
    nr::renderer::PipelinePassBindingSnapshotCallback<nr::rhi::GraphicsPipeline> dynamicBindingSnapshot{};
    nr::renderer::RasterPassRecordCallback record{};
};

[[nodiscard]] UiOverlayCallbacks makeUiOverlayCallbacks(
    std::shared_ptr<UiRuntimeCache> runtime, UiDrawFramePayload drawFrame,
    std::reference_wrapper<nr::renderer::BindlessImageTableCache> cache)
{
    nr::nrAssert(static_cast<bool>(runtime), "UiNode overlay callbacks require initialized runtime state.");
    nr::nrAssert(static_cast<bool>(drawFrame), "UiNode overlay callbacks require immutable draw-frame data.");

    return UiOverlayCallbacks{
        .prepare =
            [runtime, drawFrame, cache](
                const nr::renderer::PassPrepareContext &prepareContext,
                nr::renderer::PipelineRuntime<nr::rhi::GraphicsPipeline>::PassBindingHandle passBinding) {
                nr::nrAssert(prepareContext.device.has_value(), "UiNode prepare stage requires device access.");
                prepareBindlessTextureTableForFrame(*runtime, cache.get(), passBinding, prepareContext.frameIndex);

                auto &device = prepareContext.device->get();
                auto const frameSlot =
                    static_cast<std::size_t>(prepareContext.frameIndex % runtime->vertexBuffers.size());
                uploadUiDrawFrameBuffers(device, *runtime, frameSlot, *drawFrame);
            },
        .dynamicBindingSnapshot =
            [runtime, cache](
                const nr::renderer::PassPrepareContext &prepareContext,
                nr::renderer::PipelineRuntime<nr::rhi::GraphicsPipeline>::PassBindingHandle passBinding) {
                return makeBindlessTextureBindingSnapshotForFrame(*runtime, cache.get(), passBinding,
                                                                  prepareContext.frameIndex);
            },
        .record =
            [runtime, drawFrame](const nr::renderer::RasterPassRecordContext &rasterContext) {
                if (drawFrame->commands.empty())
                {
                    return;
                }

                auto const frameSlot =
                    static_cast<std::size_t>(rasterContext.pass.frameIndex % runtime->vertexBuffers.size());
                auto const &vertexBuffer = runtime->vertexBuffers[frameSlot];
                auto const &indexBuffer = runtime->indexBuffers[frameSlot];
                nr::nrAssert(vertexBuffer.valid(), "UiNode requires a valid per-frame vertex buffer.");
                nr::nrAssert(indexBuffer.valid(), "UiNode requires a valid per-frame index buffer.");

                auto &commandBuffer = rasterContext.commandBuffer;
                auto const vertexBuffers = std::array{vertexBuffer.handle()};
                auto const vertexOffsets = std::array<vk::DeviceSize, 1>{0u};
                commandBuffer.bindVertexBuffers(0u, vertexBuffers, vertexOffsets);
                commandBuffer.bindIndexBuffer(indexBuffer.handle(), 0u, kUiIndexType);

                std::ranges::for_each(drawFrame->commands,
                                      UiDrawCommandRecorder{
                                          .runtime = runtime,
                                          .rasterContext = std::cref(rasterContext),
                                          .pushConstants = drawFrame->pushConstants,
                                      });
            },
    };
}

struct UiValidatedDrawCounts
{
    std::size_t commandListCount = 0u;
    std::size_t vertexCount = 0u;
    std::size_t indexCount = 0u;
};

[[nodiscard]] std::size_t checkedUiDrawSizeAdd(std::size_t accumulated, std::size_t additional,
                                               std::string_view quantityName)
{
    // [TEMP-BUILD-PROFILING] BEGIN - lazy assertion context experiment. Revert to the eager std::format form to undo.
    nr::nrAssert(additional <= std::numeric_limits<std::size_t>::max() - accumulated, "UiNode {} exceeds the host size range.", quantityName);
    // [TEMP-BUILD-PROFILING] END
    return accumulated + additional;
}

// [TEMP-BUILD-PROFILING] BEGIN - temporary UiNode build-stage sub-timers. Remove with the whole block.
struct TempUiProfile
{
    double validateMilliseconds = 0.0;
    double copyMilliseconds = 0.0;
    double textureSyncMilliseconds = 0.0;
    double declareMilliseconds = 0.0;
    double totalMilliseconds = 0.0;
    std::uint64_t frames = 0u;
    std::uint64_t vertices = 0u;
    std::uint64_t indices = 0u;
    std::uint64_t commands = 0u;
};

inline TempUiProfile tempUiProfile{};

[[nodiscard]] inline double tempElapsedMs(std::chrono::steady_clock::time_point start,
                                          std::chrono::steady_clock::time_point finish) noexcept
{
    return std::chrono::duration<double, std::milli>(finish - start).count();
}

inline void tempReportUiProfile()
{
    ++tempUiProfile.frames;
    if (tempUiProfile.frames % 100u != 0u)
    {
        return;
    }
    auto const frames = static_cast<double>(tempUiProfile.frames);
    nr::nrLog<nr::LogLevel::info>(
        "[TEMP-BUILD-PROFILING][Ui] frames={}, totalAvgMs={:.4f} (validate={:.4f}, copyOnly={:.4f}, textureSync={:.4f}, "
        "declare={:.4f}), avgVertices={}, avgIndices={}, avgCommands={}",
        tempUiProfile.frames, tempUiProfile.totalMilliseconds / frames, tempUiProfile.validateMilliseconds / frames,
        (tempUiProfile.copyMilliseconds - tempUiProfile.validateMilliseconds) / frames,
        tempUiProfile.textureSyncMilliseconds / frames, tempUiProfile.declareMilliseconds / frames,
        static_cast<std::uint64_t>(static_cast<double>(tempUiProfile.vertices) / frames),
        static_cast<std::uint64_t>(static_cast<double>(tempUiProfile.indices) / frames),
        static_cast<std::uint64_t>(static_cast<double>(tempUiProfile.commands) / frames));
}
// [TEMP-BUILD-PROFILING] END

[[nodiscard]] UiValidatedDrawCounts validateUiDrawData(const ImDrawData &drawData)
{
    nr::nrAssert(drawData.Valid, "UiNode requires finalized valid ImDrawData.");
    nr::nrAssert(drawData.CmdListsCount >= 0, "UiNode ImDrawData command-list count cannot be negative.");
    nr::nrAssert(drawData.TotalVtxCount >= 0, "UiNode ImDrawData total vertex count cannot be negative.");
    nr::nrAssert(drawData.TotalIdxCount >= 0, "UiNode ImDrawData total index count cannot be negative.");
    nr::nrAssert(drawData.CmdLists.Size >= 0, "UiNode ImDrawData command-list storage size cannot be negative.");
    nr::nrAssert(drawData.CmdLists.Size == drawData.CmdListsCount, "UiNode ImDrawData reports {} command lists but stores {}.", drawData.CmdListsCount,
                             drawData.CmdLists.Size);
    nr::nrAssert(drawData.CmdLists.Size == 0 || drawData.CmdLists.Data != nullptr,
                 "UiNode ImDrawData requires storage for non-empty command lists.");
    nr::nrAssert(std::in_range<std::size_t>(drawData.CmdListsCount),
                 "UiNode ImDrawData command-list count exceeds the host size range.");
    nr::nrAssert(std::in_range<std::size_t>(drawData.TotalVtxCount),
                 "UiNode ImDrawData total vertex count exceeds the host size range.");
    nr::nrAssert(std::in_range<std::size_t>(drawData.TotalIdxCount),
                 "UiNode ImDrawData total index count exceeds the host size range.");

    auto counts = UiValidatedDrawCounts{
        .commandListCount = static_cast<std::size_t>(drawData.CmdListsCount),
    };
    auto commandListIndices = std::views::iota(std::size_t{0u}, counts.commandListCount);
    std::ranges::for_each(commandListIndices, [&](std::size_t commandListIndex) {
        auto const *commandList = drawData.CmdLists.Data[commandListIndex];
        nr::nrAssert(commandList != nullptr, "UiNode ImDrawData command list {} cannot be null.", commandListIndex);
        nr::nrAssert(commandList->VtxBuffer.Size >= 0, "UiNode command list {} vertex count cannot be negative.", commandListIndex);
        nr::nrAssert(commandList->IdxBuffer.Size >= 0, "UiNode command list {} index count cannot be negative.", commandListIndex);
        nr::nrAssert(commandList->CmdBuffer.Size >= 0, "UiNode command list {} command count cannot be negative.", commandListIndex);
        nr::nrAssert(commandList->VtxBuffer.Size == 0 || commandList->VtxBuffer.Data != nullptr, "UiNode command list {} requires storage for non-empty vertices.", commandListIndex);
        nr::nrAssert(commandList->IdxBuffer.Size == 0 || commandList->IdxBuffer.Data != nullptr, "UiNode command list {} requires storage for non-empty indices.", commandListIndex);
        nr::nrAssert(commandList->CmdBuffer.Size == 0 || commandList->CmdBuffer.Data != nullptr, "UiNode command list {} requires storage for non-empty commands.", commandListIndex);
        nr::nrAssert(std::in_range<std::size_t>(commandList->VtxBuffer.Size), "UiNode command list {} vertex count exceeds the host size range.", commandListIndex);
        nr::nrAssert(std::in_range<std::size_t>(commandList->IdxBuffer.Size), "UiNode command list {} index count exceeds the host size range.", commandListIndex);
        nr::nrAssert(std::in_range<std::size_t>(commandList->CmdBuffer.Size), "UiNode command list {} command count exceeds the host size range.", commandListIndex);

        auto const listVertexCount = static_cast<std::size_t>(commandList->VtxBuffer.Size);
        auto const listIndexCount = static_cast<std::size_t>(commandList->IdxBuffer.Size);
        auto const listCommandCount = static_cast<std::size_t>(commandList->CmdBuffer.Size);
        counts.vertexCount = checkedUiDrawSizeAdd(counts.vertexCount, listVertexCount, "draw vertex count");
        counts.indexCount = checkedUiDrawSizeAdd(counts.indexCount, listIndexCount, "draw index count");

        auto commandIndices = std::views::iota(std::size_t{0u}, listCommandCount);
        std::ranges::for_each(commandIndices, [&](std::size_t commandIndex) {
            auto const &command = commandList->CmdBuffer.Data[commandIndex];
            nr::nrAssert(command.UserCallback == nullptr, "UiNode does not support ImDrawCmd callbacks (list {}, command {}).",
                                     commandListIndex, commandIndex);
            nr::nrAssert(std::in_range<std::size_t>(command.IdxOffset),
                         "UiNode draw-command index offset exceeds the host size range.");
            nr::nrAssert(std::in_range<std::size_t>(command.ElemCount),
                         "UiNode draw-command element count exceeds the host size range.");
            nr::nrAssert(std::in_range<std::size_t>(command.VtxOffset),
                         "UiNode draw-command vertex offset exceeds the host size range.");

            auto const indexOffset = static_cast<std::size_t>(command.IdxOffset);
            auto const elementCount = static_cast<std::size_t>(command.ElemCount);
            auto const vertexOffset = static_cast<std::size_t>(command.VtxOffset);
            nr::nrAssert(indexOffset <= listIndexCount,
                         "UiNode draw-command index offset exceeds its command-list index buffer.");
            nr::nrAssert(elementCount <= listIndexCount - indexOffset,
                         "UiNode draw-command index span exceeds its command-list index buffer.");
            nr::nrAssert(vertexOffset <= listVertexCount,
                         "UiNode draw-command vertex offset exceeds its command-list vertex buffer.");

            auto elementOffsets = std::views::iota(std::size_t{0u}, elementCount);
            std::ranges::for_each(elementOffsets, [&](std::size_t elementOffset) {
                auto const indexPosition =
                    checkedUiDrawSizeAdd(indexOffset, elementOffset, "draw-command index position");
                auto const localIndex = commandList->IdxBuffer.Data[indexPosition];
                nr::nrAssert(std::in_range<std::size_t>(localIndex),
                             "UiNode draw-command index value exceeds the host size range.");
                auto const referencedVertex = checkedUiDrawSizeAdd(
                    vertexOffset, static_cast<std::size_t>(localIndex), "draw-command vertex reference");
                nr::nrAssert(referencedVertex < listVertexCount,
                             "UiNode draw-command index references outside its command-list vertex buffer.");
            });
        });
    });

    nr::nrAssert(counts.vertexCount == static_cast<std::size_t>(drawData.TotalVtxCount), "UiNode ImDrawData reports {} total vertices but command lists contain {}.",
                             drawData.TotalVtxCount, counts.vertexCount);
    nr::nrAssert(counts.indexCount == static_cast<std::size_t>(drawData.TotalIdxCount), "UiNode ImDrawData reports {} total indices but command lists contain {}.",
                             drawData.TotalIdxCount, counts.indexCount);
    return counts;
}

[[nodiscard]] UiFrameDrawData copyUiDrawData(const ImDrawData &drawData, vk::Extent2D swapchainExtent)
{
    // [TEMP-BUILD-PROFILING] BEGIN - temporary UiNode build-stage sub-timers. Remove with the whole block.
    auto const tempValidateStart = std::chrono::steady_clock::now();
    // [TEMP-BUILD-PROFILING] END
    auto const validatedCounts = validateUiDrawData(drawData);
    // [TEMP-BUILD-PROFILING] BEGIN - temporary UiNode build-stage sub-timers. Remove with the whole block.
    tempUiProfile.validateMilliseconds += tempElapsedMs(tempValidateStart, std::chrono::steady_clock::now());
    tempUiProfile.vertices += validatedCounts.vertexCount;
    tempUiProfile.indices += validatedCounts.indexCount;
    // [TEMP-BUILD-PROFILING] END
    auto output = UiFrameDrawData{};
    output.framebufferExtent = vk::Extent2D{
        std::max(1u, swapchainExtent.width),
        std::max(1u, swapchainExtent.height),
    };

    auto const framebufferWidth =
        static_cast<double>(drawData.DisplaySize.x) * static_cast<double>(drawData.FramebufferScale.x);
    auto const framebufferHeight =
        static_cast<double>(drawData.DisplaySize.y) * static_cast<double>(drawData.FramebufferScale.y);
    constexpr auto maximumFramebufferDimension =
        static_cast<double>(std::numeric_limits<std::uint32_t>::max());
    if (!std::isfinite(framebufferWidth) || !std::isfinite(framebufferHeight) || framebufferWidth <= 0.0 ||
        framebufferHeight <= 0.0 || framebufferWidth > maximumFramebufferDimension ||
        framebufferHeight > maximumFramebufferDimension)
    {
        return output;
    }

    auto const finalizedFramebufferExtent = vk::Extent2D{
        static_cast<std::uint32_t>(framebufferWidth),
        static_cast<std::uint32_t>(framebufferHeight),
    };
    if (finalizedFramebufferExtent != output.framebufferExtent)
    {
        return output;
    }

    if (validatedCounts.vertexCount == 0u || validatedCounts.indexCount == 0u)
    {
        return output;
    }

    output.pushConstants = UiPushConstants{
        .scale =
            glm::vec2{
                2.0f / drawData.DisplaySize.x,
                2.0f / drawData.DisplaySize.y,
            },
        .translate =
            glm::vec2{
                -1.0f - drawData.DisplayPos.x * (2.0f / drawData.DisplaySize.x),
                -1.0f - drawData.DisplayPos.y * (2.0f / drawData.DisplaySize.y),
            },
    };

    output.vertices.reserve(validatedCounts.vertexCount);
    output.indices.reserve(validatedCounts.indexCount);

    auto globalVertexOffset = std::size_t{0u};
    auto globalIndexOffset = std::size_t{0u};

    auto commandListIndices = std::views::iota(std::size_t{0u}, validatedCounts.commandListCount);
    std::ranges::for_each(commandListIndices, [&](std::size_t commandListIndex) {
        auto const *commandList = drawData.CmdLists.Data[commandListIndex];
        auto const listVertexCount = static_cast<std::size_t>(commandList->VtxBuffer.Size);
        auto const listIndexCount = static_cast<std::size_t>(commandList->IdxBuffer.Size);
        auto const listCommandCount = static_cast<std::size_t>(commandList->CmdBuffer.Size);
        if (listVertexCount > 0u)
        {
            output.vertices.insert(output.vertices.end(), commandList->VtxBuffer.Data,
                                   commandList->VtxBuffer.Data + listVertexCount);
        }
        if (listIndexCount > 0u)
        {
            output.indices.insert(output.indices.end(), commandList->IdxBuffer.Data,
                                  commandList->IdxBuffer.Data + listIndexCount);
        }

        auto commandIndices = std::views::iota(std::size_t{0u}, listCommandCount);
        std::ranges::for_each(commandIndices, [&](std::size_t commandIndex) {
            auto const &command = commandList->CmdBuffer.Data[commandIndex];
            auto const firstIndex = checkedUiDrawSizeAdd(
                globalIndexOffset, static_cast<std::size_t>(command.IdxOffset), "global draw-command first index");
            auto const vertexOffset = checkedUiDrawSizeAdd(
                globalVertexOffset, static_cast<std::size_t>(command.VtxOffset), "global draw-command vertex offset");
            nr::nrAssert(std::in_range<std::uint32_t>(command.ElemCount),
                         "UiNode draw-command element count exceeds the Vulkan draw range.");
            nr::nrAssert(std::in_range<std::uint32_t>(firstIndex),
                         "UiNode draw-command first index exceeds the Vulkan draw range.");
            nr::nrAssert(std::in_range<std::int32_t>(vertexOffset),
                         "UiNode draw-command vertex offset exceeds the Vulkan draw range.");
            auto const elementCountValue = static_cast<std::uint32_t>(command.ElemCount);
            auto const firstIndexValue = static_cast<std::uint32_t>(firstIndex);
            auto const vertexOffsetValue = static_cast<std::int32_t>(vertexOffset);

            auto const clipMinX =
                std::max(0.0f, (command.ClipRect.x - drawData.DisplayPos.x) * drawData.FramebufferScale.x);
            auto const clipMinY =
                std::max(0.0f, (command.ClipRect.y - drawData.DisplayPos.y) * drawData.FramebufferScale.y);
            auto const clipMaxX = std::min(static_cast<float>(output.framebufferExtent.width),
                                           (command.ClipRect.z - drawData.DisplayPos.x) * drawData.FramebufferScale.x);
            auto const clipMaxY = std::min(static_cast<float>(output.framebufferExtent.height),
                                           (command.ClipRect.w - drawData.DisplayPos.y) * drawData.FramebufferScale.y);

            if (clipMaxX <= clipMinX || clipMaxY <= clipMinY)
            {
                return;
            }

            auto const offsetX = static_cast<std::int32_t>(clipMinX);
            auto const offsetY = static_cast<std::int32_t>(clipMinY);
            auto const extentWidth = static_cast<std::uint32_t>(clipMaxX - clipMinX);
            auto const extentHeight = static_cast<std::uint32_t>(clipMaxY - clipMinY);
            auto scissor = vk::Rect2D{};
            scissor.offset = vk::Offset2D{offsetX, offsetY};
            scissor.extent = vk::Extent2D{extentWidth, extentHeight};

            output.commands.push_back(UiDrawCommand{
                .scissor = scissor,
                .elementCount = elementCountValue,
                .firstIndex = firstIndexValue,
                .vertexOffset = vertexOffsetValue,
                .textureSlot = textureSlotFromId(command.GetTexID()),
            });
        });

        globalVertexOffset =
            checkedUiDrawSizeAdd(globalVertexOffset, listVertexCount, "global draw vertex offset");
        globalIndexOffset = checkedUiDrawSizeAdd(globalIndexOffset, listIndexCount, "global draw index offset");
    });

    return output;
}

[[nodiscard]] UiDrawFramePayload makeUiDrawFramePayload(UiFrameDrawData drawFrame)
{
    return std::make_shared<const UiFrameDrawData>(std::move(drawFrame));
}

[[nodiscard]] UiDrawFramePayload prepareUiDrawFrame(nr::rhi::Device &device, UiRuntimeCache &runtime,
                                                    const nr::renderer::NodeFrameParameters &frameParameters)
{
    nr::nrAssert(frameParameters.frameIndex < nr::maxFrameInFlight,
                 "UiNode draw preflight requires frameIndex to identify a valid RHI frame slot.");
    auto const currentFrameSlot = static_cast<std::size_t>(frameParameters.frameIndex);
    cleanupRetiredTextures(runtime, currentFrameSlot);

    auto const bufferExtent = vk::Extent2D{
        std::max(1u, frameParameters.swapchainExtent.width),
        std::max(1u, frameParameters.swapchainExtent.height),
    };
    auto drawFrame = UiFrameDrawData{};
    drawFrame.framebufferExtent = bufferExtent;
    auto uiSystem = tryGetUiOverlaySystem(frameParameters);
    if (!uiSystem.has_value())
    {
        return makeUiDrawFramePayload(std::move(drawFrame));
    }

    auto drawData = uiSystem->get().drawData();
    if (!drawData.has_value())
    {
        return makeUiDrawFramePayload(std::move(drawFrame));
    }

    // [TEMP-BUILD-PROFILING] BEGIN - temporary UiNode build-stage sub-timers. Remove with the whole block.
    auto const tempSyncStart = std::chrono::steady_clock::now();
    // [TEMP-BUILD-PROFILING] END
    synchronizeUiTextures(device, runtime, drawData->get(), currentFrameSlot);
    // [TEMP-BUILD-PROFILING] BEGIN - temporary UiNode build-stage sub-timers. Remove with the whole block.
    auto const tempCopyStart = std::chrono::steady_clock::now();
    tempUiProfile.textureSyncMilliseconds += tempElapsedMs(tempSyncStart, tempCopyStart);
    auto tempPayload = makeUiDrawFramePayload(copyUiDrawData(drawData->get(), bufferExtent));
    tempUiProfile.copyMilliseconds += tempElapsedMs(tempCopyStart, std::chrono::steady_clock::now());
    tempUiProfile.commands += tempPayload->commands.size();
    return tempPayload;
    // [TEMP-BUILD-PROFILING] END
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

void UiNode::initialize(NodeInitContext &context)
{
    nr::nrAssert(context.shaderPrograms.size() == 2u && context.shaderPrograms[0].entryPoint() != nullptr &&
                     context.shaderPrograms[0].entryPoint()->stage == SLANG_STAGE_VERTEX &&
                     context.shaderPrograms[1].entryPoint() != nullptr &&
                     context.shaderPrograms[1].entryPoint()->stage == SLANG_STAGE_FRAGMENT,
                 "Ui initialization requires ordered vertex and fragment shaders.");
    device_ = context.device;

    auto const frozenBufferFormat = detail::resolveUiBufferFormat(input);
    runtime_ = detail::ensureUiRuntime(context.device.get(), context.shaderPrograms, frozenBufferFormat,
                                       context.runtimeName + ".Pipeline");
}

void UiNode::finalizeInitialization()
{
    nr::nrAssert(runtime_ && runtime_->pipeline && runtime_->pipeline->valid(),
                 "Ui async graphics PSO construction failed.");
    static_cast<void>(detail::validatedFrozenUiBufferFormat(input, *runtime_));
}

void UiNode::build(NodeBuildContext &context, const NodeFrameParameters &frameParameters)
{
    materializeCurrentFrame(context, frameParameters);
}

void UiNode::materializeCurrentFrame(NodeBuildContext &context, const NodeFrameParameters &frameParameters)
{
    nr::nrAssert(static_cast<bool>(runtime_), "UiNode build stage requires initialized runtime state.");
    nr::nrAssert(device_.has_value(), "UiNode build stage requires initialize() device reference.");
    // [TEMP-BUILD-PROFILING] BEGIN - temporary UiNode build-stage sub-timers. Remove with the whole block.
    auto const tempMaterializeStart = std::chrono::steady_clock::now();
    // [TEMP-BUILD-PROFILING] END
    auto const bufferFormat = validatedFrozenUiBufferFormat(input, *runtime_);

    auto const bufferExtent = vk::Extent2D{
        std::max(1u, frameParameters.swapchainExtent.width),
        std::max(1u, frameParameters.swapchainExtent.height),
    };
    ensureUiBufferImage(device_->get(), *runtime_, bufferExtent);

    auto const frameSlot = static_cast<std::size_t>(frameParameters.frameIndex % nr::maxFrameInFlight);
    auto uiBuffer = context.importSampledColor(runtime_->uiBuffers[frameSlot], std::format("Ui.Buffer[{}]", frameSlot),
                                               bufferExtent, bufferFormat, nr::renderer::ResourceLifetime::FrameLocal);

    context.publishFrameResource(nr::renderer::frameResource::uiColor, uiBuffer);

    auto drawFrame = prepareUiDrawFrame(device_->get(), *runtime_, frameParameters);
    nr::nrAssert(static_cast<bool>(drawFrame), "UiNode build requires immutable draw-frame data.");
    // [TEMP-BUILD-PROFILING] BEGIN - temporary UiNode build-stage sub-timers. Remove with the whole block.
    auto const tempDeclareStart = std::chrono::steady_clock::now();
    // [TEMP-BUILD-PROFILING] END

    auto textureResources = registerUiTextureImageResources(context, *runtime_);

    auto runtime = runtime_;
    auto &bindlessImageTableCache = context.globalResources.get().bindlessImageTableCache.get();
    auto callbacks = detail::makeUiOverlayCallbacks(runtime, drawFrame, std::ref(bindlessImageTableCache));
    auto overlayPass = nr::renderer::RasterPassBuilder{context, "Ui.Overlay", runtime_->pipeline};
    overlayPass.viewport(drawFrame->framebufferExtent)
        .colorAttachment(uiBuffer, vk::ClearValue{vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}}})
        .rasterState(nr::rhi::MeshRasterState{
            .cullMode = vk::CullModeFlagBits::eNone,
            .depthCompareOp = vk::CompareOp::eAlways,
        })
        .prepare(std::move(callbacks.prepare))
        .dynamicBindingSnapshot(std::move(callbacks.dynamicBindingSnapshot))
        .record(std::move(callbacks.record));

    std::ranges::for_each(textureResources, [&](nr::renderer::GraphResourceHandle resource) {
        overlayPass.resourceUse(nr::renderer::use::withShaderStages(nr::renderer::use::sampledRead(resource),
                                                                    nr::renderer::ShaderStageIntent::Fragment));
    });

    [[maybe_unused]] auto overlayPassHandle = overlayPass.build();
    // [TEMP-BUILD-PROFILING] BEGIN - temporary UiNode build-stage sub-timers. Remove with the whole block.
    auto const tempMaterializeFinish = std::chrono::steady_clock::now();
    tempUiProfile.declareMilliseconds += tempElapsedMs(tempDeclareStart, tempMaterializeFinish);
    tempUiProfile.totalMilliseconds += tempElapsedMs(tempMaterializeStart, tempMaterializeFinish);
    tempReportUiProfile();
    // [TEMP-BUILD-PROFILING] END
}

void UiNode::shutdown(NodeShutdownContext &)
{
    if (runtime_ && runtime_->pipeline)
    {
        runtime_->pipeline->clearBindingSets();
    }

    runtime_.reset();
    device_.reset();
}
} // namespace nr::renderPasses
