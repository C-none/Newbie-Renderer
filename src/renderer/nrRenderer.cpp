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

void accumulateCpuTimings(RendererCpuFrameTimings &target, const RendererCpuFrameTimings &sample) noexcept
{
    target.cpuWaitGpuMilliseconds += sample.cpuWaitGpuMilliseconds;
    target.frameSetupMilliseconds += sample.frameSetupMilliseconds;
    target.sceneMilliseconds += sample.sceneMilliseconds;
    target.postSceneMilliseconds += sample.postSceneMilliseconds;
    target.buildMilliseconds += sample.buildMilliseconds;
    target.compileMilliseconds += sample.compileMilliseconds;
    target.prepareMilliseconds += sample.prepareMilliseconds;
    target.executeMilliseconds += sample.executeMilliseconds;
    target.presentMilliseconds += sample.presentMilliseconds;
    target.totalMilliseconds += sample.totalMilliseconds;
}

[[nodiscard]] RendererCpuFrameTimings averageCpuTimings(const RendererCpuFrameTimings &total, std::uint32_t frameCount) noexcept
{
    auto const divisor = static_cast<double>(std::max(frameCount, 1u));
    return RendererCpuFrameTimings{
        .cpuWaitGpuMilliseconds = total.cpuWaitGpuMilliseconds / divisor,
        .frameSetupMilliseconds = total.frameSetupMilliseconds / divisor,
        .sceneMilliseconds = total.sceneMilliseconds / divisor,
        .postSceneMilliseconds = total.postSceneMilliseconds / divisor,
        .buildMilliseconds = total.buildMilliseconds / divisor,
        .compileMilliseconds = total.compileMilliseconds / divisor,
        .prepareMilliseconds = total.prepareMilliseconds / divisor,
        .executeMilliseconds = total.executeMilliseconds / divisor,
        .presentMilliseconds = total.presentMilliseconds / divisor,
        .totalMilliseconds = total.totalMilliseconds / divisor,
    };
}

[[nodiscard]] std::vector<RendererGpuPassAverage> averageGpuPassTimings(const std::map<std::pair<std::uint32_t, std::string>, RendererGpuPassAverage> &totals)
{
    auto averages = totals | std::views::values | std::views::transform([](const RendererGpuPassAverage &total) {
                        auto average = total;
                        auto const divisor = static_cast<double>(std::max(average.sampleCount, 1u));
                        average.milliseconds /= divisor;
                        return average;
                    }) |
                    std::ranges::to<std::vector>();

    std::ranges::sort(averages, [](const RendererGpuPassAverage &lhs, const RendererGpuPassAverage &rhs) { return std::tie(lhs.pass.value, lhs.debugName) < std::tie(rhs.pass.value, rhs.debugName); });
    return averages;
}

[[nodiscard]] std::optional<nr::scene::SceneMaterialTextureIds> collectSceneMaterialTextureIds(const nr::scene::Scene &scene, nr::resource::MaterialHandle materialHandle, std::map<std::uint32_t, nr::resource::TextureHandle> &sceneTextureHandlesById)
{
    auto materialRecordRef = scene.tryGetMaterialAsset(materialHandle);
    nrAssert(materialRecordRef.has_value(), std::format("Renderer scene texture collection expected material handle (slot={}, generation={}) to resolve.", materialHandle.slot, materialHandle.generation));

    auto const &materialRecord = materialRecordRef->get();
    nrAssert(materialRecord.cpuReady, std::format("Renderer scene texture collection expected material '{}' to be CPU ready.", materialRecord.cpu.name));

    auto textureIds = nr::scene::SceneMaterialTextureIds{};
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
        textureIds[slotIndex] = textureId;
        sceneTextureHandlesById.insert_or_assign(binding->descriptorIndex, textureHandle);
    });
    return textureIds;
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
                static_cast<void>(collectSceneMaterialTextureIds(scene, geometry.material, sceneTextureHandlesById));
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

[[nodiscard]] std::optional<NodeImageResourceDesc> describeGraphImageResource(const GraphResourceDesc &resource)
{
    return std::visit(
        [](const auto &desc) -> std::optional<NodeImageResourceDesc> {
            using DescT = std::remove_cvref_t<decltype(desc)>;
            if constexpr (std::same_as<DescT, GraphImportedImageDesc> || std::same_as<DescT, GraphTransientImageDesc>)
            {
                return NodeImageResourceDesc{
                    .debugName = desc.debugName,
                    .extent = desc.extent,
                    .format = desc.format,
                    .aspect = desc.aspect,
                };
            }
            else if constexpr (std::same_as<DescT, GraphImportedSwapchainImageDesc>)
            {
                return NodeImageResourceDesc{
                    .debugName = desc.debugName,
                    .extent = desc.extent,
                    .format = desc.format,
                };
            }
            else
            {
                return std::nullopt;
            }
        },
        resource.desc);
}

void NodeBuildContext::publishFrameResource(std::string_view key, GraphResourceHandle resource) const
{
    nrAssert(resource.valid(), std::format("NodeBuildContext::publishFrameResource requires a valid resource for '{}'.", key));
    nrAssert(!key.empty(), "NodeBuildContext::publishFrameResource requires a non-empty key.");
    frameResources.get().insert_or_assign(std::string(key), resource);
}

[[nodiscard]] GraphResourceHandle NodeBuildContext::resolveFrameResource(std::string_view key) const
{
    auto const resourceIt = frameResources.get().find(std::string(key));
    if (resourceIt == frameResources.get().end())
    {
        return {};
    }
    return resourceIt->second;
}

[[nodiscard]] GraphResourceHandle NodeBuildContext::requireFrameResource(std::string_view key, std::string_view consumerDebugName) const
{
    auto resource = resolveFrameResource(key);
    nrAssert(resource.valid(), std::format("{} requires frame resource '{}', but it has not been published.", consumerDebugName, key));
    return resource;
}

void NodeBuildContext::publishFrameData(std::string_view key, GraphFrameDataHandle frameData) const
{
    nrAssert(frameData.valid(), std::format("NodeBuildContext::publishFrameData requires valid frame data for '{}'.", key));
    nrAssert(!key.empty(), "NodeBuildContext::publishFrameData requires a non-empty key.");
    frameDataResources.get().insert_or_assign(std::string(key), frameData);
}

[[nodiscard]] GraphFrameDataHandle NodeBuildContext::resolveFrameData(std::string_view key) const
{
    auto const frameDataIt = frameDataResources.get().find(std::string(key));
    if (frameDataIt == frameDataResources.get().end())
    {
        return {};
    }
    return frameDataIt->second;
}

[[nodiscard]] GraphFrameDataHandle NodeBuildContext::requireFrameData(std::string_view key, std::string_view consumerDebugName) const
{
    auto frameData = resolveFrameData(key);
    nrAssert(frameData.valid(), std::format("{} requires frame data '{}', but it has not been published.", consumerDebugName, key));
    return frameData;
}

[[nodiscard]] std::optional<std::reference_wrapper<const std::any>> NodeBuildContext::resolveFrameDataPayload(GraphFrameDataHandle handle) const
{
    if (!handle.valid())
    {
        return {};
    }

    auto const &frameData = graphBuilder.get().frame().frameData;
    auto const frameDataIt = std::ranges::find_if(frameData, [handle](const GraphFrameDataDesc &desc) { return desc.handle == handle; });
    if (frameDataIt == frameData.end())
    {
        return {};
    }
    return std::cref(frameDataIt->payload);
}

[[nodiscard]] std::optional<NodeImageResourceDesc> NodeBuildContext::describeImageResource(GraphResourceHandle resource) const
{
    if (!resource.valid())
    {
        return std::nullopt;
    }

    auto const &resources = graphBuilder.get().frame().resources;
    auto const resourceIt = std::ranges::find_if(resources, [resource](const GraphResourceDesc &desc) { return desc.handle == resource; });
    if (resourceIt == resources.end())
    {
        return std::nullopt;
    }

    return describeGraphImageResource(*resourceIt);
}

[[nodiscard]] GraphResourceHandle NodeBuildContext::transientColor(std::string_view debugName, vk::Extent2D extent, vk::Format format)
{
    return addResource(GraphTransientImageDesc{
        .debugName = std::string(debugName),
        .extent = vk::Extent3D{extent.width, extent.height, 1},
        .format = format,
        .usageIntents =
            {
                ImageUsageIntent::ColorAttachment,
                ImageUsageIntent::TransferSrc,
                ImageUsageIntent::Sampled,
            },
        .initialLayout = ImageLayoutIntent::ColorAttachment,
    });
}

[[nodiscard]] GraphResourceHandle NodeBuildContext::importColor(const nr::rhi::Image &image, std::string_view debugName, vk::Extent2D extent, vk::Format format, ResourceLifetime lifetime)
{
    return importImage(image, debugName, extent, format, lifetime,
                       {
                           ImageUsageIntent::ColorAttachment,
                           ImageUsageIntent::TransferSrc,
                           ImageUsageIntent::Sampled,
                       });
}

[[nodiscard]] GraphResourceHandle NodeBuildContext::importStorageColor(const nr::rhi::Image &image, std::string_view debugName, vk::Extent2D extent, vk::Format format, ResourceLifetime lifetime)
{
    return importImage(image, debugName, extent, format, lifetime,
                       {
                           ImageUsageIntent::StorageWrite,
                           ImageUsageIntent::TransferSrc,
                       });
}

[[nodiscard]] GraphResourceHandle NodeBuildContext::importRetainedStorageColor(const nr::rhi::Image &image, RetainedImageState &state, std::string_view debugName, vk::Extent2D extent, vk::Format format, ResourceLifetime lifetime)
{
    nrAssert(image.valid(), std::format("{} image is invalid.", debugName));

    return addResource(GraphImportedImageDesc{
        .debugName = std::string(debugName),
        .lifetime = lifetime,
        .initialOwnership = state.common.initialized ? state.common.ownership : ResourceOwnershipDomain::Undefined,
        .extent = vk::Extent3D{extent.width, extent.height, 1},
        .format = format,
        .usageIntents =
            {
                ImageUsageIntent::StorageWrite,
                ImageUsageIntent::TransferSrc,
            },
        .initialLayout = state.common.initialized ? state.layout : ImageLayoutIntent::Undefined,
        .initialAccessScope = state.common.initialized ? state.common.access : AccessScope{},
        .importedResource = std::cref(image),
        .retainedState = std::ref(state),
    });
}

[[nodiscard]] GraphResourceHandle NodeBuildContext::importSampledColor(const nr::rhi::Image &image, std::string_view debugName, vk::Extent2D extent, vk::Format format, ResourceLifetime lifetime)
{
    return importImage(image, debugName, extent, format, lifetime,
                       {
                           ImageUsageIntent::ColorAttachment,
                           ImageUsageIntent::Sampled,
                       });
}

[[nodiscard]] GraphResourceHandle NodeBuildContext::importSampledImage(const nr::rhi::Image &image, std::string_view debugName, vk::Extent3D extent, vk::Format format, ResourceLifetime lifetime, ResourceOwnershipDomain initialOwnership)
{
    nrAssert(image.valid(), std::format("{} image is invalid.", debugName));

    return addResource(GraphImportedImageDesc{
        .debugName = std::string(debugName),
        .lifetime = lifetime,
        .initialOwnership = initialOwnership,
        .extent = extent,
        .format = format,
        .usageIntents =
            {
                ImageUsageIntent::Sampled,
            },
        .initialLayout = ImageLayoutIntent::ShaderReadOnly,
        .importedResource = std::cref(image),
    });
}

[[nodiscard]] GraphResourceHandle NodeBuildContext::importDepth(const nr::rhi::Image &image, std::string_view debugName, vk::Extent2D extent, vk::Format format, ResourceLifetime lifetime)
{
    return importImage(image, debugName, extent, format, lifetime,
                       {
                           ImageUsageIntent::DepthStencilAttachment,
                       },
                       ImageAspectIntent::Depth);
}

[[nodiscard]] GraphResourceHandle NodeBuildContext::importBuffer(const nr::rhi::Buffer &buffer, std::string_view debugName, ResourceLifetime lifetime, std::initializer_list<BufferUsageIntent> usageIntents, ResourceOwnershipDomain initialOwnership)
{
    nrAssert(buffer.valid(), std::format("{} buffer is invalid.", debugName));

    return addResource(GraphImportedBufferDesc{
        .debugName = std::string(debugName),
        .lifetime = lifetime,
        .initialOwnership = initialOwnership,
        .size = buffer.size(),
        .usageIntents = std::vector<BufferUsageIntent>{usageIntents},
        .importedResource = std::cref(buffer),
    });
}

[[nodiscard]] GraphResourceHandle NodeBuildContext::importAccelerationStructure(const nr::rhi::AccelerationStructureResource &accelerationStructure, std::string_view debugName, ResourceLifetime lifetime, ResourceOwnershipDomain initialOwnership)
{
    nrAssert(accelerationStructure.valid(), std::format("{} acceleration structure is invalid.", debugName));

    return addResource(GraphImportedAccelerationStructureDesc{
        .debugName = std::string(debugName),
        .lifetime = lifetime,
        .initialOwnership = initialOwnership,
        .type = accelerationStructure.type(),
        .size = accelerationStructure.size(),
        .importedResource = std::cref(accelerationStructure),
    });
}

