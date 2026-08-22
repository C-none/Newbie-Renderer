import std;
import dependency.shaderShare;
import dependency.vulkan;
import nr.rhi;
import nr.test;
import nr.utils;

namespace
{
using nr::shader::share::AlphaMode;
using nr::shader::share::MaterialTextureSlot;
using nr::shader::share::RtGeometryFlag;
using nr::shader::share::RtGeometryMetadata;
using nr::shader::share::RtInstanceMetadata;
using nr::shader::share::RtMaterialFeatureFlag;
using nr::shader::share::RtMaterialHeader;
using nr::shader::share::RtMaterialTextureRef;

inline constexpr auto kLaneCount = std::uint32_t{5u};
inline constexpr auto kGeometryCount = std::uint32_t{4u};
inline constexpr auto kMaterialCount = std::uint32_t{3u};
inline constexpr auto kTextureSlotCount = static_cast<std::uint32_t>(MaterialTextureSlot::count);
inline constexpr auto kVertexStride = std::uint32_t{72u};
inline constexpr auto kInvalidMetadataGeometryCount = std::uint32_t{2u};
inline constexpr auto kInvalidMetadataResultCount = std::uint32_t{4u};
inline constexpr auto kInvalidMetadataNearAccepted = std::uint32_t{0x4e454152u};
inline constexpr auto kInvalidMetadataMaterialMiss = std::uint32_t{0x4d495353u};

struct RtContractVertex
{
    std::array<float, 3> position{};
    std::array<float, 3> normal{};
    std::array<float, 4> tangent{};
    std::array<float, 2> texCoord0{};
    std::array<float, 2> texCoord1{};
    std::array<float, 4> color{};
};

static_assert(sizeof(RtContractVertex) == kVertexStride);
static_assert(nr::memberOffset<&RtContractVertex::position>() == 0u);
static_assert(nr::memberOffset<&RtContractVertex::normal>() == 12u);
static_assert(nr::memberOffset<&RtContractVertex::tangent>() == 24u);
static_assert(nr::memberOffset<&RtContractVertex::texCoord0>() == 40u);
static_assert(nr::memberOffset<&RtContractVertex::texCoord1>() == 48u);
static_assert(nr::memberOffset<&RtContractVertex::color>() == 56u);

[[nodiscard]] RtContractVertex makeVertex(float x, float y) noexcept
{
    return RtContractVertex{
        .position = {x, y, 0.0f},
        .normal = {0.0f, 0.0f, 1.0f},
        .tangent = {1.0f, 0.0f, 0.0f, 1.0f},
        .texCoord0 = {0.5f, 0.5f},
        .texCoord1 = {0.5f, 0.5f},
        .color = {1.0f, 1.0f, 1.0f, 1.0f},
    };
}

[[nodiscard]] RtContractVertex makeInvalidMetadataVertex(float x, float y, float z) noexcept
{
    auto vertex = makeVertex(x, y);
    vertex.position[2] = z;
    return vertex;
}

[[nodiscard]] std::array<RtContractVertex, 12> makeVertices() noexcept
{
    auto const triangle = [](float centerX) {
        return std::array{
            makeVertex(centerX - 0.75f, -0.75f),
            makeVertex(centerX + 0.75f, -0.75f),
            makeVertex(centerX, 0.75f),
        };
    };

    auto const lane1 = triangle(-2.0f);
    auto const lane2 = triangle(0.0f);
    auto const lane3 = triangle(2.0f);
    auto const lane4 = triangle(4.0f);
    return {
        lane1[0], lane1[1], lane1[2], lane2[0], lane2[1], lane2[2],
        lane3[0], lane3[1], lane3[2], lane4[0], lane4[1], lane4[2],
    };
}

[[nodiscard]] std::array<std::uint32_t, 12> makeIndices() noexcept
{
    // Vulkan RT treats the first two triangles as front-facing from +Z. The last two deliberately reverse winding.
    return {0u, 1u, 2u, 3u, 4u, 5u, 6u, 8u, 7u, 9u, 11u, 10u};
}

[[nodiscard]] std::array<RtMaterialHeader, kMaterialCount> makeMaterialHeaders()
{
    auto headers = std::array<RtMaterialHeader, kMaterialCount>{};
    std::ranges::for_each(std::views::iota(std::uint32_t{0u}, kMaterialCount), [&](std::uint32_t materialIndex) {
        auto &header = headers[materialIndex];
        header.textureRefOffset = materialIndex * kTextureSlotCount;
        header.textureRefCount = kTextureSlotCount;
        header.alphaMode = AlphaMode::opaque;
        header.alphaCutoff = 0.5f;
        header.baseColorFactor = DirectX::XMFLOAT4{1.0f, 1.0f, 1.0f, 1.0f};
        header.roughnessNormalOcclusionAlpha = DirectX::XMFLOAT4{1.0f, 1.0f, 1.0f, 1.0f};
    });

    headers[1].featureFlags = RtMaterialFeatureFlag::alphaMask;
    headers[1].alphaMode = AlphaMode::mask;
    headers[1].baseColorFactor = DirectX::XMFLOAT4{1.0f, 1.0f, 1.0f, 0.0f};

    headers[2].featureFlags = RtMaterialFeatureFlag::alphaMask | RtMaterialFeatureFlag::doubleSided;
    headers[2].alphaMode = AlphaMode::mask;
    return headers;
}

[[nodiscard]] std::vector<RtMaterialTextureRef> makeMaterialTextureRefs()
{
    auto refs = std::vector<RtMaterialTextureRef>(
        static_cast<std::size_t>(kMaterialCount) * static_cast<std::size_t>(kTextureSlotCount));
    std::ranges::for_each(refs, [](RtMaterialTextureRef &ref) {
        ref.uvLinear = DirectX::XMFLOAT4{1.0f, 0.0f, 0.0f, 1.0f};
        ref.textureId = 0u;
        ref.uvSet = 0u;
    });
    return refs;
}

[[nodiscard]] std::array<RtGeometryMetadata, kGeometryCount> makeGeometryMetadata()
{
    constexpr auto materialIndices = std::array{0u, 1u, 0u, 2u};
    auto metadata = std::array<RtGeometryMetadata, kGeometryCount>{};
    std::ranges::for_each(std::views::iota(std::uint32_t{0u}, kGeometryCount), [&](std::uint32_t geometryIndex) {
        metadata[geometryIndex] = RtGeometryMetadata{
            .materialIndex = materialIndices[geometryIndex],
            .geometryIndex = geometryIndex,
            .primitiveOffset = geometryIndex * 3u,
            .primitiveCount = 1u,
            .flags = RtGeometryFlag::indexed,
        };
    });
    return metadata;
}

[[nodiscard]] RtInstanceMetadata makeInstanceMetadata() noexcept
{
    return RtInstanceMetadata{
        .geometryCount = kGeometryCount,
        .vertexStride = kVertexStride,
    };
}

[[nodiscard]] std::array<RtContractVertex, 6> makeInvalidMetadataVertices() noexcept
{
    auto const triangleAtDepth = [](float z) {
        return std::array{
            makeInvalidMetadataVertex(-0.75f, -0.75f, z),
            makeInvalidMetadataVertex(0.75f, -0.75f, z),
            makeInvalidMetadataVertex(0.0f, 0.75f, z),
        };
    };

    auto const far = triangleAtDepth(0.0f);
    auto const near = triangleAtDepth(0.5f);
    return {far[0], far[1], far[2], near[0], near[1], near[2]};
}

[[nodiscard]] std::array<std::uint32_t, 6> makeInvalidMetadataIndices() noexcept
{
    return {0u, 1u, 2u, 3u, 4u, 5u};
}

[[nodiscard]] std::array<RtMaterialHeader, 1> makeInvalidMetadataMaterialHeaders()
{
    auto headers = std::array<RtMaterialHeader, 1>{};
    auto &header = headers.front();
    header.textureRefCount = kTextureSlotCount;
    header.featureFlags = RtMaterialFeatureFlag::alphaMask;
    header.alphaMode = AlphaMode::mask;
    header.alphaCutoff = 0.5f;
    header.baseColorFactor = DirectX::XMFLOAT4{1.0f, 1.0f, 1.0f, 0.0f};
    header.roughnessNormalOcclusionAlpha = DirectX::XMFLOAT4{1.0f, 1.0f, 1.0f, 1.0f};
    return headers;
}

[[nodiscard]] std::array<RtMaterialTextureRef, kTextureSlotCount> makeInvalidMetadataMaterialTextureRefs()
{
    auto refs = std::array<RtMaterialTextureRef, kTextureSlotCount>{};
    std::ranges::for_each(refs, [](RtMaterialTextureRef &ref) {
        ref.uvLinear = DirectX::XMFLOAT4{1.0f, 0.0f, 0.0f, 1.0f};
        ref.textureId = 0u;
        ref.uvSet = 0u;
    });
    return refs;
}

[[nodiscard]] std::array<RtGeometryMetadata, 1> makeInvalidMetadataGeometryMetadata() noexcept
{
    return {RtGeometryMetadata{
        .materialIndex = 0u,
        .geometryIndex = 0u,
        .primitiveCount = 1u,
        .flags = RtGeometryFlag::indexed,
    }};
}

[[nodiscard]] RtInstanceMetadata makeInvalidMetadataInstanceMetadata() noexcept
{
    return RtInstanceMetadata{
        .geometryCount = 1u,
        .vertexStride = kVertexStride,
    };
}

template <typename T>
[[nodiscard]] nr::rhi::Buffer createCpuToGpuBuffer(nr::rhi::Device &device, std::span<const T> values,
                                                    vk::BufferUsageFlags usage, std::string_view debugName)
{
    auto buffer = device.resourceFactory.createBuffer(
        nr::rhi::makeBufferCreateInfo(values.size_bytes(), usage), nr::rhi::MemoryUsage::CpuToGpu, debugName);
    nr::test::require(buffer.valid(), std::format("{} should be a valid CPU-to-GPU buffer", debugName));
    buffer.writeMappedAndFlush(values);
    return buffer;
}

[[nodiscard]] nr::rhi::Buffer createGpuBuffer(nr::rhi::Device &device, vk::DeviceSize size,
                                               vk::BufferUsageFlags usage, std::string_view debugName)
{
    auto buffer = device.resourceFactory.createBuffer(nr::rhi::makeBufferCreateInfo(size, usage),
                                                       nr::rhi::MemoryUsage::GpuOnly, debugName);
    nr::test::require(buffer.valid(), std::format("{} should be a valid GPU buffer", debugName));
    return buffer;
}

[[nodiscard]] nr::rhi::ops::BufferUploadOwnershipPlan makeTextureUploadPlan(const nr::rhi::Device &device)
{
    auto const queueFamilies = device.queueManager.familyIndices();
    return nr::rhi::ops::makeTransferUploadOwnershipPlan(queueFamilies.transfer, queueFamilies.graphics,
                                                         nr::rhi::ops::QueueAccessScope{
                                                             .stages = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                                                             .access = vk::AccessFlagBits2::eShaderSampledRead,
                                                         });
}

void acquireTextureOnGraphics(nr::rhi::Device &device, const nr::rhi::ops::ImageUploadTicket &ticket)
{
    nr::rhi::submitOneShot(device.device, device.queueManager.graphics(),
                           nr::rhi::OneShotSyncPlan{
                               .waitSemaphore = *device.uploadReadback().uploadTimelineSemaphore(),
                               .waitStage = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                               .waitValue = ticket.signalValue,
                           },
                           [&](const vk::raii::CommandBuffer &commandBuffer) {
                               device.uploadReadback().recordImageAcquireBarrier(commandBuffer, ticket);
                           });
}

[[nodiscard]] nr::rhi::Image createWhiteTexture(nr::rhi::Device &device)
{
    auto image = device.resourceFactory.createImage(
        nr::rhi::makeImageCreateInfo(vk::Format::eR8G8B8A8Unorm, vk::Extent2D{1u, 1u},
                                     vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled),
        nr::rhi::MemoryUsage::GpuOnly, "shadow_ray_contract_white_texture");
    nr::test::require(image.valid(), "shadow-ray contract white texture should be valid");

    auto const white = std::array{
        std::byte{0xff},
        std::byte{0xff},
        std::byte{0xff},
        std::byte{0xff},
    };
    auto ticket = device.uploadReadback().uploadImage(white, image, vk::ImageLayout::eUndefined,
                                                       vk::ImageLayout::eShaderReadOnlyOptimal,
                                                       makeTextureUploadPlan(device));
    nr::test::require(ticket.valid(), "shadow-ray contract texture upload ticket should be valid");
    acquireTextureOnGraphics(device, ticket);
    return image;
}

[[nodiscard]] std::vector<nr::rhi::SlangProgram> compileContractPrograms()
{
    auto &shaderService = nr::rhi::ShaderService::instance();
    shaderService.configure();
    auto const requests = std::array{
        nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"test/rt/shadowRayTypeContract/raygen"},
        },
        nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"test/rt/shadowRayTypeContract/materialMissPoison"},
        },
        nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/pathTracing/shadowMiss"},
        },
        nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"test/rt/shadowRayTypeContract/materialAnyHitPoison"},
        },
        nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/pathTracing/shadowAnyHit"},
        },
    };
    auto programs = shaderService.compileProgramsByFile(requests);
    nr::test::requireEqual(programs.size(), requests.size());
    nr::test::require(std::ranges::all_of(programs, &nr::rhi::SlangProgram::valid),
                      "shadow-ray GPU contract shader batch should compile");
    return programs;
}

