module nr.renderPasses;
import :lightPrepare;

import dependency.math;
import dependency.vulkan;
import nr.renderer;
import nr.resource;
import nr.rhi;
import nr.scene;
import nr.utils;
import std;
import :nodeType;

namespace nr::renderPasses::detail
{
struct LightPrepareFrameSlot
{
    nr::rhi::Buffer headerBuffer{};
    nr::rhi::Buffer lightBuffer{};
    nr::rhi::Buffer aliasBuffer{};
    std::uint32_t lightCapacity = 0;
    std::uint32_t aliasCapacity = 0;
};

struct LightPrepareRuntimeCache
{
    std::array<LightPrepareFrameSlot, nr::maxFrameInFlight> frameSlots{};
};

struct LightPrepareFrameData
{
    std::size_t frameSlot = 0;
    nr::scene::SceneLightGpuHeader header{};
    std::vector<nr::scene::SceneLightGpuRecord> records{};
    std::vector<nr::scene::SceneLightAliasGpuRecord> aliasRecords{};
};

[[nodiscard]] std::uint32_t grownCapacity(std::uint32_t currentCapacity, std::uint32_t requiredCapacity) noexcept
{
    auto capacity = std::max(currentCapacity, 1u);
    while (capacity < requiredCapacity)
    {
        capacity = std::max(requiredCapacity, capacity * 2u);
    }
    return capacity;
}

void ensureHeaderBuffer(nr::rhi::Device &device, LightPrepareFrameSlot &slot, std::size_t frameSlot)
{
    if (slot.headerBuffer.valid())
    {
        return;
    }

    slot.headerBuffer = device.resourceFactory.createBuffer(
        nr::rhi::makeBufferCreateInfo(
            sizeof(nr::scene::SceneLightGpuHeader),
            vk::BufferUsageFlagBits::eUniformBuffer),
        nr::rhi::MemoryUsage::CpuToGpu,
        std::format("LightPrepare.Header[{}]", frameSlot));
    nr::nrAssert(slot.headerBuffer.valid(), "LightPrepareNode failed to create scene light header buffer.");
}

void ensureLightBuffer(
    nr::rhi::Device &device,
    LightPrepareFrameSlot &slot,
    std::uint32_t requiredRecordCount,
    std::uint32_t initialCapacity,
    std::size_t frameSlot)
{
    requiredRecordCount = std::max(requiredRecordCount, 1u);
    auto const desiredCapacity = std::max(requiredRecordCount, std::max(initialCapacity, 1u));
    if (slot.lightBuffer.valid() && slot.lightCapacity >= requiredRecordCount)
    {
        return;
    }

    auto const capacity = grownCapacity(slot.lightCapacity, desiredCapacity);
    slot.lightBuffer = device.resourceFactory.createBuffer(
        nr::rhi::makeBufferCreateInfo(
            static_cast<vk::DeviceSize>(capacity) * sizeof(nr::scene::SceneLightGpuRecord),
            vk::BufferUsageFlagBits::eStorageBuffer),
        nr::rhi::MemoryUsage::CpuToGpu,
        std::format("LightPrepare.Records[{}]", frameSlot));
    nr::nrAssert(slot.lightBuffer.valid(), "LightPrepareNode failed to create scene light record buffer.");
    slot.lightCapacity = capacity;
}

void ensureAliasBuffer(
    nr::rhi::Device &device,
    LightPrepareFrameSlot &slot,
    std::uint32_t requiredRecordCount,
    std::uint32_t initialCapacity,
    std::size_t frameSlot)
{
    requiredRecordCount = std::max(requiredRecordCount, 1u);
    auto const desiredCapacity = std::max(requiredRecordCount, std::max(initialCapacity, 1u));
    if (slot.aliasBuffer.valid() && slot.aliasCapacity >= requiredRecordCount)
    {
        return;
    }

    auto const capacity = grownCapacity(slot.aliasCapacity, desiredCapacity);
    slot.aliasBuffer = device.resourceFactory.createBuffer(
        nr::rhi::makeBufferCreateInfo(
            static_cast<vk::DeviceSize>(capacity) * sizeof(nr::scene::SceneLightAliasGpuRecord),
            vk::BufferUsageFlagBits::eStorageBuffer),
        nr::rhi::MemoryUsage::CpuToGpu,
        std::format("LightPrepare.AliasTable[{}]", frameSlot));
    nr::nrAssert(slot.aliasBuffer.valid(), "LightPrepareNode failed to create scene light alias table buffer.");
    slot.aliasCapacity = capacity;
}

[[nodiscard]] glm::vec3 normalizedDirection(glm::vec3 direction) noexcept
{
    if (!nr::resource::math::finiteVec(direction) || glm::dot(direction, direction) <= 1.0e-8f)
    {
        return glm::vec3{0.0f, 0.0f, -1.0f};
    }
    return glm::normalize(direction);
}

[[nodiscard]] nr::scene::SceneLightGpuRecord makeDefaultSunLightRecord() noexcept
{
    // Synthetic default directional light follows glTF directional semantics:
    // intensity is illuminance in lux and color is a unitless linear RGB filter.
    return nr::scene::SceneLightGpuRecord{
        .meta = glm::uvec4{
            nr::scene::sceneLightGpuType(nr::resource::LightType::directional),
            0u,
            std::numeric_limits<std::uint32_t>::max(),
            0u,
        },
        .colorIntensity = glm::vec4{glm::vec3{1.0f, 0.92f, 0.72f}, 256.0f},
        .direction = glm::vec4{normalizedDirection(glm::vec3{0.482f, -0.704f, -0.522f}), 0.0f},
    };
}

[[nodiscard]] nr::scene::SceneLightGpuRecord packLightRecord(
    const nr::resource::LightAsset &light,
    const nr::scene::SceneLightPacket &packet)
{
    auto const innerCone = std::isfinite(light.innerConeRadians) && light.innerConeRadians >= 0.0f
                               ? light.innerConeRadians
                               : 0.0f;
    auto const outerCone = std::isfinite(light.outerConeRadians) && light.outerConeRadians >= innerCone
                               ? light.outerConeRadians
                               : innerCone;
    auto const intensity = std::isfinite(light.intensity) && light.intensity > 0.0f ? light.intensity : 0.0f;
    auto const hasFiniteRange =
        light.type != nr::resource::LightType::directional &&
        std::isfinite(light.range) &&
        light.range > 0.0f;
    auto const range = hasFiniteRange ? light.range : 0.0f;
    auto const flags = light.castShadow ? nr::scene::kSceneLightGpuFlagCastShadow : 0u;

    return nr::scene::SceneLightGpuRecord{
        .meta = glm::uvec4{
            nr::scene::sceneLightGpuType(light.type),
            flags,
            packet.stableInstanceId,
            0u,
        },
        .colorIntensity = glm::vec4{light.color, intensity},
        .positionRange = glm::vec4{packet.position, range},
        .direction = glm::vec4{normalizedDirection(packet.direction), 0.0f},
        .spotCone = glm::vec4{
            innerCone,
            outerCone,
            std::cos(innerCone),
            std::cos(outerCone),
        },
    };
}

[[nodiscard]] std::vector<nr::scene::SceneLightGpuRecord> buildLightRecords(
    const nr::renderer::NodeFrameParameters &frameParameters)
{
    auto records = std::vector<nr::scene::SceneLightGpuRecord>{};
    records.push_back(makeDefaultSunLightRecord());

    if (!frameParameters.scene.has_value() || !frameParameters.scenePackets.has_value())
    {
        return records;
    }

    auto const &scene = frameParameters.scene->get();
    auto const &lightPackets = frameParameters.scenePackets->get().lights;
    records.reserve(lightPackets.size() + 1u);

    std::ranges::for_each(lightPackets, [&](const nr::scene::SceneLightPacket &packet) {
        if (!packet.light.valid())
        {
            return;
        }

        auto lightRecord = scene.tryGetLightAsset(packet.light);
        if (!lightRecord.has_value() || !lightRecord->get().cpuReady)
        {
            return;
        }

        records.push_back(packLightRecord(lightRecord->get().cpu, packet));
    });

    return records;
}

[[nodiscard]] LightPrepareFrameData makeFrameData(
    std::size_t frameSlot,
    std::vector<nr::scene::SceneLightGpuRecord> records)
{
    nr::nrAssert(
        records.size() <= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()),
        "LightPrepareNode light count exceeds SceneLightGpuHeader::lightCount range.");