[[nodiscard]] GraphResourceHandle NodeBuildContext::importSwapchain(std::string_view debugName, const NodeFrameParameters &frameParameters)
{
    return addResource(GraphImportedSwapchainImageDesc{
        .debugName = std::string(debugName),
        .extent =
            vk::Extent3D{
                frameParameters.swapchainExtent.width,
                frameParameters.swapchainExtent.height,
                1,
            },
        .format = frameParameters.swapchainFormat,
    });
}

[[nodiscard]] GraphPassHandle NodeBuildContext::addPass(std::span<const PassResourceUseDesc> intentList, std::string_view debugName, PassRecordCallback executeLambda, PassPrepareCallback prepareCallback, bool isCopyPass, vk::PipelineStageFlags2 shaderStages)
{
    return graphBuilder.get().addPass(debugName, nodeHandle, intentList, std::move(executeLambda), std::move(prepareCallback), isCopyPass, shaderStages);
}

[[nodiscard]] GraphPassHandle NodeBuildContext::addPass(std::span<const PassResourceUseDesc> intentList, std::string_view debugName, PassParallelRecordDesc parallelRecord, PassPrepareCallback prepareCallback, vk::PipelineStageFlags2 shaderStages)
{
    return graphBuilder.get().addPass(debugName, nodeHandle, intentList, std::move(parallelRecord), std::move(prepareCallback), shaderStages);
}

[[nodiscard]] GraphSubmitHandle NodeBuildContext::addSubmitNode(std::string_view debugName)
{
    return graphBuilder.get().addSubmitNode(debugName);
}

[[nodiscard]] GraphSubmitHandle NodeBuildContext::addSwapchainAcquireNode(std::string_view debugName)
{
    return graphBuilder.get().addSubmitNode(debugName, SubmitBoundaryKind::SwapchainAcquire);
}

[[nodiscard]] GraphResourceHandle NodeBuildContext::importImage(const nr::rhi::Image &image, std::string_view debugName, vk::Extent2D extent, vk::Format format, ResourceLifetime lifetime, std::initializer_list<ImageUsageIntent> usageIntents, ImageAspectIntent aspect)
{
    nrAssert(image.valid(), std::format("{} image is invalid.", debugName));

    auto desc = GraphImportedImageDesc{
        .debugName = std::string(debugName),
        .lifetime = lifetime,
        .extent = vk::Extent3D{extent.width, extent.height, 1},
        .format = format,
        .usageIntents = std::vector<ImageUsageIntent>(usageIntents),
        .importedResource = std::cref(image),
    };

    if (aspect != ImageAspectIntent::Color)
    {
        desc.aspect = aspect;
    }

    return addResource(desc);
}

[[nodiscard]] std::size_t NodeBuildContext::frameSlotIndex(std::size_t frameSlotCount) const
{
    nrAssert(frameSlotCount > 0, "NodeBuildContext frame resource helper requires at least one frame slot.");
    return static_cast<std::size_t>(frameIndex) % frameSlotCount;
}

[[nodiscard]] std::string NodeBuildContext::indexedFrameDebugName(std::string_view debugName, std::size_t frameSlot)
{
    return std::format("{}[{}]", debugName, frameSlot);
}

void FrameUniformArena::initialize(nr::rhi::Device &device, vk::DeviceSize bytesPerFrame, std::string_view debugName)
{
    nrAssert(bytesPerFrame > 0u, "FrameUniformArena::initialize requires bytesPerFrame > 0.");

    debugName_ = debugName;

    auto const limits = device.physicalDevice.getProperties().limits;
    uniformOffsetAlignment_ = std::max<vk::DeviceSize>(1u, limits.minUniformBufferOffsetAlignment);
    maxUniformBufferRange_ = limits.maxUniformBufferRange;
    frameSliceSize_ = alignUp(bytesPerFrame, uniformOffsetAlignment_);

    auto bufferInfo = vk::BufferCreateInfo{};
    bufferInfo.size = frameSliceSize_ * nr::maxFrameInFlight;
    bufferInfo.usage = vk::BufferUsageFlagBits::eUniformBuffer;
    bufferInfo.sharingMode = vk::SharingMode::eExclusive;

    buffer_ = device.resourceFactory.createBuffer(bufferInfo, nr::rhi::MemoryUsage::CpuToGpu, debugName_);
    nrAssert(buffer_.valid(), std::format("FrameUniformArena failed to create uniform buffer '{}'.", debugName_));
}

void FrameUniformArena::beginFrame(std::uint32_t frameIndex)
{
    nrAssert(valid(), "FrameUniformArena::beginFrame requires initialized uniform buffer.");
    auto const frameSlot = static_cast<vk::DeviceSize>(frameIndex % nr::maxFrameInFlight);
    currentFrameBaseOffset_ = frameSlot * frameSliceSize_;
    currentFrameCursor_ = 0;
}

[[nodiscard]] bool FrameUniformArena::valid() const noexcept
{
    return buffer_.valid() && frameSliceSize_ > 0u;
}

[[nodiscard]] FrameUniformBinding FrameUniformArena::uploadBytes(RenderGraphBuilder &graphBuilder, std::string_view debugName, std::span<const std::byte> bytes)
{
    nrAssert(valid(), "FrameUniformArena::uploadBytes requires initialized uniform buffer.");
    nrAssert(!bytes.empty(), "FrameUniformArena::uploadBytes requires a non-empty payload.");

    auto const range = static_cast<vk::DeviceSize>(bytes.size_bytes());
    nrAssert(range <= maxUniformBufferRange_, std::format("FrameUniformArena payload exceeds maxUniformBufferRange. debugName='{}' range={} max={}", debugName, range, maxUniformBufferRange_));
    auto const allocationSize = alignUp(range, uniformOffsetAlignment_);
    nrAssert(currentFrameCursor_ + allocationSize <= frameSliceSize_, std::format("FrameUniformArena frame slice overflow. debugName='{}' cursor={} allocation={} frameSliceSize={}", debugName, currentFrameCursor_, allocationSize, frameSliceSize_));

    auto const offset = currentFrameBaseOffset_ + currentFrameCursor_;
    buffer_.writeMappedAndFlush(bytes, offset);
    currentFrameCursor_ += allocationSize;

    auto resource = graphBuilder.addResource(GraphImportedBufferDesc{
        .debugName = std::format("{}@{}", debugName, offset),
        .lifetime = ResourceLifetime::FrameLocal,
        .size = buffer_.size(),
        .usageIntents =
            {
                BufferUsageIntent::Uniform,
            },
        .importedResource = std::cref(buffer_),
    });

    return FrameUniformBinding{
        .resource = resource,
        .offset = offset,
        .range = range,
    };
}

[[nodiscard]] FrameUniformBinding FrameUniformArena::patchUploadBytes(
    RenderGraphSkeletonPatchContext& patchContext,
    std::size_t resourceSlot,
    std::string_view debugName,
    std::span<const std::byte> bytes)
{
    nrAssert(valid(), "FrameUniformArena::patchUploadBytes requires initialized uniform buffer.");
    nrAssert(!bytes.empty(), "FrameUniformArena::patchUploadBytes requires a non-empty payload.");

    auto const range = static_cast<vk::DeviceSize>(bytes.size_bytes());
    nrAssert(range <= maxUniformBufferRange_, "FrameUniformArena patch payload exceeds maxUniformBufferRange.");
    auto const allocationSize = alignUp(range, uniformOffsetAlignment_);
    nrAssert(currentFrameCursor_ + allocationSize <= frameSliceSize_, "FrameUniformArena patch frame slice overflow.");

    auto const offset = currentFrameBaseOffset_ + currentFrameCursor_;
    buffer_.writeMappedAndFlush(bytes, offset);
    currentFrameCursor_ += allocationSize;
    patchContext.patchResource(resourceSlot, GraphImportedBufferDesc{
        .debugName = std::format("{}@{}", debugName, offset),
        .lifetime = ResourceLifetime::FrameLocal,
        .size = buffer_.size(),
        .usageIntents = {BufferUsageIntent::Uniform},
        .importedResource = std::cref(buffer_),
    });
    return FrameUniformBinding{
        .resource = patchContext.namedResource("Renderer.GlobalFrameUniforms"),
        .offset = offset,
        .range = range,
    };
}

[[nodiscard]] vk::DeviceSize FrameUniformArena::alignUp(vk::DeviceSize value, vk::DeviceSize alignment) noexcept
{
    if (alignment <= 1u)
    {
        return value;
    }
    auto const remainder = value % alignment;
    return remainder == 0u ? value : value + (alignment - remainder);
}

RasterPassBuilder::RasterPassBuilder(NodeBuildContext &context, std::string_view debugName, std::shared_ptr<PipelineRuntime<nr::rhi::GraphicsPipeline>> runtime) : Base(context, debugName, std::move(runtime), "RasterPassBuilder")
{
}

RasterPassBuilder &RasterPassBuilder::viewport(vk::Extent2D extent)
{
    viewportExtent_ = extent;
    return *this;
}

RasterPassBuilder &RasterPassBuilder::viewportYMode(RasterViewportYMode mode)
{
    viewportYMode_ = mode;
    return *this;
}

RasterPassBuilder &RasterPassBuilder::colorAttachment(GraphResourceHandle resource, vk::ClearValue clearValue)
{
    nrAssert(resource.valid(), "RasterPassBuilder::colorAttachment requires a valid graph resource.");
    colorAttachments_.push_back(RasterColorAttachment{
        .resource = resource,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .clearValue = clearValue,
    });
    Base::resourceUse(use::colorReadWrite(resource));
    return *this;
}

RasterPassBuilder &RasterPassBuilder::depthAttachment(GraphResourceHandle resource)
{
    nrAssert(resource.valid(), "RasterPassBuilder::depthAttachment requires a valid graph resource.");
    depthAttachment_ = RasterDepthAttachment{
        .resource = resource,
        .loadOp = vk::AttachmentLoadOp::eClear,
    };
    Base::resourceUse(use::depthReadWrite(resource));
    return *this;
}

RasterPassBuilder &RasterPassBuilder::rasterState(nr::rhi::MeshRasterState state)
{
    rasterState_ = state;
    return *this;
}

RasterPassBuilder &RasterPassBuilder::primitiveTopology(vk::PrimitiveTopology topology)
{
    primitiveTopology_ = topology;
    return *this;
}

RasterPassBuilder &RasterPassBuilder::record(RasterPassRecordCallback callback)
{
    nrAssert(!parallelItemCountCallback_ && !parallelRangeRecordCallback_, "RasterPassBuilder::record conflicts with recordParallel.");
    recordCallback_ = std::move(callback);
    return *this;
}

RasterPassBuilder &RasterPassBuilder::recordParallel(RasterPassItemCountCallback itemCountCallback, RasterPassRangeRecordCallback rangeRecordCallback)
{
    nrAssert(static_cast<bool>(itemCountCallback), "RasterPassBuilder::recordParallel requires an item-count callback.");
    nrAssert(static_cast<bool>(rangeRecordCallback), "RasterPassBuilder::recordParallel requires a range-record callback.");
    nrAssert(!recordCallback_, "RasterPassBuilder::recordParallel conflicts with record.");
    parallelItemCountCallback_ = std::move(itemCountCallback);
    parallelRangeRecordCallback_ = std::move(rangeRecordCallback);
    return *this;
}

[[nodiscard]] RasterPassBuilder::RasterPassRenderingSetup RasterPassBuilder::makeRenderingSetup(const PassRecordContext &recordContext, std::span<const RasterColorAttachment> colorAttachments, const std::optional<RasterDepthAttachment> &depthAttachment, std::optional<vk::Extent2D> viewportExtent,
                                                                                                std::string_view debugName)
{
    nrAssert(static_cast<bool>(recordContext.resolveImage), "RasterPassBuilder record requires image resolver callback.");

    auto setup = RasterPassRenderingSetup{};
    setup.resolvedColors = colorAttachments | std::views::transform([&](const RasterColorAttachment &attachment) {
                               auto image = recordContext.resolveImage(attachment.resource);
                               nrAssert(image.has_value(), std::format("RasterPassBuilder failed to resolve color image for pass '{}'.", debugName));
                               nrAssert(image->view != vk::ImageView{}, std::format("RasterPassBuilder pass '{}' requires a valid color image view.", debugName));
                               return *image;
                           }) |
                           std::ranges::to<std::vector>();

    if (depthAttachment.has_value())
    {
        auto depthImage = recordContext.resolveImage(depthAttachment->resource);
        nrAssert(depthImage.has_value(), std::format("RasterPassBuilder failed to resolve depth image for pass '{}'.", debugName));
        nrAssert(depthImage->view != vk::ImageView{}, std::format("RasterPassBuilder pass '{}' requires a valid depth image view.", debugName));
        setup.resolvedDepth = *depthImage;
    }

    setup.targetExtent = resolveTargetExtent(viewportExtent, setup.resolvedColors, setup.resolvedDepth);
    setup.colorAttachments = std::views::iota(std::size_t{0}, colorAttachments.size()) | std::views::transform([&](std::size_t attachmentIndex) {
                                 auto const &attachment = colorAttachments[attachmentIndex];
                                 auto const &image = setup.resolvedColors[attachmentIndex];
                                 return nr::rhi::ops::RenderingAttachmentDesc{
                                     .imageView = image.view,
                                     .loadOp = attachment.loadOp,
                                     .storeOp = attachment.storeOp,
                                     .clearValue = attachment.clearValue,
                                 };
                             }) |
                             std::ranges::to<std::vector>();

    if (depthAttachment.has_value() && setup.resolvedDepth.has_value())
    {
        setup.depthAttachment = nr::rhi::ops::RenderingDepthStencilAttachmentDesc{
            .imageView = setup.resolvedDepth->view,
            .depthLoadOp = depthAttachment->loadOp,
            .depthStoreOp = depthAttachment->storeOp,
            .stencilLoadOp = depthAttachment->stencilLoadOp,
            .stencilStoreOp = depthAttachment->stencilStoreOp,
            .clearValue = depthAttachment->clearValue,
        };
    }

    return setup;
}

