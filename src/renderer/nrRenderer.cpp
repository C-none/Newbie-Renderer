module nr.renderer;
import :renderer;
import dependency.assets;
import dependency.json;
import dependency.math;
import dependency.vulkan;
import nr.rhi;
import nr.scene;
import nr.resource;
import nr.utils;
import std;
import :frameServices;
import :renderGraphBuilder;
import :renderGraphCompiler;
import :renderGraphExecutor;
import :rendererSubmission;

namespace nr::renderer
{
namespace
{
template <typename Function> class ScopeExit
{
  public:
    explicit ScopeExit(Function function) noexcept(std::is_nothrow_move_constructible_v<Function>)
        : function_(std::move(function))
    {
    }

    ~ScopeExit() noexcept
    {
        function_();
    }

    ScopeExit(const ScopeExit &) = delete;
    ScopeExit &operator=(const ScopeExit &) = delete;
    ScopeExit(ScopeExit &&) = delete;
    ScopeExit &operator=(ScopeExit &&) = delete;

  private:
    [[no_unique_address]] Function function_;
};

template <typename Function> ScopeExit(Function) -> ScopeExit<Function>;

struct RendererGlobalFrameUniforms
{
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::mat4 viewProjection{1.0f};
    glm::mat4 inverseViewProjection{1.0f};
    glm::mat4 previousView{1.0f};
    glm::mat4 previousViewProjection{1.0f};
    glm::vec4 cameraWorld{0.0f, 0.0f, 0.0f, 1.0f};
    glm::uvec4 frameState{0u, 0u, 0u, 0u};
};

static_assert(sizeof(RendererGlobalFrameUniforms) == 416u, "Renderer.GlobalFrameUniforms must match shader/include/globalUniform.slang.");

[[nodiscard]] vk::Extent2D sanitizeViewportExtent(vk::Extent2D extent) noexcept
{
    return vk::Extent2D{
        std::max(1u, extent.width),
        std::max(1u, extent.height),
    };
}

[[nodiscard]] RendererGlobalFrameUniforms makeGlobalFrameUniforms(const nr::scene::SceneBridgeFrameConstants &frameConstants, const nr::scene::SceneBridgeFrameConstants &previousFrameConstants, std::uint32_t frameIndex, std::uint64_t sampleFrameOrdinal) noexcept
{
    auto inverseViewProjection = glm::mat4{1.0f};
    auto const determinant = glm::determinant(frameConstants.viewProjection);
    if (std::isfinite(determinant) && std::abs(determinant) > std::numeric_limits<float>::epsilon())
    {
        inverseViewProjection = glm::inverse(frameConstants.viewProjection);
    }

    auto const sampleFrameLow = static_cast<std::uint32_t>(sampleFrameOrdinal);
    auto const sampleFrameHigh = static_cast<std::uint32_t>(sampleFrameOrdinal >> 32u);

    return RendererGlobalFrameUniforms{
        .view = frameConstants.view,
        .projection = frameConstants.projection,
        .viewProjection = frameConstants.viewProjection,
        .inverseViewProjection = inverseViewProjection,
        .previousView = previousFrameConstants.view,
        .previousViewProjection = frameConstants.projection * previousFrameConstants.view,
        .cameraWorld = glm::vec4{frameConstants.cameraWorld, 1.0f},
        .frameState = glm::uvec4{sampleFrameLow, sampleFrameHigh, frameIndex, 0u},
    };
}

[[nodiscard]] double elapsedMilliseconds(std::chrono::steady_clock::time_point begin, std::chrono::steady_clock::time_point end) noexcept
{
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

[[nodiscard]] std::optional<nr::scene::SceneMaterialTextureBindings> collectSceneMaterialTextures(const nr::scene::Scene &scene, nr::resource::MaterialHandle materialHandle, std::map<std::uint32_t, nr::resource::TextureHandle> &sceneTextureHandlesById)
{
    auto materialRecordRef = scene.tryGetMaterialAsset(materialHandle);
    nrAssert(materialRecordRef.has_value(), std::format("Renderer scene texture collection expected material handle (slot={}, generation={}) to resolve.", materialHandle.slot, materialHandle.generation));

    auto const &materialRecord = materialRecordRef->get();
    nrAssert(materialRecord.cpuReady, std::format("Renderer scene texture collection expected material '{}' to be CPU ready.", materialRecord.cpu.name));

    auto textures = nr::scene::SceneMaterialTextureBindings{};
    auto slotIndices = std::views::iota(std::size_t{0}, materialRecord.cpu.textureSlots.size());
    std::ranges::for_each(slotIndices, [&](std::size_t slotIndex) {
        auto textureHandle = materialRecord.cpu.textureSlots[slotIndex].texture;
        if (!textureHandle.valid())
        {
            return;
        }

        auto binding = scene.tryGetSampledTextureBinding(textureHandle);
        nrAssert(binding.has_value(), std::format("Renderer scene texture collection expected resident sampled texture for material '{}' slot {}.", materialRecord.cpu.name, slotIndex));
        nrAssert(binding->descriptorIndex < kSceneTextureDescriptorCapacity, std::format("Scene texture descriptor id {} exceeds capacity {}.", binding->descriptorIndex, kSceneTextureDescriptorCapacity));
        nrAssert(binding->descriptorIndex <= nr::scene::kMaxSceneTextureId, std::format("Scene texture descriptor id {} exceeds packed uint16 id capacity {}.", binding->descriptorIndex, nr::scene::kMaxSceneTextureId));

        auto const textureId = static_cast<nr::scene::SceneTextureId>(binding->descriptorIndex);
        textures.ids[slotIndex] = textureId;
        sceneTextureHandlesById.insert_or_assign(binding->descriptorIndex, textureHandle);
    });

    auto const normalSlotIndex = nr::resource::materialTextureSlotIndex(nr::resource::MaterialTextureSlotSemantic::normal);
    auto const& normalSlot = materialRecord.cpu.textureSlots[normalSlotIndex];
    nrAssert(
        normalSlot.uvSet <= 1u,
        std::format(
            "Renderer normal texture sampling expected material '{}' UV set to be 0 or 1, got {}.",
            materialRecord.cpu.name,
            normalSlot.uvSet));
    textures.normal = nr::scene::SceneMaterialNormalTextureBinding{
        .textureId = textures.ids[normalSlotIndex],
        .uvSet = normalSlot.uvSet,
        .uvLinear = normalSlot.transform.linear,
        .uvOffset = normalSlot.transform.offset,
        .normalScale = materialRecord.cpu.core.normalScale,
    };
    return textures;
}

void collectTlasSceneTextureHandles(const nr::scene::Scene &scene, std::span<const nr::scene::TlasBuildInputPacket> tlasPackets, std::map<std::uint32_t, nr::resource::TextureHandle> &sceneTextureHandlesById)
{
    std::ranges::for_each(tlasPackets, [&](const nr::scene::TlasBuildInputPacket &packet) {
        auto meshRecordRef = scene.tryGetMeshAsset(packet.mesh);
        nrAssert(meshRecordRef.has_value(), std::format("Renderer TLAS texture collection expected mesh handle (slot={}, generation={}) to resolve.", packet.mesh.slot, packet.mesh.generation));
        auto const &meshRecord = meshRecordRef->get();
        nrAssert(meshRecord.cpuReady, std::format("Renderer TLAS texture collection expected mesh handle (slot={}, generation={}) to be CPU ready.", packet.mesh.slot, packet.mesh.generation));

        std::ranges::for_each(meshRecord.cpu.geometries, [&](const nr::resource::MeshGeometry &geometry) {
            if (geometry.material.valid())
            {
                static_cast<void>(collectSceneMaterialTextures(scene, geometry.material, sceneTextureHandlesById));
            }
        });
    });
}

[[nodiscard]] RendererTlasTextureCollectionKey makeTlasTextureCollectionKey(const nr::scene::SceneRevisionSnapshot &revisions, std::span<const nr::scene::TlasBuildInputPacket> packets)
{
    auto identities = packets | std::views::transform([](const nr::scene::TlasBuildInputPacket &packet) {
                          return RendererTlasTexturePacketIdentity{
                              .renderableId = static_cast<std::uint64_t>(packet.renderable.id()),
                              .mesh = packet.mesh,
                              .tlasBucket = packet.tlasBucket,
                          };
                      }) |
                      std::ranges::to<std::vector>();
    return RendererTlasTextureCollectionKey{
        .sceneIdentity = revisions.sceneIdentity,
        .revisions = RendererTlasTextureRevisionProjection::capture(revisions.rt),
        .packets = std::move(identities),
    };
}
} // namespace

[[nodiscard]] float haltonSequenceValue(std::uint32_t index, std::uint32_t base) noexcept
{
    if (base < 2u)
    {
        return 0.0f;
    }

    auto result = 0.0f;
    auto fraction = 1.0f / static_cast<float>(base);
    while (index > 0u)
    {
        result += fraction * static_cast<float>(index % base);
        index /= base;
        fraction /= static_cast<float>(base);
    }
    return result;
}

[[nodiscard]] RendererCameraJitterSample makeHalton23CameraJitterSample(std::uint64_t frameOrdinal, vk::Extent2D viewportExtent, std::uint32_t cycleLength) noexcept
{
    auto const extent = sanitizeViewportExtent(viewportExtent);
    auto const cycle = std::max(1u, cycleLength);
    auto const sampleIndex = static_cast<std::uint32_t>(frameOrdinal % static_cast<std::uint64_t>(cycle)) + 1u;
    auto const sample = glm::vec2{
        haltonSequenceValue(sampleIndex, 2u),
        haltonSequenceValue(sampleIndex, 3u),
    };
    auto const pixelOffset = sample - glm::vec2{0.5f};
    auto const ndcOffset = glm::vec2{
        2.0f * pixelOffset.x / static_cast<float>(extent.width),
        -2.0f * pixelOffset.y / static_cast<float>(extent.height),
    };

    return RendererCameraJitterSample{
        .sampleIndex = sampleIndex,
        .pixelOffset = pixelOffset,
        .ndcOffset = ndcOffset,
    };
}

[[nodiscard]] glm::mat4 applyCameraProjectionJitter(const glm::mat4 &projection, glm::vec2 ndcOffset) noexcept
{
    auto result = projection;
    result[2][0] -= ndcOffset.x;
    result[2][1] -= ndcOffset.y;
    return result;
}

[[nodiscard]] RendererCameraFrameState makeRendererCameraFrameState(const RendererCameraJitterConfig &jitterConfig, std::uint64_t frameOrdinal, vk::Extent2D viewportExtent) noexcept
{
    auto const extent = sanitizeViewportExtent(viewportExtent);
    auto state = RendererCameraFrameState{
        .jitterEnabled = jitterConfig.enabled(),
        .viewportExtent = extent,
    };
    if (!state.jitterEnabled)
    {
        return state;
    }

    switch (jitterConfig.sequence)
    {
    case RendererCameraJitterSequence::Halton23:
        state.jitter = makeHalton23CameraJitterSample(frameOrdinal, extent, jitterConfig.cycleLength);
        break;
    case RendererCameraJitterSequence::None:
        break;
    }
    return state;
}

void NodeRuntime::initialize(NodeInitContext &)
{
}

[[nodiscard]] std::string_view NodeRuntime::actionableSemantic() const noexcept
{
    return {};
}

FrameEffectSink::FrameEffectSink(std::optional<nr::options::FrameEffect> effect)
    : effect_(std::move(effect))
{
}

[[nodiscard]] const std::optional<nr::options::FrameEffect>& FrameEffectSink::effect() const noexcept
{
    return effect_;
}

[[nodiscard]] bool FrameEffectSink::claim(NodeRuntime& runtime, GraphPassHandle targetPass) noexcept
{
    if (!effect_.has_value() || claimedRuntime_.has_value() || !targetPass.valid())
    {
        return false;
    }
    claimedRuntime_ = std::ref(runtime);
    targetPass_ = targetPass;
    return true;
}

[[nodiscard]] bool FrameEffectSink::claimed() const noexcept
{
    return claimedRuntime_.has_value() && targetPass_.valid();
}

[[nodiscard]] std::optional<std::reference_wrapper<NodeRuntime>>
FrameEffectSink::claimedRuntime() const noexcept
{
    return claimedRuntime_;
}

[[nodiscard]] GraphPassHandle FrameEffectSink::targetPass() const noexcept
{
    return targetPass_;
}

void NodeRuntime::declareOptions(nr::options::OptionCatalogBuilder&) const
{
}

void NodeRuntime::collectOptionAvailability(
    const nr::options::OptionFrameSnapshot&,
    nr::options::OptionAvailabilityMap&) const
{
}

[[nodiscard]] bool NodeRuntime::supportsRenderGraphSkeleton() const noexcept
{
    return false;
}

[[nodiscard]] std::optional<NodeRuntime::StructuralSnapshot> NodeRuntime::structuralSnapshot(
    const NodeFrameParameters& frameParameters) const
{
    if (!supportsRenderGraphSkeleton())
    {
        return std::nullopt;
    }

    return StructuralSnapshot{
        .branchKey = std::format(
            "display={}x{};render={}x{};swapchain={}x{};format={};colorSpace={};reset={};scenePackets={};tlasPackets={}",
            frameParameters.resolutionPlan.displayExtent.width,
            frameParameters.resolutionPlan.displayExtent.height,
            frameParameters.resolutionPlan.renderExtent.width,
            frameParameters.resolutionPlan.renderExtent.height,
            frameParameters.swapchainExtent.width,
            frameParameters.swapchainExtent.height,
            static_cast<std::uint32_t>(frameParameters.swapchainFormat),
            static_cast<std::uint32_t>(frameParameters.swapchainColorSpace),
            frameParameters.resolutionPlan.resetHistory ? 1 : 0,
            frameParameters.scenePackets.has_value() ? frameParameters.scenePackets->get().rtInstances.size() : 0u,
            frameParameters.sceneTlasBuildInputs.has_value() ? frameParameters.sceneTlasBuildInputs->get().size() : 0u),
    };
}

bool NodeRuntime::materializeRenderGraphSkeleton(
    RenderGraphSkeletonPatchContext&,
    const NodeFrameParameters&,
    const StructuralSnapshot&)
{
    return false;
}

void NodeRuntime::advanceContinuations(std::uint32_t)
{
}

void NodeRuntime::flushContinuations()
{
}

[[nodiscard]] FrameEffectFinalizeDisposition NodeRuntime::finalizeFrameEffect(
    const nr::options::FrameEffect&,
    bool targetBatchSubmitted,
    std::uint32_t)
{
    return targetBatchSubmitted
               ? FrameEffectFinalizeDisposition::terminalSucceeded
               : FrameEffectFinalizeDisposition::terminalFailed;
}

void NodeRuntime::shutdown(NodeShutdownContext &)
{
}

void Renderer::ensureSceneTextureFallback()
{
    nrAssert(static_cast<bool>(device_), "Renderer::ensureSceneTextureFallback requires initialized device.");

    if (sceneTextureFallback_.valid())
    {
        return;
    }

    auto imageInfo = nr::rhi::makeImageCreateInfo(vk::Format::eR8G8B8A8Unorm, vk::Extent2D{1u, 1u}, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled);
    sceneTextureFallback_ = device_->resourceFactory.createImage(imageInfo, nr::rhi::MemoryUsage::GpuOnly, "Renderer.SceneTextureFallback.Neutral");
    nrAssert(sceneTextureFallback_.valid(), "Renderer failed to create neutral scene texture fallback.");

    uploadSceneTextureFallback();
}

void Renderer::ensureEnvironmentMapFallback()
{
    nrAssert(static_cast<bool>(device_), "Renderer::ensureEnvironmentMapFallback requires initialized device.");
    if (environmentMapImage_.valid())
    {
        return;
    }

    auto const fallbackPixel = std::array<nr::dependency::imath::Half, 4u>{
        nr::dependency::imath::Half{0.015f},
        nr::dependency::imath::Half{0.018f},
        nr::dependency::imath::Half{0.022f},
        nr::dependency::imath::Half{1.0f},
    };
    auto fallbackBytes = std::vector<std::byte>(sizeof(fallbackPixel));
    std::memcpy(fallbackBytes.data(), fallbackPixel.data(), sizeof(fallbackPixel));

    auto level = nr::resource::ImageLevel{};
    level.width = 1u;
    level.height = 1u;
    level.bytes = std::move(fallbackBytes);

    auto texture = nr::resource::Texture{};
    texture.name = "Renderer.EnvironmentMapFallback";
    texture.format = vk::Format::eR16G16B16A16Sfloat;
    texture.width = 1u;
    texture.height = 1u;
    texture.srgb = false;
    texture.levels.push_back(std::move(level));

    auto environment = nr::resource::EnvironmentMap{};
    environment.radiance = std::move(texture);
    setEnvironmentMap(std::move(environment));
}

[[nodiscard]] nr::rhi::ops::BufferUploadOwnershipPlan Renderer::makeSampledImageUploadPlan() const
{
    nrAssert(static_cast<bool>(device_), "Renderer::makeSampledImageUploadPlan requires initialized device.");

    auto const transferQueueFamily = device_->queueManager.transfer().queueFamilyIndex();
    auto const graphicsQueueFamily = device_->queueManager.graphics().queueFamilyIndex();

    auto plan = nr::rhi::ops::BufferUploadOwnershipPlan{};
    plan.releaseToDestination = nr::rhi::ops::makeQueueOwnershipTransfer(transferQueueFamily, graphicsQueueFamily,
                                                                         nr::rhi::ops::QueueAccessScope{
                                                                             .stages = vk::PipelineStageFlagBits2::eTransfer,
                                                                             .access = vk::AccessFlagBits2::eTransferWrite,
                                                                         },
                                                                         nr::rhi::ops::QueueAccessScope{
                                                                             .stages = vk::PipelineStageFlagBits2::eAllCommands,
                                                                             .access = vk::AccessFlagBits2::eShaderSampledRead,
                                                                         });
    return plan;
}

void Renderer::synchronizeSampledImageUpload(const nr::rhi::ops::ImageUploadTicket &uploadTicket, std::string_view debugName)
{
    nrAssert(static_cast<bool>(device_), "Renderer::synchronizeSampledImageUpload requires initialized device.");
    nrAssert(uploadTicket.valid(), std::format("{} upload ticket is invalid.", debugName));

    auto &uploadContext = device_->uploadReadback();
    auto const transferQueueFamily = device_->queueManager.transfer().queueFamilyIndex();
    auto const graphicsQueueFamily = device_->queueManager.graphics().queueFamilyIndex();
    if (transferQueueFamily == graphicsQueueFamily)
    {
        uploadContext.waitUploadComplete(uploadTicket.signalValue);
        uploadContext.reclaimCompletedUploads();
        return;
    }

    auto commandPool = nr::rhi::CommandPool{
        device_->device,
        graphicsQueueFamily,
        vk::CommandPoolCreateFlagBits::eTransient,
    };
    auto commandBuffers = commandPool.allocatePrimary(1);
    auto &commandBuffer = commandBuffers.front();

    nr::rhi::CommandRecorder::beginPrimary(commandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    uploadContext.recordImageAcquireBarrier(commandBuffer, uploadTicket);
    nr::rhi::CommandRecorder::end(commandBuffer);

    auto syncBatch = nr::rhi::CommandBatch{};
    syncBatch.addWait(uploadContext.uploadTimelineSemaphore(), vk::PipelineStageFlagBits2::eAllCommands, uploadTicket.signalValue);
    syncBatch.addCommandBuffer(commandBuffer);

    auto fence = vk::raii::Fence(device_->device, vk::FenceCreateInfo{});
    device_->queueManager.graphics().submit(std::move(syncBatch), std::cref(fence));
    auto const waitResult = device_->device.waitForFences(*fence, vk::True, std::numeric_limits<std::uint64_t>::max());
    nrAssert(waitResult == vk::Result::eSuccess, std::format("Renderer failed waiting for {} upload synchronization.", debugName));
    uploadContext.reclaimCompletedUploads();
}

void Renderer::uploadSceneTextureFallback()
{
    nrAssert(static_cast<bool>(device_), "Renderer::uploadSceneTextureFallback requires initialized device.");
    nrAssert(sceneTextureFallback_.valid(), "Renderer::uploadSceneTextureFallback requires a valid fallback image.");

    // Scene texture id 0 is the neutral default sampled for every unauthored material slot. It is
    // 1x1 linear white RGBA(1,1,1,1) so multiplicative sampling (base color, metallic-roughness,
    // emissive, clearcoat, sheen, transmission) resolves to a neutral factor of 1 instead of a
    // debug color. Genuinely invalid texture references are reported at load/scene residency.
    auto neutralPixel = std::array{
        static_cast<std::byte>(0xFFu),
        static_cast<std::byte>(0xFFu),
        static_cast<std::byte>(0xFFu),
        static_cast<std::byte>(0xFFu),
    };

    auto &uploadContext = device_->uploadReadback();
    auto const uploadPlan = makeSampledImageUploadPlan();
    auto uploadTicket = uploadContext.uploadImage(std::span<const std::byte>{neutralPixel}, sceneTextureFallback_, vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal, uploadPlan);
    nrAssert(uploadTicket.valid(), "Renderer failed to upload neutral scene texture fallback.");
    synchronizeSampledImageUpload(uploadTicket, "neutral scene texture fallback");
}

[[nodiscard]] RendererSceneTextureDescriptorTable Renderer::buildSceneTextureDescriptorTable(const NodeFrameParameters &frameParameters, const std::map<std::uint32_t, nr::resource::TextureHandle> &sceneTextureHandlesById)
{
    ensureSceneTextureFallback();
    return cacheSuite_.globalDescriptorTableCache.buildSceneTextureDescriptorTable(RendererSceneTextureDescriptorTableInput{
        .fallbackImage = std::cref(sceneTextureFallback_),
        .scene = frameParameters.scene,
        .sceneTextureHandlesById = sceneTextureHandlesById,
    });
}

Renderer::~Renderer() noexcept
{
    shutdown();
}

void Renderer::initialize(const RendererCreateInfo &info)
{
    if (device_)
    {
        return;
    }

    auto &shaderService = nr::rhi::ShaderService::instance();
    shaderService.configure();

    device_ = std::make_unique<nr::rhi::Device>();
    device_->initialize(info.appName, info.engineName, info.pipelineCache);
    frameUniformArena_.initialize(*device_, info.frameUniformBytesPerFrame, "Renderer.FrameUniformArena");
    submissionTimelines_.initialize(device_->device, 0);
    ensureSceneTextureFallback();
    ensureEnvironmentMapFallback();
}

void Renderer::setEnvironmentMap(nr::resource::EnvironmentMap environment)
{
    nrAssert(static_cast<bool>(device_), "Renderer::setEnvironmentMap requires initialize() first.");
    nrAssert(environment.valid(), "Renderer::setEnvironmentMap requires a valid RGBA16F environment resource.");

    device_->waitIdle();
    builder_.clear();
    executor_.clearRetainedState();
    cacheSuite_.clear();
    auto const &texture = environment.radiance;
    auto imageInfo = nr::rhi::makeImageCreateInfo(texture.format, vk::Extent2D{texture.width, texture.height}, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled);
    auto image = device_->resourceFactory.createImage(imageInfo, nr::rhi::MemoryUsage::GpuOnly, std::format("Renderer.EnvironmentMap.{}", texture.name));
    nrAssert(image.valid(), "Renderer::setEnvironmentMap failed to create the GPU image.");

    auto &uploadContext = device_->uploadReadback();
    auto uploadTicket = uploadContext.uploadImage(texture.levels.front().bytes, image, vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal, makeSampledImageUploadPlan());
    synchronizeSampledImageUpload(uploadTicket, "environment map");

    environmentMapImage_ = std::move(image);
    environmentMapParameters_ = EnvironmentMapParameters{
        .radianceDecodeScale = environment.radianceDecodeScale,
        .intensity = environment.intensity,
        .yawRadians = environment.yawRadians,
    };
    environmentMapState_ = RetainedImageState{
        .common =
            RetainedExternalResourceState{
                .initialized = true,
                .ownership = ResourceOwnershipDomain::Graphics,
                .access =
                    AccessScope{
                        .stages = vk::PipelineStageFlagBits2::eAllCommands,
                        .access = vk::AccessFlagBits2::eShaderSampledRead,
                    },
            },
        .layout = ImageLayoutIntent::ShaderReadOnly,
    };
    temporalHistoryResetPending_ = true;
}

[[nodiscard]] RendererGraphPreflightResult Renderer::preflightGraph(
    const RendererGraphSpec& spec) const
{
    if (!device_)
    {
        return RendererGraphPreflightResult{
            .message = "Renderer graph preflight requires initialize() first.",
        };
    }

    auto knownNames = std::set<std::string>{};
    auto knownSemantics = std::set<std::string>{};
    auto validationError = std::string{};
    auto nodeIndices = std::views::iota(std::size_t{0u}, spec.nodes.size());
    std::ranges::for_each(nodeIndices, [&](std::size_t nodeIndex) {
        if (!validationError.empty())
        {
            return;
        }

        auto const& createInfo = spec.nodes[nodeIndex];
        if (!createInfo.runtime)
        {
            validationError = std::format(
                "Renderer graph node {} has no runtime.",
                nodeIndex);
            return;
        }
        if (createInfo.config.instanceName.empty())
        {
            validationError = std::format(
                "Renderer graph node {} has an empty instance name.",
                nodeIndex);
            return;
        }
        if (!knownNames.emplace(createInfo.config.instanceName).second)
        {
            validationError = std::format(
                "Renderer graph contains duplicate node name '{}'.",
                createInfo.config.instanceName);
            return;
        }

        auto const semantic = createInfo.runtime->actionableSemantic();
        if (!semantic.empty() &&
            !knownSemantics.emplace(semantic).second)
        {
            validationError = std::format(
                "Renderer graph contains duplicate actionable semantic '{}'.",
                semantic);
        }
    });
    if (!validationError.empty())
    {
        return RendererGraphPreflightResult{
            .message = std::move(validationError),
        };
    }

    auto const invalidSubmit = std::ranges::find_if(
        spec.submitNodes,
        [&](const SubmitNodeSpec& submitSpec) {
            return submitSpec.afterNodeIndex >= spec.nodes.size();
        });
    if (invalidSubmit != spec.submitNodes.end())
    {
        return RendererGraphPreflightResult{
            .message = std::format(
                "Renderer graph submit '{}' references node index {} but the graph has {} node(s).",
                invalidSubmit->debugName,
                invalidSubmit->afterNodeIndex,
                spec.nodes.size()),
        };
    }

    auto optionBuilder = nr::options::OptionCatalogBuilder{};
    std::ranges::for_each(
        spec.nodes,
        [&](const NodeCreateInfo& createInfo) {
            createInfo.runtime->declareOptions(optionBuilder);
        });
    auto optionCatalog = optionBuilder.build();
    if (!optionCatalog.valid())
    {
        auto const& issue = optionCatalog.issues.front();
        return RendererGraphPreflightResult{
            .message = issue.id.has_value()
                           ? std::format(
                                 "Renderer graph option '{}' failed preflight: {}.",
                                 issue.id->value(),
                                 issue.detail)
                           : std::format(
                                 "Renderer graph option catalog failed preflight: {}.",
                                 issue.detail),
        };
    }
    auto const nonGraphOption = std::ranges::find_if(
        optionCatalog.catalog->definitions(),
        [](auto const& entry) {
            return entry.second.scope != nr::options::OptionScope::graph;
        });
    if (nonGraphOption != optionCatalog.catalog->definitions().end())
    {
        return RendererGraphPreflightResult{
            .message = std::format(
                "Renderer node option '{}' must use graph scope.",
                nonGraphOption->first.value()),
        };
    }

    if (!spec.frameResolutionOptionRequirements.empty() &&
        (!spec.frameResolutionResolver.has_value() ||
         !static_cast<bool>(*spec.frameResolutionResolver)))
    {
        return RendererGraphPreflightResult{
            .message =
                "Renderer graph declares frame-resolution option requirements without a resolver.",
        };
    }
    auto const missingResolverOption = std::ranges::find_if(
        spec.frameResolutionOptionRequirements,
        [&](const nr::options::OptionId& id) {
            return optionCatalog.catalog->find(id) == nullptr;
        });
    if (missingResolverOption != spec.frameResolutionOptionRequirements.end())
    {
        return RendererGraphPreflightResult{
            .message = std::format(
                "Frame resolution resolver requires undeclared graph option '{}'.",
                missingResolverOption->value()),
        };
    }

    return RendererGraphPreflightResult{
        .valid = true,
        .optionCatalog = std::move(optionCatalog.catalog),
    };
}

[[nodiscard]] bool Renderer::installGraph(const RendererGraphSpec &spec)
{
    auto const preflight = preflightGraph(spec);
    if (!preflight)
    {
        nr::nrLog(nr::LogLevel::error, "RENDERER", preflight.message);
        return false;
    }

    device_->waitIdle();
    teardownInstalledGraph();
    cacheSuite_.clear();

    auto installed = std::vector<InstalledNode>{};
    installed.reserve(spec.nodes.size());

    std::ranges::for_each(spec.nodes, [&](const NodeCreateInfo &createInfo) {
        auto initContext = NodeInitContext{
            .device = std::ref(*device_),
            .runtimeName = createInfo.config.instanceName,
        };
        createInfo.runtime->initialize(initContext);

        installed.push_back(InstalledNode{
            .runtime = createInfo.runtime,
            .config = createInfo.config,
        });
    });

    auto submitNodesByAfterIndex = std::multimap<std::size_t, SubmitNodeSpec>{};
    std::ranges::for_each(spec.submitNodes, [&](const SubmitNodeSpec &submitSpec) {
        submitNodesByAfterIndex.emplace(submitSpec.afterNodeIndex, submitSpec);
    });

    installedNodes_ = std::move(installed);
    submitNodesByAfterIndex_ = std::move(submitNodesByAfterIndex);
    cameraJitter_ = spec.cameraJitter;
    frameResolutionResolver_ = spec.frameResolutionResolver;
    previousGlobalFrameConstants_.reset();
    ++installedGraphGeneration_;
    observedSwapchainRecreationGeneration_ = device_->swapchainRecreationGeneration();
    graphInstalled_ = true;
    return true;
}

void Renderer::uninstallGraph()
{
    if (device_)
    {
        device_->waitIdle();
    }

    builder_.clear();
    executor_.clearRetainedState();
    cacheSuite_.clear();
    teardownInstalledGraph();
    cpuTimingAccumulator_ = {};
    cpuStatistics_ = {};
    gpuPassTimingAccumulator_.clear();
    gpuPassStatistics_ = {};
    previousGlobalFrameConstants_.reset();
    ++installedGraphGeneration_;
}

void Renderer::shutdown()
{
    if (!device_)
    {
        return;
    }

    device_->waitIdle();
    // Release frame-local graph callbacks and retained command buffers while Device is still valid.
    builder_.clear();
    executor_.clearRetainedState();
    cacheSuite_.clear();
    teardownInstalledGraph();
    submissionTimelines_ = RendererSubmissionTimelines{};
    frameUniformArena_ = FrameUniformArena{};
    sceneTextureFallback_ = nr::rhi::Image{};
    environmentMapImage_ = nr::rhi::Image{};
    environmentMapState_.reset();
    environmentMapParameters_ = EnvironmentMapParameters{};
    resetSceneBinding();
    cpuTimingAccumulator_ = {};
    cpuStatistics_ = {};
    gpuPassTimingAccumulator_.clear();
    gpuPassStatistics_ = {};
    device_.reset();
}

[[nodiscard]] bool Renderer::initialized() const noexcept
{
    return static_cast<bool>(device_);
}

[[nodiscard]] bool Renderer::graphInstalled() const noexcept
{
    return graphInstalled_;
}

void Renderer::resize()
{
    if (!device_)
    {
        return;
    }
    device_->recreateSwapchain();
    observedSwapchainRecreationGeneration_ = device_->swapchainRecreationGeneration();
    cacheSuite_.skeletonCache.clear(RenderGraphSkeletonMissReason::Invalidated);
    cacheSuite_.compileCache.clear();
    previousGlobalFrameConstants_.reset();
}

void Renderer::resetSceneBinding() noexcept
{
    activeSceneIdentity_.reset();
    tlasTextureCollectionKey_.reset();
    tlasTextureHandlesById_.clear();
    sceneExtractProfile_.reset();
    sceneTlasExtractProfile_.reset();
    previousGlobalFrameConstants_.reset();
}

void Renderer::collectOptionAvailability(
    const nr::options::OptionFrameSnapshot& snapshot,
    nr::options::OptionAvailabilityMap& availability) const
{
    std::ranges::for_each(
        installedNodes_,
        [&](const InstalledNode& installedNode) {
            installedNode.runtime->collectOptionAvailability(snapshot, availability);
        });
}

[[nodiscard]] RendererFrameResult Renderer::renderFrame(const RendererFrameInput &input)
{
    auto frameEffectSink = FrameEffectSink{input.optionSnapshot.get().effect};
    auto frameEffectTargetBatch = std::optional<std::uint32_t>{};
    auto frameEffectFrameSlot = std::optional<std::uint32_t>{};
    auto frameEffectTargetSubmitted = false;
    auto frameEffectFailureReason = std::string{"failed_before_submission"};
    auto frameEffectFinalizer = ScopeExit{[&]() noexcept {
        if (!frameEffectSink.effect().has_value())
        {
            return;
        }

        auto const& effect = *frameEffectSink.effect();
        auto disposition = FrameEffectFinalizeDisposition::terminalFailed;
        if (!frameEffectSink.claimed())
        {
            frameEffectFailureReason = "effect_not_claimed";
        }
        else if (!frameEffectFrameSlot.has_value())
        {
            frameEffectFailureReason = "failed_before_frame_begin";
        }
        else
        {
            if (frameEffectTargetSubmitted &&
                effect.id == nr::options::optionId(nr::options::keys::presentCaptureExr))
            {
                nr::options::emitMachineRecord(nr::options::OptionMachineRecord{
                    .sequence = effect.sequence,
                    .id = effect.id,
                    .phase = nr::options::OptionLogPhase::dispatchStarted,
                    .status = nr::options::OptionLogStatus::started,
                    .frameIndex = input.optionSnapshot.get().frameIndex,
                    .origin = effect.origin,
                    .requestId = effect.requestId,
                });
            }
            disposition = frameEffectSink.claimedRuntime()->get().finalizeFrameEffect(
                effect,
                frameEffectTargetSubmitted,
                *frameEffectFrameSlot);
            if (!frameEffectTargetSubmitted &&
                disposition == FrameEffectFinalizeDisposition::continuationArmed)
            {
                disposition = FrameEffectFinalizeDisposition::terminalFailed;
            }
            if (frameEffectTargetSubmitted &&
                disposition == FrameEffectFinalizeDisposition::terminalFailed)
            {
                frameEffectFailureReason = "effect_finalize_failed";
            }
        }

        if (disposition == FrameEffectFinalizeDisposition::continuationArmed)
        {
            return;
        }
        nr::options::emitMachineRecord(nr::options::OptionMachineRecord{
            .sequence = effect.sequence,
            .id = effect.id,
            .phase = nr::options::OptionLogPhase::terminal,
            .status =
                disposition == FrameEffectFinalizeDisposition::terminalSucceeded
                    ? nr::options::OptionLogStatus::succeeded
                    : nr::options::OptionLogStatus::failed,
            .frameIndex = input.optionSnapshot.get().frameIndex,
            .origin = effect.origin,
            .requestId = effect.requestId,
            .reason =
                disposition == FrameEffectFinalizeDisposition::terminalSucceeded
                    ? std::nullopt
                    : std::optional<std::string>{frameEffectFailureReason},
        });
    }};

    if (!device_ || !graphInstalled_)
    {
        return RendererFrameResult{};
    }

    auto const totalStart = std::chrono::steady_clock::now();
    auto const beginFrameStart = std::chrono::steady_clock::now();
    auto begin = device_->beginFrame();
    frameEffectFrameSlot = begin.frameIndex;
    std::ranges::for_each(
        installedNodes_,
        [&](InstalledNode& installedNode) {
            installedNode.runtime->advanceContinuations(begin.frameIndex);
        });
    auto invalidateForSwapchainRecreation = [&] {
        auto const generation = device_->swapchainRecreationGeneration();
        if (generation == observedSwapchainRecreationGeneration_)
        {
            return;
        }
        cacheSuite_.skeletonCache.clear(RenderGraphSkeletonMissReason::Invalidated);
        cacheSuite_.compileCache.clear();
        observedSwapchainRecreationGeneration_ = generation;
    };
    invalidateForSwapchainRecreation();
    auto const hasFrameResolutionResolver = frameResolutionResolver_.has_value() && static_cast<bool>(*frameResolutionResolver_);
    auto preAcquiredFrameImage = std::optional<nr::rhi::Device::FrameAcquireResult>{};
    if (hasFrameResolutionResolver)
    {
        preAcquiredFrameImage = device_->acquireFrameImage(input.acquireTimeout);
        invalidateForSwapchainRecreation();
    }
    auto const currentDisplayExtent = device_->presentationContext.swapchainExtent();
    nrAssert(currentDisplayExtent.width > 0u && currentDisplayExtent.height > 0u, "Renderer::renderFrame requires a non-zero display extent after beginFrame().");
    auto const displayExtent = sanitizeViewportExtent(currentDisplayExtent);
    auto resolutionPlan = FrameResolutionPlan{
        .displayExtent = displayExtent,
        .renderExtent = displayExtent,
    };
    if (hasFrameResolutionResolver)
    {
        resolutionPlan = (*frameResolutionResolver_)(
            *device_,
            displayExtent,
            input.optionSnapshot.get());
    }
    nrAssert(resolutionPlan.displayExtent.width > 0u && resolutionPlan.displayExtent.height > 0u && resolutionPlan.renderExtent.width > 0u && resolutionPlan.renderExtent.height > 0u, "Renderer::renderFrame resolution resolver returned a zero display or render extent.");
    nrAssert(resolutionPlan.displayExtent == displayExtent, "Renderer::renderFrame resolution resolver display extent does not match the current presentation extent.");
    resolutionPlan.resetHistory = resolutionPlan.resetHistory || temporalHistoryResetPending_;
    temporalHistoryResetPending_ = false;
    auto const sampleFrameOrdinal = sampleFrameOrdinal_;
    if (sampleFrameOrdinal_ < std::numeric_limits<std::uint64_t>::max())
    {
        ++sampleFrameOrdinal_;
    }
    frameUniformArena_.beginFrame(begin.frameIndex);
    auto cpuTimings = RendererCpuFrameTimings{
        .cpuWaitGpuMilliseconds = begin.cpuWaitGpuMilliseconds,
        .frameSetupMilliseconds = std::max(0.0, elapsedMilliseconds(beginFrameStart, std::chrono::steady_clock::now()) - begin.cpuWaitGpuMilliseconds),
    };

    auto scenePackets = std::optional<nr::scene::ScenePacketSet>{};
    auto sceneTlasPackets = std::optional<nr::scene::ScenePacketSet>{};
    auto primaryCamera = std::optional<nr::scene::SceneResolvedCamera>{};
    auto sceneBridgeFrame = std::optional<nr::scene::SceneBridgeFrame>{};
    auto sceneTextureHandlesById = std::map<std::uint32_t, nr::resource::TextureHandle>{};
    auto sceneExtractProfileCreated = false;
    auto sceneCameraOverride = input.cameraOverride;
    auto sceneBeginUploadMilliseconds = 0.0;
    auto sceneRasterExtractMilliseconds = 0.0;
    auto sceneTlasExtractMilliseconds = 0.0;
    auto sceneBridgeMilliseconds = 0.0;

    auto const sceneStart = std::chrono::steady_clock::now();
    if (input.scene.has_value())
    {
        auto &scene = input.scene->get();
        auto const sceneBeginUploadStart = std::chrono::steady_clock::now();
        scene.beginFrame(begin.frameIndex);
        scene.uploadPending();
        sceneBeginUploadMilliseconds = elapsedMilliseconds(sceneBeginUploadStart, std::chrono::steady_clock::now());

        auto [profile, created] = ensureSceneExtractProfile(scene);
        sceneExtractProfileCreated = created;
        auto [tlasProfile, tlasProfileCreated] = ensureSceneTlasExtractProfile(scene);
        sceneExtractProfileCreated = sceneExtractProfileCreated || tlasProfileCreated;

        auto extractInput = input.sceneExtractInput.value_or(nr::scene::SceneExtractInput{});
        if (!extractInput.viewportExtent.has_value())
        {
            extractInput.viewportExtent = glm::uvec2{displayExtent.width, displayExtent.height};
        }

        if (sceneCameraOverride.has_value())
        {
            extractInput.visibility = nr::scene::SceneVisibilityMode::customFrustum;
            extractInput.customFrustum = sceneCameraOverride->frustum;
        }

        auto tlasExtractInput = extractInput;
        tlasExtractInput.visibility = nr::scene::SceneVisibilityMode::none;
        tlasExtractInput.customFrustum.reset();

        auto const rasterExtractStart = std::chrono::steady_clock::now();
        scenePackets = scene.extractPackets(profile, extractInput);
        sceneRasterExtractMilliseconds = elapsedMilliseconds(rasterExtractStart, std::chrono::steady_clock::now());
        auto const tlasExtractStart = std::chrono::steady_clock::now();
        sceneTlasPackets = scene.extractPackets(tlasProfile, tlasExtractInput);
        sceneTlasExtractMilliseconds = elapsedMilliseconds(tlasExtractStart, std::chrono::steady_clock::now());
        if (!sceneCameraOverride.has_value())
        {
            primaryCamera = scene.tryGetPrimaryCamera(extractInput.viewportExtent);
        }

        auto bridgeBuildInput = nr::scene::SceneRenderBridgeBuildInput{
            .packetSet = std::cref(*scenePackets),
        };
        auto rasterGeometryBuffers = scene.tryGetRasterGeometryBuffers();
        bridgeBuildInput.resolveGeometryBuffers = [rasterGeometryBuffers]() -> std::optional<nr::scene::SceneBridgeGeometryBuffers> { return rasterGeometryBuffers; };

        bridgeBuildInput.resolveMaterialTextures = [&](nr::resource::MaterialHandle materialHandle) -> std::optional<nr::scene::SceneMaterialTextureBindings> { return collectSceneMaterialTextures(scene, materialHandle, sceneTextureHandlesById); };

        if (sceneCameraOverride.has_value())
        {
            bridgeBuildInput.frameConstantsOverride = sceneCameraOverride->frameConstants;
        }

        bridgeBuildInput.resolveMaterialRasterState = [&](nr::resource::MaterialHandle materialHandle) -> std::optional<nr::scene::SceneBridgeMaterialRasterState> {
            auto materialRecordRef = scene.tryGetMaterialAsset(materialHandle);
            if (!materialRecordRef.has_value())
            {
                return std::nullopt;
            }

            auto const &materialRecord = materialRecordRef->get();
            if (!materialRecord.cpuReady)
            {
                return std::nullopt;
            }

            auto const doubleSided = materialRecord.cpu.core.doubleSided;
            return nr::scene::SceneBridgeMaterialRasterState{
                .cullMode = doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack,
                .doubleSided = doubleSided,
            };
        };

        bridgeBuildInput.resolveRasterDrawGeometry = [&](nr::resource::MeshHandle meshHandle, std::uint32_t geometryIndex) -> std::optional<nr::scene::SceneBridgeDrawGeometry> {
            auto meshRecordRef = scene.tryGetMeshAsset(meshHandle);
            if (!meshRecordRef.has_value())
            {
                return std::nullopt;
            }

            auto const &meshRecord = meshRecordRef->get();
            if (!meshRecord.cpuReady || !meshRecord.gpu.has_value())
            {
                return std::nullopt;
            }

            if (!rasterGeometryBuffers.has_value() || !rasterGeometryBuffers->hasVertexBuffer())
            {
                return std::nullopt;
            }

            if (geometryIndex >= meshRecord.cpu.geometries.size())
            {
                return std::nullopt;
            }

            auto const &meshGeometry = meshRecord.cpu.geometries[geometryIndex];
            auto const &atlas = meshRecord.gpu->atlas;
            auto checkedAddUint32 = [](std::uint32_t base, std::uint32_t offset, std::string_view label) {
                auto const value = static_cast<std::uint64_t>(base) + static_cast<std::uint64_t>(offset);
                nrAssert(value <= std::numeric_limits<std::uint32_t>::max(), std::format("{} value {} exceeds uint32_t range.", label, value));
                return static_cast<std::uint32_t>(value);
            };
            auto checkedAddInt32 = [](std::uint32_t base, std::uint32_t offset, std::string_view label) {
                auto const value = static_cast<std::uint64_t>(base) + static_cast<std::uint64_t>(offset);
                nrAssert(value <= static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()), std::format("{} value {} exceeds int32_t range.", label, value));
                return static_cast<std::int32_t>(value);
            };

            auto drawGeometry = nr::scene::SceneBridgeDrawGeometry{};
            drawGeometry.vertexBuffer = rasterGeometryBuffers->vertexBuffer;
            drawGeometry.frontFace = meshRecord.cpu.clockwiseFrontFace ? vk::FrontFace::eClockwise : vk::FrontFace::eCounterClockwise;

            auto const indexedGeometry = atlas.indexCount > 0u;
            if (indexedGeometry)
            {
                if (!rasterGeometryBuffers->hasIndexBuffer())
                {
                    return std::nullopt;
                }

                drawGeometry.indexBuffer = rasterGeometryBuffers->indexBuffer;
                drawGeometry.indexType = rasterGeometryBuffers->indexType;
                drawGeometry.firstIndex = checkedAddUint32(atlas.indexBase, meshGeometry.firstIndex, "Scene raster draw firstIndex");
                drawGeometry.indexCount = meshGeometry.indexCount > 0 ? meshGeometry.indexCount : atlas.indexCount;
                drawGeometry.vertexOffset = checkedAddInt32(atlas.vertexBase, meshGeometry.vertexOffset, "Scene raster draw vertexOffset");
                return drawGeometry;
            }

            drawGeometry.firstVertex = checkedAddUint32(atlas.vertexBase, meshGeometry.firstIndex, "Scene raster draw firstVertex");
            drawGeometry.vertexCount = meshGeometry.indexCount > 0 ? meshGeometry.indexCount : atlas.vertexCount;
            return drawGeometry;
        };

        if (primaryCamera.has_value())
        {
            bridgeBuildInput.primaryCamera = std::cref(*primaryCamera);
        }

        auto const sceneBridgeStart = std::chrono::steady_clock::now();
        sceneBridgeFrame = nr::scene::SceneRenderBridge::buildFrame(bridgeBuildInput);
        sceneBridgeMilliseconds = elapsedMilliseconds(sceneBridgeStart, std::chrono::steady_clock::now());
    }
    cpuTimings.sceneMilliseconds = elapsedMilliseconds(sceneStart, std::chrono::steady_clock::now());
    auto const postSceneStart = std::chrono::steady_clock::now();
    auto tlasTextureCollectionMilliseconds = 0.0;

    auto frameParameters = NodeFrameParameters{
        .optionSnapshot = input.optionSnapshot,
    };
    frameParameters.frameIndex = begin.frameIndex;
    frameParameters.swapchainExtent = displayExtent;
    frameParameters.resolutionPlan = resolutionPlan;
    frameParameters.swapchainFormat = device_->presentationContext.swapchainFormat();
    frameParameters.swapchainColorSpace = device_->presentationContext.swapchainColorSpace();
    frameParameters.frameEffectSink = std::ref(frameEffectSink);
    if (input.frameServices.has_value())
    {
        frameParameters.frameServices = input.frameServices;
    }

    if (input.scene.has_value())
    {
        frameParameters.scene = std::cref(input.scene->get());
    }

    if (scenePackets.has_value())
    {
        frameParameters.scenePackets = std::cref(*scenePackets);
    }

    if (sceneTlasPackets.has_value())
    {
        frameParameters.sceneTlasBuildInputs = std::cref(sceneTlasPackets->tlasBuildInputs);
        frameParameters.sceneRevisions = sceneTlasPackets->revisions;
    }

    if (input.scene.has_value() && sceneTlasPackets.has_value())
    {
        auto const tlasTextureCollectionStart = std::chrono::steady_clock::now();
        auto const packets = std::span<const nr::scene::TlasBuildInputPacket>{sceneTlasPackets->tlasBuildInputs};
        auto key = makeTlasTextureCollectionKey(sceneTlasPackets->revisions, packets);
        if (!tlasTextureCollectionKey_.has_value() || *tlasTextureCollectionKey_ != key)
        {
            auto collected = std::map<std::uint32_t, nr::resource::TextureHandle>{};
            collectTlasSceneTextureHandles(input.scene->get(), packets, collected);
            tlasTextureHandlesById_ = std::move(collected);
            tlasTextureCollectionKey_ = std::move(key);
        }
        std::ranges::for_each(tlasTextureHandlesById_, [&](const auto &entry) { sceneTextureHandlesById.insert_or_assign(entry.first, entry.second); });
        tlasTextureCollectionMilliseconds = elapsedMilliseconds(tlasTextureCollectionStart, std::chrono::steady_clock::now());
    }

    if (primaryCamera.has_value())
    {
        frameParameters.primaryCamera = std::cref(*primaryCamera);
    }

    auto sceneBridgeFrameRef = std::optional<std::reference_wrapper<const nr::scene::SceneBridgeFrame>>{};
    if (sceneBridgeFrame.has_value())
    {
        sceneBridgeFrameRef = std::cref(*sceneBridgeFrame);
    }

    auto globalFrameConstants = nr::scene::SceneBridgeFrameConstants{};
    if (sceneBridgeFrame.has_value())
    {
        globalFrameConstants = sceneBridgeFrame->frameConstants;
    }
    else if (sceneCameraOverride.has_value())
    {
        globalFrameConstants = sceneCameraOverride->frameConstants;
    }
    frameParameters.renderCameraConstants = globalFrameConstants;

    auto const cameraFrameState = makeRendererCameraFrameState(cameraJitter_, sampleFrameOrdinal, frameParameters.resolutionPlan.renderExtent);
    auto renderingFrameConstants = globalFrameConstants;
    if (cameraFrameState.jitterEnabled)
    {
        renderingFrameConstants.projection = applyCameraProjectionJitter(globalFrameConstants.projection, cameraFrameState.jitter.ndcOffset);
        renderingFrameConstants.viewProjection = renderingFrameConstants.projection * renderingFrameConstants.view;
    }

    auto const buildStart = std::chrono::steady_clock::now();
    cpuTimings.postSceneMilliseconds = elapsedMilliseconds(postSceneStart, buildStart);
    auto const graphBuildTimings = buildInstalledGraph(frameParameters, renderingFrameConstants, cameraFrameState, sampleFrameOrdinal, sceneBridgeFrameRef, sceneTextureHandlesById);
    cpuTimings.buildMilliseconds = elapsedMilliseconds(buildStart, std::chrono::steady_clock::now());

    auto const compileStart = std::chrono::steady_clock::now();
    auto compiled = cacheSuite_.compileCache.compileConsumingCached(builder_.mutableFrame());
    if (frameEffectSink.claimed())
    {
        auto const targetPass = frameEffectSink.targetPass();
        std::ranges::for_each(
            compiled.submitBatches,
            [&](const CompiledSubmitBatch& batch) {
                if (std::ranges::any_of(
                        batch.passes,
                        [&](const CompiledPass& pass) {
                            return pass.handle == targetPass;
                        }))
                {
                    nrAssert(
                        !frameEffectTargetBatch.has_value(),
                        "A frame-effect target pass must belong to exactly one compiled submit batch.");
                    frameEffectTargetBatch = batch.batchIndex;
                }
            });
        if (!frameEffectTargetBatch.has_value())
        {
            frameEffectFailureReason = "target_pass_not_compiled";
        }
    }
    cpuTimings.compileMilliseconds = elapsedMilliseconds(compileStart, std::chrono::steady_clock::now());

    auto const benchmarkCapturing = benchmarkPhase_ == RendererBenchmarkPhase::measure;
    auto executorBenchmarkTelemetry = ExecutorBenchmarkTelemetry{};
    auto executeContext = RenderGraphExecutor::ExecuteContext{
        .device = *device_,
        .frameIndex = begin.frameIndex,
        .frameOrdinal = sampleFrameOrdinal,
        .acquireTimeout = input.acquireTimeout,
        .preAcquiredFrameImage = preAcquiredFrameImage,
        .submissionTimelines = submissionTimelines_.valid() ? std::optional<std::reference_wrapper<RendererSubmissionTimelines>>(std::ref(submissionTimelines_)) : std::nullopt,
        .benchmarkTelemetry = benchmarkCapturing ? std::optional<std::reference_wrapper<ExecutorBenchmarkTelemetry>>(std::ref(executorBenchmarkTelemetry)) : std::nullopt,
    };

    auto const prepareStart = std::chrono::steady_clock::now();
    auto prepared = executor_.prepareFrame(std::move(compiled), executeContext);
    cpuTimings.prepareMilliseconds = elapsedMilliseconds(prepareStart, std::chrono::steady_clock::now());

    auto const executeStart = std::chrono::steady_clock::now();
    auto executeReport = executor_.executePrepared(prepared, executeContext);
    if (frameEffectTargetBatch.has_value())
    {
        frameEffectTargetSubmitted = std::ranges::contains(
            executeReport.submittedCompiledBatchIndices,
            *frameEffectTargetBatch);
    }
    nrAssert(executeReport.swapchainImageIndex.has_value(), "Renderer::renderFrame expected the graph to acquire one swapchain image before presentation.");
    cpuTimings.executeMilliseconds = elapsedMilliseconds(executeStart, std::chrono::steady_clock::now());
    if (executeReport.completedGpuPassTimingFrame.has_value())
    {
        recordGpuPassTimingSample(*executeReport.completedGpuPassTimingFrame);
        recordBenchmarkGpuPassTimings(*executeReport.completedGpuPassTimingFrame);
    }

    auto const presentStart = std::chrono::steady_clock::now();
    auto present = device_->presentFrame();
    cpuTimings.presentMilliseconds = elapsedMilliseconds(presentStart, std::chrono::steady_clock::now());
    cpuTimings.totalMilliseconds = elapsedMilliseconds(totalStart, std::chrono::steady_clock::now());
    if (benchmarkCapturing)
    {
        auto const executeAccountedMainThreadMilliseconds = rendererBenchmarkExecuteAccountedMainThreadMilliseconds(executorBenchmarkTelemetry);
        auto const executeResidualMilliseconds = cpuTimings.executeMilliseconds - executeAccountedMainThreadMilliseconds;
        benchmarkFrames_.push_back(RendererBenchmarkFrame{
            .frameOrdinal = sampleFrameOrdinal,
            .frameSlot = begin.frameIndex % nr::maxFrameInFlight,
            .displayExtent = resolutionPlan.displayExtent,
            .renderExtent = resolutionPlan.renderExtent,
            .cpu = cpuTimings,
            .sceneBeginUploadMilliseconds = sceneBeginUploadMilliseconds,
            .sceneRasterExtractMilliseconds = sceneRasterExtractMilliseconds,
            .sceneTlasExtractMilliseconds = sceneTlasExtractMilliseconds,
            .sceneBridgeMilliseconds = sceneBridgeMilliseconds,
            .tlasTextureCollectionMilliseconds = tlasTextureCollectionMilliseconds,
            .graphPreludeMilliseconds = graphBuildTimings.preludeMilliseconds,
            .uiCollectMilliseconds = graphBuildTimings.uiCollectMilliseconds,
            .nodeLoopMilliseconds = graphBuildTimings.nodeLoopMilliseconds,
            .skeletonPatchMilliseconds =
                graphBuildTimings.skeletonPatchMilliseconds,
            .skeletonRebuildMilliseconds =
                graphBuildTimings.skeletonRebuildMilliseconds,
            .skeletonHit = graphBuildTimings.skeletonHit,
            .skeletonMissReason = graphBuildTimings.skeletonMissReason,
            .execute = executorBenchmarkTelemetry,
            .executeAccountedMainThreadMilliseconds = executeAccountedMainThreadMilliseconds,
            .executeUnclassifiedMilliseconds = executeResidualMilliseconds >= -0.001 ? std::max(0.0, executeResidualMilliseconds) : executeResidualMilliseconds,
            .sceneRasterPacketCount = scenePackets.has_value() ? scenePackets->rasterDraws.size() : 0u,
            .sceneRtPacketCount = scenePackets.has_value() ? scenePackets->rtInstances.size() : 0u,
            .sceneTlasPacketCount = sceneTlasPackets.has_value() ? sceneTlasPackets->tlasBuildInputs.size() : 0u,
            .submitBatchCount = executeReport.submittedBatchCount,
            .recordTaskCount = executeReport.submittedRecordTaskCount,
        });
        benchmarkNodeBuildMilliseconds_.insert(benchmarkNodeBuildMilliseconds_.end(), benchmarkCurrentNodeBuildMilliseconds_.begin(), benchmarkCurrentNodeBuildMilliseconds_.end());
        benchmarkAsTelemetry_.push_back(benchmarkCurrentAsTelemetry_);
        if (benchmarkFrames_.size() >= benchmarkConfig_.measureFrames)
        {
            benchmarkPhase_ = RendererBenchmarkPhase::drain;
        }
    }
    else if (benchmarkPhase_ == RendererBenchmarkPhase::warmup)
    {
        ++benchmarkWarmupAccepted_;
        if (benchmarkWarmupAccepted_ >= benchmarkConfig_.warmupFrames)
        {
            benchmarkPhase_ = RendererBenchmarkPhase::measure;
        }
    }
    else if (benchmarkPhase_ == RendererBenchmarkPhase::drain)
    {
        ++benchmarkDrainRendered_;
        if (benchmarkDrainRendered_ >= nr::maxFrameInFlight)
        {
            benchmarkPhase_ = RendererBenchmarkPhase::finalized;
        }
    }

    recordCpuTimingSample(cpuTimings);

    return RendererFrameResult{
        .rendered = true,
        .frameIndex = begin.frameIndex,
        .swapchainImageIndex = *executeReport.swapchainImageIndex,
        .presentResult = present.result,
        .compiledSubmitBatchCount = prepared.compiled.submitBatches.size(),
        .submittedBatchCount = executeReport.submittedBatchCount,
        .invokedPassPrepareCount = executeReport.invokedPassPrepareCount,
        .invokedPassRecordCount = executeReport.invokedPassRecordCount,
        .replayedSecondaryCommandBufferCount = executeReport.replayedSecondaryCommandBufferCount,
        .appliedInPassBarrierCount = executeReport.appliedInPassBarrierCount,
        .appliedAcquireBarrierCount = executeReport.appliedAcquireBarrierCount,
        .appliedReleaseBarrierCount = executeReport.appliedReleaseBarrierCount,
        .syntheticPresentBatchUsed = executeReport.plan.requiresSyntheticPresentBatch,
        .usedScenePath = input.scene.has_value(),
        .usedCameraOverride = sceneCameraOverride.has_value(),
        .sceneExtractProfileCreated = sceneExtractProfileCreated,
        .sceneBridgeDrawCount = sceneBridgeFrame.has_value() ? sceneBridgeFrame->rasterDraws.size() : 0,
        .sceneRasterPacketCount = scenePackets.has_value() ? scenePackets->rasterDraws.size() : 0,
        .sceneRtPacketCount = scenePackets.has_value() ? scenePackets->rtInstances.size() : 0,
        .sceneTlasPacketCount = sceneTlasPackets.has_value() ? sceneTlasPackets->tlasBuildInputs.size() : 0,
        .cpuStatistics = cpuStatistics_,
        .gpuPassStatistics = gpuPassStatistics_,
    };
}

[[nodiscard]] nr::rhi::Device &Renderer::device()
{
    return *device_;
}

[[nodiscard]] const nr::rhi::Device &Renderer::device() const
{
    return *device_;
}

[[nodiscard]] RenderGraphExecutor &Renderer::graphExecutor() noexcept
{
    return executor_;
}

[[nodiscard]] const RenderGraphExecutor &Renderer::graphExecutor() const noexcept
{
    return executor_;
}

[[nodiscard]] const RendererCpuStatistics &Renderer::cpuStatistics() const noexcept
{
    return cpuStatistics_;
}

[[nodiscard]] const RendererGpuPassStatistics &Renderer::gpuPassStatistics() const noexcept
{
    return gpuPassStatistics_;
}

[[nodiscard]] RendererGraphBuildTimings Renderer::buildInstalledGraph(const NodeFrameParameters &frameParameters, const nr::scene::SceneBridgeFrameConstants &frameConstants, const RendererCameraFrameState &cameraFrameState, std::uint64_t sampleFrameOrdinal,
                                                                      std::optional<std::reference_wrapper<const nr::scene::SceneBridgeFrame>> sceneBridgeFrame, const std::map<std::uint32_t, nr::resource::TextureHandle> &sceneTextureHandlesById)
{
    nrAssert(graphInstalled_, "Renderer::buildInstalledGraph requires installGraph() before rendering.");

    auto telemetry = std::optional<RendererBenchmarkBuildTelemetry>{};
    if (benchmarkPhase_ == RendererBenchmarkPhase::measure)
    {
        std::ranges::fill(benchmarkCurrentNodeBuildMilliseconds_, 0.0);
        benchmarkCurrentAsTelemetry_ = {};
        telemetry = RendererBenchmarkBuildTelemetry{
            .nodeBuildMilliseconds = benchmarkCurrentNodeBuildMilliseconds_,
            .accelerationStructure = std::ref(benchmarkCurrentAsTelemetry_),
        };
    }
    auto const graphPreludeStart = telemetry.has_value() ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    auto const previousFrameConstants = previousGlobalFrameConstants_.value_or(frameConstants);
    auto const globalFrameUniforms = makeGlobalFrameUniforms(frameConstants, previousFrameConstants, frameParameters.frameIndex, sampleFrameOrdinal);
    previousGlobalFrameConstants_ = frameConstants;
    nrAssert(environmentMapImage_.valid() && environmentMapState_.common.initialized, "Renderer::buildInstalledGraph requires a resident environment map.");
    auto sceneTextureDescriptorTable = buildSceneTextureDescriptorTable(frameParameters, sceneTextureHandlesById);
    auto nodeFrameParameters = frameParameters;
    nodeFrameParameters.benchmarkTelemetry = telemetry.has_value()
                                                 ? std::optional<std::reference_wrapper<RendererBenchmarkBuildTelemetry>>{
                                                       std::ref(*telemetry)}
                                                 : std::nullopt;

    auto const nodeBuildStart = telemetry.has_value() ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};

