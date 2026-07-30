module nr.scene;
import :scene;
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
[[nodiscard]] detail::MaterialGpuData buildMaterialGpuData(const nr::resource::Material &material)
{
    auto data = detail::MaterialGpuData{};
    data.featureFlags = static_cast<std::uint32_t>(material.featureFlags());
    data.alphaMode = static_cast<std::uint32_t>(material.core.alphaMode);
    data.textureSlotCount = static_cast<std::uint32_t>(material.textureSlots.size());

    data.baseColorFactor = material.core.baseColorFactor;
    data.emissiveAndMetallic = glm::vec4{material.core.emissiveFactor, material.core.metallicFactor};
    data.roughnessNormalOcclusionAlpha = glm::vec4{
        material.core.roughnessFactor,
        material.core.normalScale,
        material.core.occlusionStrength,
        material.core.alphaCutoff,
    };

    if (material.clearcoat.has_value())
    {
        data.clearcoatFactorRoughness = glm::vec4{
            material.clearcoat->factor,
            material.clearcoat->roughnessFactor,
            0.0f,
            0.0f,
        };
    }

    if (material.sheen.has_value())
    {
        data.sheenColorRoughness = glm::vec4{
            material.sheen->colorFactor,
            material.sheen->roughnessFactor,
        };
    }

    data.transmissionAnisotropy = glm::vec4{
        material.transmission.has_value() ? material.transmission->factor : 0.0f,
        material.anisotropy.has_value() ? material.anisotropy->factor : 0.0f,
        material.anisotropy.has_value() ? material.anisotropy->rotation : 0.0f,
        0.0f,
    };

    auto slotIndices = std::views::iota(std::size_t{0}, material.textureSlots.size());
    std::ranges::for_each(slotIndices, [&](std::size_t slotIndex) {
        auto const& slot = material.textureSlots[slotIndex];
        data.textureHandles[slotIndex] = slot.texture.packed();
        data.uvSets[slotIndex] = slot.uvSet;
        data.uvLinear[slotIndex] = slot.transform.linear;
        data.uvOffsets[slotIndex] = slot.transform.offset;
    });

    return data;
}

[[nodiscard]] detail::CameraGpuData buildCameraGpuData(const nr::resource::CameraAsset &camera)
{
    return detail::CameraGpuData{
        .projection = static_cast<std::uint32_t>(camera.projection),
        .verticalFovRadians = camera.verticalFovRadians,
        .orthoHeight = camera.orthoHeight,
        .nearPlane = camera.nearPlane,
        .farPlane = camera.farPlane,
    };
}

[[nodiscard]] detail::LightGpuData buildLightGpuData(const nr::resource::LightAsset &light)
{
    return detail::LightGpuData{
        .type = static_cast<std::uint32_t>(light.type),
        .intensity = light.intensity,
        .color = light.color,
        .range = light.range,
        .innerConeRadians = light.innerConeRadians,
        .outerConeRadians = light.outerConeRadians,
        .castShadow = light.castShadow ? 1u : 0u,
    };
}

} // namespace nr::scene::detail