[[nodiscard]] PassPrimaryRecordScope RasterPassBuilder::makeDynamicRenderingSecondaryScope(const RasterPassRenderingSetup &setup, const PipelineRuntime<nr::rhi::GraphicsPipeline> &runtime, std::string_view debugName)
{
    auto const &graphicsDesc = runtime.state().graphicsDesc;
    nrAssert(graphicsDesc.has_value(), std::format("RasterPassBuilder pass '{}' requires retained graphics pipeline dynamic-rendering state.", debugName));

    auto dynamicRendering = PassDynamicRenderingSecondaryScope{
        .renderArea = vk::Rect2D{vk::Offset2D{0, 0}, setup.targetExtent},
        .colorAttachments = setup.colorAttachments,
        .depthAttachment = setup.depthAttachment,
        .colorAttachmentFormats = graphicsDesc->colorAttachmentFormats,
        .depthAttachmentFormat = graphicsDesc->depthAttachmentFormat.value_or(vk::Format::eUndefined),
        .stencilAttachmentFormat = graphicsDesc->stencilAttachmentFormat.value_or(vk::Format::eUndefined),
        .rasterizationSamples = graphicsDesc->sampleCount,
    };
    if (graphicsDesc->stencilAttachmentFormat.has_value() && setup.depthAttachment.has_value())
    {
        dynamicRendering.stencilAttachment = setup.depthAttachment;
    }

    return PassPrimaryRecordScope{
        .kind = PassPrimaryRecordScopeKind::DynamicRenderingSecondaryContents,
        .dynamicRendering = std::move(dynamicRendering),
    };
}

void RasterPassBuilder::bindGraphicsSetup(const vk::raii::CommandBuffer &commandBuffer, const PipelineRuntime<nr::rhi::GraphicsPipeline> &runtime, const nr::rhi::ShaderBindingSnapshot &bindingSnapshot, std::uint32_t frameIndex, vk::Extent2D targetExtent, RasterViewportYMode viewportYMode,
                                          nr::rhi::MeshRasterState rasterState, vk::PrimitiveTopology primitiveTopology)
{
    Base::bindPipelinePreparedResourcesAndPushConstants(commandBuffer, runtime, bindingSnapshot, frameIndex);

    auto viewport = vk::Viewport{};
    viewport.x = 0.0f;
    viewport.width = static_cast<float>(targetExtent.width);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    if (viewportYMode == RasterViewportYMode::ClipSpaceYUp)
    {
        viewport.y = static_cast<float>(targetExtent.height);
        viewport.height = -static_cast<float>(targetExtent.height);
    }
    else
    {
        viewport.y = 0.0f;
        viewport.height = static_cast<float>(targetExtent.height);
    }
    commandBuffer.setViewport(0, {viewport});
    commandBuffer.setScissor(0, {vk::Rect2D{vk::Offset2D{0, 0}, targetExtent}});
    commandBuffer.setPrimitiveTopology(primitiveTopology);
    nr::rhi::mesh::applyRasterState(commandBuffer, rasterState);
}

[[nodiscard]] GraphPassHandle RasterPassBuilder::build()
{
    nrAssert(!colorAttachments_.empty(), "RasterPassBuilder::build requires at least one color attachment.");
    auto const hasSerialRecord = static_cast<bool>(recordCallback_);
    auto const hasParallelRecord = static_cast<bool>(parallelItemCountCallback_) || static_cast<bool>(parallelRangeRecordCallback_);
    nrAssert(hasSerialRecord != hasParallelRecord, "RasterPassBuilder::build requires exactly one serial record or parallel record callback.");
    nrAssert(!hasParallelRecord || (parallelItemCountCallback_ && parallelRangeRecordCallback_), "RasterPassBuilder::build parallel record requires both item-count and range-record callbacks.");

    auto common = takeCommonBuildState();
    auto resourceUses = std::move(common.resourceUses);
    auto runtime = std::move(common.runtime);
    auto debugName = std::move(common.debugName);
    auto bindingSnapshot = std::move(common.bindingSnapshot);
    auto prepareCallback = std::move(common.prepareCallback);
    auto colorAttachments = std::move(colorAttachments_);
    auto depthAttachment = depthAttachment_;
    auto viewportExtent = viewportExtent_;
    auto viewportYMode = viewportYMode_;
    auto rasterState = rasterState_;
    auto primitiveTopology = primitiveTopology_;
    auto recordCallback = std::move(recordCallback_);
    auto parallelItemCountCallback = std::move(parallelItemCountCallback_);
    auto parallelRangeRecordCallback = std::move(parallelRangeRecordCallback_);

    if (parallelItemCountCallback && parallelRangeRecordCallback)
    {
        auto parallelRecord = PassParallelRecordDesc{
            .itemCount = std::move(parallelItemCountCallback),
            .primaryScope =
                [runtime, colorAttachments, depthAttachment, viewportExtent, debugName](const PassRecordContext &recordContext) {
                    nrAssert(static_cast<bool>(runtime), "RasterPassBuilder primary scope requires initialized runtime state.");
                    auto setup = makeRenderingSetup(recordContext, std::span<const RasterColorAttachment>{colorAttachments.data(), colorAttachments.size()}, depthAttachment, viewportExtent, debugName);
                    return makeDynamicRenderingSecondaryScope(setup, *runtime, debugName);
                },
            .recordRange =
                [runtime, colorAttachments, depthAttachment, bindingSnapshot, viewportExtent, viewportYMode, rasterState, primitiveTopology, debugName, rangeRecordCallback = std::move(parallelRangeRecordCallback)](const PassRangeRecordContext &rangeContext) {
                    nrAssert(static_cast<bool>(runtime), "RasterPassBuilder range record requires initialized runtime state.");

                    auto setup = makeRenderingSetup(rangeContext.pass, std::span<const RasterColorAttachment>{colorAttachments.data(), colorAttachments.size()}, depthAttachment, viewportExtent, debugName);
                    auto &commandBuffer = rangeContext.commandBuffer.get();
                    bindGraphicsSetup(commandBuffer, *runtime, bindingSnapshot, rangeContext.pass.frameIndex, setup.targetExtent, viewportYMode, rasterState, primitiveTopology);

                    rangeRecordCallback(RasterPassRangeRecordContext{
                        .pass = rangeContext.pass,
                        .plan = rangeContext.plan.get(),
                        .chunkIndex = rangeContext.chunkIndex,
                        .range = rangeContext.range,
                        .commandBuffer = commandBuffer,
                        .descriptorLayout = runtime->state().descriptorLayout,
                        .pipelineLayout = runtime->state().layout,
                        .extent = setup.targetExtent,
                    });
                },
        };

        return context_.get().addPass(std::span<const PassResourceUseDesc>{resourceUses.data(), resourceUses.size()}, debugName, std::move(parallelRecord), std::move(prepareCallback), vk::PipelineStageFlagBits2::eAllGraphics);
    }

    return context_.get().addPass(
        std::span<const PassResourceUseDesc>{resourceUses.data(), resourceUses.size()}, debugName,
        [runtime, colorAttachments, depthAttachment, bindingSnapshot, viewportExtent, viewportYMode, rasterState, primitiveTopology, debugName, recordCallback = std::move(recordCallback)](const PassRecordContext &recordContext) {
            nrAssert(recordContext.commandBuffer.has_value(), "RasterPassBuilder record requires RAII command buffer access.");
            nrAssert(static_cast<bool>(runtime), "RasterPassBuilder record requires initialized runtime state.");

            auto setup = makeRenderingSetup(recordContext, std::span<const RasterColorAttachment>{colorAttachments.data(), colorAttachments.size()}, depthAttachment, viewportExtent, debugName);

            auto renderingScope = nr::rhi::ops::RenderingScopeDesc{
                .renderArea = vk::Rect2D{vk::Offset2D{0, 0}, setup.targetExtent},
                .colorAttachments = std::span<const nr::rhi::ops::RenderingAttachmentDesc>{setup.colorAttachments.data(), setup.colorAttachments.size()},
                .depthAttachment = setup.depthAttachment,
                .stencilAttachment = setup.stencilAttachment,
            };

            auto &commandBuffer = recordContext.commandBuffer->get();
            auto scopedRendering = nr::rhi::ops::ScopedRendering(commandBuffer, renderingScope);
            bindGraphicsSetup(commandBuffer, *runtime, bindingSnapshot, recordContext.frameIndex, setup.targetExtent, viewportYMode, rasterState, primitiveTopology);

            recordCallback(RasterPassRecordContext{
                .pass = recordContext,
                .commandBuffer = commandBuffer,
                .descriptorLayout = runtime->state().descriptorLayout,
                .pipelineLayout = runtime->state().layout,
                .extent = setup.targetExtent,
            });
        },
        std::move(prepareCallback), false, vk::PipelineStageFlagBits2::eAllGraphics);
}

RasterPassPatchBuilder::RasterPassPatchBuilder(
    RenderGraphSkeletonPatchContext& context,
    std::size_t passSlot,
    std::string_view debugName,
    std::shared_ptr<PipelineRuntime<nr::rhi::GraphicsPipeline>> runtime)
    : context_(context)
    , passSlot_(passSlot)
    , debugName_(debugName)
    , runtime_(std::move(runtime))
{
    nrAssert(static_cast<bool>(runtime_) && runtime_->valid(), "RasterPassPatchBuilder requires initialized runtime state.");
    rootCursor_ = runtime_->rootCursor();
}

RasterPassPatchBuilder& RasterPassPatchBuilder::viewport(vk::Extent2D extent)
{
    viewportExtent_ = extent;
    return *this;
}

RasterPassPatchBuilder& RasterPassPatchBuilder::colorAttachment(GraphResourceHandle resource, vk::ClearValue clearValue)
{
    nrAssert(resource.valid(), "RasterPassPatchBuilder::colorAttachment requires a valid resource.");
    colorAttachments_.push_back(RasterColorAttachment{
        .resource = resource,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .clearValue = clearValue,
    });
    return *this;
}

RasterPassPatchBuilder& RasterPassPatchBuilder::rasterState(nr::rhi::MeshRasterState state)
{
    rasterState_ = state;
    return *this;
}

RasterPassPatchBuilder& RasterPassPatchBuilder::prepare(RasterPassPrepareCallback callback)
{
    prepareCallbacks_.push_back(std::move(callback));
    return *this;
}

RasterPassPatchBuilder& RasterPassPatchBuilder::dynamicBindingSnapshot(
    PassBindingSnapshotCallback callback,
    nr::rhi::LogicalDescriptorResolver resolver)
{
    nrAssert(static_cast<bool>(callback), "RasterPassPatchBuilder requires a dynamic snapshot callback.");
    dynamicBindingSnapshots_.push_back(DynamicBindingSnapshotDesc{
        .snapshot = std::move(callback),
        .resolver = std::move(resolver),
    });
    return *this;
}

RasterPassPatchBuilder& RasterPassPatchBuilder::record(RasterPassRecordCallback callback)
{
    recordCallback_ = std::move(callback);
    return *this;
}

void RasterPassPatchBuilder::patch()
{
    nrAssert(!colorAttachments_.empty(), "RasterPassPatchBuilder requires a color attachment.");
    nrAssert(static_cast<bool>(recordCallback_), "RasterPassPatchBuilder requires a record callback.");
    auto bindingSnapshot = rootCursor_.snapshot();
    rootCursor_.clearSnapshot();
    auto runtime = std::move(runtime_);
    auto colorAttachments = std::move(colorAttachments_);
    auto viewportExtent = viewportExtent_;
    auto rasterState = rasterState_;
    auto debugName = debugName_;
    auto prepareCallbacks = std::move(prepareCallbacks_);
    auto dynamicSnapshots = std::move(dynamicBindingSnapshots_);
    auto prepareCallback = [runtime, bindingSnapshot, prepareCallbacks = std::move(prepareCallbacks),
                            dynamicSnapshots = std::move(dynamicSnapshots)](const PassPrepareContext& prepareContext) {
        std::ranges::for_each(prepareCallbacks, [&](const RasterPassPrepareCallback& callback) {
            if (callback)
            {
                callback(prepareContext);
            }
        });
        auto& cache = runtime->descriptorWriteCacheForFrame(prepareContext.frameIndex);
        nr::rhi::updateResourcesForBindingSnapshot(
            runtime->state().bindingPool, runtime->bindingSetsForFrame(prepareContext.frameIndex),
            cache, bindingSnapshot, makeDefaultLogicalDescriptorResolver(prepareContext));
        std::ranges::for_each(dynamicSnapshots, [&](const DynamicBindingSnapshotDesc& desc) {
            auto snapshot = desc.snapshot(prepareContext);
            auto resolver = desc.resolver ? desc.resolver : makeDefaultLogicalDescriptorResolver(prepareContext);
            nr::rhi::updateResourcesForBindingSnapshot(
                runtime->state().bindingPool, runtime->bindingSetsForFrame(prepareContext.frameIndex),
                cache, snapshot, std::move(resolver));
        });
    };
    context_.get().patchPass(
        passSlot_, debugName_, std::move(prepareCallback),
        [runtime, bindingSnapshot, colorAttachments = std::move(colorAttachments), viewportExtent,
         rasterState, debugName = std::move(debugName),
         recordCallback = std::move(recordCallback_)](const PassRecordContext& recordContext) {
            nrAssert(recordContext.commandBuffer.has_value(), "RasterPassPatchBuilder record requires a command buffer.");
            auto setup = RasterPassBuilder::makeRenderingSetup(
                recordContext, colorAttachments, std::nullopt, viewportExtent, debugName);
            auto renderingScope = nr::rhi::ops::RenderingScopeDesc{
                .renderArea = vk::Rect2D{vk::Offset2D{0, 0}, setup.targetExtent},
                .colorAttachments = setup.colorAttachments,
            };
            auto& commandBuffer = recordContext.commandBuffer->get();
            auto scopedRendering = nr::rhi::ops::ScopedRendering(commandBuffer, renderingScope);
            RasterPassBuilder::bindGraphicsSetup(
                commandBuffer, *runtime, bindingSnapshot, recordContext.frameIndex,
                setup.targetExtent, RasterViewportYMode::FramebufferTopLeft,
                rasterState, vk::PrimitiveTopology::eTriangleList);
            recordCallback(RasterPassRecordContext{
                .pass = recordContext,
                .commandBuffer = commandBuffer,
                .descriptorLayout = runtime->state().descriptorLayout,
                .pipelineLayout = runtime->state().layout,
                .extent = setup.targetExtent,
            });
        });
}

