export module nr.scene:scene;
import dependency.math;
import dependency.ecs;
import dependency.vulkan;

import nr.load;
import nr.resource;
import nr.rhi;
import nr.utils;
import std;
import :bridge;
import :utils;
import :type;

namespace nr::scene::detail
{
template <typename HandleT, typename RecordT> class SlotMapStorage
{
  public:
    [[nodiscard]] HandleT emplace(auto &&builder)
    {
        auto slotIndex = std::uint32_t{};
        if (freeList_.empty())
        {
            slotIndex = static_cast<std::uint32_t>(slots_.size());
            slots_.push_back(Slot{});
        }
        else
        {
            slotIndex = freeList_.back();
            freeList_.pop_back();
        }

        auto &slot = slots_[slotIndex];
        auto handle = HandleT{slotIndex, slot.generation};
        slot.value = builder(handle);
        slot.occupied = true;
        ++liveCount_;
        return handle;
    }

    [[nodiscard]] RecordT *tryGet(HandleT handle) noexcept
    {
        if (!handle.valid() || handle.slot >= slots_.size())
        {
            return nullptr;
        }

        auto &slot = slots_[handle.slot];
        if (!slot.occupied || slot.generation != handle.generation)
        {
            return nullptr;
        }

        return std::addressof(slot.value);
    }

    [[nodiscard]] const RecordT *tryGet(HandleT handle) const noexcept
    {
        if (!handle.valid() || handle.slot >= slots_.size())
        {
            return nullptr;
        }

        auto const &slot = slots_[handle.slot];
        if (!slot.occupied || slot.generation != handle.generation)
        {
            return nullptr;
        }

        return std::addressof(slot.value);
    }

    bool erase(HandleT handle) noexcept
    {
        if (tryGet(handle) == nullptr)
        {
            return false;
        }

        auto &slot = slots_[handle.slot];
        slot.value = RecordT{};
        slot.occupied = false;
        slot.generation = nextGeneration(slot.generation);
        freeList_.push_back(handle.slot);
        --liveCount_;
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return liveCount_;
    }

  private:
    struct Slot
    {
        RecordT value{};
        std::uint32_t generation = 1;
        bool occupied = false;
    };

    [[nodiscard]] static std::uint32_t nextGeneration(std::uint32_t generation) noexcept
    {
        if (generation == std::numeric_limits<std::uint32_t>::max())
        {
            return 1;
        }

        return generation + 1u;
    }

    std::vector<Slot> slots_{};
    std::vector<std::uint32_t> freeList_{};
    std::size_t liveCount_ = 0;
};

template <typename HandleT, typename RecordT> class KeyedSlotMapStorage
{
  public:
    [[nodiscard]] std::pair<HandleT, bool> getOrCreate(std::string stableKey, auto &&builder)
    {
        if (auto it = handlesByStableKey_.find(stableKey); it != handlesByStableKey_.end())
        {
            if (storage_.tryGet(it->second) != nullptr)
            {
                return {it->second, false};
            }
        }

        auto handle = storage_.emplace([&](HandleT newHandle) { return builder(newHandle, stableKey); });
        handlesByStableKey_.emplace(std::move(stableKey), handle);
        return {handle, true};
    }

    [[nodiscard]] RecordT *tryGet(HandleT handle) noexcept
    {
        return storage_.tryGet(handle);
    }

    [[nodiscard]] const RecordT *tryGet(HandleT handle) const noexcept
    {
        return storage_.tryGet(handle);
    }

    [[nodiscard]] std::optional<HandleT> findHandleByStableKey(std::string_view stableKey) const noexcept
    {
        if (auto it = handlesByStableKey_.find(stableKey); it != handlesByStableKey_.end())
        {
            if (storage_.tryGet(it->second) != nullptr)
            {
                return it->second;
            }
        }

        return std::nullopt;
    }

    bool erase(HandleT handle) noexcept
    {
        if (storage_.tryGet(handle) == nullptr)
        {
            return false;
        }

        std::erase_if(handlesByStableKey_, [handle](const auto &entry) { return entry.second == handle; });

        return storage_.erase(handle);
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return storage_.size();
    }

  private:
    SlotMapStorage<HandleT, RecordT> storage_{};
    std::map<std::string, HandleT, std::less<>> handlesByStableKey_{};
};

void registerSceneComponents(flecs::world &world);

[[nodiscard]] flecs::world makeSceneWorld();

inline constexpr std::uint64_t kDefaultRetireLatencySerial = 3;
} // namespace nr::scene::detail

export namespace nr::scene
{
class Scene : public nr::revision::RevisionSyntax
{
    friend struct nr::revision::RevisionSyntax;

  public:
    explicit Scene(const SceneCreateInfo &createInfo);

    Scene() = delete;
    Scene(const Scene &) = delete;
    Scene &operator=(const Scene &) = delete;
    Scene(Scene &&) = delete;
    Scene &operator=(Scene &&) = delete;
    ~Scene() noexcept;

    [[nodiscard]] bool usesDevice(const nr::rhi::Device &device) const noexcept;

    [[nodiscard]] SceneRevisionSnapshot revisionsSnapshot() const noexcept;

    [[nodiscard]] SceneTemplateHandle registerTemplate(const nr::load::SceneAsset &sceneAsset,
                                                       const SceneTemplateCreateInfo &createInfo = {});