[[nodiscard]] nr::rhi::RayTracingProgramAssemblyDesc makeContractAssembly(
    const std::vector<nr::rhi::SlangProgram> &programs)
{
    auto assembly = nr::rhi::RayTracingProgramAssemblyDesc{};
    assembly.stages = {
        nr::rhi::RayTracingPipelineStageSelection{
            .program = std::cref(programs[0]),
            .logicalEntryPointName = "rgShadowContract",
        },
        nr::rhi::RayTracingPipelineStageSelection{
            .program = std::cref(programs[1]),
            .logicalEntryPointName = "msMaterialPoison",
        },
        nr::rhi::RayTracingPipelineStageSelection{
            .program = std::cref(programs[2]),
            .logicalEntryPointName = "msShadow",
        },
        nr::rhi::RayTracingPipelineStageSelection{
            .program = std::cref(programs[3]),
            .logicalEntryPointName = "ahMaterialPoison",
        },
        nr::rhi::RayTracingPipelineStageSelection{
            .program = std::cref(programs[4]),
            .logicalEntryPointName = "ahShadow",
        },
    };
    assembly.groups = {
        nr::rhi::RayTracingShaderGroupDesc{
            .name = "raygen",
            .generalEntryPoint = "rgShadowContract",
        },
        nr::rhi::RayTracingShaderGroupDesc{
            .name = "miss_material_poison",
            .generalEntryPoint = "msMaterialPoison",
        },
        nr::rhi::RayTracingShaderGroupDesc{
            .name = "miss_shadow",
            .generalEntryPoint = "msShadow",
        },
        nr::rhi::RayTracingShaderGroupDesc{
            .name = "hit_material_poison",
            .type = vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup,
            .anyHitEntryPoint = "ahMaterialPoison",
        },
        nr::rhi::RayTracingShaderGroupDesc{
            .name = "hit_shadow",
            .type = vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup,
            .anyHitEntryPoint = "ahShadow",
        },
    };
    return assembly;
}