[[nodiscard]] vk::Extent2D RasterPassBuilder::resolveTargetExtent(std::optional<vk::Extent2D> viewportExtent, std::span<const PassImageResource> resolvedColors, const std::optional<PassImageResource> &resolvedDepth)
{
    nrAssert(!resolvedColors.empty(), "RasterPassBuilder::resolveTargetExtent requires at least one resolved color image.");

    auto targetExtent = viewportExtent.value_or(vk::Extent2D{
        resolvedColors.front().extent.width,
        resolvedColors.front().extent.height,
    });

    std::ranges::for_each(resolvedColors, [&](const PassImageResource &image) {
        targetExtent = vk::Extent2D{
            std::max(1u, std::min(targetExtent.width, image.extent.width)),
            std::max(1u, std::min(targetExtent.height, image.extent.height)),
        };
    });

    if (resolvedDepth.has_value())
    {
        targetExtent = vk::Extent2D{
            std::max(1u, std::min(targetExtent.width, resolvedDepth->extent.width)),
            std::max(1u, std::min(targetExtent.height, resolvedDepth->extent.height)),
        };
    }

    return targetExtent;
}

ComputePassBuilder::ComputePassBuilder(NodeBuildContext &context, std::string_view debugName, std::shared_ptr<PipelineRuntime<nr::rhi::ComputePipeline>> runtime) : Base(context, debugName, std::move(runtime), "ComputePassBuilder")
{
}

ComputePassBuilder &ComputePassBuilder::record(ComputePassRecordCallback callback)
{
    recordCallback_ = std::move(callback);
    return *this;
}

[[nodiscard]] GraphPassHandle ComputePassBuilder::build()
{
    nrAssert(static_cast<bool>(recordCallback_), "ComputePassBuilder::build requires a record callback.");

    auto common = takeCommonBuildState();
    auto resourceUses = std::move(common.resourceUses);
    auto runtime = std::move(common.runtime);
    auto debugName = std::move(common.debugName);
    auto bindingSnapshot = std::move(common.bindingSnapshot);
    auto prepareCallback = std::move(common.prepareCallback);
    auto recordCallback = std::move(recordCallback_);

    return context_.get().addPass(
        std::span<const PassResourceUseDesc>{resourceUses.data(), resourceUses.size()}, debugName,
        [runtime, bindingSnapshot, recordCallback = std::move(recordCallback)](const PassRecordContext &recordContext) {
            nrAssert(recordContext.commandBuffer.has_value(), "ComputePassBuilder record requires RAII command buffer access.");
            nrAssert(static_cast<bool>(runtime), "ComputePassBuilder record requires initialized runtime state.");

            auto &commandBuffer = recordContext.commandBuffer->get();
            Base::bindPipelinePreparedResourcesAndPushConstants(commandBuffer, *runtime, bindingSnapshot, recordContext.frameIndex);

            recordCallback(ComputePassRecordContext{
                .pass = recordContext,
                .commandBuffer = commandBuffer,
                .descriptorLayout = runtime->state().descriptorLayout,
                .pipelineLayout = runtime->state().layout,
            });
        },
        std::move(prepareCallback), false, vk::PipelineStageFlagBits2::eComputeShader);
}

ComputePassPatchBuilder::ComputePassPatchBuilder(
    RenderGraphSkeletonPatchContext& context,
    std::size_t passSlot,
    std::string_view debugName,
    std::shared_ptr<PipelineRuntime<nr::rhi::ComputePipeline>> runtime)
    : context_(context)
    , passSlot_(passSlot)
    , debugName_(debugName)
    , runtime_(std::move(runtime))
{
    nrAssert(static_cast<bool>(runtime_) && runtime_->valid(), "ComputePassPatchBuilder requires initialized runtime state.");
    rootCursor_ = runtime_->rootCursor();
}

ComputePassPatchBuilder& ComputePassPatchBuilder::sampledImage(
    std::string_view shaderPath,
    GraphResourceHandle resource,
    std::string_view debugName)
{
    nrAssert(resource.valid(), "ComputePassPatchBuilder::sampledImage requires a valid resource.");
    auto cursor = rootCursor_.getPath(shaderPath);
    static_cast<void>(cursor.setObject(nr::rhi::LogicalResourceDescriptorWrite{
        .logicalResourceId = resource.value,
        .debugName = std::string(debugName),
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    }));
    return *this;
}

ComputePassPatchBuilder& ComputePassPatchBuilder::storageImage(
    std::string_view shaderPath,
    GraphResourceHandle resource,
    std::string_view debugName)
{
    nrAssert(resource.valid(), "ComputePassPatchBuilder::storageImage requires a valid resource.");
    auto cursor = rootCursor_.getPath(shaderPath);
    static_cast<void>(cursor.setObject(nr::rhi::LogicalResourceDescriptorWrite{
        .logicalResourceId = resource.value,
        .debugName = std::string(debugName),
    }));
    return *this;
}

ComputePassPatchBuilder& ComputePassPatchBuilder::record(ComputePassRecordCallback callback)
{
    recordCallback_ = std::move(callback);
    return *this;
}

void ComputePassPatchBuilder::patch()
{
    nrAssert(static_cast<bool>(recordCallback_), "ComputePassPatchBuilder::patch requires a record callback.");
    auto bindingSnapshot = rootCursor_.snapshot();
    rootCursor_.clearSnapshot();
    auto runtime = std::move(runtime_);
    auto recordCallback = std::move(recordCallback_);
    auto prepareCallback = [runtime, bindingSnapshot](const PassPrepareContext& prepareContext) {
        auto& descriptorWriteCache = runtime->descriptorWriteCacheForFrame(prepareContext.frameIndex);
        nr::rhi::updateResourcesForBindingSnapshot(
            runtime->state().bindingPool,
            runtime->bindingSetsForFrame(prepareContext.frameIndex),
            descriptorWriteCache,
            bindingSnapshot,
            makeDefaultLogicalDescriptorResolver(prepareContext));
    };
    context_.get().patchPass(
        passSlot_,
        debugName_,
        std::move(prepareCallback),
        [runtime, bindingSnapshot, recordCallback = std::move(recordCallback)](const PassRecordContext& recordContext) {
            nrAssert(recordContext.commandBuffer.has_value(), "ComputePassPatchBuilder record requires RAII command buffer access.");
            auto& commandBuffer = recordContext.commandBuffer->get();
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, runtime->pipeline().raw());
            nr::rhi::bindPreparedResourcesToCommandBuffer(
                commandBuffer,
                vk::PipelineBindPoint::eCompute,
                runtime->state().layout,
                runtime->bindingSetsForFrame(recordContext.frameIndex));
            nr::rhi::pushConstantsToCommandBuffer(commandBuffer, runtime->state().layout, bindingSnapshot);
            recordCallback(ComputePassRecordContext{
                .pass = recordContext,
                .commandBuffer = commandBuffer,
                .descriptorLayout = runtime->state().descriptorLayout,
                .pipelineLayout = runtime->state().layout,
            });
        });
}

RayTracingPassBuilder::RayTracingPassBuilder(NodeBuildContext &context, std::string_view debugName, std::shared_ptr<PipelineRuntime<nr::rhi::RayTracingPipeline>> runtime) : Base(context, debugName, std::move(runtime), "RayTracingPassBuilder")
{
}

RayTracingPassBuilder &RayTracingPassBuilder::record(RayTracingPassRecordCallback callback)
{
    recordCallback_ = std::move(callback);
    return *this;
}

[[nodiscard]] GraphPassHandle RayTracingPassBuilder::build()
{
    nrAssert(static_cast<bool>(recordCallback_), "RayTracingPassBuilder::build requires a record callback.");

    auto common = takeCommonBuildState();
    auto resourceUses = std::move(common.resourceUses);
    auto runtime = std::move(common.runtime);
    auto debugName = std::move(common.debugName);
    auto bindingSnapshot = std::move(common.bindingSnapshot);
    auto prepareCallback = std::move(common.prepareCallback);
    auto recordCallback = std::move(recordCallback_);

    return context_.get().addPass(
        std::span<const PassResourceUseDesc>{resourceUses.data(), resourceUses.size()}, debugName,
        [runtime, bindingSnapshot, recordCallback = std::move(recordCallback)](const PassRecordContext &recordContext) {
            nrAssert(recordContext.commandBuffer.has_value(), "RayTracingPassBuilder record requires RAII command buffer access.");
            nrAssert(static_cast<bool>(runtime), "RayTracingPassBuilder record requires initialized runtime state.");

            auto &commandBuffer = recordContext.commandBuffer->get();
            Base::bindPipelinePreparedResourcesAndPushConstants(commandBuffer, *runtime, bindingSnapshot, recordContext.frameIndex);

            recordCallback(RayTracingPassRecordContext{
                .pass = recordContext,
                .commandBuffer = commandBuffer,
                .descriptorLayout = runtime->state().descriptorLayout,
                .pipelineLayout = runtime->state().layout,
            });
        },
        std::move(prepareCallback), false, vk::PipelineStageFlagBits2::eRayTracingShaderKHR);
}

RayTracingPassPatchBuilder::RayTracingPassPatchBuilder(
    RenderGraphSkeletonPatchContext& context,
    std::size_t passSlot,
    std::string_view debugName,
    std::shared_ptr<PipelineRuntime<nr::rhi::RayTracingPipeline>> runtime)
    : context_(context)
    , passSlot_(passSlot)
    , debugName_(debugName)
    , runtime_(std::move(runtime))
{
    nrAssert(static_cast<bool>(runtime_) && runtime_->valid(), "RayTracingPassPatchBuilder requires initialized runtime state.");
    rootCursor_ = runtime_->rootCursor();
}

RayTracingPassPatchBuilder& RayTracingPassPatchBuilder::descriptor(
    std::string_view shaderPath, GraphResourceHandle resource, std::string_view debugName,
    vk::ImageLayout imageLayout, vk::DeviceSize offset, vk::DeviceSize range)
{
    nrAssert(resource.valid(), "RayTracingPassPatchBuilder descriptor requires a valid resource.");
    auto cursor = rootCursor_.getPath(shaderPath);
    static_cast<void>(cursor.setObject(nr::rhi::LogicalResourceDescriptorWrite{
        .logicalResourceId = resource.value,
        .debugName = std::string(debugName),
        .imageLayout = imageLayout,
        .offset = offset,
        .range = range,
    }));
    return *this;
}

RayTracingPassPatchBuilder& RayTracingPassPatchBuilder::accelerationStructure(
    std::string_view shaderPath, GraphResourceHandle resource, std::string_view debugName)
{
    return descriptor(shaderPath, resource, debugName);
}

RayTracingPassPatchBuilder& RayTracingPassPatchBuilder::sampledImage(
    std::string_view shaderPath, GraphResourceHandle resource, std::string_view debugName)
{
    return descriptor(shaderPath, resource, debugName, vk::ImageLayout::eShaderReadOnlyOptimal);
}

RayTracingPassPatchBuilder& RayTracingPassPatchBuilder::storageImage(
    std::string_view shaderPath, GraphResourceHandle resource, std::string_view debugName)
{
    return descriptor(shaderPath, resource, debugName);
}

RayTracingPassPatchBuilder& RayTracingPassPatchBuilder::storageBuffer(
    std::string_view shaderPath, GraphResourceHandle resource, std::string_view debugName)
{
    return descriptor(shaderPath, resource, debugName);
}

RayTracingPassPatchBuilder& RayTracingPassPatchBuilder::uniform(
    std::string_view shaderPath, GraphResourceHandle resource, std::string_view debugName)
{
    return descriptor(shaderPath, resource, debugName);
}

RayTracingPassPatchBuilder& RayTracingPassPatchBuilder::uniform(
    std::string_view shaderPath, FrameUniformBinding binding, std::string_view debugName)
{
    nrAssert(binding.range > 0u, "RayTracingPassPatchBuilder uniform requires a non-zero range.");
    return descriptor(shaderPath, binding.resource, debugName, vk::ImageLayout::eGeneral, binding.offset, binding.range);
}

RayTracingPassPatchBuilder& RayTracingPassPatchBuilder::prepare(ShaderVisiblePassPrepareCallback callback)
{
    prepareCallbacks_.push_back(std::move(callback));
    return *this;
}