    [[nodiscard]] SceneInstanceHandle instantiate(SceneTemplateHandle templateHandle,
                                                  const SceneInstantiateInfo &createInfo = {});

    [[nodiscard]] DestroyTemplateResult destroyTemplate(SceneTemplateHandle templateHandle);

    void destroyInstance(SceneInstanceHandle instanceHandle);

    void updateSimulation(const SceneUpdateInput &input);

    void beginFrame(std::uint32_t frameSlot);

    void uploadPending();

    [[nodiscard]] SceneExtractProfileHandle registerExtractProfile(const SceneExtractProfileCreateInfo &createInfo);

    void destroyExtractProfile(SceneExtractProfileHandle profile);

    [[nodiscard]] ScenePacketSet extractPackets(SceneExtractProfileHandle profile,
                                                const SceneExtractInput &input = {}) const;

    [[nodiscard]] std::optional<SceneResolvedCamera> tryGetPrimaryCamera(
        const std::optional<glm::uvec2> &viewportExtent = std::nullopt) const;

    [[nodiscard]] SceneFrameStamp currentFrameStamp() const noexcept;

    [[nodiscard]] SceneStatistics statistics() const noexcept;

    [[nodiscard]] std::optional<std::reference_wrapper<const SceneTemplateRecord>> tryGetTemplate(
        SceneTemplateHandle handle) const noexcept;

    [[nodiscard]] std::optional<std::reference_wrapper<const SceneInstanceRecord>> tryGetInstance(
        SceneInstanceHandle handle) const noexcept;

    [[nodiscard]] std::optional<std::reference_wrapper<const MeshAssetRecord>> tryGetMeshAsset(
        nr::resource::MeshHandle handle) const noexcept;

    [[nodiscard]] std::optional<std::reference_wrapper<const MaterialAssetRecord>> tryGetMaterialAsset(
        nr::resource::MaterialHandle handle) const noexcept;

    [[nodiscard]] std::optional<std::reference_wrapper<const TextureAssetRecord>> tryGetTextureAsset(
        nr::resource::TextureHandle handle) const noexcept;

    [[nodiscard]] std::optional<SceneSampledTextureBinding> tryGetSampledTextureBinding(
        nr::resource::TextureHandle handle) const noexcept;

    [[nodiscard]] std::optional<std::reference_wrapper<const CameraAssetRecord>> tryGetCameraAsset(
        nr::resource::CameraAssetHandle handle) const noexcept;

    [[nodiscard]] std::optional<std::reference_wrapper<const LightAssetRecord>> tryGetLightAsset(
        nr::resource::LightAssetHandle handle) const noexcept;

    [[nodiscard]] std::optional<SceneBridgeGeometryBuffers> tryGetRasterGeometryBuffers() const noexcept;

    [[nodiscard]] SceneGeometryAtlasBackingGeneration geometryAtlasBackingGeneration() const noexcept;

    [[nodiscard]] SceneGeometryAtlasStats geometryAtlasStats() const noexcept;

    [[nodiscard]] std::optional<SceneAccelerationStructureMeshSemanticKey> tryGetAccelerationStructureMeshSemanticKey(
        nr::resource::MeshHandle handle) const noexcept;

    [[nodiscard]] std::optional<SceneAccelerationStructureMesh> tryGetAccelerationStructureMesh(
        nr::resource::MeshHandle handle) const noexcept;

    [[nodiscard]] std::optional<nr::resource::MeshHandle> findMeshHandleByStableKey(
        std::string_view stableKey) const noexcept;

    [[nodiscard]] std::optional<nr::resource::MaterialHandle> findMaterialHandleByStableKey(
        std::string_view stableKey) const noexcept;

    [[nodiscard]] std::optional<nr::resource::TextureHandle> findTextureHandleByStableKey(
        std::string_view stableKey) const noexcept;

    [[nodiscard]] std::optional<nr::resource::CameraAssetHandle> findCameraHandleByStableKey(
        std::string_view stableKey) const noexcept;

    [[nodiscard]] std::optional<nr::resource::LightAssetHandle> findLightHandleByStableKey(
        std::string_view stableKey) const noexcept;

  private:
    struct RasterMaterialResolution
    {
        SceneMaterialTextureBindings textures{};
        SceneBridgeMaterialRasterState raster{};
        SceneTextureHandleTable textureHandlesById{};
    };

    enum class ImportStage : std::uint8_t
    {
        scene,
        texture,
        material,
        mesh,
        camera,
        light,
        templateRegistration,
        instanceRegistration,
    };

    enum class GpuAssetKind : std::uint8_t
    {
        mesh,
        texture,
    };

    struct GpuAssetHandleRef
    {
        GpuAssetKind kind = GpuAssetKind::mesh;
        std::uint32_t slot = nr::resource::invalidResourceSlot;
        std::uint32_t generation = 0;
    };

    struct PendingGraphicsSyncBatch
    {
        GpuAssetHandleRef asset{};
        std::uint64_t targetGpuVersion = 0;
        std::vector<nr::rhi::ops::BufferUploadTicket> bufferTickets{};
        std::vector<nr::rhi::ops::ImageUploadTicket> imageTickets{};
    };

    struct GraphicsSyncCompletion
    {
        GpuAssetHandleRef asset{};
        std::uint64_t targetGpuVersion = 0;
    };

    struct SubmittedGraphicsSyncWork
    {
        nr::rhi::CommandPool commandPool{};
        std::optional<vk::raii::CommandBuffers> commandBuffers{};
        vk::raii::Fence fence{nullptr};
        std::vector<GraphicsSyncCompletion> completions{};
    };

