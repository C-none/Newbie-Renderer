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
    DirectX::XMFLOAT4X4 viewProjection{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                       0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    DirectX::XMFLOAT4X4 inverseViewProjection{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                              0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    DirectX::XMFLOAT4X4 unjitteredViewProjection{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                                 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    DirectX::XMFLOAT4X4 previousViewProjection{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                                0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    DirectX::XMFLOAT4 cameraWorld{0.0f, 0.0f, 0.0f, 1.0f};
    DirectX::XMUINT4 frameState{0u, 0u, 0u, 0u};
};

static_assert(sizeof(RendererGlobalFrameUniforms) == 288u,
              "Renderer.GlobalFrameUniforms must match shader/include/globalUniform.slang.");
static_assert(nr::memberOffset<&RendererGlobalFrameUniforms::viewProjection>() == 0u);
static_assert(nr::memberOffset<&RendererGlobalFrameUniforms::inverseViewProjection>() == 64u);
static_assert(nr::memberOffset<&RendererGlobalFrameUniforms::unjitteredViewProjection>() == 128u);
static_assert(nr::memberOffset<&RendererGlobalFrameUniforms::previousViewProjection>() == 192u);
static_assert(nr::memberOffset<&RendererGlobalFrameUniforms::cameraWorld>() == 256u);
static_assert(nr::memberOffset<&RendererGlobalFrameUniforms::frameState>() == 272u);

[[nodiscard]] vk::Extent2D sanitizeViewportExtent(vk::Extent2D extent) noexcept
{
    return vk::Extent2D{
        std::max(1u, extent.width),
        std::max(1u, extent.height),
    };
}

[[nodiscard]] RendererGlobalFrameUniforms makeGlobalFrameUniforms(
    const nr::scene::SceneBridgeFrameConstants &renderingFrameConstants,
    const nr::scene::SceneBridgeFrameConstants &unjitteredFrameConstants,
    const nr::scene::SceneBridgeFrameConstants &previousUnjitteredFrameConstants, std::uint32_t frameIndex,
    std::uint64_t sampleFrameOrdinal) noexcept
{
    auto inverseViewProjection = DirectX::XMFLOAT4X4{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                                      0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    auto const source = DirectX::XMLoadFloat4x4(&renderingFrameConstants.viewProjection);
    auto const determinant = DirectX::XMMatrixDeterminant(source);
    auto determinantValue = DirectX::XMFLOAT4{};
    DirectX::XMStoreFloat4(&determinantValue, determinant);
    if (std::isfinite(determinantValue.x) && std::abs(determinantValue.x) > std::numeric_limits<float>::epsilon())
    {
        DirectX::XMStoreFloat4x4(&inverseViewProjection, DirectX::XMMatrixInverse(nullptr, source));
    }

    auto const sampleFrameLow = static_cast<std::uint32_t>(sampleFrameOrdinal);
    auto const sampleFrameHigh = static_cast<std::uint32_t>(sampleFrameOrdinal >> 32u);

    return RendererGlobalFrameUniforms{
        .viewProjection = renderingFrameConstants.viewProjection,
        .inverseViewProjection = inverseViewProjection,
        .unjitteredViewProjection = unjitteredFrameConstants.viewProjection,
        .previousViewProjection = previousUnjitteredFrameConstants.viewProjection,
        .cameraWorld = DirectX::XMFLOAT4{renderingFrameConstants.cameraWorld.x, renderingFrameConstants.cameraWorld.y,
                                         renderingFrameConstants.cameraWorld.z, 1.0f},
        .frameState = DirectX::XMUINT4{sampleFrameLow, sampleFrameHigh, frameIndex, 0u},
    };
}

[[nodiscard]] double elapsedMilliseconds(std::chrono::steady_clock::time_point begin,
                                         std::chrono::steady_clock::time_point end) noexcept
{
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

void collectTlasMaterialTextureHandles(const nr::scene::Scene &scene, nr::resource::MaterialHandle materialHandle,
                                       std::map<std::uint32_t, nr::resource::TextureHandle> &sceneTextureHandlesById)
{
    auto materialRecordRef = scene.tryGetMaterialAsset(materialHandle);
    nrAssert(materialRecordRef.has_value(),
             "Renderer scene texture collection expected material handle (slot={}, generation={}) to resolve.",
             materialHandle.slot, materialHandle.generation);

    auto const &materialRecord = materialRecordRef->get();
    nrAssert(materialRecord.cpuReady, "Renderer scene texture collection expected material '{}' to be CPU ready.",
             materialRecord.cpu.name);

    auto slotIndices = std::views::iota(std::size_t{0}, materialRecord.cpu.textureSlots.size());
    std::ranges::for_each(slotIndices, [&](std::size_t slotIndex) {
        auto textureHandle = materialRecord.cpu.textureSlots[slotIndex].texture;
        if (!textureHandle.valid())
        {
            return;
        }

        auto binding = scene.tryGetSampledTextureBinding(textureHandle);
        auto const anisotropySlotIndex =
            nr::resource::materialTextureSlotIndex(nr::resource::MaterialTextureSlotSemantic::anisotropy);
        if (!binding.has_value() && slotIndex == anisotropySlotIndex)
        {
            return;
        }

        nrAssert(binding.has_value(),
                 "Renderer scene texture collection expected resident sampled texture for material '{}' slot {}.",
                 materialRecord.cpu.name, slotIndex);
        nrAssert(binding->descriptorIndex < kSceneTextureDescriptorCapacity,
                 "Scene texture descriptor id {} exceeds capacity {}.", binding->descriptorIndex,
                 kSceneTextureDescriptorCapacity);
        nrAssert(binding->descriptorIndex <= nr::scene::kMaxSceneTextureId,
                 "Scene texture descriptor id {} exceeds packed uint16 id capacity {}.", binding->descriptorIndex,
                 nr::scene::kMaxSceneTextureId);

        sceneTextureHandlesById.insert_or_assign(binding->descriptorIndex, textureHandle);
    });
}

void collectTlasSceneTextureHandles(const nr::scene::Scene &scene,
                                    std::span<const nr::scene::TlasBuildInputPacket> tlasPackets,
                                    std::map<std::uint32_t, nr::resource::TextureHandle> &sceneTextureHandlesById)
{
    std::ranges::for_each(tlasPackets, [&](const nr::scene::TlasBuildInputPacket &packet) {
        auto meshRecordRef = scene.tryGetMeshAsset(packet.mesh);
        nrAssert(meshRecordRef.has_value(),
                 "Renderer TLAS texture collection expected mesh handle (slot={}, generation={}) to resolve.",
                 packet.mesh.slot, packet.mesh.generation);
        auto const &meshRecord = meshRecordRef->get();
        nrAssert(meshRecord.cpuReady,
                 "Renderer TLAS texture collection expected mesh handle (slot={}, generation={}) to be CPU ready.",
                 packet.mesh.slot, packet.mesh.generation);

        std::ranges::for_each(meshRecord.cpu.geometries, [&](const nr::resource::MeshGeometry &geometry) {
            if (geometry.material.valid())
            {
                collectTlasMaterialTextureHandles(scene, geometry.material, sceneTextureHandlesById);
            }
        });
    });
}

[[nodiscard]] RendererTlasTextureCollectionKey makeTlasTextureCollectionKey(
    const nr::scene::SceneRevisionSnapshot &revisions, std::span<const nr::scene::TlasBuildInputPacket> packets)
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

[[nodiscard]] RendererCameraJitterSample makeHalton23CameraJitterSample(std::uint64_t frameOrdinal,
                                                                        vk::Extent2D viewportExtent,
                                                                        std::uint32_t cycleLength) noexcept
{
    auto const extent = sanitizeViewportExtent(viewportExtent);
    auto const cycle = std::max(1u, cycleLength);
    auto const sampleIndex = static_cast<std::uint32_t>(frameOrdinal % static_cast<std::uint64_t>(cycle)) + 1u;
    auto const sample = DirectX::XMFLOAT2{
        haltonSequenceValue(sampleIndex, 2u),
        haltonSequenceValue(sampleIndex, 3u),
    };
    auto const pixelOffset = DirectX::XMFLOAT2{sample.x - 0.5f, sample.y - 0.5f};
    auto const ndcOffset = DirectX::XMFLOAT2{
        2.0f * pixelOffset.x / static_cast<float>(extent.width),
        -2.0f * pixelOffset.y / static_cast<float>(extent.height),
    };

    return RendererCameraJitterSample{
        .sampleIndex = sampleIndex,
        .pixelOffset = pixelOffset,
        .ndcOffset = ndcOffset,
    };
}

[[nodiscard]] DirectX::XMFLOAT4X4 applyCameraProjectionJitter(const DirectX::XMFLOAT4X4 &projection,
                                                               DirectX::XMFLOAT2 ndcOffset) noexcept
{
    auto result = projection;
    result._31 -= ndcOffset.x;
    result._32 -= ndcOffset.y;
    return result;
}

[[nodiscard]] RendererCameraFrameState makeRendererCameraFrameState(const RendererCameraJitterConfig &jitterConfig,
                                                                    std::uint64_t frameOrdinal,
                                                                    vk::Extent2D viewportExtent) noexcept
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

void NodeRuntime::finalizeInitialization()
{
}

[[nodiscard]] std::string_view NodeRuntime::actionableSemantic() const noexcept
{
    return {};
}

FrameEffectSink::FrameEffectSink(std::optional<nr::options::FrameEffect> effect) : effect_(std::move(effect))
{
}

[[nodiscard]] const std::optional<nr::options::FrameEffect> &FrameEffectSink::effect() const noexcept
{
    return effect_;
}

[[nodiscard]] bool FrameEffectSink::claim(NodeRuntime &runtime, GraphPassHandle targetPass) noexcept
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

[[nodiscard]] std::optional<std::reference_wrapper<NodeRuntime>> FrameEffectSink::claimedRuntime() const noexcept
{
    return claimedRuntime_;
}

[[nodiscard]] GraphPassHandle FrameEffectSink::targetPass() const noexcept
{
    return targetPass_;
}

void NodeRuntime::declareOptions(nr::options::OptionCatalogBuilder &) const
{
}

void NodeRuntime::collectOptionAvailability(const nr::options::OptionFrameSnapshot &,
                                            nr::options::OptionAvailabilityMap &) const
{
}

[[nodiscard]] std::vector<nr::rhi::SlangProgramCompileFileRequest> NodeRuntime::shaderRequests() const
{
    return {};
}

void NodeRuntime::advanceContinuations(std::uint32_t)
{
}

void NodeRuntime::flushContinuations()
{
}

[[nodiscard]] FrameEffectFinalizeDisposition NodeRuntime::finalizeFrameEffect(const nr::options::FrameEffect &,
                                                                              bool targetBatchSubmitted, std::uint32_t)
{
    return targetBatchSubmitted ? FrameEffectFinalizeDisposition::terminalSucceeded
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

    auto imageInfo =
        nr::rhi::makeImageCreateInfo(vk::Format::eR8G8B8A8Unorm, vk::Extent2D{1u, 1u},
                                     vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled);
    sceneTextureFallback_ = device_->resourceFactory.createImage(imageInfo, nr::rhi::MemoryUsage::GpuOnly,
                                                                 "Renderer.SceneTextureFallback.Neutral");
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

    auto const queueFamilies = device_->queueManager.familyIndices();
    return nr::rhi::ops::makeTransferUploadOwnershipPlan(queueFamilies.transfer, queueFamilies.graphics,
                                                         nr::rhi::ops::QueueAccessScope{
                                                             .stages = vk::PipelineStageFlagBits2::eAllCommands,
                                                             .access = vk::AccessFlagBits2::eShaderSampledRead,
                                                         });
}

void Renderer::synchronizeSampledImageUpload(const nr::rhi::ops::ImageUploadTicket &uploadTicket,
                                             std::string_view debugName)
{
    nrAssert(static_cast<bool>(device_), "Renderer::synchronizeSampledImageUpload requires initialized device.");
    nrAssert(uploadTicket.valid(), "{} upload ticket is invalid.", debugName);

    auto &uploadContext = device_->uploadReadback();
    auto const queueFamilies = device_->queueManager.familyIndices();
    if (queueFamilies.transfer == queueFamilies.graphics)
    {
        uploadContext.waitUploadComplete(uploadTicket.signalValue);
        uploadContext.reclaimCompletedUploads();
        return;
    }

    nr::rhi::submitOneShot(device_->device, device_->queueManager.graphics(),
                           nr::rhi::OneShotSyncPlan{
                               .waitSemaphore = *uploadContext.uploadTimelineSemaphore(),
                               .waitStage = vk::PipelineStageFlagBits2::eAllCommands,
                               .waitValue = uploadTicket.signalValue,
                           },
                           [&](const vk::raii::CommandBuffer &commandBuffer) {
                               uploadContext.recordImageAcquireBarrier(commandBuffer, uploadTicket);
                           });
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
    auto uploadTicket =
        uploadContext.uploadImage(std::span<const std::byte>{neutralPixel}, sceneTextureFallback_,
                                  vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal, uploadPlan);
    nrAssert(uploadTicket.valid(), "Renderer failed to upload neutral scene texture fallback.");
    synchronizeSampledImageUpload(uploadTicket, "neutral scene texture fallback");
}

[[nodiscard]] RendererSceneTextureDescriptorTable Renderer::buildSceneTextureDescriptorTable(
    const NodeFrameParameters &frameParameters,
    const std::map<std::uint32_t, nr::resource::TextureHandle> &sceneTextureHandlesById)
{
    ensureSceneTextureFallback();
    return cacheSuite_.globalDescriptorTableCache.buildSceneTextureDescriptorTable(
        RendererSceneTextureDescriptorTableInput{
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

    device_ = nr::rhi::Device::createUnique(info.appName, info.engineName,
                                            std::filesystem::path{std::string{nr::psoCacheRoot}},
                                            info.debugShaderInstrumentationEnabled);
    frameUniformArena_.initialize(*device_, info.frameUniformBytesPerFrame, "Renderer.FrameUniformArena");
    submissionTimelines_.initialize(device_->device, 0);
    ensureSceneTextureFallback();
    ensureEnvironmentMapFallback();
}

void Renderer::setEnvironmentMap(nr::resource::EnvironmentMap environment)
{
    nrAssert(static_cast<bool>(device_), "Renderer::setEnvironmentMap requires initialize() first.");
    nrAssert(environment.valid(), "Renderer::setEnvironmentMap requires a valid RGBA16F environment resource.");

    auto const totalStart = std::chrono::steady_clock::now();
    auto const waitIdleStart = std::chrono::steady_clock::now();
    device_->waitIdle();
    auto const waitIdleFinished = std::chrono::steady_clock::now();
    auto const resetStart = std::chrono::steady_clock::now();
    builder_.clear();
    executor_.clearRetainedState();
    cacheSuite_.clear();
    auto const resetFinished = std::chrono::steady_clock::now();
    auto const &texture = environment.radiance;
    auto const imageCreateStart = std::chrono::steady_clock::now();
    auto imageInfo =
        nr::rhi::makeImageCreateInfo(texture.format, vk::Extent2D{texture.width, texture.height},
                                     vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled);
    auto image = device_->resourceFactory.createImage(imageInfo, nr::rhi::MemoryUsage::GpuOnly,
                                                      std::format("Renderer.EnvironmentMap.{}", texture.name));
    nrAssert(image.valid(), "Renderer::setEnvironmentMap failed to create the GPU image.");
    auto const imageCreateFinished = std::chrono::steady_clock::now();

    auto &uploadContext = device_->uploadReadback();
    auto const uploadSubmitStart = std::chrono::steady_clock::now();
    auto uploadTicket =
        uploadContext.uploadImage(texture.levels.front().bytes, image, vk::ImageLayout::eUndefined,
                                  vk::ImageLayout::eShaderReadOnlyOptimal, makeSampledImageUploadPlan());
    auto const uploadSubmitFinished = std::chrono::steady_clock::now();
    auto const synchronizeStart = std::chrono::steady_clock::now();
    synchronizeSampledImageUpload(uploadTicket, "environment map");
    auto const synchronizeFinished = std::chrono::steady_clock::now();

    auto const stateAssignStart = std::chrono::steady_clock::now();
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
    auto const stateAssignFinished = std::chrono::steady_clock::now();
    nrLog<LogLevel::info>(
        "[Renderer::setEnvironmentMap] name='{}', extent={}x{}, payloadMiB={:.3f}, phaseMs={{waitIdle={:.3f}, "
        "resetState={:.3f}, imageCreate={:.3f}, uploadSubmit={:.3f}, synchronize={:.3f}, "
        "stateAssign={:.3f}, total={:.3f}}}",
        texture.name, texture.width, texture.height,
        static_cast<double>(texture.levels.front().bytes.size()) / (1024.0 * 1024.0),
        elapsedMilliseconds(waitIdleStart, waitIdleFinished), elapsedMilliseconds(resetStart, resetFinished),
        elapsedMilliseconds(imageCreateStart, imageCreateFinished), elapsedMilliseconds(uploadSubmitStart, uploadSubmitFinished),
        elapsedMilliseconds(synchronizeStart, synchronizeFinished), elapsedMilliseconds(stateAssignStart, stateAssignFinished),
        elapsedMilliseconds(totalStart, stateAssignFinished));
}

void Renderer::requestTemporalHistoryReset() noexcept
{
    temporalHistoryResetPending_ = true;
}

[[nodiscard]] RendererGraphPreflightResult Renderer::preflightGraph(const RendererGraphSpec &spec) const
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

        auto const &createInfo = spec.nodes[nodeIndex];
        if (!createInfo.runtime)
        {
            validationError = std::format("Renderer graph node {} has no runtime.", nodeIndex);
            return;
        }
        if (createInfo.config.instanceName.empty())
        {
            validationError = std::format("Renderer graph node {} has an empty instance name.", nodeIndex);
            return;
        }
        if (!knownNames.emplace(createInfo.config.instanceName).second)
        {
            validationError =
                std::format("Renderer graph contains duplicate node name '{}'.", createInfo.config.instanceName);
            return;
        }

        auto const semantic = createInfo.runtime->actionableSemantic();
        if (!semantic.empty() && !knownSemantics.emplace(semantic).second)
        {
            validationError = std::format("Renderer graph contains duplicate actionable semantic '{}'.", semantic);
        }
    });
    if (!validationError.empty())
    {
        return RendererGraphPreflightResult{
            .message = std::move(validationError),
        };
    }

    auto const invalidSubmit = std::ranges::find_if(spec.submitNodes, [&](const SubmitNodeSpec &submitSpec) {
        return submitSpec.afterNodeIndex >= spec.nodes.size();
    });
    if (invalidSubmit != spec.submitNodes.end())
    {
        return RendererGraphPreflightResult{
            .message = std::format("Renderer graph submit '{}' references node index {} but the graph has {} node(s).",
                                   invalidSubmit->debugName, invalidSubmit->afterNodeIndex, spec.nodes.size()),
        };
    }

    auto optionBuilder = nr::options::OptionCatalogBuilder{};
    std::ranges::for_each(spec.nodes,
                          [&](const NodeCreateInfo &createInfo) { createInfo.runtime->declareOptions(optionBuilder); });
    auto optionCatalog = optionBuilder.build();
    if (!optionCatalog.valid())
    {
        auto const &issue = optionCatalog.issues.front();
        return RendererGraphPreflightResult{
            .message =
                issue.id.has_value()
                    ? std::format("Renderer graph option '{}' failed preflight: {}.", issue.id->value(), issue.detail)
                    : std::format("Renderer graph option catalog failed preflight: {}.", issue.detail),
        };
    }
    auto const nonGraphOption = std::ranges::find_if(optionCatalog.catalog->definitions(), [](auto const &entry) {
        return entry.second.scope != nr::options::OptionScope::graph;
    });
    if (nonGraphOption != optionCatalog.catalog->definitions().end())
    {
        return RendererGraphPreflightResult{
            .message = std::format("Renderer node option '{}' must use graph scope.", nonGraphOption->first.value()),
        };
    }

    if (!spec.frameResolutionOptionRequirements.empty() &&
        (!spec.frameResolutionResolver.has_value() || !static_cast<bool>(*spec.frameResolutionResolver)))
    {
        return RendererGraphPreflightResult{
            .message = "Renderer graph declares frame-resolution option requirements without a resolver.",
        };
    }
    auto const missingResolverOption =
        std::ranges::find_if(spec.frameResolutionOptionRequirements, [&](const nr::options::OptionId &id) {
            return optionCatalog.catalog->find(id) == nullptr;
        });
    if (missingResolverOption != spec.frameResolutionOptionRequirements.end())
    {
        return RendererGraphPreflightResult{
            .message = std::format("Frame resolution resolver requires undeclared graph option '{}'.",
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
        nr::nrLog<nr::LogLevel::warning, "RENDERER">("{}", preflight.message);
        return false;
    }

    struct NodeShaderSlice
    {
        std::size_t offset = 0u;
        std::size_t count = 0u;
    };

    auto shaderRequests = std::vector<nr::rhi::SlangProgramCompileFileRequest>{};
    auto shaderSlices = std::vector<NodeShaderSlice>{};
    shaderSlices.reserve(spec.nodes.size());
    std::ranges::for_each(spec.nodes, [&](const NodeCreateInfo &createInfo) {
        auto nodeRequests = createInfo.runtime->shaderRequests();
        shaderSlices.push_back(NodeShaderSlice{
            .offset = shaderRequests.size(),
            .count = nodeRequests.size(),
        });
        std::ranges::move(nodeRequests, std::back_inserter(shaderRequests));
    });

    auto shaderPrograms = shaderRequests.empty()
                              ? std::vector<nr::rhi::SlangProgram>{}
                              : nr::rhi::ShaderService::instance().compileProgramsByFile(shaderRequests);
    if (shaderPrograms.size() != shaderRequests.size())
    {
        nr::nrLog<nr::LogLevel::warning, "RENDERER">(
            "Static shader batch returned {} programs for {} requests.", shaderPrograms.size(), shaderRequests.size());
        return false;
    }

    auto const invalidProgram =
        std::ranges::find_if(shaderPrograms, [](const nr::rhi::SlangProgram &program) { return !program.valid(); });
    if (invalidProgram != shaderPrograms.end())
    {
        auto const invalidIndex = static_cast<std::size_t>(std::distance(shaderPrograms.begin(), invalidProgram));
        auto const nodeIndices = std::views::iota(std::size_t{0u}, shaderSlices.size());
        auto const ownerIt = std::ranges::find_if(nodeIndices, [&](std::size_t index) {
            auto const &slice = shaderSlices[index];
            return invalidIndex >= slice.offset && invalidIndex < slice.offset + slice.count;
        });
        auto const ownerName =
            ownerIt != nodeIndices.end() ? spec.nodes[*ownerIt].config.instanceName : std::string{"<unknown>"};
        nr::nrLog<nr::LogLevel::warning, "RENDERER">(
            "Static shader '{}' failed while installing node '{}'.", shaderRequests[invalidIndex].sourcePath.generic_string(),
            ownerName);
        return false;
    }

    device_->waitIdle();
    teardownInstalledGraph();
    cacheSuite_.clear();

    auto installed = std::vector<InstalledNode>{};
    installed.reserve(spec.nodes.size());

    auto const nodeIndices = std::views::iota(std::size_t{0u}, spec.nodes.size());
    std::ranges::for_each(nodeIndices, [&](std::size_t nodeIndex) {
        auto const &createInfo = spec.nodes[nodeIndex];
        auto const &shaderSlice = shaderSlices[nodeIndex];
        auto initContext = NodeInitContext{
            .device = std::ref(*device_),
            .runtimeName = createInfo.config.instanceName,
            .shaderPrograms =
                std::span<const nr::rhi::SlangProgram>{shaderPrograms}.subspan(shaderSlice.offset, shaderSlice.count),
        };
        createInfo.runtime->initialize(initContext);

        installed.push_back(InstalledNode{
            .runtime = createInfo.runtime,
            .config = createInfo.config,
        });
    });
    device_->pipeline().waitForBuilds();
    std::ranges::for_each(installed, [](InstalledNode &node) { node.runtime->finalizeInitialization(); });

    auto submitNodesByAfterIndex = std::multimap<std::size_t, SubmitNodeSpec>{};
    std::ranges::for_each(spec.submitNodes, [&](const SubmitNodeSpec &submitSpec) {
        submitNodesByAfterIndex.emplace(submitSpec.afterNodeIndex, submitSpec);
    });

    installedNodes_ = std::move(installed);
    submitNodesByAfterIndex_ = std::move(submitNodesByAfterIndex);
    cameraJitter_ = spec.cameraJitter;
    frameResolutionResolver_ = spec.frameResolutionResolver;
    acceptedTemporalFrameState_.reset();
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
    acceptedTemporalFrameState_.reset();
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
    cacheSuite_.compileCache.clear();
    acceptedTemporalFrameState_.reset();
}

void Renderer::resetSceneBinding() noexcept
{
    activeSceneIdentity_.reset();
    tlasTextureCollectionKey_.reset();
    tlasTextureHandlesById_.clear();
    sceneExtractProfile_.reset();
    sceneTlasExtractProfile_.reset();
    acceptedTemporalFrameState_.reset();
}

void Renderer::collectOptionAvailability(const nr::options::OptionFrameSnapshot &snapshot,
                                         nr::options::OptionAvailabilityMap &availability) const
{
    std::ranges::for_each(installedNodes_, [&](const InstalledNode &installedNode) {
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

        auto const &effect = *frameEffectSink.effect();
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
            if (frameEffectTargetSubmitted && effect.id == nr::options::optionId(nr::options::keys::presentCaptureExr))
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
                effect, frameEffectTargetSubmitted, *frameEffectFrameSlot);
            if (!frameEffectTargetSubmitted && disposition == FrameEffectFinalizeDisposition::continuationArmed)
            {
                disposition = FrameEffectFinalizeDisposition::terminalFailed;
            }
            if (frameEffectTargetSubmitted && disposition == FrameEffectFinalizeDisposition::terminalFailed)
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
            .status = disposition == FrameEffectFinalizeDisposition::terminalSucceeded
                          ? nr::options::OptionLogStatus::succeeded
                          : nr::options::OptionLogStatus::failed,
            .frameIndex = input.optionSnapshot.get().frameIndex,
            .origin = effect.origin,
            .requestId = effect.requestId,
            .reason = disposition == FrameEffectFinalizeDisposition::terminalSucceeded
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
    std::ranges::for_each(installedNodes_, [&](InstalledNode &installedNode) {
        installedNode.runtime->advanceContinuations(begin.frameIndex);
    });
    auto invalidateForSwapchainRecreation = [&] {
        auto const generation = device_->swapchainRecreationGeneration();
        if (generation == observedSwapchainRecreationGeneration_)
        {
            return;
        }
        cacheSuite_.compileCache.clear();
        observedSwapchainRecreationGeneration_ = generation;
    };
    invalidateForSwapchainRecreation();
    auto const hasFrameResolutionResolver =
        frameResolutionResolver_.has_value() && static_cast<bool>(*frameResolutionResolver_);
    auto preAcquiredFrameImage = std::optional<nr::rhi::Device::FrameAcquireResult>{};
    if (hasFrameResolutionResolver)
    {
        preAcquiredFrameImage = device_->acquireFrameImage(input.acquireTimeout);
        invalidateForSwapchainRecreation();
    }
    auto const currentDisplayExtent = device_->presentationContext.swapchainExtent();
    nrAssert(currentDisplayExtent.width > 0u && currentDisplayExtent.height > 0u,
             "Renderer::renderFrame requires a non-zero display extent after beginFrame().");
    auto const displayExtent = sanitizeViewportExtent(currentDisplayExtent);
    auto resolutionPlan = FrameResolutionPlan{
        .displayExtent = displayExtent,
        .renderExtent = displayExtent,
    };
    if (hasFrameResolutionResolver)
    {
        resolutionPlan = (*frameResolutionResolver_)(*device_, displayExtent, input.optionSnapshot.get());
    }
    nrAssert(resolutionPlan.displayExtent.width > 0u && resolutionPlan.displayExtent.height > 0u &&
                 resolutionPlan.renderExtent.width > 0u && resolutionPlan.renderExtent.height > 0u,
             "Renderer::renderFrame resolution resolver returned a zero display or render extent.");
    nrAssert(
        resolutionPlan.displayExtent == displayExtent,
        "Renderer::renderFrame resolution resolver display extent does not match the current presentation extent.");
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
        .frameSetupMilliseconds = std::max(0.0, elapsedMilliseconds(beginFrameStart, std::chrono::steady_clock::now()) -
                                                    begin.cpuWaitGpuMilliseconds),
    };

    auto scenePackets = std::optional<nr::scene::ScenePacketSet>{};
    auto sceneTlasPackets = std::optional<nr::scene::ScenePacketSet>{};
    auto sceneBridgeFrame = std::optional<nr::scene::SceneBridgeFrame>{};
    auto sceneTextureHandlesById = std::map<std::uint32_t, nr::resource::TextureHandle>{};
    auto sceneExtractProfileCreated = false;
    auto sceneCameraOverride = input.cameraOverride;

    auto const sceneStart = std::chrono::steady_clock::now();
    if (input.scene.has_value())
    {
        auto &scene = input.scene->get();
        scene.beginFrame(begin.frameIndex);
        scene.uploadPending();

        auto [profile, created] = ensureSceneExtractProfile(scene);
        sceneExtractProfileCreated = created;
        auto [tlasProfile, tlasProfileCreated] = ensureSceneTlasExtractProfile(scene);
        sceneExtractProfileCreated = sceneExtractProfileCreated || tlasProfileCreated;

        auto extractInput = input.sceneExtractInput.value_or(nr::scene::SceneExtractInput{});
        if (!extractInput.viewportExtent.has_value())
        {
            extractInput.viewportExtent = DirectX::XMUINT2{displayExtent.width, displayExtent.height};
        }

        if (sceneCameraOverride.has_value())
        {
            extractInput.visibility = nr::scene::SceneVisibilityMode::customFrustum;
            extractInput.customFrustum = sceneCameraOverride->frustum;
        }

        auto tlasExtractInput = extractInput;
        tlasExtractInput.visibility = nr::scene::SceneVisibilityMode::none;
        tlasExtractInput.customFrustum.reset();

        scenePackets = scene.extractPackets(profile, extractInput);
        sceneTlasPackets = scene.extractPackets(tlasProfile, tlasExtractInput);
        {
            auto primaryCamera = sceneCameraOverride.has_value()
                                     ? std::optional<nr::scene::SceneResolvedCamera>{}
                                     : scene.tryGetPrimaryCamera(extractInput.viewportExtent);
            auto bridgeBuildInput = nr::scene::SceneRenderBridgeBuildInput{
                .packetSet = std::cref(*scenePackets),
            };

            if (sceneCameraOverride.has_value())
            {
                bridgeBuildInput.frameConstantsOverride = sceneCameraOverride->frameConstants;
            }

            if (primaryCamera.has_value())
            {
                bridgeBuildInput.primaryCamera = std::cref(*primaryCamera);
            }

            sceneBridgeFrame = nr::scene::SceneRenderBridge::buildFrame(bridgeBuildInput);
        }
        std::ranges::for_each(sceneBridgeFrame->rasterTextureHandlesById, [&](const auto &entry) {
            nrAssert(entry.first < kSceneTextureDescriptorCapacity, "Scene texture descriptor id {} exceeds capacity {}.",
                     entry.first, kSceneTextureDescriptorCapacity);
            auto [it, inserted] = sceneTextureHandlesById.try_emplace(entry.first, entry.second);
            nrAssert(inserted || it->second == entry.second,
                     "Scene texture descriptor id {} resolved to conflicting handles.", entry.first);
        });
    }
    cpuTimings.sceneMilliseconds = elapsedMilliseconds(sceneStart, std::chrono::steady_clock::now());
    auto const postSceneStart = std::chrono::steady_clock::now();

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
        auto const packets = std::span<const nr::scene::TlasBuildInputPacket>{sceneTlasPackets->tlasBuildInputs};
        auto key = makeTlasTextureCollectionKey(sceneTlasPackets->revisions, packets);
        if (!tlasTextureCollectionKey_.has_value() || *tlasTextureCollectionKey_ != key)
        {
            auto collected = std::map<std::uint32_t, nr::resource::TextureHandle>{};
            collectTlasSceneTextureHandles(input.scene->get(), packets, collected);
            tlasTextureHandlesById_ = std::move(collected);
            tlasTextureCollectionKey_ = std::move(key);
        }
        std::ranges::for_each(tlasTextureHandlesById_, [&](const auto &entry) {
            auto [it, inserted] = sceneTextureHandlesById.try_emplace(entry.first, entry.second);
            nrAssert(inserted || it->second == entry.second,
                     "Scene texture descriptor id {} resolved to conflicting handles.", entry.first);
        });
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
    auto currentTemporalFrameState = AcceptedTemporalFrameState{
        .unjitteredCameraConstants = globalFrameConstants,
        .sceneRevisions = sceneTlasPackets.has_value()
                              ? std::optional<nr::scene::SceneRevisionSnapshot>{sceneTlasPackets->revisions}
                              : std::nullopt,
    };
    resolutionPlan.resetHistory =
        resolutionPlan.resetHistory || !acceptedTemporalFrameState_.has_value() ||
        acceptedTemporalFrameState_->sceneRevisions != currentTemporalFrameState.sceneRevisions;
    frameParameters.resolutionPlan = resolutionPlan;
    frameParameters.renderCameraConstants = globalFrameConstants;

    auto const cameraFrameState =
        makeRendererCameraFrameState(cameraJitter_, sampleFrameOrdinal, frameParameters.resolutionPlan.renderExtent);
    auto renderingFrameConstants = globalFrameConstants;
    if (cameraFrameState.jitterEnabled)
    {
        renderingFrameConstants.projection =
            applyCameraProjectionJitter(globalFrameConstants.projection, cameraFrameState.jitter.ndcOffset);
        DirectX::XMStoreFloat4x4(
            &renderingFrameConstants.viewProjection,
            DirectX::XMMatrixMultiply(DirectX::XMLoadFloat4x4(&renderingFrameConstants.view),
                                      DirectX::XMLoadFloat4x4(&renderingFrameConstants.projection)));
    }

    cpuTimings.postSceneMilliseconds = elapsedMilliseconds(postSceneStart, std::chrono::steady_clock::now());
    auto const buildStart = std::chrono::steady_clock::now();
    buildInstalledGraph(frameParameters, renderingFrameConstants, globalFrameConstants, cameraFrameState,
                        sampleFrameOrdinal, sceneBridgeFrameRef, sceneTextureHandlesById);
    cpuTimings.buildMilliseconds = elapsedMilliseconds(buildStart, std::chrono::steady_clock::now());

    auto const compileStart = std::chrono::steady_clock::now();
    auto compiled = cacheSuite_.compileCache.compileConsumingCached(builder_.mutableFrame());
    if (frameEffectSink.claimed())
    {
        auto const targetPass = frameEffectSink.targetPass();
        std::ranges::for_each(compiled.submitBatches, [&](const CompiledSubmitBatch &batch) {
            if (std::ranges::any_of(batch.passes, [&](const CompiledPass &pass) { return pass.handle == targetPass; }))
            {
                nrAssert(!frameEffectTargetBatch.has_value(),
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
    auto executeContext = RenderGraphExecutor::ExecuteContext{
        .device = *device_,
        .frameIndex = begin.frameIndex,
        .frameOrdinal = sampleFrameOrdinal,
        .acquireTimeout = input.acquireTimeout,
        .preAcquiredFrameImage = preAcquiredFrameImage,
        .submissionTimelines =
            submissionTimelines_.valid()
                ? std::optional<std::reference_wrapper<RendererSubmissionTimelines>>(std::ref(submissionTimelines_))
                : std::nullopt,
    };

    auto const prepareStart = std::chrono::steady_clock::now();
    auto prepared = executor_.prepareFrame(std::move(compiled), executeContext);
    cpuTimings.prepareMilliseconds = elapsedMilliseconds(prepareStart, std::chrono::steady_clock::now());

    auto const executeStart = std::chrono::steady_clock::now();
    auto executeReport = executor_.executePrepared(prepared, executeContext);
    if (frameEffectTargetBatch.has_value())
    {
        frameEffectTargetSubmitted =
            std::ranges::contains(executeReport.submittedCompiledBatchIndices, *frameEffectTargetBatch);
    }
    nrAssert(executeReport.swapchainImageIndex.has_value(),
             "Renderer::renderFrame expected the graph to acquire one swapchain image before presentation.");
    acceptedTemporalFrameState_ = std::move(currentTemporalFrameState);
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
        benchmarkFrames_.push_back(RendererBenchmarkFrame{
            .frameOrdinal = sampleFrameOrdinal,
            .frameSlot = begin.frameIndex % nr::maxFrameInFlight,
            .displayExtent = resolutionPlan.displayExtent,
            .renderExtent = resolutionPlan.renderExtent,
            .cpu = cpuTimings,
        });
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

void Renderer::buildInstalledGraph(
    const NodeFrameParameters &frameParameters,
    const nr::scene::SceneBridgeFrameConstants &renderingFrameConstants,
    const nr::scene::SceneBridgeFrameConstants &unjitteredFrameConstants,
    const RendererCameraFrameState &cameraFrameState, std::uint64_t sampleFrameOrdinal,
    std::optional<std::reference_wrapper<const nr::scene::SceneBridgeFrame>> sceneBridgeFrame,
    const std::map<std::uint32_t, nr::resource::TextureHandle> &sceneTextureHandlesById)
{
    nrAssert(graphInstalled_, "Renderer::buildInstalledGraph requires installGraph() before rendering.");

    auto const &previousUnjitteredFrameConstants = acceptedTemporalFrameState_.has_value()
                                                      ? acceptedTemporalFrameState_->unjitteredCameraConstants
                                                      : unjitteredFrameConstants;
    auto const globalFrameUniforms = makeGlobalFrameUniforms(
        renderingFrameConstants, unjitteredFrameConstants, previousUnjitteredFrameConstants,
        frameParameters.frameIndex, sampleFrameOrdinal);
    nrAssert(environmentMapImage_.valid() && environmentMapState_.common.initialized,
             "Renderer::buildInstalledGraph requires a resident environment map.");
    auto sceneTextureDescriptorTable = buildSceneTextureDescriptorTable(frameParameters, sceneTextureHandlesById);
    auto nodeFrameParameters = frameParameters;
    builder_.clear();
    auto frameResources = std::map<std::string, GraphResourceHandle>{};
    auto frameDataResources = std::map<std::string, GraphFrameDataHandle>{};
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
    auto nodeOrdinals = std::views::iota(std::size_t{0}, installedNodes_.size());
    std::ranges::for_each(nodeOrdinals, [&](std::size_t nodeIndex) {
        auto &installedNode = installedNodes_[nodeIndex];
        auto nodeHandle = builder_.addNode(installedNode.config.instanceName, installedNode.config.queue);
        auto buildContext = NodeBuildContext{
            .graphBuilder = std::ref(builder_),
            .nodeHandle = nodeHandle,
            .queue = installedNode.config.queue,
            .frameIndex = frameParameters.frameIndex,
            .runtimeName = installedNode.config.instanceName,
            .globalResources = std::cref(globalResources),
            .frameResources = std::ref(frameResources),
            .frameDataResources = std::ref(frameDataResources),
        };
        installedNode.runtime->build(buildContext, nodeFrameParameters);
        auto boundaries = submitNodesByAfterIndex_.equal_range(nodeIndex);
        std::ranges::for_each(std::ranges::subrange(boundaries.first, boundaries.second), [&](const auto &entry) {
            auto debugName = entry.second.debugName.empty() ? std::format("Submit.After.{}", installedNode.config.instanceName)
                                                            : entry.second.debugName;
            nrAssert(builder_.addSubmitNode(debugName).valid(),
                     "Renderer::buildInstalledGraph failed to add a valid submit node.");
        });
    });
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

[[nodiscard]] std::pair<nr::scene::SceneExtractProfileHandle, bool> Renderer::ensureSceneExtractProfile(
    nr::scene::Scene &scene)
{
    auto const sceneIdentity = scene.revisionsSnapshot().sceneIdentity;
    auto const sameScene = activeSceneIdentity_ == sceneIdentity;
    if (!sameScene)
    {
        sceneExtractProfile_.reset();
        sceneTlasExtractProfile_.reset();
        acceptedTemporalFrameState_.reset();
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

[[nodiscard]] std::pair<nr::scene::SceneExtractProfileHandle, bool> Renderer::ensureSceneTlasExtractProfile(
    nr::scene::Scene &scene)
{
    auto const sceneIdentity = scene.revisionsSnapshot().sceneIdentity;
    auto const sameScene = activeSceneIdentity_ == sceneIdentity;
    if (!sameScene)
    {
        sceneExtractProfile_.reset();
        sceneTlasExtractProfile_.reset();
        acceptedTemporalFrameState_.reset();
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