    auto structuralSnapshots = std::vector<NodeRuntime::StructuralSnapshot>{};
    structuralSnapshots.reserve(installedNodes_.size());
    auto skeletonEligible = renderGraphSkeletonMode_ != RenderGraphSkeletonMode::Legacy;
    std::ranges::for_each(installedNodes_, [&](const InstalledNode& installedNode) {
        if (!skeletonEligible)
        {
            return;
        }
        auto snapshot = installedNode.runtime->structuralSnapshot(nodeFrameParameters);
        if (!snapshot.has_value())
        {
            skeletonEligible = false;
            structuralSnapshots.clear();
            return;
        }
        structuralSnapshots.push_back(std::move(*snapshot));
    });

    auto skeletonKey = RenderGraphSkeletonKey{};
    auto skeletonKeyHit = false;
    if (skeletonEligible)
    {
        skeletonKey.installedGraphGeneration = installedGraphGeneration_;
        skeletonKey.displayExtent = frameParameters.resolutionPlan.displayExtent;
        skeletonKey.renderExtent = frameParameters.resolutionPlan.renderExtent;
        skeletonKey.swapchainExtent = frameParameters.swapchainExtent;
        skeletonKey.swapchainFormat = frameParameters.swapchainFormat;
        skeletonKey.swapchainColorSpace = frameParameters.swapchainColorSpace;
        skeletonKey.shaderSessionGeneration = device_->shaderCompiler().sessionGeneration();
        skeletonKey.swapchainRecreationGeneration = device_->swapchainRecreationGeneration();
        skeletonKey.submitAcquirePolicyRevision = installedGraphGeneration_;
        skeletonKey.hasSceneBridgeFrame = sceneBridgeFrame.has_value();
        auto nodeOrdinalsForKey = std::views::iota(std::size_t{0}, installedNodes_.size());
        std::ranges::for_each(nodeOrdinalsForKey, [&](std::size_t nodeIndex) {
            skeletonKey.nodes.push_back(RenderGraphSkeletonNodeKey{
                .configurationRevision = installedNodes_[nodeIndex].config.configurationRevision,
                .runtimeConfigurationRevision = structuralSnapshots[nodeIndex].configurationRevision,
                .structuralBranchKey = structuralSnapshots[nodeIndex].branchKey,
            });
        });
        skeletonKeyHit = cacheSuite_.skeletonCache.contains(skeletonKey);
    }
    else
    {
        cacheSuite_.skeletonCache.recordMiss(
            renderGraphSkeletonMode_ == RenderGraphSkeletonMode::Legacy
                ? RenderGraphSkeletonMissReason::Disabled
                : RenderGraphSkeletonMissReason::UnsupportedNode);
    }