    struct SubmittedGeometryAtlasGrowWork
    {
        nr::rhi::CommandPool commandPool{};
        std::optional<vk::raii::CommandBuffers> commandBuffers{};
        vk::raii::Fence fence{nullptr};
        detail::RetiredSceneGeometryAtlasBuffers retiredBuffers{};
    };

    struct UploadBudgetState
    {
        std::size_t remaining = 0;
        std::uint64_t frameSerial = std::numeric_limits<std::uint64_t>::max();
        bool consumed = false;
    };

    struct GeometryAtlasRangeAllocationPlan
    {
        vk::DeviceSize byteOffset = 0;
        vk::DeviceSize highWaterBytes = 0;
        std::vector<detail::SceneGeometryAtlasReusableRange> reusableRanges{};
    };

    class TemplateRegistrationTransaction
    {
      public:
        explicit TemplateRegistrationTransaction(Scene &scene) noexcept;
        TemplateRegistrationTransaction(const TemplateRegistrationTransaction &) = delete;
        TemplateRegistrationTransaction &operator=(const TemplateRegistrationTransaction &) = delete;
        TemplateRegistrationTransaction(TemplateRegistrationTransaction &&) = delete;
        TemplateRegistrationTransaction &operator=(TemplateRegistrationTransaction &&) = delete;
        ~TemplateRegistrationTransaction() noexcept;

        void recordCreated(nr::resource::TextureHandle handle);
        void recordCreated(nr::resource::MaterialHandle handle);
        void recordCreated(nr::resource::MeshHandle handle);
        void recordCreated(nr::resource::CameraAssetHandle handle);
        void recordCreated(nr::resource::LightAssetHandle handle);
        void stageTemplate(SceneTemplateHandle handle) noexcept;

        [[nodiscard]] bool commit();

      private:
        template <typename HandleT> static void appendUnique(std::vector<HandleT> &handles, HandleT handle)
        {
            if (std::ranges::find(handles, handle) == handles.end())
            {
                handles.push_back(handle);
            }
        }

        Scene &scene_;
        std::vector<nr::resource::TextureHandle> createdTextures_{};
        std::vector<nr::resource::MaterialHandle> createdMaterials_{};
        std::vector<nr::resource::MeshHandle> createdMeshes_{};
        std::vector<nr::resource::CameraAssetHandle> createdCameras_{};
        std::vector<nr::resource::LightAssetHandle> createdLights_{};
        SceneTemplateHandle stagedTemplate_{};
        bool committed_ = false;
    };

    template <typename HandleT>
    [[nodiscard]] static GpuAssetHandleRef makeGpuHandleRef(HandleT handle, GpuAssetKind kind) noexcept
    {
        return GpuAssetHandleRef{
            .kind = kind,
            .slot = handle.slot,
            .generation = handle.generation,
        };
    }

    template <typename HandleT, typename StorageT, typename Fn>
    static void forEachLiveRecord(std::span<HandleT> handles, StorageT &storage, Fn &&fn)
    {
        std::ranges::for_each(handles, [&](HandleT handle) {
            if (auto *record = storage.tryGet(handle); record != nullptr)
            {
                fn(handle, *record);
            }
        });
    }

    template <typename PayloadT, typename RetiredPayloadT>
    void queueRetiredPayload(std::optional<PayloadT> &payload, std::vector<RetiredPayloadT> &retiredPayloads)
    {
        if (!payload.has_value())
        {
            return;
        }

        retiredPayloads.push_back(RetiredPayloadT{
            .payload = std::move(*payload),
            .retireAfterSerial = currentFrame_.frameSerial + detail::kDefaultRetireLatencySerial,
        });
        payload.reset();
    }

    [[nodiscard]] static bool matchesSelectionMask(std::uint64_t bits, const SceneSelectionMask &mask) noexcept;

    [[nodiscard]] static bool intersectsFrustum(const nr::resource::Aabb &bounds, const SceneFrustum &frustum) noexcept;

    [[nodiscard]] static glm::mat4 buildViewMatrixFromWorld(const glm::mat4 &world) noexcept;

    [[nodiscard]] static std::optional<float> aspectRatioFromViewportExtent(
        const std::optional<glm::uvec2> &viewportExtent) noexcept;

    [[nodiscard]] static float resolveProjectionAspectRatio(const nr::resource::CameraAsset &camera,
                                                            const std::optional<glm::uvec2> &viewportExtent) noexcept;

    [[nodiscard]] static glm::mat4 buildProjectionMatrix(const nr::resource::CameraAsset &camera,
                                                         float aspectRatio) noexcept;

    [[nodiscard]] static SceneFrustum buildFrustumFromViewProjection(const glm::mat4 &viewProjection) noexcept;

    [[nodiscard]] std::optional<SceneResolvedCamera> buildResolvedCamera(
        flecs::entity entity, nr::resource::CameraAssetHandle cameraHandle, bool fallback,
        const std::optional<glm::uvec2> &viewportExtent) const;

    [[nodiscard]] std::optional<SceneFrustum> resolveExtractFrustum(const SceneExtractInput &input) const;

    [[nodiscard]] const flecs::query<const RenderableBinding, const SceneSelectionBits, const ScenePartitionId,
                                     const TlasBucketId, const WorldTransform, const WorldBounds> &
    candidateQueryFor(ScenePacketDomain domain) const noexcept;