namespace nr::scene
{
void Scene::uploadPending()
{
    if (!uploadContextAvailable())
    {
        return;
    }

    auto &uploadContext = device_.uploadReadback();
    reapSubmittedGeometryAtlasGrowWork();
    reapSubmittedGraphicsSyncWork();

    auto uploadBudgetRemaining = uploadBudgetBytesPerFrame_;

    uploadQueuedCameraAssets(uploadBudgetRemaining);
    uploadQueuedLightAssets(uploadBudgetRemaining);
    uploadQueuedMaterialAssets(uploadContext, uploadBudgetRemaining);
    uploadQueuedMeshAssets(uploadContext, uploadBudgetRemaining);
    uploadQueuedTextureAssets(uploadContext, uploadBudgetRemaining);
    submitPendingGraphicsSyncBatches(uploadContext);
}

[[nodiscard]] bool Scene::uploadContextAvailable() const noexcept
{
    return *device_.device != nullptr && device_.uploadReadbackContext_.has_value();
}

[[nodiscard]] bool Scene::geometryAtlasReadyForUse() const noexcept
{
    return submittedGeometryAtlasGrowWork_.empty();
}

[[nodiscard]] nr::rhi::ops::BufferUploadOwnershipPlan Scene::makeTransferToGraphicsUploadPlan(vk::PipelineStageFlags2 acquireStages, vk::AccessFlags2 acquireAccess) const
{
    auto const transferQueueFamily = device_.queueManager.transfer().queueFamilyIndex();
    auto const graphicsQueueFamily = device_.queueManager.graphics().queueFamilyIndex();
    nrAssert(transferQueueFamily != graphicsQueueFamily, "Scene upload requires distinct transfer and graphics queue families.");

    auto plan = nr::rhi::ops::BufferUploadOwnershipPlan{};
    plan.releaseToDestination = nr::rhi::ops::makeQueueOwnershipTransfer(transferQueueFamily, graphicsQueueFamily,
                                                                         nr::rhi::ops::QueueAccessScope{
                                                                             .stages = vk::PipelineStageFlagBits2::eTransfer,
                                                                             .access = vk::AccessFlagBits2::eTransferWrite,
                                                                         },
                                                                         nr::rhi::ops::QueueAccessScope{
                                                                             .stages = acquireStages,
                                                                             .access = acquireAccess,
                                                                         });
    return plan;
}

[[nodiscard]] std::size_t Scene::meshUploadBytes(const nr::resource::Mesh &mesh) noexcept
{
    auto vertexBytes = std::span{mesh.vertices}.size_bytes();
    auto indexBytes = std::span{mesh.indices}.size_bytes();
    return vertexBytes + indexBytes;
}

[[nodiscard]] std::size_t Scene::textureUploadBytes(const nr::resource::Texture &texture) noexcept
{
    auto withPixels = std::ranges::find_if(texture.levels, [](const nr::resource::ImageLevel &level) { return !level.bytes.empty(); });

    if (withPixels == texture.levels.end())
    {
        return 0;
    }

    return withPixels->bytes.size();
}

[[nodiscard]] std::optional<std::reference_wrapper<const nr::resource::ImageLevel>> Scene::firstTextureLevelWithPixels(const nr::resource::Texture &texture)
{
    auto withPixels = std::ranges::find_if(texture.levels, [](const nr::resource::ImageLevel &level) { return !level.bytes.empty(); });

    if (withPixels == texture.levels.end())
    {
        return std::nullopt;
    }

    return std::cref(*withPixels);
}

[[nodiscard]] vk::DeviceSize Scene::alignUp(vk::DeviceSize value, vk::DeviceSize alignment) noexcept
{
    if (alignment <= 1u)
    {
        return value;
    }

    auto const remainder = value % alignment;
    return remainder == 0u ? value : value + (alignment - remainder);
}

[[nodiscard]] std::uint32_t Scene::checkedDeviceSizeToUint32(vk::DeviceSize value, std::string_view label)
{
    nrAssert(value <= std::numeric_limits<std::uint32_t>::max(), std::format("{} value {} exceeds uint32_t range.", label, value));
    return static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::vector<std::uint32_t> Scene::makeGeometryAtlasQueueFamilyIndices() const
{
    auto families = std::vector<std::uint32_t>{
        device_.queueManager.transfer().queueFamilyIndex(),
        device_.queueManager.graphics().queueFamilyIndex(),
        device_.queueManager.compute().queueFamilyIndex(),
    };
    std::ranges::sort(families);
    auto uniqueTail = std::ranges::unique(families);
    families.erase(uniqueTail.begin(), uniqueTail.end());
    return families;
}

[[nodiscard]] vk::BufferCreateInfo Scene::makeGeometryAtlasBufferCreateInfo(vk::DeviceSize size, vk::BufferUsageFlags bindingUsage, std::span<const std::uint32_t> queueFamilyIndices) noexcept
{
    auto createInfo = vk::BufferCreateInfo{};
    createInfo.size = size;
    createInfo.usage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | bindingUsage;
    if (queueFamilyIndices.size() > 1u)
    {
        createInfo.sharingMode = vk::SharingMode::eConcurrent;
        createInfo.queueFamilyIndexCount = static_cast<std::uint32_t>(queueFamilyIndices.size());
        createInfo.pQueueFamilyIndices = queueFamilyIndices.data();
    }
    else
    {
        createInfo.sharingMode = vk::SharingMode::eExclusive;
    }
    return createInfo;
}

[[nodiscard]] vk::DeviceSize Scene::grownGeometryAtlasCapacity(vk::DeviceSize currentCapacity, vk::DeviceSize requiredCapacity) noexcept
{
    constexpr auto minCapacity = vk::DeviceSize{1024u * 1024u};
    auto const maxCapacity = std::numeric_limits<vk::DeviceSize>::max();
    auto const doubledCapacity = currentCapacity > (maxCapacity / 2u) ? maxCapacity : currentCapacity * 2u;
    return std::max({requiredCapacity, doubledCapacity, minCapacity});
}

[[nodiscard]] detail::MeshGeometryAtlasAllocation Scene::reserveGeometryAtlasAllocation(const nr::resource::Mesh &mesh, nr::rhi::ops::UploadReadbackContext &uploadContext)
{
    auto vertexBytes = std::as_bytes(std::span{mesh.vertices});
    auto indexBytes = std::as_bytes(std::span{mesh.indices});
    nrAssert(!vertexBytes.empty(), "Scene geometry atlas allocation requires vertex data.");

    constexpr auto vertexStride = vk::DeviceSize{sizeof(nr::resource::Vertex)};
    constexpr auto indexStride = vk::DeviceSize{sizeof(std::uint32_t)};

    auto const vertexByteOffset = alignUp(geometryAtlas_.vertexUsedBytes, vertexStride);
    auto const indexByteOffset = indexBytes.empty() ? vk::DeviceSize{0} : alignUp(geometryAtlas_.indexUsedBytes, indexStride);
    nrAssert(std::numeric_limits<vk::DeviceSize>::max() - vertexByteOffset >= vertexBytes.size_bytes(), "Scene geometry vertex atlas allocation overflowed.");
    nrAssert(indexBytes.empty() || std::numeric_limits<vk::DeviceSize>::max() - indexByteOffset >= indexBytes.size_bytes(), "Scene geometry index atlas allocation overflowed.");

    auto const requiredVertexBytes = vertexByteOffset + vertexBytes.size_bytes();
    auto const requiredIndexBytes = indexBytes.empty() ? geometryAtlas_.indexUsedBytes : indexByteOffset + indexBytes.size_bytes();
    ensureGeometryAtlasCapacity(requiredVertexBytes, requiredIndexBytes, uploadContext);

    geometryAtlas_.vertexUsedBytes = requiredVertexBytes;
    geometryAtlas_.indexUsedBytes = requiredIndexBytes;

    auto allocation = detail::MeshGeometryAtlasAllocation{
        .vertexByteOffset = vertexByteOffset,
        .indexByteOffset = indexByteOffset,
        .vertexBase = checkedDeviceSizeToUint32(vertexByteOffset / vertexStride, "Scene geometry atlas vertex base"),
        .indexBase = checkedDeviceSizeToUint32(indexByteOffset / indexStride, "Scene geometry atlas index base"),
        .vertexCount = checkedDeviceSizeToUint32(static_cast<vk::DeviceSize>(mesh.vertices.size()), "Scene mesh vertex count"),
        .indexCount = checkedDeviceSizeToUint32(static_cast<vk::DeviceSize>(mesh.indices.size()), "Scene mesh index count"),
    };
    return allocation;
}

void Scene::ensureGeometryAtlasCapacity(vk::DeviceSize requiredVertexBytes, vk::DeviceSize requiredIndexBytes, nr::rhi::ops::UploadReadbackContext &uploadContext)
{
    auto const growVertex = requiredVertexBytes > geometryAtlas_.vertexCapacityBytes;
    auto const growIndex = requiredIndexBytes > geometryAtlas_.indexCapacityBytes;
    if (!growVertex && !growIndex)
    {
        return;
    }

    auto const copyExistingVertexPrefix = growVertex && geometryAtlas_.vertexBuffer.valid() && geometryAtlas_.vertexUsedBytes > 0u;
    auto const copyExistingIndexPrefix = growIndex && geometryAtlas_.indexBuffer.valid() && geometryAtlas_.indexUsedBytes > 0u;
    if ((copyExistingVertexPrefix || copyExistingIndexPrefix) && !pendingGraphicsSyncBatches_.empty())
    {
        // Consume ticket references before moving the atlas Buffer wrappers. The
        // graphics queue waits on the upload timeline, so this remains CPU-nonblocking.
        submitPendingGraphicsSyncBatches(uploadContext);
    }

    auto oldVertexBuffer = std::optional<nr::rhi::Buffer>{};
    auto oldIndexBuffer = std::optional<nr::rhi::Buffer>{};
    auto oldVertexUsedBytes = vk::DeviceSize{0};
    auto oldIndexUsedBytes = vk::DeviceSize{0};

    if (growVertex)
    {
        oldVertexUsedBytes = geometryAtlas_.vertexUsedBytes;
        if (geometryAtlas_.vertexBuffer.valid())
        {
            oldVertexBuffer.emplace(std::move(geometryAtlas_.vertexBuffer));
        }

        geometryAtlas_.vertexCapacityBytes = grownGeometryAtlasCapacity(geometryAtlas_.vertexCapacityBytes, requiredVertexBytes);
        auto atlasQueueFamilies = makeGeometryAtlasQueueFamilyIndices();
        geometryAtlas_.vertexBuffer = device_.resourceFactory.createBuffer(makeGeometryAtlasBufferCreateInfo(geometryAtlas_.vertexCapacityBytes, vk::BufferUsageFlagBits::eVertexBuffer, std::span<const std::uint32_t>{atlasQueueFamilies}), nr::rhi::MemoryUsage::GpuOnly, "scene_geometry_vertex_atlas");
        nrAssert(geometryAtlas_.vertexBuffer.valid(), "Scene failed to create vertex geometry atlas buffer.");
    }

    if (growIndex && requiredIndexBytes > 0u)
    {
        oldIndexUsedBytes = geometryAtlas_.indexUsedBytes;
        if (geometryAtlas_.indexBuffer.valid())
        {
            oldIndexBuffer.emplace(std::move(geometryAtlas_.indexBuffer));
        }

        geometryAtlas_.indexCapacityBytes = grownGeometryAtlasCapacity(geometryAtlas_.indexCapacityBytes, requiredIndexBytes);
        auto atlasQueueFamilies = makeGeometryAtlasQueueFamilyIndices();
        geometryAtlas_.indexBuffer = device_.resourceFactory.createBuffer(makeGeometryAtlasBufferCreateInfo(geometryAtlas_.indexCapacityBytes, vk::BufferUsageFlagBits::eIndexBuffer, std::span<const std::uint32_t>{atlasQueueFamilies}), nr::rhi::MemoryUsage::GpuOnly, "scene_geometry_index_atlas");
        nrAssert(geometryAtlas_.indexBuffer.valid(), "Scene failed to create index geometry atlas buffer.");
    }

    submitGeometryAtlasGrowCopy(std::move(oldVertexBuffer), oldVertexUsedBytes, std::move(oldIndexBuffer), oldIndexUsedBytes);
}

void Scene::submitGeometryAtlasGrowCopy(std::optional<nr::rhi::Buffer> oldVertexBuffer, vk::DeviceSize oldVertexUsedBytes, std::optional<nr::rhi::Buffer> oldIndexBuffer, vk::DeviceSize oldIndexUsedBytes)
{
    auto retiredBuffers = detail::RetiredSceneGeometryAtlasBuffers{};
    if (oldVertexBuffer.has_value())
    {
        retiredBuffers.vertexBuffer = std::move(*oldVertexBuffer);
    }
    if (oldIndexBuffer.has_value())
    {
        retiredBuffers.indexBuffer = std::move(*oldIndexBuffer);
    }

    auto const copyVertexPrefix = retiredBuffers.vertexBuffer.valid() && geometryAtlas_.vertexBuffer.valid() && oldVertexUsedBytes > 0u;
    auto const copyIndexPrefix = retiredBuffers.indexBuffer.valid() && geometryAtlas_.indexBuffer.valid() && oldIndexUsedBytes > 0u;

    if (!copyVertexPrefix && !copyIndexPrefix)
    {
        retireGeometryAtlasBuffers(std::move(retiredBuffers));
        return;
    }

    auto growWork = SubmittedGeometryAtlasGrowWork{};
    growWork.retiredBuffers = std::move(retiredBuffers);
    growWork.commandPool = nr::rhi::CommandPool{
        device_.device,
        device_.queueManager.graphics().queueFamilyIndex(),
        vk::CommandPoolCreateFlagBits::eTransient,
    };
    growWork.commandBuffers.emplace(growWork.commandPool.allocatePrimary(1));
    auto &commandBuffer = growWork.commandBuffers->front();

    nr::rhi::CommandRecorder::beginPrimary(commandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    {
        auto postCopyBarriers = nr::rhi::ops::BarrierBatch{};

        if (copyVertexPrefix)
        {
            auto copyRegion = vk::BufferCopy{};
            copyRegion.size = oldVertexUsedBytes;
            nr::rhi::ops::copyBuffer2(commandBuffer, growWork.retiredBuffers.vertexBuffer.handle(), geometryAtlas_.vertexBuffer.handle(), nr::rhi::ops::toBufferCopy2(copyRegion));
            postCopyBarriers.addBuffer(geometryAtlas_.vertexBuffer, vk::BufferMemoryBarrier2{
                                                                        vk::PipelineStageFlagBits2::eTransfer,
                                                                        vk::AccessFlagBits2::eTransferWrite,
                                                                        vk::PipelineStageFlagBits2::eAllCommands,
                                                                        vk::AccessFlagBits2::eMemoryRead,
                                                                        nr::rhi::ops::kIgnoredQueueFamilyIndex,
                                                                        nr::rhi::ops::kIgnoredQueueFamilyIndex,
                                                                        vk::Buffer{},
                                                                        0,
                                                                        oldVertexUsedBytes,
                                                                        nullptr,
                                                                    });
        }

        if (copyIndexPrefix)
        {
            auto copyRegion = vk::BufferCopy{};
            copyRegion.size = oldIndexUsedBytes;
            nr::rhi::ops::copyBuffer2(commandBuffer, growWork.retiredBuffers.indexBuffer.handle(), geometryAtlas_.indexBuffer.handle(), nr::rhi::ops::toBufferCopy2(copyRegion));
            postCopyBarriers.addBuffer(geometryAtlas_.indexBuffer, vk::BufferMemoryBarrier2{
                                                                       vk::PipelineStageFlagBits2::eTransfer,
                                                                       vk::AccessFlagBits2::eTransferWrite,
                                                                       vk::PipelineStageFlagBits2::eAllCommands,
                                                                       vk::AccessFlagBits2::eMemoryRead,
                                                                       nr::rhi::ops::kIgnoredQueueFamilyIndex,
                                                                       nr::rhi::ops::kIgnoredQueueFamilyIndex,
                                                                       vk::Buffer{},
                                                                       0,
                                                                       oldIndexUsedBytes,
                                                                       nullptr,
                                                                   });
        }

        nr::rhi::ops::pipelineBarrier(commandBuffer, postCopyBarriers);
    }
    nr::rhi::CommandRecorder::end(commandBuffer);

    auto growSubmission = nr::rhi::CommandBatch{};
    growSubmission.addCommandBuffer(commandBuffer);

    growWork.fence = vk::raii::Fence(device_.device, vk::FenceCreateInfo{});
    device_.queueManager.graphics().submit(std::move(growSubmission), std::cref(growWork.fence));
    submittedGeometryAtlasGrowWork_.push_back(std::move(growWork));
}

void Scene::retireGeometryAtlasBuffers(detail::RetiredSceneGeometryAtlasBuffers retiredBuffers)
{
    if (!retiredBuffers.vertexBuffer.valid() && !retiredBuffers.indexBuffer.valid())
    {
        return;
    }

    retiredBuffers.retireAfterSerial = currentFrame_.frameSerial + detail::kDefaultRetireLatencySerial;
    retiredGeometryAtlasBuffers_.push_back(std::move(retiredBuffers));
}

void Scene::reapSubmittedGeometryAtlasGrowWork()
{
    std::erase_if(submittedGeometryAtlasGrowWork_, [&](SubmittedGeometryAtlasGrowWork &work) {
        nrAssert(*work.fence != nullptr, "Scene geometry atlas grow work requires a valid fence.");
        auto result = device_.device.waitForFences(*work.fence, vk::True, 0u);
        nrAssert(result == vk::Result::eSuccess || result == vk::Result::eTimeout, "Scene failed querying geometry atlas grow fence status.");
        if (result != vk::Result::eSuccess)
        {
            return false;
        }

        retireGeometryAtlasBuffers(std::move(work.retiredBuffers));
        return true;
    });
}

void Scene::queueGpuUploadsForFrame()
{
    queueUploadsFor(std::span{meshHandles_}, meshes_);
    queueUploadsFor(std::span{materialHandles_}, materials_);
    queueUploadsFor(std::span{textureHandles_}, textures_);
    queueUploadsFor(std::span{cameraHandles_}, cameras_);
    queueUploadsFor(std::span{lightHandles_}, lights_);
}

void Scene::reapRetiredGpuVersions()
{
    auto const serial = currentFrame_.frameSerial;
    auto expired = [serial](const auto &retiredVersion) { return retiredVersion.retireAfterSerial <= serial; };

    reapRetiredFor(std::span{meshHandles_}, meshes_, expired);
    reapRetiredFor(std::span{materialHandles_}, materials_, expired);
    reapRetiredFor(std::span{textureHandles_}, textures_, expired);
    reapRetiredFor(std::span{cameraHandles_}, cameras_, expired);
    reapRetiredFor(std::span{lightHandles_}, lights_, expired);

    std::erase_if(retiredMeshPayloadGraveyard_, expired);
    std::erase_if(retiredMaterialPayloadGraveyard_, expired);
    std::erase_if(retiredTexturePayloadGraveyard_, expired);
    std::erase_if(retiredCameraPayloadGraveyard_, expired);
    std::erase_if(retiredLightPayloadGraveyard_, expired);
    std::erase_if(retiredGeometryAtlasBuffers_, expired);
}

[[nodiscard]] std::size_t Scene::pixelFormatByteSize(nr::resource::PixelFormat format)
{
    switch (format)
    {
    case vk::Format::eR8Unorm:
        return 1;
    case vk::Format::eR8G8Unorm:
        return 2;
    case vk::Format::eR8G8B8A8Unorm:
    case vk::Format::eR8G8B8A8Srgb:
    case vk::Format::eB8G8R8A8Unorm:
    case vk::Format::eB8G8R8A8Srgb:
        return 4;
    case vk::Format::eR16G16B16A16Sfloat:
        return 8;
    case vk::Format::eR32G32B32A32Sfloat:
        return 16;
    default:
        break;
    }

    nrAssert(false, std::format("Unsupported vk::Format '{}' for Scene texture byte-size calculation.", static_cast<std::uint32_t>(format)));
    return 4;
}

[[nodiscard]] std::vector<std::byte> Scene::makeFallbackTextureBytes(nr::resource::PixelFormat format)
{
    auto const byteSize = pixelFormatByteSize(format);
    return std::vector<std::byte>(byteSize, std::byte{0xFF});
}

[[nodiscard]] bool Scene::uploadMaterialAsset(nr::resource::MaterialHandle handle, MaterialAssetRecord &record, nr::rhi::ops::UploadReadbackContext &uploadContext)
{
    auto materialGpuData = detail::buildMaterialGpuData(record.cpu);
    auto materialBytes = std::as_bytes(std::span{std::addressof(materialGpuData), std::size_t{1}});

    auto createInfo = vk::BufferCreateInfo{};
    createInfo.size = materialBytes.size();
    createInfo.usage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eUniformBuffer;
    createInfo.sharingMode = vk::SharingMode::eExclusive;

    auto payload = detail::MaterialGpuPayload{};
    payload.buffer = device_.resourceFactory.createBuffer(createInfo, nr::rhi::MemoryUsage::GpuOnly, std::format("scene_material_{}_gpu", handle.slot));
    payload.byteSize = materialBytes.size();

    auto uploadPlan = makeTransferToGraphicsUploadPlan(vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eMemoryRead);
    auto uploadTicket = uploadContext.uploadBuffer(materialBytes, payload.buffer, 0, uploadPlan);
    if (!uploadTicket.valid())
    {
        return false;
    }

    queueRetiredPayload(record.gpu, record.retiredGpu);
    record.gpu = std::move(payload);
    record.uploadQueued = false;
    record.gpuState = GpuResidencyState::waitingGraphicsSync;
    record.lastUploadFrameSerial = currentFrame_.frameSerial;

    // uploadTicket stores a Buffer reference; rebind to the record-owned payload after move.
    uploadTicket.buffer = std::cref(record.gpu->buffer);

    auto pendingBatch = PendingGraphicsSyncBatch{};
    pendingBatch.asset = makeGpuHandleRef(handle, GpuAssetKind::material);
    pendingBatch.targetGpuVersion = record.cpuVersion;
    pendingBatch.bufferTickets.push_back(uploadTicket);
    pendingGraphicsSyncBatches_.push_back(std::move(pendingBatch));
    return true;
}

[[nodiscard]] bool Scene::uploadMeshAsset(nr::resource::MeshHandle handle, MeshAssetRecord &record, nr::rhi::ops::UploadReadbackContext &uploadContext)
{
    auto vertexBytes = std::as_bytes(std::span{record.cpu.vertices});
    auto indexBytes = std::as_bytes(std::span{record.cpu.indices});
    if (vertexBytes.empty())
    {
        record.uploadQueued = false;
        record.gpuState = GpuResidencyState::none;
        return false;
    }

    auto payload = detail::MeshGpuPayload{};
    payload.atlas = reserveGeometryAtlasAllocation(record.cpu, uploadContext);
    nrAssert(geometryAtlas_.vertexBuffer.valid(), "Scene mesh upload requires a valid vertex geometry atlas.");

    auto pendingBatch = PendingGraphicsSyncBatch{};
    pendingBatch.asset = makeGpuHandleRef(handle, GpuAssetKind::mesh);
    pendingBatch.targetGpuVersion = record.cpuVersion;

    auto vertexTicket = uploadContext.uploadBuffer(vertexBytes, geometryAtlas_.vertexBuffer, payload.atlas.vertexByteOffset);
    if (!vertexTicket.valid())
    {
        return false;
    }

    auto indexTicket = std::optional<nr::rhi::ops::BufferUploadTicket>{};
    if (!indexBytes.empty())
    {
        nrAssert(geometryAtlas_.indexBuffer.valid(), "Scene indexed mesh upload requires a valid index geometry atlas.");
        auto uploadedIndexTicket = uploadContext.uploadBuffer(indexBytes, geometryAtlas_.indexBuffer, payload.atlas.indexByteOffset);
        if (!uploadedIndexTicket.valid())
        {
            return false;
        }

        indexTicket = std::move(uploadedIndexTicket);
    }

    queueRetiredPayload(record.gpu, record.retiredGpu);
    record.gpu = std::move(payload);
    record.uploadQueued = false;
    record.gpuState = GpuResidencyState::waitingGraphicsSync;
    record.lastUploadFrameSerial = currentFrame_.frameSerial;

    // Tickets store Buffer references; keep them bound to the scene-owned atlas buffers.
    vertexTicket.buffer = std::cref(geometryAtlas_.vertexBuffer);
    pendingBatch.bufferTickets.push_back(vertexTicket);

    if (indexTicket.has_value() && geometryAtlas_.indexBuffer.valid())
    {
        indexTicket->buffer = std::cref(geometryAtlas_.indexBuffer);
        pendingBatch.bufferTickets.push_back(*indexTicket);
    }

    pendingGraphicsSyncBatches_.push_back(std::move(pendingBatch));
    return true;
}

[[nodiscard]] bool Scene::uploadTextureAsset(nr::resource::TextureHandle handle, TextureAssetRecord &record, nr::rhi::ops::UploadReadbackContext &uploadContext)
{
    auto fallbackBytes = std::vector<std::byte>{};
    auto sourceLevel = firstTextureLevelWithPixels(record.cpu);

    auto levelExtent = vk::Extent3D{
        std::max(record.cpu.width, 1u),
        std::max(record.cpu.height, 1u),
        std::max(record.cpu.depth, 1u),
    };

    auto imageData = std::span<const std::byte>{};
    if (sourceLevel.has_value())
    {
        auto const &level = sourceLevel->get();
        levelExtent = vk::Extent3D{
            std::max(level.width, 1u),
            std::max(level.height, 1u),
            std::max(level.depth, 1u),
        };
        imageData = std::span<const std::byte>{level.bytes};
    }
    else
    {
        fallbackBytes = makeFallbackTextureBytes(record.cpu.format);
        imageData = std::span<const std::byte>{fallbackBytes};
    }

    auto imageCreateInfo = vk::ImageCreateInfo{};
    imageCreateInfo.imageType = record.cpu.dimension;
    imageCreateInfo.format = record.cpu.format;
    imageCreateInfo.extent = levelExtent;
    imageCreateInfo.mipLevels = 1;
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.samples = vk::SampleCountFlagBits::e1;
    imageCreateInfo.tiling = vk::ImageTiling::eOptimal;
    imageCreateInfo.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled;
    imageCreateInfo.sharingMode = vk::SharingMode::eExclusive;
    imageCreateInfo.initialLayout = vk::ImageLayout::eUndefined;

    auto payload = detail::TextureGpuPayload{};
    payload.image = device_.resourceFactory.createImage(imageCreateInfo, nr::rhi::MemoryUsage::GpuOnly, std::format("scene_texture_{}_gpu", handle.slot));
    payload.layout = vk::ImageLayout::eShaderReadOnlyOptimal;

    auto uploadPlan = makeTransferToGraphicsUploadPlan(vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eShaderRead);
    auto uploadTicket = uploadContext.uploadImage(imageData, payload.image, vk::ImageLayout::eUndefined, payload.layout, uploadPlan);
    if (!uploadTicket.valid())
    {
        return false;
    }

    queueRetiredPayload(record.gpu, record.retiredGpu);
    record.gpu = std::move(payload);
    record.uploadQueued = false;
    record.gpuState = GpuResidencyState::waitingGraphicsSync;
    record.lastUploadFrameSerial = currentFrame_.frameSerial;

    // uploadTicket stores an Image reference; rebind to the record-owned payload after move.
    uploadTicket.image = std::cref(record.gpu->image);

    auto pendingBatch = PendingGraphicsSyncBatch{};
    pendingBatch.asset = makeGpuHandleRef(handle, GpuAssetKind::texture);
    pendingBatch.targetGpuVersion = record.cpuVersion;
    pendingBatch.imageTickets.push_back(uploadTicket);
    pendingGraphicsSyncBatches_.push_back(std::move(pendingBatch));
    return true;
}

[[nodiscard]] bool Scene::uploadCameraAsset(nr::resource::CameraAssetHandle handle, CameraAssetRecord &record)
{
    if (!record.gpu.has_value() || !record.gpu->buffer.valid())
    {
        queueRetiredPayload(record.gpu, record.retiredGpu);

        auto createInfo = vk::BufferCreateInfo{};
        createInfo.size = sizeof(detail::CameraGpuData);
        createInfo.usage = vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eStorageBuffer;
        createInfo.sharingMode = vk::SharingMode::eExclusive;

        auto payload = detail::CameraGpuPayload{};
        payload.buffer = device_.resourceFactory.createBuffer(createInfo, nr::rhi::MemoryUsage::CpuToGpu, std::format("scene_camera_{}_cpu", handle.slot));
        record.gpu = std::move(payload);
    }

    auto cameraGpuData = detail::buildCameraGpuData(record.cpu);
    record.gpu->buffer.writeMappedAndFlush(cameraGpuData);

    record.uploadQueued = false;
    record.gpuState = GpuResidencyState::resident;
    record.gpuVersion = record.cpuVersion;
    record.lastUploadFrameSerial = currentFrame_.frameSerial;
    return true;
}

[[nodiscard]] bool Scene::uploadLightAsset(nr::resource::LightAssetHandle handle, LightAssetRecord &record)
{
    if (!record.gpu.has_value() || !record.gpu->buffer.valid())
    {
        queueRetiredPayload(record.gpu, record.retiredGpu);

        auto createInfo = vk::BufferCreateInfo{};
        createInfo.size = sizeof(detail::LightGpuData);
        createInfo.usage = vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eStorageBuffer;
        createInfo.sharingMode = vk::SharingMode::eExclusive;

        auto payload = detail::LightGpuPayload{};
        payload.buffer = device_.resourceFactory.createBuffer(createInfo, nr::rhi::MemoryUsage::CpuToGpu, std::format("scene_light_{}_cpu", handle.slot));
        record.gpu = std::move(payload);
    }

    auto lightGpuData = detail::buildLightGpuData(record.cpu);
    record.gpu->buffer.writeMappedAndFlush(lightGpuData);

    record.uploadQueued = false;
    record.gpuState = GpuResidencyState::resident;
    record.gpuVersion = record.cpuVersion;
    record.lastUploadFrameSerial = currentFrame_.frameSerial;
    return true;
}

void Scene::uploadQueuedCameraAssets(std::size_t &uploadBudgetRemaining)
{
    (void)uploadBudgetRemaining;
    forEachLiveRecord(std::span{cameraHandles_}, cameras_, [&](nr::resource::CameraAssetHandle handle, CameraAssetRecord &record) {
        if (!record.uploadQueued)
        {
            return;
        }

        if (uploadCameraAsset(handle, record))
        {
            uploadBytesThisFrame_ += sizeof(detail::CameraGpuData);
        }
    });
}

void Scene::uploadQueuedLightAssets(std::size_t &uploadBudgetRemaining)
{
    (void)uploadBudgetRemaining;
    forEachLiveRecord(std::span{lightHandles_}, lights_, [&](nr::resource::LightAssetHandle handle, LightAssetRecord &record) {
        if (!record.uploadQueued)
        {
            return;
        }

        if (uploadLightAsset(handle, record))
        {
            uploadBytesThisFrame_ += sizeof(detail::LightGpuData);
        }
    });
}

void Scene::uploadQueuedMaterialAssets(nr::rhi::ops::UploadReadbackContext &uploadContext, std::size_t &uploadBudgetRemaining)
{
    forEachLiveRecord(std::span{materialHandles_}, materials_, [&](nr::resource::MaterialHandle handle, MaterialAssetRecord &record) {
        if (!record.uploadQueued)
        {
            return;
        }

        auto const bytesNeeded = sizeof(detail::MaterialGpuData);
        if (bytesNeeded > uploadBudgetRemaining)
        {
            return;
        }

        if (uploadMaterialAsset(handle, record, uploadContext))
        {
            uploadBudgetRemaining -= bytesNeeded;
            uploadBytesThisFrame_ += bytesNeeded;
        }
    });
}

void Scene::uploadQueuedMeshAssets(nr::rhi::ops::UploadReadbackContext &uploadContext, std::size_t &uploadBudgetRemaining)
{
    forEachLiveRecord(std::span{meshHandles_}, meshes_, [&](nr::resource::MeshHandle handle, MeshAssetRecord &record) {
        if (!record.uploadQueued)
        {
            return;
        }

        auto const bytesNeeded = meshUploadBytes(record.cpu);
        if (bytesNeeded == 0 || bytesNeeded > uploadBudgetRemaining)
        {
            return;
        }

        if (uploadMeshAsset(handle, record, uploadContext))
        {
            uploadBudgetRemaining -= bytesNeeded;
            uploadBytesThisFrame_ += bytesNeeded;
        }
    });
}

void Scene::uploadQueuedTextureAssets(nr::rhi::ops::UploadReadbackContext &uploadContext, std::size_t &uploadBudgetRemaining)
{
    forEachLiveRecord(std::span{textureHandles_}, textures_, [&](nr::resource::TextureHandle handle, TextureAssetRecord &record) {
        if (!record.uploadQueued)
        {
            return;
        }

        auto bytesNeeded = textureUploadBytes(record.cpu);
        if (bytesNeeded == 0)
        {
            bytesNeeded = pixelFormatByteSize(record.cpu.format);
        }

        if (bytesNeeded > uploadBudgetRemaining)
        {
            return;
        }

        if (uploadTextureAsset(handle, record, uploadContext))
        {
            uploadBudgetRemaining -= bytesNeeded;
            uploadBytesThisFrame_ += bytesNeeded;
        }
    });
}

void Scene::markGraphicsSyncCompletionResident(const GraphicsSyncCompletion &completion)
{
    switch (completion.asset.kind)
    {
    case GpuAssetKind::mesh: {
        auto handle = nr::resource::MeshHandle{completion.asset.slot, completion.asset.generation};
        if (auto *record = meshes_.tryGet(handle); record != nullptr)
        {
            record->gpuState = GpuResidencyState::resident;
            record->gpuVersion = completion.targetGpuVersion;
            record->lastUploadFrameSerial = currentFrame_.frameSerial;
            record->uploadQueued = false;
            if (record->cpuVersion <= completion.targetGpuVersion)
            {
                discardUploadSourceIfConfigured(*record);
            }
            commitMutation(SceneRevisionMutation::meshResident);
            collectUnusedMeshAsset(handle);
        }
        break;
    }
    case GpuAssetKind::material: {
        auto handle = nr::resource::MaterialHandle{completion.asset.slot, completion.asset.generation};
        if (auto *record = materials_.tryGet(handle); record != nullptr)
        {
            record->gpuState = GpuResidencyState::resident;
            record->gpuVersion = completion.targetGpuVersion;
            record->lastUploadFrameSerial = currentFrame_.frameSerial;
            record->uploadQueued = false;
            if (record->cpuVersion <= completion.targetGpuVersion)
            {
                discardUploadSourceIfConfigured(*record);
            }
            commitMutation(SceneRevisionMutation::materialResident);
            collectUnusedMaterialAsset(handle);
        }
        break;
    }
    case GpuAssetKind::texture: {
        auto handle = nr::resource::TextureHandle{completion.asset.slot, completion.asset.generation};
        if (auto *record = textures_.tryGet(handle); record != nullptr)
        {
            record->gpuState = GpuResidencyState::resident;
            record->gpuVersion = completion.targetGpuVersion;
            record->lastUploadFrameSerial = currentFrame_.frameSerial;
            record->uploadQueued = false;
            if (record->cpuVersion <= completion.targetGpuVersion)
            {
                discardUploadSourceIfConfigured(*record);
            }
            commitMutation(SceneRevisionMutation::textureResident);
            collectUnusedTextureAsset(handle);
        }
        break;
    }
    case GpuAssetKind::camera: {
        auto handle = nr::resource::CameraAssetHandle{completion.asset.slot, completion.asset.generation};
        if (auto *record = cameras_.tryGet(handle); record != nullptr)
        {
            record->gpuState = GpuResidencyState::resident;
            record->gpuVersion = completion.targetGpuVersion;
            record->lastUploadFrameSerial = currentFrame_.frameSerial;
            record->uploadQueued = false;
            if (record->cpuVersion <= completion.targetGpuVersion)
            {
                discardUploadSourceIfConfigured(*record);
            }
            collectUnusedCameraAsset(handle);
        }
        break;
    }
    case GpuAssetKind::light: {
        auto handle = nr::resource::LightAssetHandle{completion.asset.slot, completion.asset.generation};
        if (auto *record = lights_.tryGet(handle); record != nullptr)
        {
            record->gpuState = GpuResidencyState::resident;
            record->gpuVersion = completion.targetGpuVersion;
            record->lastUploadFrameSerial = currentFrame_.frameSerial;
            record->uploadQueued = false;
            if (record->cpuVersion <= completion.targetGpuVersion)
            {
                discardUploadSourceIfConfigured(*record);
            }
            collectUnusedLightAsset(handle);
        }
        break;
    }
    default:
        break;
    }
}

[[nodiscard]] std::uint64_t Scene::maxUploadSignalValue(const PendingGraphicsSyncBatch &batch) noexcept
{
    auto maxSignalValue = std::uint64_t{0};
    std::ranges::for_each(batch.bufferTickets, [&](const nr::rhi::ops::BufferUploadTicket &ticket) { maxSignalValue = std::max(maxSignalValue, ticket.signalValue); });
    std::ranges::for_each(batch.imageTickets, [&](const nr::rhi::ops::ImageUploadTicket &ticket) { maxSignalValue = std::max(maxSignalValue, ticket.signalValue); });
    return maxSignalValue;
}

void Scene::reapSubmittedGraphicsSyncWork()
{
    auto completed = std::vector<GraphicsSyncCompletion>{};
    std::erase_if(submittedGraphicsSyncWork_, [&](SubmittedGraphicsSyncWork &work) {
        nrAssert(*work.fence != nullptr, "Scene submitted graphics sync work requires a valid fence.");
        auto result = device_.device.waitForFences(*work.fence, vk::True, 0u);
        nrAssert(result == vk::Result::eSuccess || result == vk::Result::eTimeout, "Scene failed querying graphics sync fence status.");
        if (result != vk::Result::eSuccess || !submittedGeometryAtlasGrowWork_.empty())
        {
            return false;
        }

        std::ranges::move(work.completions, std::back_inserter(completed));
        return true;
    });

    std::ranges::for_each(completed, [&](const GraphicsSyncCompletion &completion) { markGraphicsSyncCompletionResident(completion); });
    if (!completed.empty())
    {
        compactDeadAssetHandles();
    }
}

void Scene::submitPendingGraphicsSyncBatches(nr::rhi::ops::UploadReadbackContext &uploadContext)
{
    if (pendingGraphicsSyncBatches_.empty())
    {
        return;
    }

    uploadContext.reclaimCompletedUploads();
    auto batches = std::exchange(pendingGraphicsSyncBatches_, {});
    auto maxSignalValue = std::uint64_t{0};
    std::ranges::for_each(batches, [&](const PendingGraphicsSyncBatch &batch) { maxSignalValue = std::max(maxSignalValue, maxUploadSignalValue(batch)); });
    nrAssert(maxSignalValue > 0u, "Scene staged graphics sync requires a valid upload timeline signal.");

    auto syncWork = SubmittedGraphicsSyncWork{};
    syncWork.completions = batches | std::views::transform([](const PendingGraphicsSyncBatch &batch) {
                               return GraphicsSyncCompletion{
                                   .asset = batch.asset,
                                   .targetGpuVersion = batch.targetGpuVersion,
                               };
                           }) |
                           std::ranges::to<std::vector>();

    auto syncSubmission = nr::rhi::CommandBatch{};
    syncSubmission.addWait(uploadContext.uploadTimelineSemaphore(), vk::PipelineStageFlagBits2::eAllCommands, maxSignalValue);

    syncWork.commandPool = nr::rhi::CommandPool{
        device_.device,
        device_.queueManager.graphics().queueFamilyIndex(),
        vk::CommandPoolCreateFlagBits::eTransient,
    };
    syncWork.commandBuffers.emplace(syncWork.commandPool.allocatePrimary(1));
    auto &syncCommandBuffer = syncWork.commandBuffers->front();

    nr::rhi::CommandRecorder::beginPrimary(syncCommandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    std::ranges::for_each(batches, [&](const PendingGraphicsSyncBatch &batch) {
        std::ranges::for_each(batch.bufferTickets, [&](const nr::rhi::ops::BufferUploadTicket &ticket) { uploadContext.recordAcquireBarrier(syncCommandBuffer, ticket); });
        std::ranges::for_each(batch.imageTickets, [&](const nr::rhi::ops::ImageUploadTicket &ticket) { uploadContext.recordImageAcquireBarrier(syncCommandBuffer, ticket); });
    });
    nr::rhi::CommandRecorder::end(syncCommandBuffer);
    syncSubmission.addCommandBuffer(syncCommandBuffer);

    syncWork.fence = vk::raii::Fence(device_.device, vk::FenceCreateInfo{});
    device_.queueManager.graphics().submit(std::move(syncSubmission), std::cref(syncWork.fence));
    submittedGraphicsSyncWork_.push_back(std::move(syncWork));
}

void Scene::subtractSaturating(std::size_t &value, std::size_t delta) noexcept
{
    value = value >= delta ? value - delta : 0u;
}

void Scene::compactDeadAssetHandles()
{
    compactDeadHandles(meshHandles_, meshes_);
    compactDeadHandles(materialHandles_, materials_);
    compactDeadHandles(textureHandles_, textures_);
    compactDeadHandles(cameraHandles_, cameras_);
    compactDeadHandles(lightHandles_, lights_);
}

void Scene::collectUnusedMeshAsset(nr::resource::MeshHandle handle)
{
    collectUnusedAsset(handle, meshes_, retiredMeshPayloadGraveyard_);
}

void Scene::collectUnusedMaterialAsset(nr::resource::MaterialHandle handle)
{
    collectUnusedAsset(handle, materials_, retiredMaterialPayloadGraveyard_);
}

void Scene::collectUnusedTextureAsset(nr::resource::TextureHandle handle)
{
    collectUnusedAsset(handle, textures_, retiredTexturePayloadGraveyard_);
}

void Scene::collectUnusedCameraAsset(nr::resource::CameraAssetHandle handle)
{
    collectUnusedAsset(handle, cameras_, retiredCameraPayloadGraveyard_);
}

void Scene::collectUnusedLightAsset(nr::resource::LightAssetHandle handle)
{
    collectUnusedAsset(handle, lights_, retiredLightPayloadGraveyard_);
}

void Scene::collectTemplatePinnedAssets(const TemplateResourcePinSet &pinSet)
{
    auto collectAssets = [&]<typename CollectorFn>(const auto &handles, CollectorFn &&collector) { std::ranges::for_each(handles, std::forward<CollectorFn>(collector)); };

    collectAssets(pinSet.meshes, [this](auto h) { collectUnusedMeshAsset(h); });
    collectAssets(pinSet.materials, [this](auto h) { collectUnusedMaterialAsset(h); });
    collectAssets(pinSet.textures, [this](auto h) { collectUnusedTextureAsset(h); });
    collectAssets(pinSet.cameras, [this](auto h) { collectUnusedCameraAsset(h); });
    collectAssets(pinSet.lights, [this](auto h) { collectUnusedLightAsset(h); });
}


} // namespace nr::scene