RayTracingPassPatchBuilder& RayTracingPassPatchBuilder::dynamicBindingSnapshot(
    PassBindingSnapshotCallback callback,
    nr::rhi::LogicalDescriptorResolver resolver)
{
    nrAssert(static_cast<bool>(callback), "RayTracingPassPatchBuilder requires a dynamic snapshot callback.");
    dynamicBindingSnapshots_.push_back(DynamicBindingSnapshotDesc{
        .snapshot = std::move(callback),
        .resolver = std::move(resolver),
    });
    return *this;
}

RayTracingPassPatchBuilder& RayTracingPassPatchBuilder::record(RayTracingPassRecordCallback callback)
{
    recordCallback_ = std::move(callback);
    return *this;
}

void RayTracingPassPatchBuilder::patch()
{
    nrAssert(static_cast<bool>(recordCallback_), "RayTracingPassPatchBuilder requires a record callback.");
    auto bindingSnapshot = rootCursor_.snapshot();
    rootCursor_.clearSnapshot();
    auto runtime = std::move(runtime_);
    auto prepareCallbacks = std::move(prepareCallbacks_);
    auto dynamicSnapshots = std::move(dynamicBindingSnapshots_);
    auto prepareCallback = [runtime, bindingSnapshot, prepareCallbacks = std::move(prepareCallbacks),
                            dynamicSnapshots = std::move(dynamicSnapshots)](const PassPrepareContext& prepareContext) {
        std::ranges::for_each(prepareCallbacks, [&](const ShaderVisiblePassPrepareCallback& callback) {
            if (callback)
            {
                callback(prepareContext);
            }
        });
        auto& cache = runtime->descriptorWriteCacheForFrame(prepareContext.frameIndex);
        nr::rhi::updateResourcesForBindingSnapshot(
            runtime->state().bindingPool, runtime->bindingSetsForFrame(prepareContext.frameIndex),
            cache, bindingSnapshot, makeDefaultLogicalDescriptorResolver(prepareContext));
        std::ranges::for_each(dynamicSnapshots, [&](const DynamicBindingSnapshotDesc& desc) {
            auto snapshot = desc.snapshot(prepareContext);
            auto resolver = desc.resolver ? desc.resolver : makeDefaultLogicalDescriptorResolver(prepareContext);
            nr::rhi::updateResourcesForBindingSnapshot(
                runtime->state().bindingPool, runtime->bindingSetsForFrame(prepareContext.frameIndex),
                cache, snapshot, std::move(resolver));
        });
    };
    context_.get().patchPass(
        passSlot_, debugName_, std::move(prepareCallback),
        [runtime, bindingSnapshot, recordCallback = std::move(recordCallback_)](
            const PassRecordContext& recordContext) {
            nrAssert(recordContext.commandBuffer.has_value(), "RayTracingPassPatchBuilder record requires a command buffer.");
            auto& commandBuffer = recordContext.commandBuffer->get();
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, runtime->pipeline().raw());
            nr::rhi::bindPreparedResourcesToCommandBuffer(
                commandBuffer, vk::PipelineBindPoint::eRayTracingKHR, runtime->state().layout,
                runtime->bindingSetsForFrame(recordContext.frameIndex));
            nr::rhi::pushConstantsToCommandBuffer(commandBuffer, runtime->state().layout, bindingSnapshot);
            recordCallback(RayTracingPassRecordContext{
                .pass = recordContext,
                .commandBuffer = commandBuffer,
                .descriptorLayout = runtime->state().descriptorLayout,
                .pipelineLayout = runtime->state().layout,
            });
        });
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
    activeScene_.reset();
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

        bridgeBuildInput.resolveMaterialTextureIds = [&](nr::resource::MaterialHandle materialHandle) -> std::optional<nr::scene::SceneMaterialTextureIds> { return collectSceneMaterialTextureIds(scene, materialHandle, sceneTextureHandlesById); };

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
    auto sameScene = activeScene_.has_value() && std::addressof(activeScene_->get()) == std::addressof(scene);
    if (!sameScene)
    {
        sceneExtractProfile_.reset();
        sceneTlasExtractProfile_.reset();
        previousGlobalFrameConstants_.reset();
    }

    auto needsCreate = !sameScene || !sceneExtractProfile_.has_value() || !sceneExtractProfile_->valid();

    if (needsCreate)
    {
        activeScene_ = std::ref(scene);
        sceneExtractProfile_ = scene.registerExtractProfile(nr::scene::SceneExtractProfileCreateInfo{
            .debugName = "Renderer.DefaultRasterExtract",
        });
        return {*sceneExtractProfile_, true};
    }

    return {*sceneExtractProfile_, false};
}

[[nodiscard]] std::pair<nr::scene::SceneExtractProfileHandle, bool> Renderer::ensureSceneTlasExtractProfile(nr::scene::Scene &scene)
{
    auto sameScene = activeScene_.has_value() && std::addressof(activeScene_->get()) == std::addressof(scene);
    if (!sameScene)
    {
        sceneExtractProfile_.reset();
        sceneTlasExtractProfile_.reset();
        previousGlobalFrameConstants_.reset();
    }

    auto needsCreate = !sameScene || !sceneTlasExtractProfile_.has_value() || !sceneTlasExtractProfile_->valid();

    if (needsCreate)
    {
        activeScene_ = std::ref(scene);
        sceneTlasExtractProfile_ = scene.registerExtractProfile(nr::scene::SceneExtractProfileCreateInfo{
            .debugName = "Renderer.DefaultTlasExtract",
            .domain = nr::scene::ScenePacketDomain::tlasBuildInput,
        });
        return {*sceneTlasExtractProfile_, true};
    }

    return {*sceneTlasExtractProfile_, false};
}

void Renderer::recordCpuTimingSample(const RendererCpuFrameTimings &timings) noexcept
{
    accumulateCpuTimings(cpuTimingAccumulator_, timings);
    ++cpuStatistics_.pendingSampleFrameCount;

    if (cpuStatistics_.pendingSampleFrameCount < nr::statistics::sampleFrameCount())
    {
        return;
    }

    cpuStatistics_.average = averageCpuTimings(cpuTimingAccumulator_, cpuStatistics_.pendingSampleFrameCount);
    cpuStatistics_.averagedFrameCount = cpuStatistics_.pendingSampleFrameCount;
    cpuStatistics_.pendingSampleFrameCount = 0u;
    cpuStatistics_.valid = true;
    cpuTimingAccumulator_ = {};
}

void Renderer::recordGpuPassTimingSample(const GpuPassTimingFrame &timings)
{
    std::ranges::for_each(timings.passes, [&](const GpuPassTimingSample &sample) {
        auto key = std::pair{sample.pass.value, sample.debugName};
        auto [entryIt, inserted] = gpuPassTimingAccumulator_.try_emplace(key);
        if (inserted)
        {
            entryIt->second.pass = sample.pass;
            entryIt->second.debugName = sample.debugName;
            entryIt->second.queue = sample.queue;
            entryIt->second.isCopyPass = sample.isCopyPass;
        }

        entryIt->second.milliseconds += sample.milliseconds;
        ++entryIt->second.sampleCount;
    });

    ++gpuPassStatistics_.pendingSampleFrameCount;
    if (gpuPassStatistics_.pendingSampleFrameCount < nr::statistics::sampleFrameCount())
    {
        return;
    }

    gpuPassStatistics_.averages = averageGpuPassTimings(gpuPassTimingAccumulator_);
    gpuPassStatistics_.averagedFrameCount = gpuPassStatistics_.pendingSampleFrameCount;
    gpuPassStatistics_.pendingSampleFrameCount = 0u;
    gpuPassStatistics_.valid = true;
    gpuPassTimingAccumulator_.clear();
}

[[nodiscard]] double benchmarkType7Quantile(std::vector<double> values, double probability)
{
    if (values.empty() || probability < 0.0 || probability > 1.0 || !std::ranges::all_of(values, [](double value) { return std::isfinite(value); }))
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    std::ranges::sort(values);
    if (values.size() == 1u)
    {
        return values.front();
    }
    auto const position = probability * static_cast<double>(values.size() - 1u);
    auto const lower = static_cast<std::size_t>(std::floor(position));
    auto const upper = static_cast<std::size_t>(std::ceil(position));
    auto const fraction = position - static_cast<double>(lower);
    return values[lower] + (values[upper] - values[lower]) * fraction;
}

[[nodiscard]] RendererBenchmarkDistribution makeRendererBenchmarkDistribution(std::vector<double> values)
{
    auto distribution = RendererBenchmarkDistribution{};
    distribution.count = values.size();
    if (values.empty())
    {
        return distribution;
    }
    distribution.minimum = *std::ranges::min_element(values);
    distribution.maximum = *std::ranges::max_element(values);
    distribution.mean = std::ranges::fold_left(values, 0.0, std::plus{}) / static_cast<double>(values.size());
    auto const variance = std::ranges::fold_left(values | std::views::transform([&](double value) {
                                                     auto const delta = value - distribution.mean;
                                                     return delta * delta;
                                                 }),
                                                 0.0, std::plus{}) /
                          static_cast<double>(values.size());
    distribution.p50 = benchmarkType7Quantile(values, .5);
    distribution.p95 = benchmarkType7Quantile(values, .95);
    distribution.p99 = benchmarkType7Quantile(values, .99);
    distribution.populationStddev = std::sqrt(variance);
    return distribution;
}

[[nodiscard]] std::span<const std::string_view> rendererBenchmarkExecuteCsvColumns() noexcept
{
    static constexpr auto columns = std::array<std::string_view, 22u>{
        std::string_view{"execute_executor_setup_ms"},
        "execute_completed_gpu_timing_readback_ms",
        "execute_timing_setup_ms",
        "execute_per_frame_lookup_ms",
        "execute_swapchain_acquire_ms",
        "execute_deferred_prepare_ms",
        "execute_task_plan_launch_ms",
        "execute_primary_record_before_collect_ms",
        "execute_record_completion_wait_ms",
        "execute_primary_replay_barrier_timestamp_ms",
        "execute_primary_end_and_submit_build_ms",
        "execute_queue_submit_ms",
        "execute_initial_release_record_submit_ms",
        "execute_synthetic_present_record_submit_ms",
        "execute_finalization_ms",
        "execute_accounted_main_thread_ms",
        "execute_unclassified_ms",
        "execute_compiled_batches",
        "execute_acquire_batches",
        "execute_record_tasks",
        "execute_replayed_secondary_command_buffers",
        "execute_queue_submits",
    };
    return columns;
}

[[nodiscard]] std::string_view rendererBenchmarkSchemaVersion() noexcept
{
    return "nr-renderer-benchmark-v2";
}

[[nodiscard]] std::span<const std::string_view> rendererBenchmarkCpuStageColumns() noexcept
{
    static constexpr auto columns = std::array<std::string_view, 13u>{
        std::string_view{"wait_gpu_ms"}, "frame_setup_ms", "scene_ms", "post_scene_ms", "build_ms", "compile_ms", "prepare_ms", "execute_ms", "present_ms", "total_ms", "cpu_work_ms", "classified_ms", "unclassified_ms",
    };
    return columns;
}

[[nodiscard]] std::span<const std::string_view> rendererBenchmarkCpuSubstageColumns() noexcept
{
    static constexpr auto columns = std::array<std::string_view, 8u>{
        std::string_view{"scene_begin_upload_ms"}, "scene_raster_extract_ms", "scene_tlas_extract_ms", "scene_bridge_ms", "tlas_texture_collection_ms", "graph_prelude_ms", "ui_collect_ms", "node_loop_ms",
    };
    return columns;
}

[[nodiscard]] std::span<const std::string_view> rendererBenchmarkExecuteSummarySections() noexcept
{
    static constexpr auto sections = std::array<std::string_view, 2u>{
        std::string_view{"execute_substages"},
        "execute_counts",
    };
    return sections;
}

[[nodiscard]] double rendererBenchmarkClassifiedCpuMilliseconds(const RendererCpuFrameTimings &timings) noexcept
{
    return timings.cpuWaitGpuMilliseconds + timings.frameSetupMilliseconds + timings.sceneMilliseconds + timings.postSceneMilliseconds + timings.buildMilliseconds + timings.compileMilliseconds + timings.prepareMilliseconds + timings.executeMilliseconds + timings.presentMilliseconds;
}

[[nodiscard]] double rendererBenchmarkExecuteAccountedMainThreadMilliseconds(const ExecutorBenchmarkTelemetry &telemetry) noexcept
{
    return telemetry.executorSetupMilliseconds + telemetry.completedGpuTimingReadbackMilliseconds + telemetry.timingSetupMilliseconds + telemetry.perFrameLookupMilliseconds + telemetry.swapchainAcquireMilliseconds + telemetry.deferredPrepareMilliseconds + telemetry.taskPlanLaunchMilliseconds +
           telemetry.primaryRecordBeforeCollectMilliseconds + telemetry.recordCompletionWaitMilliseconds + telemetry.primaryReplayBarrierTimestampMilliseconds + telemetry.primaryEndAndSubmitBuildMilliseconds + telemetry.queueSubmitMilliseconds + telemetry.initialReleaseRecordSubmitMilliseconds +
           telemetry.syntheticPresentRecordSubmitMilliseconds + telemetry.finalizationMilliseconds;
}