    [[nodiscard]] ScenePacketSet extractPacketsDedicated(const SceneExtractProfileRecord &profileRecord,
                                                         const SceneExtractInput &input) const;

    [[nodiscard]] bool belongsToActiveInstance(flecs::entity entity) const;

    [[nodiscard]] bool meshIsResident(nr::resource::MeshHandle meshHandle) const noexcept;

    [[nodiscard]] bool textureIsResident(nr::resource::TextureHandle textureHandle) const noexcept;

    [[nodiscard]] bool materialTexturesReady(const nr::resource::Material &material,
                                             bool allowUnavailableAnisotropy) const noexcept;

    [[nodiscard]] std::optional<nr::resource::MaterialHandle> meshGeometryMaterial(
        nr::resource::MeshHandle meshHandle, std::uint32_t geometryIndex) const noexcept;

    [[nodiscard]] std::optional<SceneBridgeDrawGeometry> tryResolveRasterDrawRange(
        nr::resource::MeshHandle meshHandle, std::uint32_t geometryIndex,
        const SceneBridgeGeometryBuffers &geometryBuffers) const noexcept;

    [[nodiscard]] std::optional<RasterMaterialResolution> resolveRasterMaterial(
        nr::resource::MaterialHandle materialHandle) const noexcept;

    [[nodiscard]] bool renderableReadyForRaster(const RenderableBinding &binding) const noexcept;

    [[nodiscard]] bool renderableReadyForMeshOnlyDomain(const RenderableBinding &binding) const noexcept;

    [[nodiscard]] bool renderableReadyForRayTracing(const RenderableBinding &binding) const noexcept;

    [[nodiscard]] bool renderableReadyForDomain(ScenePacketDomain domain,
                                                const RenderableBinding &binding) const noexcept;

    [[nodiscard]] std::uint64_t rasterSortKey(nr::resource::MeshHandle meshHandle,
                                              nr::resource::MaterialHandle materialHandle, std::uint64_t selectionBits,
                                              std::uint32_t geometryIndex, flecs::entity entity) const noexcept;

    [[nodiscard]] static std::uint32_t makeRayTracingInstanceMask(std::uint64_t selectionBits) noexcept;

    [[nodiscard]] std::uint32_t resolveRenderableGeometryCount(const RenderableBinding &binding) const noexcept;

    template <ScenePacketDomain Domain>
    void appendPacketsForCandidate(ScenePacketSet &packetSet, flecs::entity entity, const RenderableBinding &binding,
                                   std::uint64_t selectionBits, bool resolveRasterResources,
                                   const TlasBucketId &tlasBucketId, const WorldTransform &worldTransform,
                                   const WorldBounds &worldBounds) const
    {
        if constexpr (Domain == ScenePacketDomain::rasterDraw)
        {
            auto const geometryCount = resolveRenderableGeometryCount(binding);
            auto const geometryIndices = std::views::iota(std::uint32_t{0}, geometryCount);
            std::ranges::for_each(geometryIndices, [&](std::uint32_t geometryIndex) {
                auto const materialHandle =
                    meshGeometryMaterial(binding.mesh, geometryIndex).value_or(binding.material);
                auto packet = RasterDrawPacket{
                    .renderable = entity,
                    .mesh = binding.mesh,
                    .material = materialHandle,
                    .geometryIndex = geometryIndex,
                    .world = worldTransform.value,
                    .worldBounds = worldBounds.value,
                    .sortKey = rasterSortKey(binding.mesh, materialHandle, selectionBits, geometryIndex, entity),
                };

                if (resolveRasterResources)
                {
                    auto geometry =
                        tryResolveRasterDrawRange(binding.mesh, geometryIndex, packetSet.rasterGeometryBuffers);
                    auto material = resolveRasterMaterial(materialHandle);
                    nrAssert(geometry.has_value() && material.has_value(),
                             "Raster readiness passed but packet resolution failed for mesh ({}, {}) geometry {} "
                             "and material ({}, {}).",
                             binding.mesh.slot, binding.mesh.generation, geometryIndex, materialHandle.slot,
                             materialHandle.generation);
                    if (!geometry.has_value() || !material.has_value())
                    {
                        return;
                    }

                    packet.geometry = *geometry;
                    packet.materialTextures = material->textures;
                    packet.materialRaster = material->raster;
                    std::ranges::for_each(material->textureHandlesById, [&](const auto &entry) {
                        auto [it, inserted] = packetSet.rasterTextureHandlesById.try_emplace(entry.first, entry.second);
                        nrAssert(inserted || it->second == entry.second,
                                 "Scene texture id resolved to conflicting texture handles during raster extraction.");
                    });
                }

                packetSet.rasterDraws.push_back(std::move(packet));
            });
        }
        else if constexpr (Domain == ScenePacketDomain::rayTracingInstance)
        {
            packetSet.rtInstances.push_back(RayTracingInstancePacket{
                .renderable = entity,
                .mesh = binding.mesh,
                .world = worldTransform.value,
                .instanceMask = makeRayTracingInstanceMask(selectionBits),
                .tlasBucket = tlasBucketId.value,
            });
        }
        else if constexpr (Domain == ScenePacketDomain::tlasBuildInput)
        {
            packetSet.tlasBuildInputs.push_back(TlasBuildInputPacket{
                .renderable = entity,
                .mesh = binding.mesh,
                .world = worldTransform.value,
                .instanceMask = makeRayTracingInstanceMask(selectionBits),
                .tlasBucket = tlasBucketId.value,
            });
        }
    }