    auto aliasTable = nr::scene::buildSceneLightAliasTable(records);
    auto frameData = LightPrepareFrameData{
        .frameSlot = frameSlot,
        .header = nr::scene::SceneLightGpuHeader{
            .lightCount = static_cast<std::uint32_t>(records.size()),
            .aliasCount = aliasTable.aliasCount,
            .totalEnergy = aliasTable.totalEnergy,
        },
        .records = std::move(records),
        .aliasRecords = std::move(aliasTable.records),
    };

    if (frameData.records.empty())
    {
        frameData.records.push_back(nr::scene::SceneLightGpuRecord{});
    }
    if (frameData.aliasRecords.empty())
    {
        frameData.aliasRecords.push_back(nr::scene::SceneLightAliasGpuRecord{});
    }

    return frameData;
}
} // namespace nr::renderPasses::detail

namespace nr::renderPasses
{
LightPrepareNode::~LightPrepareNode() = default;

NodeDescription LightPrepareNode::describe() const
{
    return NodeDescription{
        .name = "LightPrepare",
    };
}

void LightPrepareNode::initialize(NodeInitContext& context)
{
    device_ = context.device;
    runtime_ = std::make_shared<detail::LightPrepareRuntimeCache>();
}

void LightPrepareNode::build(NodeBuildContext& context, const NodeFrameParameters& frameParameters)
{
    nr::nrAssert(static_cast<bool>(runtime_), "LightPrepare build stage requires initialized runtime state.");
    nr::nrAssert(device_.has_value(), "LightPrepare build stage requires device reference from initialize stage.");

    auto const frameSlot = static_cast<std::size_t>(frameParameters.frameIndex % nr::maxFrameInFlight);
    auto lightRecords = detail::buildLightRecords(frameParameters);
    auto frameData = detail::makeFrameData(frameSlot, std::move(lightRecords));
    auto &slot = runtime_->frameSlots[frameSlot];
    detail::ensureHeaderBuffer(device_->get(), slot, frameSlot);
    detail::ensureLightBuffer(
        device_->get(),
        slot,
        static_cast<std::uint32_t>(frameData.records.size()),
        input.initialLightCapacity,
        frameSlot);
    detail::ensureAliasBuffer(
        device_->get(),
        slot,
        static_cast<std::uint32_t>(frameData.aliasRecords.size()),
        input.initialLightCapacity,
        frameSlot);

    auto const headerResource = context.importBuffer(
        slot.headerBuffer,
        std::format("LightPrepare.Header[{}]", frameSlot),
        nr::renderer::ResourceLifetime::FrameLocal,
        {
            nr::renderer::BufferUsageIntent::Uniform,
        },
        nr::renderer::ownershipDomainFromQueue(context.queue));
    auto const lightsResource = context.importBuffer(
        slot.lightBuffer,
        std::format("LightPrepare.Records[{}]", frameSlot),
        nr::renderer::ResourceLifetime::FrameLocal,
        {
            nr::renderer::BufferUsageIntent::StorageRead,
        },
        nr::renderer::ownershipDomainFromQueue(context.queue));
    auto const aliasResource = context.importBuffer(
        slot.aliasBuffer,
        std::format("LightPrepare.AliasTable[{}]", frameSlot),
        nr::renderer::ResourceLifetime::FrameLocal,
        {
            nr::renderer::BufferUsageIntent::StorageRead,
        },
        nr::renderer::ownershipDomainFromQueue(context.queue));

    context.publishFrameResource(nr::renderer::frameResource::sceneLightHeader, headerResource);
    context.publishFrameResource(nr::renderer::frameResource::sceneLights, lightsResource);
    context.publishFrameResource(nr::renderer::frameResource::sceneLightAliasTable, aliasResource);

    auto const frameDataHandle = context.importFrameData(
        "LightPrepare.UploadData",
        std::move(frameData));
    auto resourceUses = std::array{
        nr::renderer::PassResourceUseDesc{
            .resource = headerResource,
            .bufferUsage = nr::renderer::BufferUsageIntent::Uniform,
            .bufferAccess = nr::renderer::BufferAccessIntent::HostWrite,
        },
        nr::renderer::PassResourceUseDesc{
            .resource = lightsResource,
            .bufferUsage = nr::renderer::BufferUsageIntent::StorageRead,
            .bufferAccess = nr::renderer::BufferAccessIntent::HostWrite,
        },
        nr::renderer::PassResourceUseDesc{
            .resource = aliasResource,
            .bufferUsage = nr::renderer::BufferUsageIntent::StorageRead,
            .bufferAccess = nr::renderer::BufferAccessIntent::HostWrite,
        },
    };

    [[maybe_unused]] auto uploadPass = context.addPass(
        std::span<const nr::renderer::PassResourceUseDesc>{resourceUses.data(), resourceUses.size()},
        "LightPrepare.Upload",
        [](const nr::renderer::PassRecordContext&) {},
        [runtime = runtime_, frameDataHandle](const nr::renderer::PassPrepareContext& prepareContext) {
            auto const &data = prepareContext.frameData<detail::LightPrepareFrameData>(frameDataHandle);
            auto &frameSlotState = runtime->frameSlots[data.frameSlot];

            frameSlotState.headerBuffer.writeMappedAndFlush(data.header);
            frameSlotState.lightBuffer.writeMappedAndFlush(
                std::span<const nr::scene::SceneLightGpuRecord>{data.records.data(), data.records.size()});
            frameSlotState.aliasBuffer.writeMappedAndFlush(
                std::span<const nr::scene::SceneLightAliasGpuRecord>{data.aliasRecords.data(), data.aliasRecords.size()});
        });
}

void LightPrepareNode::shutdown(NodeShutdownContext& context)
{
    (void)context;
    runtime_.reset();
    device_.reset();
}
} // namespace nr::renderPasses