    auto timings = RendererGraphBuildTimings{};
    auto skeletonProbe = RenderGraphSkeletonCache::ProbeResult{};
    auto patched = false;
    auto patchFailed = false;
    if (skeletonEligible && skeletonKeyHit && renderGraphSkeletonMode_ == RenderGraphSkeletonMode::Enabled)
    {
        auto const patchStart = std::chrono::steady_clock::now();
        auto skeleton = cacheSuite_.skeletonCache.lookup(skeletonKey);
        nrAssert(static_cast<bool>(skeleton), "Renderer Skeleton key hit must resolve an owned template.");
        if (skeleton->nodePatchLayouts.size() == installedNodes_.size())
        {
            auto currentFrame = RenderGraphSkeletonCache::instantiate(*skeleton);
            auto namedFrameResources = skeleton->namedFrameResources;
            auto namedFrameData = skeleton->namedFrameData;
            auto globalPatch = RenderGraphSkeletonPatchContext{
                currentFrame,
                skeleton->globalPatchLayout,
                namedFrameResources,
                namedFrameData,
            };
            globalPatch.patchResource(0u, GraphImportedImageDesc{
                .debugName = "Renderer.EnvironmentMap",
                .lifetime = ResourceLifetime::RendererPersistent,
                .initialOwnership = environmentMapState_.common.ownership,
                .extent = environmentMapImage_.extent(),
                .format = environmentMapImage_.format(),
                .usageIntents = {ImageUsageIntent::Sampled},
                .initialLayout = environmentMapState_.layout,
                .initialAccessScope = environmentMapState_.common.access,
                .importedResource = std::cref(environmentMapImage_),
                .retainedState = std::ref(environmentMapState_),
            });
            auto const frameUniform = frameUniformArena_.patchUploadBytes(
                globalPatch,
                1u,
                "Renderer.GlobalFrameUniforms",
                std::as_bytes(std::span{std::addressof(globalFrameUniforms), std::size_t{1u}}));
            auto const globalResources = FrameGlobalResources{
                .frameUniform = frameUniform,
                .environmentMap = globalPatch.namedResource("Renderer.EnvironmentMap"),
                .environmentMapParameters = environmentMapParameters_,
                .sceneTextureDescriptorsById = sceneTextureDescriptorTable.descriptorsById,
                .sceneTextureDescriptorVersion = sceneTextureDescriptorTable.version,
                .bindlessImageTableCache = std::ref(cacheSuite_.bindlessImageTableCache),
                .cameraFrameState = cameraFrameState,
            };
            if (sceneBridgeFrame.has_value())
            {
                globalPatch.patchFrameData(0u, "SceneBridgeFrame", std::make_any<nr::scene::SceneBridgeFrame>(sceneBridgeFrame->get()));
                nodeFrameParameters.sceneBridgeFrameHandle = globalPatch.namedFrameData("SceneBridgeFrame");
            }
            patched = true;
            auto nodeOrdinals = std::views::iota(std::size_t{0}, installedNodes_.size());
            std::ranges::for_each(nodeOrdinals, [&](std::size_t nodeIndex) {
                if (!patched)
                {
                    return;
                }
                auto nodePatch = RenderGraphSkeletonPatchContext{
                    currentFrame,
                    skeleton->nodePatchLayouts[nodeIndex],
                    namedFrameResources,
                    namedFrameData,
                    std::addressof(globalResources),
                };
                auto const nodeStart = telemetry.has_value() ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
                patched = installedNodes_[nodeIndex].runtime->materializeRenderGraphSkeleton(
                    nodePatch,
                    nodeFrameParameters,
                    structuralSnapshots[nodeIndex]);
                if (telemetry.has_value())
                {
                    telemetry->nodeBuildMilliseconds[nodeIndex] = elapsedMilliseconds(nodeStart, std::chrono::steady_clock::now());
                }
            });
            if (patched)
            {
                builder_.clear();
                builder_.mutableFrame() = std::move(currentFrame);
                cacheSuite_.skeletonCache.recordHit();
                skeletonProbe = RenderGraphSkeletonCache::ProbeResult{
                    .keyHit = true,
                    .structureMatches = true,
                };
                timings.skeletonPatchMilliseconds = elapsedMilliseconds(patchStart, std::chrono::steady_clock::now());
            }
        }
        patchFailed = !patched;
        if (patchFailed)
        {
            cacheSuite_.skeletonCache.recordMiss(RenderGraphSkeletonMissReason::PatchFailed);
        }
    }