    [[nodiscard]] bool uploadContextAvailable() const noexcept;

    [[nodiscard]] bool geometryAtlasReadyForUse() const noexcept;

    [[nodiscard]] nr::rhi::ops::BufferUploadOwnershipPlan makeTransferToGraphicsUploadPlan(
        vk::PipelineStageFlags2 acquireStages, vk::AccessFlags2 acquireAccess) const;

    [[nodiscard]] static std::size_t meshUploadBytes(const nr::resource::Mesh &mesh) noexcept;

    [[nodiscard]] static std::size_t textureUploadBytes(const nr::resource::Texture &texture) noexcept;

    [[nodiscard]] static std::optional<std::reference_wrapper<const nr::resource::ImageLevel>>
    firstTextureLevelWithPixels(const nr::resource::Texture &texture);

    [[nodiscard]] static std::uint32_t checkedDeviceSizeToUint32(vk::DeviceSize value, std::string_view label);

    [[nodiscard]] std::vector<std::uint32_t> makeGeometryAtlasQueueFamilyIndices() const;

    [[nodiscard]] static vk::BufferCreateInfo makeGeometryAtlasBufferCreateInfo(
        vk::DeviceSize size, vk::BufferUsageFlags bindingUsage,
        std::span<const std::uint32_t> queueFamilyIndices) noexcept;

    [[nodiscard]] static vk::DeviceSize grownGeometryAtlasCapacity(vk::DeviceSize currentCapacity,
                                                                   vk::DeviceSize requiredCapacity) noexcept;

    [[nodiscard]] static GeometryAtlasRangeAllocationPlan planGeometryAtlasRangeAllocation(
        std::span<const detail::SceneGeometryAtlasReusableRange> reusableRanges, vk::DeviceSize highWaterBytes,
        vk::DeviceSize byteSize, vk::DeviceSize alignment);

    static void releaseGeometryAtlasRange(std::vector<detail::SceneGeometryAtlasReusableRange> &reusableRanges,
                                          detail::SceneGeometryAtlasReusableRange releasedRange);

    [[nodiscard]] static vk::DeviceSize geometryAtlasReusableBytes(
        std::span<const detail::SceneGeometryAtlasReusableRange> reusableRanges,
        vk::DeviceSize highWaterBytes) noexcept;

    void releaseGeometryAtlasAllocation(const detail::MeshGeometryAtlasAllocation &allocation);

    [[nodiscard]] detail::MeshGeometryAtlasAllocation reserveGeometryAtlasAllocation(
        const nr::resource::Mesh &mesh, nr::rhi::ops::UploadReadbackContext &uploadContext);

    void ensureGeometryAtlasCapacity(vk::DeviceSize requiredVertexBytes, vk::DeviceSize requiredIndexBytes,
                                     nr::rhi::ops::UploadReadbackContext &uploadContext);

    void submitGeometryAtlasGrowCopy(std::optional<nr::rhi::Buffer> oldVertexBuffer,
                                     vk::DeviceSize oldVertexHighWaterBytes,
                                     std::optional<nr::rhi::Buffer> oldIndexBuffer,
                                     vk::DeviceSize oldIndexHighWaterBytes);

    void retireGeometryAtlasBuffers(detail::RetiredSceneGeometryAtlasBuffers retiredBuffers);

    void reapSubmittedGeometryAtlasGrowWork();

    template <typename RecordT> void markRecordUploadQueued(RecordT &record)
    {
        nrAssert(record.gpuState == GpuResidencyState::none || record.gpuState == GpuResidencyState::resident,
                 "Scene upload queue transition requires a new or resident GPU asset.");
        record.gpuState = GpuResidencyState::uploadQueued;
    }

    template <typename RecordT> [[nodiscard]] static bool canConsiderUploadQueue(const RecordT &record) noexcept
    {
        return record.cpuReady && (record.gpuState == GpuResidencyState::none ||
                                   record.gpuState == GpuResidencyState::resident);
    }

    template <typename RecordT> [[nodiscard]] bool needsUploadQueue(const RecordT &record) const noexcept
    {
        return canConsiderUploadQueue(record) && record.cpuVersion > record.gpuVersion;
    }

    template <typename HandleT, typename StorageT> void queueUploadsFor(std::span<HandleT> handles, StorageT &storage)
    {
        forEachLiveRecord(handles, storage, [&](HandleT, auto &record) {
            if (needsUploadQueue(record))
            {
                markRecordUploadQueued(record);
            }
        });
    }

    template <typename HandleT, typename StorageT, typename ExpiredFn>
    static void reapRetiredFor(std::span<HandleT> handles, StorageT &storage, ExpiredFn &&expired)
    {
        forEachLiveRecord(handles, storage, [&](HandleT, auto &record) { std::erase_if(record.retiredGpu, expired); });
    }

    void queueGpuUploadsForFrame();

    void reapRetiredGpuVersions();

    template <typename RecordT> void discardUploadSourceIfConfigured(RecordT &record)
    {
        if (cpuRetention_ != CpuRetentionPolicy::discardUploadSourceAfterResident)
        {
            return;
        }

        if constexpr (std::same_as<RecordT, MeshAssetRecord>)
        {
            record.cpu.vertices.clear();
            record.cpu.indices.clear();
        }
        else if constexpr (std::same_as<RecordT, TextureAssetRecord>)
        {
            std::ranges::for_each(record.cpu.levels, [](nr::resource::ImageLevel &level) { level.bytes.clear(); });
        }
    }