[[nodiscard]] bool validateRendererBenchmarkExecuteTelemetry(const RendererBenchmarkFrame &frame) noexcept
{
    constexpr auto accountingEpsilonMilliseconds = 0.001;
    auto const &telemetry = frame.execute;
    auto durations = std::array{
        telemetry.executorSetupMilliseconds,
        telemetry.completedGpuTimingReadbackMilliseconds,
        telemetry.timingSetupMilliseconds,
        telemetry.perFrameLookupMilliseconds,
        telemetry.swapchainAcquireMilliseconds,
        telemetry.deferredPrepareMilliseconds,
        telemetry.taskPlanLaunchMilliseconds,
        telemetry.primaryRecordBeforeCollectMilliseconds,
        telemetry.recordCompletionWaitMilliseconds,
        telemetry.primaryReplayBarrierTimestampMilliseconds,
        telemetry.primaryEndAndSubmitBuildMilliseconds,
        telemetry.queueSubmitMilliseconds,
        telemetry.initialReleaseRecordSubmitMilliseconds,
        telemetry.syntheticPresentRecordSubmitMilliseconds,
        telemetry.finalizationMilliseconds,
        frame.executeAccountedMainThreadMilliseconds,
        frame.executeUnclassifiedMilliseconds,
    };
    auto const accounted = rendererBenchmarkExecuteAccountedMainThreadMilliseconds(telemetry);
    auto const residual = frame.cpu.executeMilliseconds - accounted;
    return std::ranges::all_of(durations, [](double value) { return std::isfinite(value) && value >= 0.0; }) && std::abs(frame.executeAccountedMainThreadMilliseconds - accounted) <= accountingEpsilonMilliseconds && residual >= -accountingEpsilonMilliseconds &&
           std::abs(frame.executeUnclassifiedMilliseconds - std::max(0.0, residual)) <= accountingEpsilonMilliseconds && telemetry.acquireBatchCount <= telemetry.compiledSubmitBatchCount && telemetry.replayedSecondaryCommandBufferCount == telemetry.recordTaskCount &&
           telemetry.queueSubmitCount >= telemetry.compiledSubmitBatchCount && frame.submitBatchCount == telemetry.queueSubmitCount && frame.recordTaskCount == telemetry.recordTaskCount;
}

[[nodiscard]] bool validateBenchmarkFrames(std::span<const RendererBenchmarkFrame> frames)
{
    auto const configurationStable = frames.empty() || std::ranges::all_of(frames, [&](const RendererBenchmarkFrame &frame) {
                                         auto const &first = frames.front();
                                         return frame.configRevision == first.configRevision && frame.displayExtent == first.displayExtent && frame.renderExtent == first.renderExtent;
                                     });
    return configurationStable && std::ranges::adjacent_find(frames, [](const auto &lhs, const auto &rhs) { return lhs.frameOrdinal >= rhs.frameOrdinal; }) == frames.end() && std::ranges::all_of(frames, [](const RendererBenchmarkFrame &frame) {
               auto durations = std::array{frame.cpu.cpuWaitGpuMilliseconds,        frame.cpu.frameSetupMilliseconds, frame.cpu.sceneMilliseconds, frame.cpu.postSceneMilliseconds,    frame.cpu.buildMilliseconds,          frame.cpu.compileMilliseconds,      frame.cpu.prepareMilliseconds,
                                           frame.cpu.executeMilliseconds,           frame.cpu.presentMilliseconds,    frame.cpu.totalMilliseconds, frame.sceneBeginUploadMilliseconds, frame.sceneRasterExtractMilliseconds, frame.sceneTlasExtractMilliseconds, frame.sceneBridgeMilliseconds,
                                           frame.tlasTextureCollectionMilliseconds, frame.graphPreludeMilliseconds,   frame.uiCollectMilliseconds, frame.nodeLoopMilliseconds};
               return std::ranges::all_of(durations, [](double value) { return std::isfinite(value) && value >= 0.0; }) && [&] {
                   auto const classified = rendererBenchmarkClassifiedCpuMilliseconds(frame.cpu);
                   return frame.cpu.totalMilliseconds - frame.cpu.cpuWaitGpuMilliseconds >= -0.001 && frame.cpu.totalMilliseconds - classified >= -0.001 && validateRendererBenchmarkExecuteTelemetry(frame);
               }();
           });
}

[[nodiscard]] RendererBenchmarkQualityAudit auditRendererBenchmark(std::span<const RendererBenchmarkFrame> frames, std::span<const RendererBenchmarkGpuPass> passes, std::span<const RendererBenchmarkGpuFrameStatus> statuses, std::size_t expectedNodeCount,
                                                                   std::span<const double> nodeBuildMilliseconds, std::span<const RendererBenchmarkAsTelemetry> asTelemetry)
{
    auto audit = RendererBenchmarkQualityAudit{};
    audit.framesValid = validateBenchmarkFrames(frames);
    audit.nodeTelemetryValid = nodeBuildMilliseconds.size() == frames.size() * expectedNodeCount;
    audit.accelerationStructureTelemetryValid = asTelemetry.size() == frames.size();
    audit.missingGpuFrames = 0u;
    std::ranges::for_each(nodeBuildMilliseconds, [&](double value) { audit.nodeTelemetryValid = audit.nodeTelemetryValid && std::isfinite(value) && value >= 0.0; });
    std::ranges::for_each(asTelemetry, [&](const RendererBenchmarkAsTelemetry &as) {
        audit.accelerationStructureTelemetryValid = audit.accelerationStructureTelemetryValid && as.recorded && as.available && std::isfinite(as.cacheScanMilliseconds) && as.cacheScanMilliseconds >= 0.0 && std::isfinite(as.metadataPlanMilliseconds) && as.metadataPlanMilliseconds >= 0.0 &&
                                                    std::isfinite(as.cpuWritesMilliseconds) && as.cpuWritesMilliseconds >= 0.0 && std::isfinite(as.tlasSizingMilliseconds) && as.tlasSizingMilliseconds >= 0.0 && std::isfinite(as.graphDeclareMilliseconds) && as.graphDeclareMilliseconds >= 0.0;
    });
    std::ranges::for_each(statuses, [&](const RendererBenchmarkGpuFrameStatus &status) {
        auto count = std::ranges::count(statuses, status.frameOrdinal, &RendererBenchmarkGpuFrameStatus::frameOrdinal);
        if (count != 1u)
        {
            ++audit.duplicateGpuStatuses;
        }
        auto frame = std::ranges::find(frames, status.frameOrdinal, &RendererBenchmarkFrame::frameOrdinal);
        if (frame == frames.end())
        {
            ++audit.extraGpuStatuses;
        }
        if (!status.complete || status.expectedPassCount != status.availablePassCount)
        {
            ++audit.partialGpuFrames;
        }
    });
    std::ranges::for_each(frames, [&](const RendererBenchmarkFrame &frame) {
        auto statusCount = std::ranges::count(statuses, frame.frameOrdinal, &RendererBenchmarkGpuFrameStatus::frameOrdinal);
        if (statusCount == 0u)
        {
            ++audit.missingGpuFrames;
        }
        auto framePasses = passes | std::views::filter([&](const RendererBenchmarkGpuPass &pass) { return pass.frameOrdinal == frame.frameOrdinal; });
        auto seen = std::set<std::uint32_t>{};
        auto rowCount = std::size_t{0u};
        std::ranges::for_each(framePasses, [&](const RendererBenchmarkGpuPass &pass) {
            ++rowCount;
            if (!seen.insert(pass.passIndex).second)
            {
                ++audit.duplicateGpuPasses;
            }
            if (!std::isfinite(pass.milliseconds) || pass.milliseconds < 0.0)
            {
                ++audit.invalidGpuDurations;
            }
        });
        auto status = std::ranges::find(statuses, frame.frameOrdinal, &RendererBenchmarkGpuFrameStatus::frameOrdinal);
        if (status != statuses.end() && rowCount != status->availablePassCount)
        {
            ++audit.passRowCountMismatchFrames;
        }
    });
    auto baseline = std::map<std::uint32_t, std::tuple<std::string, QueueDomain, std::uint32_t, bool>>{};
    if (!frames.empty())
    {
        std::ranges::for_each(passes, [&](const RendererBenchmarkGpuPass &pass) {
            if (pass.frameOrdinal == frames.front().frameOrdinal)
            {
                baseline.emplace(pass.passIndex, std::tuple{pass.debugName, pass.queue, pass.batchIndex, pass.isCopyPass});
            }
        });
    }
    if (!frames.empty() && baseline.empty())
    {
        audit.valid = false;
    }
    std::ranges::for_each(frames, [&](const RendererBenchmarkFrame &frame) {
        auto schema = std::map<std::uint32_t, std::tuple<std::string, QueueDomain, std::uint32_t, bool>>{};
        std::ranges::for_each(passes, [&](const RendererBenchmarkGpuPass &pass) {
            if (pass.frameOrdinal == frame.frameOrdinal)
            {
                schema.emplace(pass.passIndex, std::tuple{pass.debugName, pass.queue, pass.batchIndex, pass.isCopyPass});
            }
        });
        if (schema != baseline)
        {
            ++audit.schemaDriftFrames;
        }
    });
    std::ranges::for_each(passes, [&](const RendererBenchmarkGpuPass &pass) {
        if (std::ranges::find(frames, pass.frameOrdinal, &RendererBenchmarkFrame::frameOrdinal) == frames.end())
        {
            ++audit.extraGpuPassFrames;
        }
    });
    audit.valid = audit.framesValid && audit.nodeTelemetryValid && audit.accelerationStructureTelemetryValid && audit.missingGpuFrames == 0u && audit.partialGpuFrames == 0u && audit.extraGpuStatuses == 0u && audit.duplicateGpuStatuses == 0u && audit.invalidGpuDurations == 0u &&
                  audit.duplicateGpuPasses == 0u && audit.passRowCountMismatchFrames == 0u && audit.extraGpuPassFrames == 0u && audit.schemaDriftFrames == 0u;
    return audit;
}

void Renderer::configureBenchmark(RendererBenchmarkConfig config)
{
    benchmarkConfig_ = std::move(config);
    benchmarkFrames_.clear();
    benchmarkGpuPasses_.clear();
    benchmarkGpuFrameStatuses_.clear();
    benchmarkGpuPassNames_.clear();
    benchmarkNodeNames_.clear();
    benchmarkCurrentNodeBuildMilliseconds_.clear();
    benchmarkNodeBuildMilliseconds_.clear();
    benchmarkAsTelemetry_.clear();
    benchmarkWarmupAccepted_ = 0u;
    benchmarkDrainRendered_ = 0u;
    benchmarkStartedAt_ = std::chrono::system_clock::now();
    benchmarkFinalized_ = false;
    benchmarkSucceeded_ = false;
    if (!benchmarkConfig_.enabled)
    {
        benchmarkPhase_ = RendererBenchmarkPhase::disabled;
        return;
    }
    nrAssert(benchmarkConfig_.measureFrames > 0u, "Renderer benchmark requires measureFrames > 0.");
    nrAssert(!benchmarkConfig_.outputDirectory.empty(), "Renderer benchmark requires an output directory.");
    nrAssert(graphInstalled_, "Renderer benchmark must be configured after graph installation.");
    benchmarkNodeNames_ = installedNodes_ | std::views::transform([](const InstalledNode &node) { return node.config.instanceName; }) | std::ranges::to<std::vector>();
    benchmarkCurrentNodeBuildMilliseconds_.resize(benchmarkNodeNames_.size());
    benchmarkFrames_.reserve(benchmarkConfig_.measureFrames);
    benchmarkGpuPasses_.reserve(static_cast<std::size_t>(benchmarkConfig_.measureFrames) * 64u);
    benchmarkGpuFrameStatuses_.reserve(benchmarkConfig_.measureFrames);
    benchmarkGpuPassNames_.reserve(64u);
    benchmarkNodeBuildMilliseconds_.reserve(benchmarkConfig_.measureFrames * benchmarkNodeNames_.size());
    benchmarkAsTelemetry_.reserve(benchmarkConfig_.measureFrames);
    benchmarkPhase_ = benchmarkConfig_.warmupFrames == 0u ? RendererBenchmarkPhase::measure : RendererBenchmarkPhase::warmup;
}

void Renderer::configureRenderGraphSkeletonMode(RenderGraphSkeletonMode mode) noexcept
{
    if (renderGraphSkeletonMode_ == mode)
    {
        return;
    }
    renderGraphSkeletonMode_ = mode;
    cacheSuite_.skeletonCache.clear(RenderGraphSkeletonMissReason::Invalidated);
}

[[nodiscard]] RenderGraphSkeletonMode Renderer::renderGraphSkeletonMode() const noexcept
{
    return renderGraphSkeletonMode_;
}

[[nodiscard]] RenderGraphSkeletonCacheStatistics Renderer::renderGraphSkeletonStatistics() const noexcept
{
    return cacheSuite_.skeletonCache.statistics();
}

[[nodiscard]] bool Renderer::benchmarkComplete() const noexcept
{
    return benchmarkPhase_ == RendererBenchmarkPhase::finalized;
}