[[nodiscard]] nr::rhi::ShaderBindingTable createContractShaderBindingTable(
    nr::rhi::Device &device, const nr::rhi::RayTracingPipeline &pipeline)
{
    auto const raygenRecords = std::array{
        nr::rhi::ShaderBindingTableRecordDesc{.groupIndex = pipeline.shaderGroupIndex("raygen")},
    };
    auto const missRecords = std::array{
        nr::rhi::ShaderBindingTableRecordDesc{.groupIndex = pipeline.shaderGroupIndex("miss_material_poison")},
        nr::rhi::ShaderBindingTableRecordDesc{.groupIndex = pipeline.shaderGroupIndex("miss_shadow")},
    };

    auto const materialHitGroup = pipeline.shaderGroupIndex("hit_material_poison");
    auto const shadowHitGroup = pipeline.shaderGroupIndex("hit_shadow");
    auto hitRecords = std::array<nr::rhi::ShaderBindingTableRecordDesc, kGeometryCount * 2u>{};
    std::ranges::for_each(std::views::iota(std::uint32_t{0u}, kGeometryCount), [&](std::uint32_t geometryIndex) {
        hitRecords[geometryIndex * 2u] = nr::rhi::ShaderBindingTableRecordDesc{.groupIndex = materialHitGroup};
        hitRecords[geometryIndex * 2u + 1u] = nr::rhi::ShaderBindingTableRecordDesc{.groupIndex = shadowHitGroup};
    });

    return nr::rhi::ShaderBindingTable::create(
        device.resourceFactory,
        nr::rhi::ShaderBindingTableBuildDesc{
            .pipeline = pipeline,
            .raygen = nr::rhi::ShaderBindingTableSectionDesc{.records = raygenRecords},
            .miss = nr::rhi::ShaderBindingTableSectionDesc{.records = missRecords},
            .hit = nr::rhi::ShaderBindingTableSectionDesc{.records = hitRecords},
            .debugName = "shadow_ray_contract_sbt",
        });
}

[[nodiscard]] std::vector<nr::rhi::SlangProgram> compileInvalidMetadataContractPrograms()
{
    auto &shaderService = nr::rhi::ShaderService::instance();
    shaderService.configure();
    auto const requests = std::array{
        nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"test/rt/shadowRayTypeContract/invalidMetadataRaygen"},
        },
        nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"test/rt/shadowRayTypeContract/invalidMetadataMaterialClosestHit"},
        },
        nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"test/rt/shadowRayTypeContract/invalidMetadataMaterialMiss"},
        },
        nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/pathTracing/anyHit"},
        },
        nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/pathTracing/shadowAnyHit"},
        },
        nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/pathTracing/shadowMiss"},
        },
    };
    auto programs = shaderService.compileProgramsByFile(requests);
    nr::test::requireEqual(programs.size(), requests.size());
    nr::test::require(std::ranges::all_of(programs, &nr::rhi::SlangProgram::valid),
                      "invalid-metadata GPU contract shader batch should compile");
    return programs;
}

[[nodiscard]] nr::rhi::RayTracingProgramAssemblyDesc makeInvalidMetadataContractAssembly(
    const std::vector<nr::rhi::SlangProgram> &programs)
{
    auto assembly = nr::rhi::RayTracingProgramAssemblyDesc{};
    assembly.stages = {
        nr::rhi::RayTracingPipelineStageSelection{
            .program = std::cref(programs[0]),
            .logicalEntryPointName = "rgInvalidMetadataContract",
        },
        nr::rhi::RayTracingPipelineStageSelection{
            .program = std::cref(programs[1]),
            .logicalEntryPointName = "chInvalidMetadataContract",
        },
        nr::rhi::RayTracingPipelineStageSelection{
            .program = std::cref(programs[2]),
            .logicalEntryPointName = "msInvalidMetadataContract",
        },
        nr::rhi::RayTracingPipelineStageSelection{
            .program = std::cref(programs[3]),
            .logicalEntryPointName = "ahMaterialPolicy",
        },
        nr::rhi::RayTracingPipelineStageSelection{
            .program = std::cref(programs[4]),
            .logicalEntryPointName = "ahShadow",
        },
        nr::rhi::RayTracingPipelineStageSelection{
            .program = std::cref(programs[5]),
            .logicalEntryPointName = "msShadow",
        },
    };
    assembly.groups = {
        nr::rhi::RayTracingShaderGroupDesc{
            .name = "invalid_metadata_raygen",
            .generalEntryPoint = "rgInvalidMetadataContract",
        },
        nr::rhi::RayTracingShaderGroupDesc{
            .name = "invalid_metadata_miss_material",
            .generalEntryPoint = "msInvalidMetadataContract",
        },
        nr::rhi::RayTracingShaderGroupDesc{
            .name = "invalid_metadata_miss_shadow",
            .generalEntryPoint = "msShadow",
        },
        nr::rhi::RayTracingShaderGroupDesc{
            .name = "invalid_metadata_hit_material",
            .type = vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup,
            .closestHitEntryPoint = "chInvalidMetadataContract",
            .anyHitEntryPoint = "ahMaterialPolicy",
        },
        nr::rhi::RayTracingShaderGroupDesc{
            .name = "invalid_metadata_hit_shadow",
            .type = vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup,
            .anyHitEntryPoint = "ahShadow",
        },
    };
    return assembly;
}