    [[nodiscard]] static std::size_t pixelFormatByteSize(nr::resource::PixelFormat format);

    [[nodiscard]] static std::vector<std::byte> makeFallbackTextureBytes(nr::resource::PixelFormat format);

    [[nodiscard]] static bool uploadBudgetAllows(const UploadBudgetState &budget, std::size_t bytesNeeded) noexcept;

    void recordBudgetedUpload(UploadBudgetState &budget, std::size_t bytesUploaded) noexcept;

    void uploadMeshAsset(nr::resource::MeshHandle handle, MeshAssetRecord &record,
                         nr::rhi::ops::UploadReadbackContext &uploadContext);

    void uploadTextureAsset(nr::resource::TextureHandle handle, TextureAssetRecord &record,
                            nr::rhi::ops::UploadReadbackContext &uploadContext);

    void uploadQueuedMeshAssets(nr::rhi::ops::UploadReadbackContext &uploadContext, UploadBudgetState &budget);

    void uploadQueuedTextureAssets(nr::rhi::ops::UploadReadbackContext &uploadContext, UploadBudgetState &budget);

    void markGraphicsSyncCompletionResident(const GraphicsSyncCompletion &completion);

    [[nodiscard]] static std::uint64_t maxUploadSignalValue(const PendingGraphicsSyncBatch &batch) noexcept;

    void reapSubmittedGraphicsSyncWork();

    void submitPendingGraphicsSyncBatches(nr::rhi::ops::UploadReadbackContext &uploadContext);

    [[nodiscard]] static constexpr std::string_view importStageName(ImportStage stage) noexcept
    {
        switch (stage)
        {
        case ImportStage::scene:
            return "scene";
        case ImportStage::texture:
            return "texture";
        case ImportStage::material:
            return "material";
        case ImportStage::mesh:
            return "mesh";
        case ImportStage::camera:
            return "camera";
        case ImportStage::light:
            return "light";
        case ImportStage::templateRegistration:
            return "templateRegistration";
        case ImportStage::instanceRegistration:
            return "instanceRegistration";
        default:
            return "unknown";
        }
    }

    template <nr::LogLevel Level>
    void reportImport(ImportStage stage, std::string message, std::string stableKey = {},
                      std::uint32_t sourceIndex = nr::load::invalidIndex)
    {
        if constexpr (Level == nr::LogLevel::error)
        {
            hasImportErrors_ = true;
        }

        auto stableKeyPart = std::string{};
        if (!stableKey.empty())
        {
            stableKeyPart = std::format(" stableKey='{}'", stableKey);
        }

        auto sourceIndexPart = std::string{};
        if (sourceIndex != nr::load::invalidIndex)
        {
            sourceIndexPart = std::format(" sourceIndex={}", sourceIndex);
        }

        // Import severity is a scene-level concept. An import error stays recoverable at process level
        // because it is accumulated into hasImportErrors_ and resolved by the caller, so it is clamped to
        // warning here and never reaches the fatal nrLog<LogLevel::error> path.
        constexpr auto logLevel = Level == nr::LogLevel::error ? nr::LogLevel::warning : Level;
        nr::nrLog<logLevel, "SCENE">("[{}]{}{} {}", importStageName(stage), stableKeyPart, sourceIndexPart, message);
    }

    template <typename StorageT, typename HandleT>
    using StorageRecordType = std::remove_cv_t<
        std::remove_pointer_t<decltype(std::declval<const StorageT &>().tryGet(std::declval<HandleT>()))>>;

    template <typename StorageT, typename HandleT>
    [[nodiscard]] static auto tryGetRecordRef(const StorageT &storage, HandleT handle) noexcept
        -> std::optional<std::reference_wrapper<const StorageRecordType<StorageT, HandleT>>>
    {
        if (auto const *record = storage.tryGet(handle); record != nullptr)
        {
            return std::cref(*record);
        }

        return std::nullopt;
    }

    template <typename HandleT>
    static void appendValidUniqueHandles(std::span<const HandleT> handles, std::vector<HandleT> &output)
    {
        auto seen = std::set<std::uint64_t>{};
        output.reserve(handles.size());

        std::ranges::for_each(handles, [&](HandleT handle) {
            if (!handle.valid())
            {
                return;
            }

            detail::appendUniqueHandle(output, seen, handle);
        });
    }

    template <typename HandleT, typename StorageT>
    static void incrementTemplatePins(std::span<const HandleT> handles, StorageT &storage)
    {
        std::ranges::for_each(handles, [&](HandleT handle) {
            if (auto *record = storage.tryGet(handle); record != nullptr)
            {
                ++record->liveTemplatePins;
            }
        });
    }

    template <typename HandleT, typename StorageT>
    static void decrementTemplatePins(std::span<const HandleT> handles, StorageT &storage)
    {
        std::ranges::for_each(handles, [&](HandleT handle) {
            if (auto *record = storage.tryGet(handle); record != nullptr && record->liveTemplatePins > 0u)
            {
                --record->liveTemplatePins;
            }
        });
    }

    template <typename RecordT> [[nodiscard]] static bool canCollect(const RecordT &record) noexcept
    {
        return record.liveTemplatePins == 0u;
    }

