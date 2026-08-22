import std;
import dependency.slang;
import dependency.vulkan;
import nr.rhi;
import nr.test;

namespace
{
constexpr auto payloadPackingResultNames = std::array{
    "oct32 +X",
    "oct32 -X",
    "oct32 +Y",
    "oct32 -Y",
    "oct32 +Z",
    "oct32 -Z",
    "oct32 lower-hemisphere seam sign",
    "oct32 zero/invalid fallback",
    "transient material-ray aliases",
    "five oct32 field mappings",
    "decoded anisotropy tangent reprojection",
    "UNORM material/specular pairs",
    "UNORM pair roughness/clearcoat",
    "UNORM pair clearcoat roughness/sheen X",
    "UNORM pair sheen Y/Z",
    "UNORM pair sheen roughness/transmission",
    "decoded UNORM field mappings and roughness floor",
    "remaining payload fields and packed base/specular",
    "material filter packet advance",
    "Charlie sheen albedo rough grazing",
    "Charlie sheen albedo medium",
    "Charlie sheen albedo smooth grazing",
    "Charlie sheen albedo minimum-roughness transition",
    "thin IOR-zero transmission activation",
    "thin IOR-zero transmission evaluation and PDF",
    "thin IOR-zero transmission sample",
    "volume IOR-zero rejection preserves diffuse",
};

void executePayloadPackingContract(const nr::rhi::SlangProgram &program)
{
    auto device = nr::rhi::Device::create("nr_rhi_fas_shader_contract_test", "NewbieRenderer");

    auto pipeline = device.pipeline().createComputePipeline(program).get();
    nr::test::require(pipeline.pipeline.valid() && pipeline.layout.valid() && pipeline.descriptorLayout.valid(),
                      "payload packing contract should create a valid compute pipeline");

    constexpr auto resultByteSize = payloadPackingResultNames.size() * sizeof(std::uint32_t);
    auto bufferInfo = vk::BufferCreateInfo{};
    bufferInfo.size = resultByteSize;
    bufferInfo.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc;
    bufferInfo.sharingMode = vk::SharingMode::eExclusive;
    auto resultBuffer = device.resourceFactory.createBuffer(bufferInfo, nr::rhi::MemoryUsage::GpuOnly,
                                                            "payload_packing_contract_results");
    nr::test::require(resultBuffer.valid(), "payload packing contract should create a valid GPU result buffer");

    auto rootCursor = pipeline.descriptorLayout.rootCursor();
    rootCursor.beginRecording();
    nr::test::require(rootCursor["gContractResults"].setObject(resultBuffer),
                      "payload packing contract should bind its reflected result buffer");
    auto bindingSnapshot = rootCursor.takeSnapshot();
    auto bindingSets = nr::rhi::allocateBindingSetsForLayout(pipeline.layout, pipeline.bindingPool);
    auto descriptorWriteCache = nr::rhi::DescriptorWriteCache{};
    nr::rhi::updateResourcesForBindingSnapshot(pipeline.bindingPool, bindingSets, descriptorWriteCache, bindingSnapshot,
                                               {});

    nr::rhi::submitOneShot(device.device, device.queueManager.compute(), {},
                           [&](const vk::raii::CommandBuffer &commandBuffer) {
                               commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline.pipeline.raw());
                               nr::rhi::bindPreparedResourcesToCommandBuffer(commandBuffer,
                                                                             vk::PipelineBindPoint::eCompute,
                                                                             pipeline.layout, bindingSets);
                               commandBuffer.dispatch(1u, 1u, 1u);
                           });

    auto readbackTicket =
        device.uploadReadback().readbackBuffer(resultBuffer, 0, resultByteSize, nr::rhi::QueueRole::Compute,
                                               nr::rhi::ops::ReadbackSyncPlan{
                                                   .preCopy =
                                                       nr::rhi::ops::ReadbackSyncScope{
                                                           .stages = vk::PipelineStageFlagBits2::eComputeShader,
                                                           .access = vk::AccessFlagBits2::eShaderStorageWrite,
                                                       },
                                                   .postCopy =
                                                       nr::rhi::ops::ReadbackSyncScope{
                                                           .stages = vk::PipelineStageFlagBits2::eComputeShader,
                                                           .access = vk::AccessFlagBits2::eShaderStorageWrite,
                                                       },
                                               });
    auto readback = device.uploadReadback().readbackBytes(readbackTicket);
    nr::test::requireEqual(readback.size(), resultByteSize);