[[nodiscard]] nr::rhi::ShaderBindingTable createInvalidMetadataContractShaderBindingTable(
    nr::rhi::Device &device, const nr::rhi::RayTracingPipeline &pipeline)
{
    auto const raygenRecords = std::array{
        nr::rhi::ShaderBindingTableRecordDesc{.groupIndex = pipeline.shaderGroupIndex("invalid_metadata_raygen")},
    };
    auto const missRecords = std::array{
        nr::rhi::ShaderBindingTableRecordDesc{
            .groupIndex = pipeline.shaderGroupIndex("invalid_metadata_miss_material")},
        nr::rhi::ShaderBindingTableRecordDesc{
            .groupIndex = pipeline.shaderGroupIndex("invalid_metadata_miss_shadow")},
    };

    auto const materialHitGroup = pipeline.shaderGroupIndex("invalid_metadata_hit_material");
    auto const shadowHitGroup = pipeline.shaderGroupIndex("invalid_metadata_hit_shadow");
    auto hitRecords = std::array<nr::rhi::ShaderBindingTableRecordDesc, kInvalidMetadataGeometryCount * 2u>{};
    std::ranges::for_each(std::views::iota(std::uint32_t{0u}, kInvalidMetadataGeometryCount),
                          [&](std::uint32_t geometryIndex) {
                              hitRecords[geometryIndex * 2u] =
                                  nr::rhi::ShaderBindingTableRecordDesc{.groupIndex = materialHitGroup};
                              hitRecords[geometryIndex * 2u + 1u] =
                                  nr::rhi::ShaderBindingTableRecordDesc{.groupIndex = shadowHitGroup};
                          });

    return nr::rhi::ShaderBindingTable::create(
        device.resourceFactory,
        nr::rhi::ShaderBindingTableBuildDesc{
            .pipeline = pipeline,
            .raygen = nr::rhi::ShaderBindingTableSectionDesc{.records = raygenRecords},
            .miss = nr::rhi::ShaderBindingTableSectionDesc{.records = missRecords},
            .hit = nr::rhi::ShaderBindingTableSectionDesc{.records = hitRecords},
            .debugName = "invalid_metadata_contract_sbt",
        });
}

[[nodiscard]] vk::DeviceAddress alignedAddress(vk::DeviceAddress address, vk::DeviceSize alignment) noexcept
{
    if (alignment <= 1u)
    {
        return address;
    }
    auto const remainder = address % alignment;
    return remainder == 0u ? address : address + alignment - remainder;
}

void addAsBuildBarrier(nr::rhi::ops::BarrierBatch &barriers, vk::PipelineStageFlags2 destinationStages,
                       vk::AccessFlags2 destinationAccess)
{
    auto barrier = vk::MemoryBarrier2{};
    barrier.srcStageMask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR;
    barrier.srcAccessMask = vk::AccessFlagBits2::eAccelerationStructureWriteKHR;
    barrier.dstStageMask = destinationStages;
    barrier.dstAccessMask = destinationAccess;
    barriers.add(barrier);
}