    if (!patched)
    {
        auto const coldBuildStart = std::chrono::steady_clock::now();
        builder_.clear();
        auto capture = RenderGraphSkeletonCapture{};
        auto frameResources = std::map<std::string, GraphResourceHandle>{};
        auto frameDataResources = std::map<std::string, GraphFrameDataHandle>{};
        auto const globalResourceBegin = builder_.frame().resources.size();
        auto const globalFrameDataBegin = builder_.frame().frameData.size();
        auto const environmentMap = builder_.addResource(GraphImportedImageDesc{
            .debugName = "Renderer.EnvironmentMap",
            .lifetime = ResourceLifetime::RendererPersistent,
            .initialOwnership = environmentMapState_.common.ownership,
            .extent = environmentMapImage_.extent(),
            .format = environmentMapImage_.format(),
            .usageIntents = {ImageUsageIntent::Sampled},
            .initialLayout = environmentMapState_.layout,
            .initialAccessScope = environmentMapState_.common.access,
            .importedResource = std::cref(environmentMapImage_),
            .retainedState = std::ref(environmentMapState_),
        });
        auto globalResources = FrameGlobalResources{
            .frameUniform = frameUniformArena_.upload(builder_, "Renderer.GlobalFrameUniforms", globalFrameUniforms),
            .environmentMap = environmentMap,
            .environmentMapParameters = environmentMapParameters_,
            .sceneTextureDescriptorsById = std::move(sceneTextureDescriptorTable.descriptorsById),
            .sceneTextureDescriptorVersion = sceneTextureDescriptorTable.version,
            .bindlessImageTableCache = std::ref(cacheSuite_.bindlessImageTableCache),
            .cameraFrameState = cameraFrameState,
        };
        frameResources.emplace("Renderer.EnvironmentMap", environmentMap);
        frameResources.emplace("Renderer.GlobalFrameUniforms", globalResources.frameUniform.resource);
        if (sceneBridgeFrame.has_value())
        {
            nodeFrameParameters.sceneBridgeFrameHandle = builder_.addFrameData("SceneBridgeFrame", sceneBridgeFrame->get());
            frameDataResources.emplace("SceneBridgeFrame", *nodeFrameParameters.sceneBridgeFrameHandle);
        }
        capture.globalPatchLayout = RenderGraphSkeletonNodePatchLayout{
            .resourceBegin = globalResourceBegin,
            .resourceCount = builder_.frame().resources.size() - globalResourceBegin,
            .frameDataBegin = globalFrameDataBegin,
            .frameDataCount = builder_.frame().frameData.size() - globalFrameDataBegin,
        };

        auto nodeOrdinals = std::views::iota(std::size_t{0}, installedNodes_.size());
        std::ranges::for_each(nodeOrdinals, [&](std::size_t nodeIndex) {
            auto& installedNode = installedNodes_[nodeIndex];
            auto const nodeStart = telemetry.has_value() ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
            auto const resourceBegin = builder_.frame().resources.size();
            auto const frameDataBegin = builder_.frame().frameData.size();
            auto const passBegin = builder_.frame().passes.size();
            auto nodeHandle = builder_.addNode(installedNode.config.instanceName, installedNode.config.queue);
            if (telemetry.has_value())
            {
                telemetry->nodeOrdinal = nodeIndex;
            }
            auto buildContext = NodeBuildContext{
                .graphBuilder = std::ref(builder_),
                .nodeHandle = nodeHandle,
                .queue = installedNode.config.queue,
                .frameIndex = frameParameters.frameIndex,
                .runtimeName = installedNode.config.instanceName,
                .globalResources = std::cref(globalResources),
                .frameResources = std::ref(frameResources),
                .frameDataResources = std::ref(frameDataResources),
                .benchmarkTelemetry = telemetry.has_value()
                                          ? std::optional<std::reference_wrapper<RendererBenchmarkBuildTelemetry>>{std::ref(*telemetry)}
                                          : std::nullopt,
            };
            installedNode.runtime->build(buildContext, nodeFrameParameters);
            capture.nodePatchLayouts.push_back(RenderGraphSkeletonNodePatchLayout{
                .queue = installedNode.config.queue,
                .resourceBegin = resourceBegin,
                .resourceCount = builder_.frame().resources.size() - resourceBegin,
                .frameDataBegin = frameDataBegin,
                .frameDataCount = builder_.frame().frameData.size() - frameDataBegin,
                .passBegin = passBegin,
                .passCount = builder_.frame().passes.size() - passBegin,
            });
            if (telemetry.has_value())
            {
                telemetry->nodeBuildMilliseconds[nodeIndex] = elapsedMilliseconds(nodeStart, std::chrono::steady_clock::now());
            }
            auto boundaries = submitNodesByAfterIndex_.equal_range(nodeIndex);
            std::ranges::for_each(std::ranges::subrange(boundaries.first, boundaries.second), [&](const auto& entry) {
                auto debugName = entry.second.debugName.empty() ? std::format("Submit.After.{}", installedNode.config.instanceName) : entry.second.debugName;
                nrAssert(builder_.addSubmitNode(debugName).valid(), "Renderer::buildInstalledGraph failed to add a valid submit node.");
            });
        });
        capture.namedFrameResources = std::move(frameResources);
        capture.namedFrameData = std::move(frameDataResources);
        if (skeletonEligible)
        {
            if (patchFailed)
            {
                cacheSuite_.skeletonCache.refreshMaterialized(skeletonKey, builder_.frame(), std::move(capture));
                skeletonProbe = RenderGraphSkeletonCache::ProbeResult{
                    .keyHit = true,
                    .missReason = RenderGraphSkeletonMissReason::PatchFailed,
                };
            }
            else
            {
                skeletonProbe = cacheSuite_.skeletonCache.acceptMaterialized(
                    skeletonKey,
                    builder_.frame(),
                    std::move(capture));
            }
        }
        timings.nodeLoopMilliseconds = elapsedMilliseconds(coldBuildStart, std::chrono::steady_clock::now());
        timings.skeletonRebuildMilliseconds = skeletonEligible ? timings.nodeLoopMilliseconds : 0.0;
    }