    auto results = std::array<std::uint32_t, payloadPackingResultNames.size()>{};
    std::memcpy(results.data(), readback.data(), resultByteSize);
    std::ranges::for_each(std::views::iota(std::size_t{0u}, results.size()), [&](std::size_t resultIndex) {
        nr::test::require(results[resultIndex] == 1u,
                          std::format("GPU payload packing contract failed: {} (value={})",
                                      payloadPackingResultNames[resultIndex], results[resultIndex]));
    });

    device.waitIdle();
}

const nr::test::CaseRegistrar materialFilterContractCase{
    "material FAS contract compiles both root policies with stable layout ABI", [] {
        auto &shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        auto compileContract = [&](bool enableFilterAfterShading) {
            auto variant = nr::rhi::SlangProgramVariantDesc{};
            variant.assign("kEnableFilterAfterShading", "bool", enableFilterAfterShading);
            return shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
                .sourcePath = std::filesystem::path{"test/rt/materialFilterAfterShadingContract"},
                .variant = variant,
            });
        };

        auto offProgram = compileContract(false);
        auto onProgram = compileContract(true);
        auto payloadPackingProgram = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"test/rt/materialPayloadPackingContract"},
        });
        nr::test::require(
            offProgram.valid() && onProgram.valid() && payloadPackingProgram.valid(),
            "material FAS and payload packing contracts should compile as separate single-entry programs");
        nr::test::require(offProgram.entryPoint()->spirv != nullptr && !offProgram.entryPoint()->spirv->empty() &&
                              onProgram.entryPoint()->spirv != nullptr && !onProgram.entryPoint()->spirv->empty() &&
                              payloadPackingProgram.entryPoint()->spirv != nullptr &&
                              !payloadPackingProgram.entryPoint()->spirv->empty(),
                          "material FAS and payload packing contracts should expose compute SPIR-V");

        auto *programLayout = offProgram.programLayout();
        nr::test::require(programLayout != nullptr, "material FAS contract should expose reflection");
        auto *rayPayloadType = programLayout->findTypeByName("ResolvedMaterialRayPayload");
        auto *rayPayloadLayout = rayPayloadType != nullptr ? programLayout->getTypeLayout(rayPayloadType) : nullptr;
        nr::test::require(rayPayloadLayout != nullptr,
                          "material FAS contract should reflect ResolvedMaterialRayPayload");
        auto const rayPayloadSize = static_cast<std::size_t>(rayPayloadLayout->getSize());
        auto const rayPayloadStride = static_cast<std::size_t>(rayPayloadLayout->getStride());
        nr::test::require(
            rayPayloadSize == 128u && rayPayloadStride == 128u,
            std::format("packed ResolvedMaterialRayPayload should remain 32 uint32 lanes: size={}, stride={}",
                        rayPayloadSize, rayPayloadStride));
        auto *rayPayloadWrapperType = programLayout->findTypeByName("MaterialRayPayload");
        auto *rayPayloadWrapperLayout =
            rayPayloadWrapperType != nullptr ? programLayout->getTypeLayout(rayPayloadWrapperType) : nullptr;
        nr::test::require(rayPayloadWrapperLayout != nullptr && rayPayloadWrapperLayout->getSize() == rayPayloadSize,
                          "the stage-facing MaterialRayPayload wrapper must add no lanes to its resolved transport "
                          "record");

        auto offLayout = nr::rhi::ShaderDescriptorLayout::create(offProgram);
        auto onLayout = nr::rhi::ShaderDescriptorLayout::create(onProgram);
        nr::test::require(offLayout.valid() && onLayout.valid(), "material FAS contract layouts should be valid");
        nr::test::require(nr::rhi::shaderLayoutAbiEquivalent(offLayout.abiSignature(), onLayout.abiSignature()),
                          "material FAS root policies must preserve contract layout ABI");

        executePayloadPackingContract(payloadPackingProgram);
    }};
} // namespace