    template <typename HandleT, typename StorageT>
    static void compactDeadHandles(std::vector<HandleT> &handles, StorageT &storage)
    {
        std::erase_if(handles, [&](HandleT handle) { return storage.tryGet(handle) == nullptr; });
    }

    static void subtractSaturating(std::size_t &value, std::size_t delta) noexcept;

    void compactDeadAssetHandles();

    template <typename RetiredPayloadT>
    static void moveRetiredPayloads(std::vector<RetiredPayloadT> &source, std::vector<RetiredPayloadT> &destination)
    {
        if (source.empty())
        {
            return;
        }

        std::ranges::move(source, std::back_inserter(destination));
        source.clear();
    }

    template <typename HandleT, typename StorageT, typename GraveyardT>
    void collectUnusedAsset(HandleT handle, StorageT &storage, GraveyardT &graveyard)
    {
        auto *record = storage.tryGet(handle);
        if (record == nullptr || !canCollect(*record) || record->gpuState == GpuResidencyState::waitingGraphicsSync)
        {
            return;
        }

        queueRetiredPayload(record->gpu, record->retiredGpu);
        moveRetiredPayloads(record->retiredGpu, graveyard);
        storage.erase(handle);
    }

    template <typename HandleT, typename StorageT> static void collectUnusedCpuAsset(HandleT handle, StorageT &storage)
    {
        auto *record = storage.tryGet(handle);
        if (record == nullptr || !canCollect(*record))
        {
            return;
        }

        storage.erase(handle);
    }

    void collectUnusedMeshAsset(nr::resource::MeshHandle handle);

    void collectUnusedMaterialAsset(nr::resource::MaterialHandle handle);

    void collectUnusedTextureAsset(nr::resource::TextureHandle handle);

    void collectUnusedCameraAsset(nr::resource::CameraAssetHandle handle);

    void collectUnusedLightAsset(nr::resource::LightAssetHandle handle);

    void collectTemplatePinnedAssets(const TemplateResourcePinSet &pinSet);

    void bridgeTextures(const nr::load::SceneAsset &sceneAsset, const SceneBridgePlan &plan,
                        TemplateRegistrationTransaction &transaction,
                        std::vector<nr::resource::TextureHandle> &textureHandlesBySource);

    void bridgeMaterials(const nr::load::SceneAsset &sceneAsset, const SceneBridgePlan &plan,
                         TemplateRegistrationTransaction &transaction,
                         const std::vector<nr::resource::TextureHandle> &textureHandlesBySource,
                         std::vector<nr::resource::MaterialHandle> &materialHandlesBySource);

    void bridgeMeshes(const nr::load::SceneAsset &sceneAsset, const SceneBridgePlan &plan,
                      TemplateRegistrationTransaction &transaction,
                      const std::vector<nr::resource::MaterialHandle> &materialHandlesBySource,
                      std::vector<nr::resource::MeshHandle> &meshHandlesBySource);

    void bridgeCameras(const nr::load::SceneAsset &sceneAsset, const SceneBridgePlan &plan,
                       TemplateRegistrationTransaction &transaction,
                       std::vector<nr::resource::CameraAssetHandle> &cameraHandlesBySource);

    void bridgeLights(const nr::load::SceneAsset &sceneAsset, const SceneBridgePlan &plan,
                      TemplateRegistrationTransaction &transaction,
                      std::vector<nr::resource::LightAssetHandle> &lightHandlesBySource);

    [[nodiscard]] TemplateResourcePinSet buildTemplatePinSet(
        std::span<const nr::resource::MeshHandle> meshHandles,
        std::span<const nr::resource::MaterialHandle> materialHandles,
        std::span<const nr::resource::TextureHandle> textureHandles,
        std::span<const nr::resource::CameraAssetHandle> cameraHandles,
        std::span<const nr::resource::LightAssetHandle> lightHandles) const;

    void retainTemplatePins(const TemplateResourcePinSet &pinSet);

    void releaseTemplatePins(const TemplateResourcePinSet &pinSet);

    [[nodiscard]] static bool useParentHierarchyStorage(const nr::load::SceneAsset &sceneAsset,
                                                        TemplateHierarchyPolicy policy) noexcept;

    static void attachHierarchyRelation(flecs::entity child, flecs::entity parent, bool useParentStorage);

    template <typename T, typename AssignFn>
    static void assignIfPresent(const std::optional<T> &value, AssignFn &&assign)
    {
        if (value.has_value())
        {
            assign(*value);
        }
    }

    [[nodiscard]] static nr::resource::AlphaMode resolveMaterialAlphaMode(
        const nr::load::MaterialAsset &sourceMaterial) noexcept;

    [[nodiscard]] nr::resource::Aabb meshLocalBounds(nr::resource::MeshHandle meshHandle) const;

    [[nodiscard]] std::uint32_t meshGeometryCount(nr::resource::MeshHandle meshHandle) const noexcept;

    [[nodiscard]] static nr::resource::MeshHandle meshHandleForEntity(flecs::entity entity) noexcept;

    [[nodiscard]] std::uint64_t defaultSelectionBits(nr::resource::MaterialHandle materialHandle) const noexcept;

    void initializeInstanceRuntimeState(SceneInstanceRecord &instanceRecord);

    [[nodiscard]] nr::resource::Aabb updateHierarchyNode(flecs::entity entity, const glm::mat4 &parentWorld);

    void updateInstanceHierarchy(SceneInstanceRecord &instanceRecord);