void Renderer::recordBenchmarkGpuPassTimings(const GpuPassTimingFrame &timings)
{
    if (!benchmarkConfig_.enabled || benchmarkFrames_.empty() || timings.frameOrdinal < benchmarkFrames_.front().frameOrdinal || timings.frameOrdinal > benchmarkFrames_.back().frameOrdinal)
    {
        return;
    }
    auto statusIt = std::ranges::lower_bound(benchmarkGpuFrameStatuses_, timings.frameOrdinal, {}, &RendererBenchmarkGpuFrameStatus::frameOrdinal);
    if (statusIt != benchmarkGpuFrameStatuses_.end() && statusIt->frameOrdinal == timings.frameOrdinal)
    {
        return;
    }
    benchmarkGpuFrameStatuses_.insert(statusIt, RendererBenchmarkGpuFrameStatus{
                                                    .frameOrdinal = timings.frameOrdinal,
                                                    .expectedPassCount = timings.expectedPassCount,
                                                    .availablePassCount = timings.availablePassCount,
                                                    .complete = timings.complete,
                                                });
    std::ranges::for_each(timings.passes, [&](const GpuPassTimingSample &sample) {
        auto const passIndex = sample.pass.value;
        if (benchmarkGpuPassNames_.size() <= passIndex)
        {
            benchmarkGpuPassNames_.resize(static_cast<std::size_t>(passIndex) + 1u);
        }
        if (benchmarkGpuPassNames_[passIndex].empty())
        {
            benchmarkGpuPassNames_[passIndex] = sample.debugName;
        }
        benchmarkGpuPasses_.push_back(RendererBenchmarkGpuPass{
            .frameOrdinal = timings.frameOrdinal,
            .passIndex = passIndex,
            .debugName = sample.debugName,
            .queue = sample.queue,
            .batchIndex = sample.batchIndex,
            .isCopyPass = sample.isCopyPass,
            .milliseconds = sample.milliseconds,
        });
    });
}