const nr::test::CaseRegistrar shadowRayTypeGpuContractCase{
    "path tracing shadow ray type routes compact payload through production miss and any-hit shaders", [] {
        auto device = nr::rhi::Device::create("nr_rhi_path_tracing_shadow_ray_gpu_contract_test", "NewbieRenderer");

        auto const vertices = makeVertices();
        auto const indices = makeIndices();
        auto const instanceMetadataValue = makeInstanceMetadata();
        auto const geometryMetadataValues = makeGeometryMetadata();
        auto const materialHeaders = makeMaterialHeaders();
        auto const materialTextureRefs = makeMaterialTextureRefs();

        auto const geometryBufferUsage = vk::BufferUsageFlagBits::eStorageBuffer |
                                         vk::BufferUsageFlagBits::eShaderDeviceAddress |
                                         vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;
        auto vertexBuffer = createCpuToGpuBuffer(
            device, std::span<const RtContractVertex>{vertices}, geometryBufferUsage, "shadow_ray_contract_vertices");
        auto indexBuffer = createCpuToGpuBuffer(
            device, std::span<const std::uint32_t>{indices}, geometryBufferUsage, "shadow_ray_contract_indices");
        auto instanceMetadataBuffer = createCpuToGpuBuffer(
            device, std::span<const RtInstanceMetadata>{&instanceMetadataValue, 1u},
            vk::BufferUsageFlagBits::eStorageBuffer, "shadow_ray_contract_instance_metadata");
        auto geometryMetadataBuffer = createCpuToGpuBuffer(
            device, std::span<const RtGeometryMetadata>{geometryMetadataValues},
            vk::BufferUsageFlagBits::eStorageBuffer, "shadow_ray_contract_geometry_metadata");
        auto materialHeaderBuffer = createCpuToGpuBuffer(
            device, std::span<const RtMaterialHeader>{materialHeaders}, vk::BufferUsageFlagBits::eStorageBuffer,
            "shadow_ray_contract_material_headers");
        auto materialTextureRefBuffer = createCpuToGpuBuffer(
            device, std::span<const RtMaterialTextureRef>{materialTextureRefs},
            vk::BufferUsageFlagBits::eStorageBuffer, "shadow_ray_contract_material_texture_refs");

        constexpr auto resultByteSize = static_cast<vk::DeviceSize>(kLaneCount * sizeof(std::uint32_t));
        auto resultBuffer = createGpuBuffer(device, resultByteSize,
                                            vk::BufferUsageFlagBits::eStorageBuffer |
                                                vk::BufferUsageFlagBits::eTransferSrc,
                                            "shadow_ray_contract_results");
        auto whiteTexture = createWhiteTexture(device);
        auto textureSampler = device.pipeline().createSampler(
            nr::rhi::SlangSamplerDesc{
                .magFilter = vk::Filter::eNearest,
                .minFilter = vk::Filter::eNearest,
                .mipmapMode = vk::SamplerMipmapMode::eNearest,
                .addressModeU = vk::SamplerAddressMode::eClampToEdge,
                .addressModeV = vk::SamplerAddressMode::eClampToEdge,
                .addressModeW = vk::SamplerAddressMode::eClampToEdge,
                .maxLod = 0.0f,
            },
            "shadow_ray_contract_sampler");
        nr::test::require(textureSampler.valid(), "shadow-ray contract sampler should be valid");

        auto const geometryFlags = std::array{
            vk::GeometryFlagsKHR{vk::GeometryFlagBitsKHR::eOpaque},
            vk::GeometryFlagsKHR{},
            vk::GeometryFlagsKHR{},
            vk::GeometryFlagsKHR{},
        };
        auto blasGeometryRecords =
            std::views::iota(std::uint32_t{0u}, kGeometryCount) |
            std::views::transform([&](std::uint32_t geometryIndex) {
                return nr::rhi::makeBlasTriangleGeometryRecord(
                    nr::rhi::BlasTriangleGeometryBuffers{
                        .vertex = std::cref(vertexBuffer),
                        .index = std::cref(indexBuffer),
                    },
                    nr::rhi::BlasGeometryLayout{
                        .vertexStride = sizeof(RtContractVertex),
                        .indexType = vk::IndexType::eUint32,
                        .maxVertex = static_cast<std::uint32_t>(vertices.size() - 1u),
                        .geometryFlags = geometryFlags[geometryIndex],
                    },
                    nr::rhi::BlasGeometryInput{
                        .vertexAddress = vertexBuffer.deviceAddress(),
                        .indexAddress = indexBuffer.deviceAddress(),
                        .primitiveCount = 1u,
                        .primitiveOffset =
                            static_cast<std::uint32_t>(geometryIndex * 3u * sizeof(std::uint32_t)),
                    });
            }) |
            std::ranges::to<std::vector>();
        auto const buildOptions = nr::rhi::AsBuildOptions{
            .buildFlags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace,
        };
        auto const blasSizes = nr::rhi::queryBlasBuildSizes(
            device.device, std::span<const nr::rhi::BlasGeometryRecord>{blasGeometryRecords}, buildOptions);
        auto blasStorageBuffer = createGpuBuffer(
            device, blasSizes.accelerationStructureSize,
            vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
                vk::BufferUsageFlagBits::eShaderDeviceAddress,
            "shadow_ray_contract_blas_storage");
        auto blas = nr::rhi::AccelerationStructureResource::create(
            device.device, blasStorageBuffer, 0u, blasSizes.accelerationStructureSize,
            vk::AccelerationStructureTypeKHR::eBottomLevel, "shadow_ray_contract_blas");
        nr::test::require(blas.valid(), "shadow-ray contract BLAS should be valid");

        auto tlasInstance = vk::AccelerationStructureInstanceKHR{};
        auto const identity = vk::TransformMatrixKHR{std::array{
            std::array{1.0f, 0.0f, 0.0f, 0.0f},
            std::array{0.0f, 1.0f, 0.0f, 0.0f},
            std::array{0.0f, 0.0f, 1.0f, 0.0f},
        }};
        tlasInstance.setTransform(identity);
        tlasInstance.setInstanceCustomIndex(0u);
        tlasInstance.setMask(0xffu);
        tlasInstance.setInstanceShaderBindingTableRecordOffset(0u);
        tlasInstance.setFlags(vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable);
        tlasInstance.setAccelerationStructureReference(blas.deviceAddress());

        auto instanceBuffer = createCpuToGpuBuffer(
            device, std::span<const vk::AccelerationStructureInstanceKHR>{&tlasInstance, 1u},
            vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
                vk::BufferUsageFlagBits::eShaderDeviceAddress,
            "shadow_ray_contract_tlas_instance");
        auto const tlasBuildInput = nr::rhi::TlasBuildInput{
            .instancesAddress = instanceBuffer.deviceAddress(),
            .instanceCount = 1u,
        };
        auto const tlasSizes = nr::rhi::queryTlasBuildSizes(device.device, tlasBuildInput, buildOptions);
        auto tlasStorageBuffer = createGpuBuffer(
            device, tlasSizes.accelerationStructureSize,
            vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
                vk::BufferUsageFlagBits::eShaderDeviceAddress,
            "shadow_ray_contract_tlas_storage");
        auto tlas = nr::rhi::AccelerationStructureResource::create(
            device.device, tlasStorageBuffer, 0u, tlasSizes.accelerationStructureSize,
            vk::AccelerationStructureTypeKHR::eTopLevel, "shadow_ray_contract_tlas");
        nr::test::require(tlas.valid(), "shadow-ray contract TLAS should be valid");

        auto const asLimits = nr::rhi::queryAsBuildLimits(device.physicalDevice);
        auto const scratchSize = std::max(blasSizes.buildScratchSize, tlasSizes.buildScratchSize);
        auto scratchBuffer = createGpuBuffer(
            device, scratchSize + std::max<vk::DeviceSize>(asLimits.minScratchAlignment, 1u) - 1u,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress,
            "shadow_ray_contract_as_scratch");
        auto const scratchAddress = alignedAddress(scratchBuffer.deviceAddress(), asLimits.minScratchAlignment);

        auto programs = compileContractPrograms();
        auto assembly = makeContractAssembly(programs);
        auto pipelineDesc = nr::rhi::RayTracingPipelineDesc{};
        pipelineDesc.descriptorBindingPolicy.defaultRuntimeDescriptorCount = 1u;
        auto pipeline = device.pipeline()
                            .createRayTracingPipeline(programs.front(), assembly, pipelineDesc, 8u, {},
                                                      "shadow_ray_contract_pipeline")
                            .get();
        nr::test::require(pipeline.pipeline.valid() && pipeline.layout.valid() && pipeline.descriptorLayout.valid(),
                          "shadow-ray contract pipeline state should be valid");
        nr::test::requireEqual(pipeline.pipeline.shaderGroupCount(), 5u,
                               "shadow-ray contract pipeline should expose exactly five groups");
        auto shaderBindingTable = createContractShaderBindingTable(device, pipeline.pipeline);
        nr::test::require(shaderBindingTable.valid(), "shadow-ray contract SBT should be valid");

        auto rootCursor = pipeline.descriptorLayout.rootCursor();
        rootCursor.beginRecording();
        nr::test::require(rootCursor["gShadowRayTypeResults"].setObject(resultBuffer),
                          "shadow-ray contract result buffer should bind through reflection");
        nr::test::require(rootCursor["scene"].setObject(tlas.raw()),
                          "shadow-ray contract TLAS should bind through reflection");
        nr::test::require(rootCursor["rtInstanceMetadata"].setObject(instanceMetadataBuffer),
                          "shadow-ray contract instance metadata should bind through reflection");
        nr::test::require(rootCursor["rtGeometryMetadata"].setObject(geometryMetadataBuffer),
                          "shadow-ray contract geometry metadata should bind through reflection");
        nr::test::require(rootCursor["rtMaterialHeaders"].setObject(materialHeaderBuffer),
                          "shadow-ray contract material headers should bind through reflection");
        nr::test::require(rootCursor["rtMaterialTextureRefs"].setObject(materialTextureRefBuffer),
                          "shadow-ray contract material texture refs should bind through reflection");
        nr::test::require(rootCursor["rtVertexData"].setObject(vertexBuffer),
                          "shadow-ray contract vertex data should bind through reflection");
        nr::test::require(rootCursor["rtIndexData"].setObject(indexBuffer),
                          "shadow-ray contract index data should bind through reflection");
        nr::test::require(rootCursor["gSceneTextures"][0u].setObject(
                              whiteTexture, textureSampler.raw(), vk::ImageLayout::eShaderReadOnlyOptimal),
                          "shadow-ray contract scene texture should bind through reflection");
        auto bindingSnapshot = rootCursor.takeSnapshot();
        auto bindingSets = nr::rhi::allocateBindingSetsForLayout(
            pipeline.layout, pipeline.bindingPool, std::map<std::uint32_t, std::uint32_t>{{1u, 1u}});
        auto descriptorWriteCache = nr::rhi::DescriptorWriteCache{};
        nr::rhi::updateResourcesForBindingSnapshot(pipeline.bindingPool, bindingSets, descriptorWriteCache,
                                                   bindingSnapshot, {});

        auto commandPool = nr::rhi::CommandPool{
            device.device,
            device.queueManager.graphics().queueFamilyIndex(),
            vk::CommandPoolCreateFlagBits::eTransient,
        };
        auto commandBuffers = commandPool.allocate<vk::CommandBufferLevel::ePrimary>(1u);
        auto const &commandBuffer = commandBuffers.front();
        commandBuffer.begin(vk::CommandBufferBeginInfo{vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
        nr::rhi::recordBuildBlasGeometries(
            commandBuffer,
            nr::rhi::BlasGeometriesBuildRecordInfo{
                .dst = blas,
                .geometries = std::span<const nr::rhi::BlasGeometryRecord>{blasGeometryRecords},
                .scratchBuffer = scratchBuffer,
                .scratchAddress = scratchAddress,
                .options = buildOptions,
            },
            asLimits.minScratchAlignment);

        auto barriers = nr::rhi::ops::BarrierBatch{};
        addAsBuildBarrier(barriers, vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
                          vk::AccessFlagBits2::eAccelerationStructureReadKHR |
                              vk::AccessFlagBits2::eAccelerationStructureWriteKHR);
        nr::rhi::ops::pipelineBarrier(commandBuffer, barriers);
        nr::rhi::recordBuildTlas(commandBuffer,
                                 nr::rhi::TlasBuildRecordInfo{
                                     .dst = tlas,
                                     .instanceBuffer = instanceBuffer,
                                     .scratchBuffer = scratchBuffer,
                                     .scratchAddress = scratchAddress,
                                     .buildInput = tlasBuildInput,
                                     .options = buildOptions,
                                 },
                                 asLimits.minScratchAlignment);

        barriers.clear();
        addAsBuildBarrier(barriers, vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                          vk::AccessFlagBits2::eAccelerationStructureReadKHR);
        nr::rhi::ops::pipelineBarrier(commandBuffer, barriers);
        nr::rhi::bindPreparedResourcesToCommandBuffer(commandBuffer, vk::PipelineBindPoint::eRayTracingKHR,
                                                      pipeline.layout, bindingSets);
        nr::rhi::traceRays(commandBuffer,
                           nr::rhi::TraceRaysDesc{
                               .pipeline = pipeline.pipeline,
                               .shaderBindingTable = shaderBindingTable,
                               .dimensions = nr::rhi::TraceRaysDimensions{
                                   .width = kLaneCount,
                               },
                               .recordingQueueRole = nr::rhi::QueueRole::Graphics,
                           });
        commandBuffer.end();

        auto batch = nr::rhi::CommandBatch{};
        batch.addCommandBuffer(commandBuffer);
        device.queueManager.graphics().submit(std::move(batch));

        auto readbackTicket = device.uploadReadback().readbackBuffer(
            resultBuffer, 0u, resultByteSize, nr::rhi::QueueRole::Graphics,
            nr::rhi::ops::ReadbackSyncPlan{
                .preCopy = nr::rhi::ops::ReadbackSyncScope{
                    .stages = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                    .access = vk::AccessFlagBits2::eShaderStorageWrite,
                },
                .postCopy = nr::rhi::ops::ReadbackSyncScope{
                    .stages = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                    .access = vk::AccessFlagBits2::eShaderStorageWrite,
                },
            });
        auto readback = device.uploadReadback().readbackBytes(readbackTicket);
        nr::test::requireEqual(readback.size(), static_cast<std::size_t>(resultByteSize));

        auto actual = std::array<std::uint32_t, kLaneCount>{};
        std::memcpy(actual.data(), readback.data(), readback.size());
        constexpr auto expected = std::array{0u, 1u, 0u, 0u, 1u};
        std::ranges::for_each(std::views::iota(std::size_t{0u}, expected.size()), [&](std::size_t lane) {
            nr::test::requireEqual(actual[lane], expected[lane],
                                   std::format("shadow-ray contract lane {} should match the routing/policy oracle",
                                               lane));
        });

        device.waitIdle();
    }};

const nr::test::CaseRegistrar invalidMetadataGpuContractCase{
    "path tracing any-hit accepts invalid metadata before valid transparent geometry", [] {
        auto device = nr::rhi::Device::create("nr_rhi_path_tracing_shadow_ray_gpu_contract_test", "NewbieRenderer");

        auto const vertices = makeInvalidMetadataVertices();
        auto const indices = makeInvalidMetadataIndices();
        auto const instanceMetadataValue = makeInvalidMetadataInstanceMetadata();
        auto const geometryMetadataValues = makeInvalidMetadataGeometryMetadata();
        auto const materialHeaders = makeInvalidMetadataMaterialHeaders();
        auto const materialTextureRefs = makeInvalidMetadataMaterialTextureRefs();

        auto const geometryBufferUsage = vk::BufferUsageFlagBits::eStorageBuffer |
                                         vk::BufferUsageFlagBits::eShaderDeviceAddress |
                                         vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;
        auto vertexBuffer = createCpuToGpuBuffer(
            device, std::span<const RtContractVertex>{vertices}, geometryBufferUsage,
            "invalid_metadata_contract_vertices");
        auto indexBuffer = createCpuToGpuBuffer(
            device, std::span<const std::uint32_t>{indices}, geometryBufferUsage,
            "invalid_metadata_contract_indices");
        auto instanceMetadataBuffer = createCpuToGpuBuffer(
            device, std::span<const RtInstanceMetadata>{&instanceMetadataValue, 1u},
            vk::BufferUsageFlagBits::eStorageBuffer, "invalid_metadata_contract_instance_metadata");
        auto geometryMetadataBuffer = createCpuToGpuBuffer(
            device, std::span<const RtGeometryMetadata>{geometryMetadataValues}, vk::BufferUsageFlagBits::eStorageBuffer,
            "invalid_metadata_contract_geometry_metadata");
        auto materialHeaderBuffer = createCpuToGpuBuffer(
            device, std::span<const RtMaterialHeader>{materialHeaders}, vk::BufferUsageFlagBits::eStorageBuffer,
            "invalid_metadata_contract_material_headers");
        auto materialTextureRefBuffer = createCpuToGpuBuffer(
            device, std::span<const RtMaterialTextureRef>{materialTextureRefs}, vk::BufferUsageFlagBits::eStorageBuffer,
            "invalid_metadata_contract_material_texture_refs");

        constexpr auto resultByteSize = static_cast<vk::DeviceSize>(kInvalidMetadataResultCount * sizeof(std::uint32_t));
        auto resultBuffer = createGpuBuffer(device, resultByteSize,
                                            vk::BufferUsageFlagBits::eStorageBuffer |
                                                vk::BufferUsageFlagBits::eTransferSrc,
                                            "invalid_metadata_contract_results");
        auto whiteTexture = createWhiteTexture(device);
        auto textureSampler = device.pipeline().createSampler(
            nr::rhi::SlangSamplerDesc{
                .magFilter = vk::Filter::eNearest,
                .minFilter = vk::Filter::eNearest,
                .mipmapMode = vk::SamplerMipmapMode::eNearest,
                .addressModeU = vk::SamplerAddressMode::eClampToEdge,
                .addressModeV = vk::SamplerAddressMode::eClampToEdge,
                .addressModeW = vk::SamplerAddressMode::eClampToEdge,
                .maxLod = 0.0f,
            },
            "invalid_metadata_contract_sampler");
        nr::test::require(textureSampler.valid(), "invalid-metadata contract sampler should be valid");

        auto const geometryFlags = std::array{
            vk::GeometryFlagsKHR{},
            vk::GeometryFlagsKHR{},
        };
        auto blasGeometryRecords =
            std::views::iota(std::uint32_t{0u}, kInvalidMetadataGeometryCount) |
            std::views::transform([&](std::uint32_t geometryIndex) {
                return nr::rhi::makeBlasTriangleGeometryRecord(
                    nr::rhi::BlasTriangleGeometryBuffers{
                        .vertex = std::cref(vertexBuffer),
                        .index = std::cref(indexBuffer),
                    },
                    nr::rhi::BlasGeometryLayout{
                        .vertexStride = sizeof(RtContractVertex),
                        .indexType = vk::IndexType::eUint32,
                        .maxVertex = static_cast<std::uint32_t>(vertices.size() - 1u),
                        .geometryFlags = geometryFlags[geometryIndex],
                    },
                    nr::rhi::BlasGeometryInput{
                        .vertexAddress = vertexBuffer.deviceAddress(),
                        .indexAddress = indexBuffer.deviceAddress(),
                        .primitiveCount = 1u,
                        .primitiveOffset =
                            static_cast<std::uint32_t>(geometryIndex * 3u * sizeof(std::uint32_t)),
                    });
            }) |
            std::ranges::to<std::vector>();
        auto const buildOptions = nr::rhi::AsBuildOptions{
            .buildFlags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace,
        };
        auto const blasSizes = nr::rhi::queryBlasBuildSizes(
            device.device, std::span<const nr::rhi::BlasGeometryRecord>{blasGeometryRecords}, buildOptions);
        auto blasStorageBuffer = createGpuBuffer(
            device, blasSizes.accelerationStructureSize,
            vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
                vk::BufferUsageFlagBits::eShaderDeviceAddress,
            "invalid_metadata_contract_blas_storage");
        auto blas = nr::rhi::AccelerationStructureResource::create(
            device.device, blasStorageBuffer, 0u, blasSizes.accelerationStructureSize,
            vk::AccelerationStructureTypeKHR::eBottomLevel, "invalid_metadata_contract_blas");
        nr::test::require(blas.valid(), "invalid-metadata contract BLAS should be valid");

        auto tlasInstance = vk::AccelerationStructureInstanceKHR{};
        auto const identity = vk::TransformMatrixKHR{std::array{
            std::array{1.0f, 0.0f, 0.0f, 0.0f},
            std::array{0.0f, 1.0f, 0.0f, 0.0f},
            std::array{0.0f, 0.0f, 1.0f, 0.0f},
        }};
        tlasInstance.setTransform(identity);
        tlasInstance.setInstanceCustomIndex(0u);
        tlasInstance.setMask(0xffu);
        tlasInstance.setInstanceShaderBindingTableRecordOffset(0u);
        tlasInstance.setFlags(vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable);
        tlasInstance.setAccelerationStructureReference(blas.deviceAddress());

        auto instanceBuffer = createCpuToGpuBuffer(
            device, std::span<const vk::AccelerationStructureInstanceKHR>{&tlasInstance, 1u},
            vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
                vk::BufferUsageFlagBits::eShaderDeviceAddress,
            "invalid_metadata_contract_tlas_instance");
        auto const tlasBuildInput = nr::rhi::TlasBuildInput{
            .instancesAddress = instanceBuffer.deviceAddress(),
            .instanceCount = 1u,
        };
        auto const tlasSizes = nr::rhi::queryTlasBuildSizes(device.device, tlasBuildInput, buildOptions);
        auto tlasStorageBuffer = createGpuBuffer(
            device, tlasSizes.accelerationStructureSize,
            vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
                vk::BufferUsageFlagBits::eShaderDeviceAddress,
            "invalid_metadata_contract_tlas_storage");
        auto tlas = nr::rhi::AccelerationStructureResource::create(
            device.device, tlasStorageBuffer, 0u, tlasSizes.accelerationStructureSize,
            vk::AccelerationStructureTypeKHR::eTopLevel, "invalid_metadata_contract_tlas");
        nr::test::require(tlas.valid(), "invalid-metadata contract TLAS should be valid");

        auto const asLimits = nr::rhi::queryAsBuildLimits(device.physicalDevice);
        auto const scratchSize = std::max(blasSizes.buildScratchSize, tlasSizes.buildScratchSize);
        auto scratchBuffer = createGpuBuffer(
            device, scratchSize + std::max<vk::DeviceSize>(asLimits.minScratchAlignment, 1u) - 1u,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress,
            "invalid_metadata_contract_as_scratch");
        auto const scratchAddress = alignedAddress(scratchBuffer.deviceAddress(), asLimits.minScratchAlignment);

        auto programs = compileInvalidMetadataContractPrograms();
        auto assembly = makeInvalidMetadataContractAssembly(programs);
        auto pipelineDesc = nr::rhi::RayTracingPipelineDesc{};
        pipelineDesc.descriptorBindingPolicy.defaultRuntimeDescriptorCount = 1u;
        auto pipeline = device.pipeline()
                            .createRayTracingPipeline(programs.front(), assembly, pipelineDesc, 8u, {},
                                                      "invalid_metadata_contract_pipeline")
                            .get();
        nr::test::require(pipeline.pipeline.valid() && pipeline.layout.valid() && pipeline.descriptorLayout.valid(),
                          "invalid-metadata contract pipeline state should be valid");
        nr::test::requireEqual(pipeline.pipeline.shaderGroupCount(), 5u,
                               "invalid-metadata contract pipeline should expose exactly five groups");
        auto shaderBindingTable = createInvalidMetadataContractShaderBindingTable(device, pipeline.pipeline);
        nr::test::require(shaderBindingTable.valid(), "invalid-metadata contract SBT should be valid");

        auto rootCursor = pipeline.descriptorLayout.rootCursor();
        rootCursor.beginRecording();
        nr::test::require(rootCursor["gShadowRayTypeResults"].setObject(resultBuffer),
                          "invalid-metadata contract result buffer should bind through reflection");
        nr::test::require(rootCursor["scene"].setObject(tlas.raw()),
                          "invalid-metadata contract TLAS should bind through reflection");
        nr::test::require(rootCursor["rtInstanceMetadata"].setObject(instanceMetadataBuffer),
                          "invalid-metadata contract instance metadata should bind through reflection");
        nr::test::require(rootCursor["rtGeometryMetadata"].setObject(geometryMetadataBuffer),
                          "invalid-metadata contract geometry metadata should bind through reflection");
        nr::test::require(rootCursor["rtMaterialHeaders"].setObject(materialHeaderBuffer),
                          "invalid-metadata contract material headers should bind through reflection");
        nr::test::require(rootCursor["rtMaterialTextureRefs"].setObject(materialTextureRefBuffer),
                          "invalid-metadata contract material texture refs should bind through reflection");
        nr::test::require(rootCursor["rtVertexData"].setObject(vertexBuffer),
                          "invalid-metadata contract vertex data should bind through reflection");
        nr::test::require(rootCursor["rtIndexData"].setObject(indexBuffer),
                          "invalid-metadata contract index data should bind through reflection");
        nr::test::require(rootCursor["gSceneTextures"][0u].setObject(
                              whiteTexture, textureSampler.raw(), vk::ImageLayout::eShaderReadOnlyOptimal),
                          "invalid-metadata contract scene texture should bind through reflection");
        auto bindingSnapshot = rootCursor.takeSnapshot();
        auto bindingSets = nr::rhi::allocateBindingSetsForLayout(
            pipeline.layout, pipeline.bindingPool, std::map<std::uint32_t, std::uint32_t>{{1u, 1u}});
        auto descriptorWriteCache = nr::rhi::DescriptorWriteCache{};
        nr::rhi::updateResourcesForBindingSnapshot(pipeline.bindingPool, bindingSets, descriptorWriteCache,
                                                   bindingSnapshot, {});

        auto commandPool = nr::rhi::CommandPool{
            device.device,
            device.queueManager.graphics().queueFamilyIndex(),
            vk::CommandPoolCreateFlagBits::eTransient,
        };
        auto commandBuffers = commandPool.allocate<vk::CommandBufferLevel::ePrimary>(1u);
        auto const &commandBuffer = commandBuffers.front();
        commandBuffer.begin(vk::CommandBufferBeginInfo{vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
        nr::rhi::recordBuildBlasGeometries(
            commandBuffer,
            nr::rhi::BlasGeometriesBuildRecordInfo{
                .dst = blas,
                .geometries = std::span<const nr::rhi::BlasGeometryRecord>{blasGeometryRecords},
                .scratchBuffer = scratchBuffer,
                .scratchAddress = scratchAddress,
                .options = buildOptions,
            },
            asLimits.minScratchAlignment);

        auto barriers = nr::rhi::ops::BarrierBatch{};
        addAsBuildBarrier(barriers, vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
                          vk::AccessFlagBits2::eAccelerationStructureReadKHR |
                              vk::AccessFlagBits2::eAccelerationStructureWriteKHR);
        nr::rhi::ops::pipelineBarrier(commandBuffer, barriers);
        nr::rhi::recordBuildTlas(commandBuffer,
                                 nr::rhi::TlasBuildRecordInfo{
                                     .dst = tlas,
                                     .instanceBuffer = instanceBuffer,
                                     .scratchBuffer = scratchBuffer,
                                     .scratchAddress = scratchAddress,
                                     .buildInput = tlasBuildInput,
                                     .options = buildOptions,
                                 },
                                 asLimits.minScratchAlignment);

        barriers.clear();
        addAsBuildBarrier(barriers, vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                          vk::AccessFlagBits2::eAccelerationStructureReadKHR);
        nr::rhi::ops::pipelineBarrier(commandBuffer, barriers);
        nr::rhi::bindPreparedResourcesToCommandBuffer(commandBuffer, vk::PipelineBindPoint::eRayTracingKHR,
                                                      pipeline.layout, bindingSets);
        nr::rhi::traceRays(commandBuffer,
                           nr::rhi::TraceRaysDesc{
                               .pipeline = pipeline.pipeline,
                               .shaderBindingTable = shaderBindingTable,
                               .dimensions = nr::rhi::TraceRaysDimensions{
                                   .width = 1u,
                               },
                               .recordingQueueRole = nr::rhi::QueueRole::Graphics,
                           });
        commandBuffer.end();

        auto batch = nr::rhi::CommandBatch{};
        batch.addCommandBuffer(commandBuffer);
        device.queueManager.graphics().submit(std::move(batch));

        auto readbackTicket = device.uploadReadback().readbackBuffer(
            resultBuffer, 0u, resultByteSize, nr::rhi::QueueRole::Graphics,
            nr::rhi::ops::ReadbackSyncPlan{
                .preCopy = nr::rhi::ops::ReadbackSyncScope{
                    .stages = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                    .access = vk::AccessFlagBits2::eShaderStorageWrite,
                },
                .postCopy = nr::rhi::ops::ReadbackSyncScope{
                    .stages = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                    .access = vk::AccessFlagBits2::eShaderStorageWrite,
                },
            });
        auto readback = device.uploadReadback().readbackBytes(readbackTicket);
        nr::test::requireEqual(readback.size(), static_cast<std::size_t>(resultByteSize));

        auto actual = std::array<std::uint32_t, kInvalidMetadataResultCount>{};
        std::memcpy(actual.data(), readback.data(), readback.size());
        constexpr auto expected = std::array{
            kInvalidMetadataNearAccepted,
            1u,
            kInvalidMetadataMaterialMiss,
            0u,
        };
        std::ranges::for_each(std::views::iota(std::size_t{0u}, expected.size()), [&](std::size_t resultIndex) {
            nr::test::requireEqual(actual[resultIndex], expected[resultIndex],
                                   std::format("invalid-metadata contract result {} should match the policy oracle",
                                               resultIndex));
        });

        device.waitIdle();
    }};
} // namespace