    [[nodiscard]] bool buildTemplateHierarchy(SceneTemplateHandle templateHandle, SceneTemplateRecord &templateRecord,
                                              const nr::load::SceneAsset &sceneAsset,
                                              std::span<const nr::resource::MeshHandle> meshHandlesBySource,
                                              std::span<const nr::resource::MaterialHandle> materialHandlesBySource,
                                              std::span<const nr::resource::CameraAssetHandle> cameraHandlesBySource,
                                              std::span<const nr::resource::LightAssetHandle> lightHandlesBySource,
                                              bool useParentStorage);

    void destroyTemplatePrefabEntities(SceneTemplateRecord &templateRecord);

    template <typename Fn> void forEachImportedCameraInActiveInstances(Fn &&visit) const
    {
        cameraCandidatesQuery_.each(
            [&](flecs::entity entity, const SceneCameraBinding &cameraBinding, const WorldTransform &) {
                if (cameraBinding.synthetic || !belongsToActiveInstance(entity))
                {
                    return;
                }

                visit(entity, cameraBinding);
            });
    }

    [[nodiscard]] bool hasImportedCameraInActiveInstances() const;

    void destroyFallbackCameraInfrastructureIfUnused();

    void syncFallbackCameraInfrastructure();

    [[nodiscard]] flecs::entity makeTemplatePrefab(SceneTemplateHandle handle, std::string_view stableKey,
                                                   std::string_view debugName);

    [[nodiscard]] flecs::entity makeInstanceEntity(SceneInstanceHandle handle, std::string_view templateStableKey);

    static constexpr std::string_view kFallbackCameraStableKey = "scene://runtime/fallback_camera/default";

    [[nodiscard]] static nr::resource::CameraAsset makeFallbackCameraAsset();

    void initializeFallbackCameraInfrastructure();

    void commitMutation(SceneRevisionMutation mutation) noexcept;

    nr::rhi::Device &device_;
    std::uint64_t identity_ = 0u;
    nr::revision::RevisionBundle<SceneRtRevisionDomain> revisions{};
    std::size_t uploadBudgetBytesPerFrame_ = 0;
    std::size_t uploadBytesThisFrame_ = 0;
    UploadBudgetState uploadBudgetState_{};
    CpuRetentionPolicy cpuRetention_ = CpuRetentionPolicy::keepAll;
    bool hasImportErrors_ = false;
    flecs::world world_ = detail::makeSceneWorld();
    flecs::query<const SceneInstanceRef> runtimeRootQuery_{};
    flecs::query<const RenderableBinding, const SceneSelectionBits, const ScenePartitionId, const TlasBucketId,
                 const WorldTransform, const WorldBounds>
        rasterCandidatesQuery_{};
    flecs::query<const RenderableBinding, const SceneSelectionBits, const ScenePartitionId, const TlasBucketId,
                 const WorldTransform, const WorldBounds>
        rtCandidatesQuery_{};
    flecs::query<const SceneCameraBinding, const WorldTransform> cameraCandidatesQuery_{};
    flecs::query<const SceneLightBinding, const WorldTransform> lightCandidatesQuery_{};
    SceneFrameStamp currentFrame_{};
    detail::SceneGeometryAtlas geometryAtlas_{};
    std::vector<PendingGraphicsSyncBatch> pendingGraphicsSyncBatches_{};
    std::vector<SubmittedGraphicsSyncWork> submittedGraphicsSyncWork_{};
    std::vector<SubmittedGeometryAtlasGrowWork> submittedGeometryAtlasGrowWork_{};

    std::vector<nr::resource::MeshHandle> meshHandles_{};
    std::vector<nr::resource::TextureHandle> textureHandles_{};
    std::vector<detail::RetiredMeshGpuPayload> retiredMeshPayloadGraveyard_{};
    std::vector<detail::RetiredTextureGpuPayload> retiredTexturePayloadGraveyard_{};
    std::vector<detail::RetiredSceneGeometryAtlasBuffers> retiredGeometryAtlasBuffers_{};

    detail::KeyedSlotMapStorage<nr::resource::MeshHandle, MeshAssetRecord> meshes_{};
    detail::KeyedSlotMapStorage<nr::resource::MaterialHandle, MaterialAssetRecord> materials_{};
    detail::KeyedSlotMapStorage<nr::resource::TextureHandle, TextureAssetRecord> textures_{};
    detail::KeyedSlotMapStorage<nr::resource::CameraAssetHandle, CameraAssetRecord> cameras_{};
    detail::KeyedSlotMapStorage<nr::resource::LightAssetHandle, LightAssetRecord> lights_{};
    detail::SlotMapStorage<SceneTemplateHandle, SceneTemplateRecord> templates_{};
    detail::SlotMapStorage<SceneInstanceHandle, SceneInstanceRecord> instances_{};
    detail::SlotMapStorage<SceneExtractProfileHandle, SceneExtractProfileRecord> extractProfiles_{};
    std::map<std::string, SceneTemplateHandle> templatesByStableKey_{};
    nr::resource::CameraAssetHandle fallbackCameraHandle_{};
    flecs::entity fallbackCameraEntity_{};
    std::size_t templateNodeCount_ = 0;
    std::size_t templateMeshBindingCount_ = 0;
    std::size_t templateCameraBindingCount_ = 0;
    std::size_t templateLightBindingCount_ = 0;
};
} // namespace nr::scene