[[nodiscard]] bool Renderer::finalizeBenchmark()
{
    if (!benchmarkConfig_.enabled)
    {
        return true;
    }
    if (benchmarkFinalized_)
    {
        return benchmarkSucceeded_;
    }
    if (!benchmarkComplete())
    {
        nr::nrLog(nr::LogLevel::error, "BENCHMARK", "Benchmark finalization requested before drain completed.");
        return false;
    }
    auto error = std::error_code{};
    std::filesystem::create_directories(benchmarkConfig_.outputDirectory, error);
    if (error)
    {
        nr::nrLog(nr::LogLevel::error, "BENCHMARK", std::format("Failed to create benchmark output directory '{}': {}", benchmarkConfig_.outputDirectory.string(), error.message()));
        return false;
    }
    auto csv = [&](std::string_view value) {
        auto const needsQuotes = value.contains(',') || value.contains('"') || value.contains('\n') || value.contains('\r');
        if (!needsQuotes)
        {
            return std::string{value};
        }
        auto escaped = std::string{};
        std::ranges::for_each(value, [&](char character) {
            escaped += character;
            if (character == '"')
            {
                escaped += character;
            }
        });
        return std::format("\"{}\"", escaped);
    };
    auto const audit = auditRendererBenchmark(benchmarkFrames_, benchmarkGpuPasses_, benchmarkGpuFrameStatuses_, benchmarkNodeNames_.size(), benchmarkNodeBuildMilliseconds_, benchmarkAsTelemetry_);
    auto const dataValid = audit.valid;
    auto const requested = benchmarkConfig_.measureFrames;
    auto const accepted = benchmarkFrames_.size();
    auto const runStatus = dataValid && accepted == requested ? "valid" : "invalid";
    auto const displayExtent = benchmarkFrames_.empty() ? vk::Extent2D{1u, 1u} : benchmarkFrames_.front().displayExtent;
    auto const renderExtent = benchmarkFrames_.empty() ? vk::Extent2D{1u, 1u} : benchmarkFrames_.front().renderExtent;
    auto const startEpochMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(benchmarkStartedAt_.time_since_epoch()).count();
    auto const endEpochMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    auto const props = device_->physicalDevice.getProperties();
    using Json = ::dependency::json::JsonValue;
    using JsonArray = Json::Array;
    using JsonObject = Json::Object;
    auto serializeBenchmarkArtifact = [](std::string_view artifactName, const Json &value, std::string &output) {
        constexpr auto maximumSerializedBenchmarkArtifactBytes = std::size_t{16u * 1024u * 1024u};
        auto const serializationError = ::dependency::json::serializeJson(value, output, maximumSerializedBenchmarkArtifactBytes - 1u);
        if (serializationError != ::dependency::json::JsonError::none)
        {
            nr::nrLog(nr::LogLevel::error, "BENCHMARK",
                      std::format("Failed to serialize benchmark artifact '{}': JSON error {}.", artifactName, static_cast<std::uint32_t>(serializationError)));
            return false;
        }
        output.push_back('\n');
        return true;
    };
    auto nodeSchema = JsonArray{};
    nodeSchema.reserve(benchmarkNodeNames_.size());
    std::ranges::for_each(std::views::iota(std::size_t{0u}, benchmarkNodeNames_.size()), [&](std::size_t nodeIndex) {
        nodeSchema.emplace_back(JsonObject{
            {"column", Json{std::format("node_build_{}_ms", nodeIndex)}},
            {"name", Json{benchmarkNodeNames_[nodeIndex]}},
        });
    });
    auto metadataDocument = Json{JsonObject{
        {"schema", Json{rendererBenchmarkSchemaVersion()}},
        {"run_status", Json{runStatus}},
        {"run_id", Json{std::format("{}", startEpochMilliseconds)}},
        {"os", Json{"Windows"}},
        {"start_epoch_ms", Json{static_cast<std::int64_t>(startEpochMilliseconds)}},
        {"end_epoch_ms", Json{static_cast<std::int64_t>(endEpochMilliseconds)}},
        {"argv", Json{benchmarkConfig_.commandLine}},
        {"pipeline", Json{benchmarkConfig_.pipelineId}},
        {"model", Json{benchmarkConfig_.modelPath}},
        {"dlss_quality", Json{benchmarkConfig_.dlssQuality}},
#if defined(NDEBUG)
        {"build_config", Json{"Release"}},
        {"validation_enabled", Json{false}},
#else
        {"build_config", Json{"Debug"}},
        {"validation_enabled", Json{true}},
#endif
        {"ui_mode", Json{"visible-static"}},
        {"display_extent",
         Json{JsonArray{
             Json{static_cast<std::uint64_t>(displayExtent.width)},
             Json{static_cast<std::uint64_t>(displayExtent.height)},
         }}},
        {"render_extent",
         Json{JsonArray{
             Json{static_cast<std::uint64_t>(renderExtent.width)},
             Json{static_cast<std::uint64_t>(renderExtent.height)},
         }}},
        {"node_build_columns", Json{std::move(nodeSchema)}},
        {"gpu", Json{std::string_view{props.deviceName.data()}}},
        {"driver_version", Json{static_cast<std::uint64_t>(props.driverVersion)}},
        {"cpu_logical_threads", Json{static_cast<std::uint64_t>(std::thread::hardware_concurrency())}},
        {"frames_in_flight", Json{static_cast<std::uint64_t>(nr::maxFrameInFlight)}},
        {"warmup_requested", Json{static_cast<std::uint64_t>(benchmarkConfig_.warmupFrames)}},
        {"warmup_accepted", Json{static_cast<std::uint64_t>(benchmarkWarmupAccepted_)}},
        {"measure_requested", Json{static_cast<std::uint64_t>(requested)}},
        {"measure_accepted", Json{static_cast<std::uint64_t>(accepted)}},
        {"drain_rendered", Json{static_cast<std::uint64_t>(benchmarkDrainRendered_)}},
        {"time_unit", Json{"milliseconds"}},
        {"cpu_nesting",
         Json{"top-level stages are mutually exclusive wall-clock intervals; Frame Setup excludes Wait GPU; Post Scene is top-level; CPU substages are nested diagnostics and must not be summed with top-level stages"}},
        {"gpu_semantics", Json{"per-pass timestamp durations only; cross-queue values are not a frame critical path"}},
        {"quantile", Json{"Hyndman-Fan type 7"}},
    }};
    auto metadataText = std::string{};
    if (!serializeBenchmarkArtifact("metadata.json", metadataDocument, metadataText))
    {
        return false;
    }
    auto metadata = std::ofstream{benchmarkConfig_.outputDirectory / "metadata.json", std::ios::binary};
    auto frames = std::ofstream{benchmarkConfig_.outputDirectory / "frames.csv", std::ios::binary};
    auto gpu = std::ofstream{benchmarkConfig_.outputDirectory / "gpu_passes.csv", std::ios::binary};
    auto summary = std::ofstream{benchmarkConfig_.outputDirectory / "summary.json", std::ios::binary};
    if (!metadata || !frames || !gpu || !summary)
    {
        nr::nrLog(nr::LogLevel::error, "BENCHMARK", "Failed to open one or more benchmark artifacts for writing.");
        return false;
    }
    metadata << metadataText;
    frames << "frame_ordinal,frame_slot,config_revision,display_width,display_height,render_width,render_height,dlss_quality";
    std::ranges::for_each(rendererBenchmarkCpuStageColumns(), [&](std::string_view column) { frames << std::format(",{}", column); });
    std::ranges::for_each(rendererBenchmarkCpuSubstageColumns(), [&](std::string_view column) { frames << std::format(",{}", column); });
    frames << ",raster_packets,rt_packets,tlas_packets,submit_batches,record_tasks";
    std::ranges::for_each(rendererBenchmarkExecuteCsvColumns(), [&](std::string_view column) { frames << std::format(",{}", column); });
    std::ranges::for_each(std::views::iota(std::size_t{0u}, benchmarkNodeNames_.size()), [&](std::size_t nodeIndex) { frames << std::format(",node_build_{}_ms", nodeIndex); });
    frames << ",as_recorded,as_available,as_cache_scan_ms,as_metadata_plan_ms,as_cpu_writes_ms,as_tlas_sizing_ms,as_graph_declare_ms,as_packets,as_instances,as_dirty_blas\n";
    std::ranges::for_each(std::views::iota(std::size_t{0u}, benchmarkFrames_.size()), [&](std::size_t frameIndex) {
        const auto &frame = benchmarkFrames_[frameIndex];
        auto const classified = rendererBenchmarkClassifiedCpuMilliseconds(frame.cpu);
        frames << std::format("{},{},{},{},{},{},{},{},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{},{},{},{},{}", frame.frameOrdinal, frame.frameSlot, frame.configRevision,
                              frame.displayExtent.width, frame.displayExtent.height, frame.renderExtent.width, frame.renderExtent.height, csv(benchmarkConfig_.dlssQuality), frame.cpu.cpuWaitGpuMilliseconds, frame.cpu.frameSetupMilliseconds, frame.cpu.sceneMilliseconds,
                              frame.cpu.postSceneMilliseconds, frame.cpu.buildMilliseconds, frame.cpu.compileMilliseconds, frame.cpu.prepareMilliseconds, frame.cpu.executeMilliseconds, frame.cpu.presentMilliseconds, frame.cpu.totalMilliseconds,
                              frame.cpu.totalMilliseconds - frame.cpu.cpuWaitGpuMilliseconds, classified, frame.cpu.totalMilliseconds - classified, frame.sceneBeginUploadMilliseconds, frame.sceneRasterExtractMilliseconds, frame.sceneTlasExtractMilliseconds, frame.sceneBridgeMilliseconds,
                              frame.tlasTextureCollectionMilliseconds, frame.graphPreludeMilliseconds, frame.uiCollectMilliseconds, frame.nodeLoopMilliseconds, frame.sceneRasterPacketCount, frame.sceneRtPacketCount, frame.sceneTlasPacketCount, frame.submitBatchCount, frame.recordTaskCount);
        auto const &execute = frame.execute;
        frames << std::format(",{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{},{},{},{},{},", execute.executorSetupMilliseconds, execute.completedGpuTimingReadbackMilliseconds, execute.timingSetupMilliseconds,
                              execute.perFrameLookupMilliseconds, execute.swapchainAcquireMilliseconds, execute.deferredPrepareMilliseconds, execute.taskPlanLaunchMilliseconds, execute.primaryRecordBeforeCollectMilliseconds, execute.recordCompletionWaitMilliseconds,
                              execute.primaryReplayBarrierTimestampMilliseconds, execute.primaryEndAndSubmitBuildMilliseconds, execute.queueSubmitMilliseconds, execute.initialReleaseRecordSubmitMilliseconds, execute.syntheticPresentRecordSubmitMilliseconds, execute.finalizationMilliseconds,
                              frame.executeAccountedMainThreadMilliseconds, frame.executeUnclassifiedMilliseconds, execute.compiledSubmitBatchCount, execute.acquireBatchCount, execute.recordTaskCount, execute.replayedSecondaryCommandBufferCount, execute.queueSubmitCount);
        auto const nodeOffset = frameIndex * benchmarkNodeNames_.size();
        std::ranges::for_each(std::views::iota(std::size_t{0u}, benchmarkNodeNames_.size()), [&](std::size_t nodeIndex) { frames << std::format(",{:.9f}", benchmarkNodeBuildMilliseconds_[nodeOffset + nodeIndex]); });
        const auto &as = benchmarkAsTelemetry_[frameIndex];
        frames << std::format(",{},{},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{},{},{}\n", as.recorded, as.available, as.cacheScanMilliseconds, as.metadataPlanMilliseconds, as.cpuWritesMilliseconds, as.tlasSizingMilliseconds, as.graphDeclareMilliseconds, as.packetCount, as.instanceCount,
                              as.dirtyBlasCount);
    });
    gpu << "frame_ordinal,pass_index,pass_name,queue,batch_index,is_copy_pass,milliseconds,expected_passes,available_passes,complete\n";
    std::ranges::for_each(benchmarkGpuPasses_, [&](const RendererBenchmarkGpuPass &pass) {
        auto const &name = pass.debugName;
        auto status = std::ranges::lower_bound(benchmarkGpuFrameStatuses_, pass.frameOrdinal, {}, &RendererBenchmarkGpuFrameStatus::frameOrdinal);
        nrAssert(status != benchmarkGpuFrameStatuses_.end() && status->frameOrdinal == pass.frameOrdinal, "Benchmark GPU pass must have a frame status.");
        gpu << std::format("{},{},{},{},{},{},{:.9f},{},{},{}\n", pass.frameOrdinal, pass.passIndex, csv(name), static_cast<std::uint32_t>(pass.queue), pass.batchIndex, pass.isCopyPass, pass.milliseconds, status->expectedPassCount, status->availablePassCount, status->complete);
    });
    auto const statisticsObject = [](std::vector<double> values) {
        auto const distribution = makeRendererBenchmarkDistribution(std::move(values));
        return Json{JsonObject{
            {"count", Json{static_cast<std::uint64_t>(distribution.count)}},
            {"min", Json{distribution.minimum}},
            {"p50", Json{distribution.p50}},
            {"p95", Json{distribution.p95}},
            {"p99", Json{distribution.p99}},
            {"max", Json{distribution.maximum}},
            {"mean", Json{distribution.mean}},
            {"stddev", Json{distribution.populationStddev}},
        }};
    };
    auto frameStatistics = [&](auto accessor) { return statisticsObject(benchmarkFrames_ | std::views::transform(accessor) | std::ranges::to<std::vector<double>>()); };
    auto asStatistics = [&](auto accessor) { return statisticsObject(benchmarkAsTelemetry_ | std::views::transform(accessor) | std::ranges::to<std::vector<double>>()); };
    auto cpuStages = JsonObject{
        {"wait_gpu_ms", frameStatistics([](const auto &frame) { return frame.cpu.cpuWaitGpuMilliseconds; })},
        {"frame_setup_ms", frameStatistics([](const auto &frame) { return frame.cpu.frameSetupMilliseconds; })},
        {"scene_ms", frameStatistics([](const auto &frame) { return frame.cpu.sceneMilliseconds; })},
        {"post_scene_ms", frameStatistics([](const auto &frame) { return frame.cpu.postSceneMilliseconds; })},
        {"build_ms", frameStatistics([](const auto &frame) { return frame.cpu.buildMilliseconds; })},
        {"compile_ms", frameStatistics([](const auto &frame) { return frame.cpu.compileMilliseconds; })},
        {"prepare_ms", frameStatistics([](const auto &frame) { return frame.cpu.prepareMilliseconds; })},
        {"execute_ms", frameStatistics([](const auto &frame) { return frame.cpu.executeMilliseconds; })},
        {"present_ms", frameStatistics([](const auto &frame) { return frame.cpu.presentMilliseconds; })},
        {"total_ms", frameStatistics([](const auto &frame) { return frame.cpu.totalMilliseconds; })},
        {"cpu_work_ms", frameStatistics([](const auto &frame) { return frame.cpu.totalMilliseconds - frame.cpu.cpuWaitGpuMilliseconds; })},
        {"classified_ms", frameStatistics([](const auto &frame) { return rendererBenchmarkClassifiedCpuMilliseconds(frame.cpu); })},
        {"unclassified_ms", frameStatistics([](const auto &frame) { return frame.cpu.totalMilliseconds - rendererBenchmarkClassifiedCpuMilliseconds(frame.cpu); })},
    };
    auto cpuSubstages = JsonObject{
        {"scene_begin_upload_ms", frameStatistics([](const auto &frame) { return frame.sceneBeginUploadMilliseconds; })},
        {"scene_raster_extract_ms", frameStatistics([](const auto &frame) { return frame.sceneRasterExtractMilliseconds; })},
        {"scene_tlas_extract_ms", frameStatistics([](const auto &frame) { return frame.sceneTlasExtractMilliseconds; })},
        {"scene_bridge_ms", frameStatistics([](const auto &frame) { return frame.sceneBridgeMilliseconds; })},
        {"tlas_texture_collection_ms", frameStatistics([](const auto &frame) { return frame.tlasTextureCollectionMilliseconds; })},
        {"graph_prelude_ms", frameStatistics([](const auto &frame) { return frame.graphPreludeMilliseconds; })},
        {"ui_collect_ms", frameStatistics([](const auto &frame) { return frame.uiCollectMilliseconds; })},
        {"node_loop_ms", frameStatistics([](const auto &frame) { return frame.nodeLoopMilliseconds; })},
    };
    auto executeSubstages = JsonObject{
        {"executor_setup_ms", frameStatistics([](const auto &frame) { return frame.execute.executorSetupMilliseconds; })},
        {"completed_gpu_timing_readback_ms", frameStatistics([](const auto &frame) { return frame.execute.completedGpuTimingReadbackMilliseconds; })},
        {"timing_setup_ms", frameStatistics([](const auto &frame) { return frame.execute.timingSetupMilliseconds; })},
        {"per_frame_lookup_ms", frameStatistics([](const auto &frame) { return frame.execute.perFrameLookupMilliseconds; })},
        {"swapchain_acquire_ms", frameStatistics([](const auto &frame) { return frame.execute.swapchainAcquireMilliseconds; })},
        {"deferred_prepare_ms", frameStatistics([](const auto &frame) { return frame.execute.deferredPrepareMilliseconds; })},
        {"task_plan_launch_ms", frameStatistics([](const auto &frame) { return frame.execute.taskPlanLaunchMilliseconds; })},
        {"primary_record_before_collect_ms", frameStatistics([](const auto &frame) { return frame.execute.primaryRecordBeforeCollectMilliseconds; })},
        {"record_completion_wait_ms", frameStatistics([](const auto &frame) { return frame.execute.recordCompletionWaitMilliseconds; })},
        {"primary_replay_barrier_timestamp_ms", frameStatistics([](const auto &frame) { return frame.execute.primaryReplayBarrierTimestampMilliseconds; })},
        {"primary_end_and_submit_build_ms", frameStatistics([](const auto &frame) { return frame.execute.primaryEndAndSubmitBuildMilliseconds; })},
        {"queue_submit_ms", frameStatistics([](const auto &frame) { return frame.execute.queueSubmitMilliseconds; })},
        {"initial_release_record_submit_ms", frameStatistics([](const auto &frame) { return frame.execute.initialReleaseRecordSubmitMilliseconds; })},
        {"synthetic_present_record_submit_ms", frameStatistics([](const auto &frame) { return frame.execute.syntheticPresentRecordSubmitMilliseconds; })},
        {"finalization_ms", frameStatistics([](const auto &frame) { return frame.execute.finalizationMilliseconds; })},
        {"accounted_main_thread_ms", frameStatistics([](const auto &frame) { return frame.executeAccountedMainThreadMilliseconds; })},
        {"unclassified_ms", frameStatistics([](const auto &frame) { return frame.executeUnclassifiedMilliseconds; })},
    };
    auto executeCounts = JsonObject{
        {"compiled_submit_batches", frameStatistics([](const auto &frame) { return static_cast<double>(frame.execute.compiledSubmitBatchCount); })},
        {"acquire_batches", frameStatistics([](const auto &frame) { return static_cast<double>(frame.execute.acquireBatchCount); })},
        {"record_tasks", frameStatistics([](const auto &frame) { return static_cast<double>(frame.execute.recordTaskCount); })},
        {"replayed_secondary_command_buffers", frameStatistics([](const auto &frame) { return static_cast<double>(frame.execute.replayedSecondaryCommandBufferCount); })},
        {"queue_submits", frameStatistics([](const auto &frame) { return static_cast<double>(frame.execute.queueSubmitCount); })},
    };
    auto asTimings = JsonObject{
        {"cache_scan_ms", asStatistics([](const auto &telemetry) { return telemetry.cacheScanMilliseconds; })},
        {"metadata_plan_ms", asStatistics([](const auto &telemetry) { return telemetry.metadataPlanMilliseconds; })},
        {"cpu_writes_ms", asStatistics([](const auto &telemetry) { return telemetry.cpuWritesMilliseconds; })},
        {"tlas_sizing_ms", asStatistics([](const auto &telemetry) { return telemetry.tlasSizingMilliseconds; })},
        {"graph_declare_ms", asStatistics([](const auto &telemetry) { return telemetry.graphDeclareMilliseconds; })},
    };
    auto asCounts = JsonObject{
        {"packets", asStatistics([](const auto &telemetry) { return static_cast<double>(telemetry.packetCount); })},
        {"instances", asStatistics([](const auto &telemetry) { return static_cast<double>(telemetry.instanceCount); })},
        {"dirty_blas", asStatistics([](const auto &telemetry) { return static_cast<double>(telemetry.dirtyBlasCount); })},
    };
    auto nodeSummary = JsonObject{};
    std::ranges::for_each(std::views::iota(std::size_t{0u}, benchmarkNodeNames_.size()), [&](std::size_t nodeIndex) {
        auto values = std::views::iota(std::size_t{0u}, benchmarkFrames_.size()) | std::views::transform([&](std::size_t frameIndex) { return benchmarkNodeBuildMilliseconds_[frameIndex * benchmarkNodeNames_.size() + nodeIndex]; }) | std::ranges::to<std::vector<double>>();
        nodeSummary.emplace(std::format("node_build_{}_ms", nodeIndex), statisticsObject(std::move(values)));
    });
    auto gpuPassSummary = JsonObject{};
    if (!benchmarkFrames_.empty())
    {
        auto baseline = std::map<std::uint32_t, RendererBenchmarkGpuPass>{};
        std::ranges::for_each(benchmarkGpuPasses_, [&](const RendererBenchmarkGpuPass &pass) {
            if (pass.frameOrdinal == benchmarkFrames_.front().frameOrdinal)
            {
                baseline.emplace(pass.passIndex, pass);
            }
        });
        std::ranges::for_each(baseline, [&](const auto &entry) {
            auto const &pass = entry.second;
            auto values = benchmarkGpuPasses_ | std::views::filter([&](const RendererBenchmarkGpuPass &candidate) { return candidate.passIndex == pass.passIndex; }) | std::views::transform([](const RendererBenchmarkGpuPass &candidate) { return candidate.milliseconds; }) |
                          std::ranges::to<std::vector<double>>();
            gpuPassSummary.emplace(
                std::format("pass_{}", pass.passIndex),
                Json{JsonObject{
                    {"pass_index", Json{static_cast<std::uint64_t>(pass.passIndex)}},
                    {"name", Json{pass.debugName}},
                    {"queue", Json{static_cast<std::uint64_t>(pass.queue)}},
                    {"batch_index", Json{static_cast<std::uint64_t>(pass.batchIndex)}},
                    {"copy_to_swapchain", Json{pass.isCopyPass}},
                    {"statistics", statisticsObject(std::move(values))},
                }});
        });
    }
    auto summaryDocument = Json{JsonObject{
        {"schema", Json{rendererBenchmarkSchemaVersion()}},
        {"run_status", Json{runStatus}},
        {"data_quality",
         Json{JsonObject{
             {"frames_valid", Json{audit.framesValid}},
             {"node_telemetry_valid", Json{audit.nodeTelemetryValid}},
             {"as_telemetry_valid", Json{audit.accelerationStructureTelemetryValid}},
             {"accepted_matches_requested", Json{accepted == requested}},
             {"missing_gpu_frames", Json{static_cast<std::uint64_t>(audit.missingGpuFrames)}},
             {"partial_gpu_frames", Json{static_cast<std::uint64_t>(audit.partialGpuFrames)}},
             {"extra_gpu_statuses", Json{static_cast<std::uint64_t>(audit.extraGpuStatuses)}},
             {"duplicate_gpu_statuses", Json{static_cast<std::uint64_t>(audit.duplicateGpuStatuses)}},
             {"invalid_gpu_durations", Json{static_cast<std::uint64_t>(audit.invalidGpuDurations)}},
             {"duplicate_gpu_passes", Json{static_cast<std::uint64_t>(audit.duplicateGpuPasses)}},
             {"schema_drift_frames", Json{static_cast<std::uint64_t>(audit.schemaDriftFrames)}},
             {"pass_row_count_mismatch_frames", Json{static_cast<std::uint64_t>(audit.passRowCountMismatchFrames)}},
             {"extra_gpu_pass_frames", Json{static_cast<std::uint64_t>(audit.extraGpuPassFrames)}},
         }}},
        {"cpu_stages", Json{std::move(cpuStages)}},
        {"cpu_substages", Json{std::move(cpuSubstages)}},
        {"execute_substages", Json{std::move(executeSubstages)}},
        {"execute_counts", Json{std::move(executeCounts)}},
        {"as_timings", Json{std::move(asTimings)}},
        {"as_counts", Json{std::move(asCounts)}},
        {"node_build", Json{std::move(nodeSummary)}},
        {"gpu_passes", Json{std::move(gpuPassSummary)}},
    }};
    auto summaryText = std::string{};
    if (!serializeBenchmarkArtifact("summary.json", summaryDocument, summaryText))
    {
        return false;
    }
    summary << summaryText;
    metadata.flush();
    frames.flush();
    gpu.flush();
    summary.flush();
    auto const streamsGood = static_cast<bool>(metadata) && static_cast<bool>(frames) && static_cast<bool>(gpu) && static_cast<bool>(summary);
    benchmarkSucceeded_ = streamsGood && dataValid && accepted == requested;
    benchmarkFinalized_ = streamsGood;
    return benchmarkSucceeded_;
}
} // namespace nr::renderer