    if (telemetry.has_value())
    {
        timings.preludeMilliseconds = elapsedMilliseconds(graphPreludeStart, nodeBuildStart);
    }
    timings.skeletonHit = skeletonProbe.structureMatches;
    timings.skeletonMissReason = skeletonEligible
                                     ? skeletonProbe.missReason
                                     : renderGraphSkeletonMode_ == RenderGraphSkeletonMode::Legacy
                                           ? RenderGraphSkeletonMissReason::Disabled
                                           : RenderGraphSkeletonMissReason::UnsupportedNode;
    if (patchFailed)
    {
        timings.skeletonMissReason = RenderGraphSkeletonMissReason::PatchFailed;
    }
    return timings;
}

void Renderer::teardownInstalledGraph()
{
    if (device_)
    {
        auto shutdownContext = NodeShutdownContext{
            .device = std::ref(*device_),
        };
        std::ranges::for_each(installedNodes_, [&](InstalledNode &installedNode) {
            if (installedNode.runtime)
            {
                installedNode.runtime->flushContinuations();
                installedNode.runtime->shutdown(shutdownContext);
            }
        });
    }

    installedNodes_.clear();
    submitNodesByAfterIndex_.clear();
    frameResolutionResolver_.reset();
    graphInstalled_ = false;
}

[[nodiscard]] std::pair<nr::scene::SceneExtractProfileHandle, bool> Renderer::ensureSceneExtractProfile(nr::scene::Scene &scene)
{
    auto const sceneIdentity = scene.revisionsSnapshot().sceneIdentity;
    auto const sameScene = activeSceneIdentity_ == sceneIdentity;
    if (!sameScene)
    {
        sceneExtractProfile_.reset();
        sceneTlasExtractProfile_.reset();
        previousGlobalFrameConstants_.reset();
    }

    auto needsCreate = !sameScene || !sceneExtractProfile_.has_value() || !sceneExtractProfile_->valid();

    if (needsCreate)
    {
        activeSceneIdentity_ = sceneIdentity;
        sceneExtractProfile_ = scene.registerExtractProfile(nr::scene::SceneExtractProfileCreateInfo{
            .debugName = "Renderer.DefaultRasterExtract",
        });
        return {*sceneExtractProfile_, true};
    }

    return {*sceneExtractProfile_, false};
}

[[nodiscard]] std::pair<nr::scene::SceneExtractProfileHandle, bool> Renderer::ensureSceneTlasExtractProfile(nr::scene::Scene &scene)
{
    auto const sceneIdentity = scene.revisionsSnapshot().sceneIdentity;
    auto const sameScene = activeSceneIdentity_ == sceneIdentity;
    if (!sameScene)
    {
        sceneExtractProfile_.reset();
        sceneTlasExtractProfile_.reset();
        previousGlobalFrameConstants_.reset();
    }

    auto needsCreate = !sameScene || !sceneTlasExtractProfile_.has_value() || !sceneTlasExtractProfile_->valid();

    if (needsCreate)
    {
        activeSceneIdentity_ = sceneIdentity;
        sceneTlasExtractProfile_ = scene.registerExtractProfile(nr::scene::SceneExtractProfileCreateInfo{
            .debugName = "Renderer.DefaultTlasExtract",
            .domain = nr::scene::ScenePacketDomain::tlasBuildInput,
        });
        return {*sceneTlasExtractProfile_, true};
    }

    return {*sceneTlasExtractProfile_, false};
}

} // namespace nr::renderer
