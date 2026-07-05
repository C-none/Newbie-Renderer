import std;
import dependency;
import nr.rhi;
import nr.renderer;
import nr.test;
import nr.utils;

namespace
{
template <typename VkHandle>
[[nodiscard]] VkHandle fakeVkHandle(std::uintptr_t value) noexcept
{
    if constexpr (std::is_pointer_v<VkHandle>)
    {
        return reinterpret_cast<VkHandle>(value);
    }
    else
    {
        return static_cast<VkHandle>(value);
    }
}

[[nodiscard]] nr::renderer::RenderGraphFrameDescription buildCrossQueueFrame()
{
    auto builder = nr::renderer::RenderGraphBuilder{};
    auto graphicsNode = builder.addNode("Geometry", nr::renderer::QueueDomain::Graphics);
    auto computeNode = builder.addNode("Resolve", nr::renderer::QueueDomain::Compute);

    auto color = builder.addResource(nr::renderer::GraphTransientImageDesc{
        .debugName = "Geometry.Color",
        .extent = vk::Extent3D{320, 180, 1},
        .format = vk::Format::eR8G8B8A8Unorm,
    });
    auto output = builder.addResource(nr::renderer::GraphTransientImageDesc{
        .debugName = "Resolve.Output",
        .extent = vk::Extent3D{320, 180, 1},
        .format = vk::Format::eR8G8B8A8Unorm,
    });

    auto graphicsUses = std::array{nr::renderer::use::colorWrite(color)};
    auto graphicsPass = builder.addPass("Geometry.Color", graphicsNode, graphicsUses, [](const nr::renderer::PassRecordContext &) {});

    auto submit = builder.addSubmitNode("GeometryToResolve");

    auto computeUses = std::array{
        nr::renderer::use::sampledRead(color),
        nr::renderer::use::storageWrite(output),
    };
    auto computePass = builder.addPass("Resolve.Compose", computeNode, computeUses, [](const nr::renderer::PassRecordContext &) {});

    nr::test::require(graphicsPass.valid(), "graphics pass should be valid");
    nr::test::require(submit.valid(), "submit boundary should be valid");
    nr::test::require(computePass.valid(), "compute pass should be valid");

    return builder.build();
}

[[nodiscard]] nr::renderer::RenderGraphFrameDescription buildMultiPassGraphicsFrame()
{
    auto builder = nr::renderer::RenderGraphBuilder{};
    auto node = builder.addNode("Graphics", nr::renderer::QueueDomain::Graphics);

    auto first = builder.addResource(nr::renderer::GraphTransientImageDesc{
        .debugName = "First",
        .extent = vk::Extent3D{64, 64, 1},
        .format = vk::Format::eR8G8B8A8Unorm,
    });
    auto second = builder.addResource(nr::renderer::GraphTransientImageDesc{
        .debugName = "Second",
        .extent = vk::Extent3D{64, 64, 1},
        .format = vk::Format::eR8G8B8A8Unorm,
    });
    auto third = builder.addResource(nr::renderer::GraphTransientImageDesc{
        .debugName = "Third",
        .extent = vk::Extent3D{64, 64, 1},
        .format = vk::Format::eR8G8B8A8Unorm,
    });

    auto firstUses = std::array{nr::renderer::use::colorWrite(first)};
    auto secondUses = std::array{nr::renderer::use::sampledRead(first), nr::renderer::use::colorWrite(second)};
    auto thirdUses = std::array{nr::renderer::use::sampledRead(second), nr::renderer::use::colorWrite(third)};

    static_cast<void>(builder.addPass("Graphics.First", node, firstUses, [](const nr::renderer::PassRecordContext&) {}));
    static_cast<void>(builder.addPass("Graphics.Second", node, secondUses, [](const nr::renderer::PassRecordContext&) {}));
    static_cast<void>(builder.addPass("Graphics.Third", node, thirdUses, [](const nr::renderer::PassRecordContext&) {}));

    return builder.build();
}

[[nodiscard]] nr::renderer::RenderGraphFrameDescription buildAccelerationStructureFrame()
{
    auto builder = nr::renderer::RenderGraphBuilder{};
    auto node = builder.addNode("RayTracing", nr::renderer::QueueDomain::Compute);

    auto tlas = builder.addResource(nr::renderer::GraphImportedAccelerationStructureDesc{
        .debugName = "Scene.TLAS",
        .type = vk::AccelerationStructureTypeKHR::eTopLevel,
        .size = 8192,
    });

    auto buildUses = std::array{nr::renderer::use::accelerationStructureBuildWrite(tlas)};
    static_cast<void>(builder.addPass("RayTracing.BuildTlas", node, buildUses, [](const nr::renderer::PassRecordContext&) {}));

    static_cast<void>(builder.addSubmitNode("BuildToTrace"));

    auto traceUses = std::array{nr::renderer::use::accelerationStructureTraceRead(tlas)};
    static_cast<void>(builder.addPass("RayTracing.Trace", node, traceUses, [](const nr::renderer::PassRecordContext&) {}));

    return builder.build();
}

[[nodiscard]] nr::renderer::RenderGraphFrameDescription buildAccelerationStructureFrameWithSize(vk::DeviceSize size)
{
    auto builder = nr::renderer::RenderGraphBuilder{};
    auto node = builder.addNode("RayTracing", nr::renderer::QueueDomain::Compute);

    auto tlas = builder.addResource(nr::renderer::GraphImportedAccelerationStructureDesc{
        .debugName = "Scene.TLAS",
        .type = vk::AccelerationStructureTypeKHR::eTopLevel,
        .size = size,
    });

    auto uses = std::array{nr::renderer::use::accelerationStructureTraceRead(tlas)};
    static_cast<void>(builder.addPass("RayTracing.Trace", node, uses, [](const nr::renderer::PassRecordContext&) {}));
    return builder.build();
}

[[nodiscard]] nr::renderer::RenderGraphFrameDescription buildCompileCachePatchFrame(
    std::uint32_t frameDataValue,
    const nr::rhi::Buffer& importedBuffer,
    std::string_view debugSuffix,
    std::uint32_t& recordedValue)
{
    auto builder = nr::renderer::RenderGraphBuilder{};
    auto node = builder.addNode(std::format("Cache.Node.{}", debugSuffix), nr::renderer::QueueDomain::Graphics);
    auto frameData = builder.addFrameData(std::format("Cache.FrameData.{}", debugSuffix), frameDataValue);
    auto constants = builder.addResource(nr::renderer::GraphImportedBufferDesc{
        .debugName = std::format("Cache.Constants.{}", debugSuffix),
        .size = 256,
        .usageIntents = {nr::renderer::BufferUsageIntent::Uniform},
        .importedResource = std::cref(importedBuffer),
    });
    auto color = builder.addResource(nr::renderer::GraphTransientImageDesc{
        .debugName = std::format("Cache.Color.{}", debugSuffix),
        .extent = vk::Extent3D{64, 64, 1},
        .format = vk::Format::eR8G8B8A8Unorm,
    });

    auto uses = std::array{
        nr::renderer::use::uniformRead(constants),
        nr::renderer::use::colorWrite(color),
    };
    static_cast<void>(builder.addPass(
        std::format("Cache.Pass.{}", debugSuffix),
        node,
        uses,
        [frameData, &recordedValue](const nr::renderer::PassRecordContext& recordContext) {
            recordedValue = recordContext.frameData<std::uint32_t>(frameData);
        }));
    return builder.build();
}

[[nodiscard]] std::optional<std::reference_wrapper<const std::any>> findFrameDataPayload(
    const nr::renderer::CompiledGraphFrame& compiled,
    nr::renderer::GraphFrameDataHandle handle)
{
    auto frameDataIt = std::ranges::find_if(
        compiled.frameData,
        [handle](const nr::renderer::GraphFrameDataDesc& desc) {
            return desc.handle == handle;
        });
    if (frameDataIt == compiled.frameData.end())
    {
        return {};
    }
    return std::cref(frameDataIt->payload);
}

[[nodiscard]] nr::renderer::RenderGraphFrameDescription buildSingleImageFrame(
    vk::Extent3D extent,
    vk::Format format)
{
    auto builder = nr::renderer::RenderGraphBuilder{};
    auto node = builder.addNode("SingleImage", nr::renderer::QueueDomain::Graphics);
    auto color = builder.addResource(nr::renderer::GraphTransientImageDesc{
        .debugName = "SingleImage.Color",
        .extent = extent,
        .format = format,
    });
    auto uses = std::array{nr::renderer::use::colorWrite(color)};
    static_cast<void>(builder.addPass("SingleImage.Pass", node, uses, [](const nr::renderer::PassRecordContext&) {}));
    return builder.build();
}

[[nodiscard]] nr::renderer::RenderGraphFrameDescription buildResourceUseFrame(bool readWrite)
{
    auto builder = nr::renderer::RenderGraphBuilder{};
    auto node = builder.addNode("ResourceUse", nr::renderer::QueueDomain::Graphics);
    auto color = builder.addResource(nr::renderer::GraphTransientImageDesc{
        .debugName = "ResourceUse.Color",
        .extent = vk::Extent3D{32, 32, 1},
        .format = vk::Format::eR8G8B8A8Unorm,
    });
    auto uses = std::array{readWrite ? nr::renderer::use::colorReadWrite(color) : nr::renderer::use::colorWrite(color)};
    static_cast<void>(builder.addPass("ResourceUse.Pass", node, uses, [](const nr::renderer::PassRecordContext&) {}));
    return builder.build();
}

[[nodiscard]] nr::renderer::RenderGraphFrameDescription buildPassOrderFrame(bool reversed)
{
    auto builder = nr::renderer::RenderGraphBuilder{};
    auto node = builder.addNode("PassOrder", nr::renderer::QueueDomain::Graphics);
    auto first = builder.addResource(nr::renderer::GraphTransientImageDesc{
        .debugName = "PassOrder.First",
        .extent = vk::Extent3D{32, 32, 1},
        .format = vk::Format::eR8G8B8A8Unorm,
    });
    auto second = builder.addResource(nr::renderer::GraphTransientImageDesc{
        .debugName = "PassOrder.Second",
        .extent = vk::Extent3D{32, 32, 1},
        .format = vk::Format::eR8G8B8A8Unorm,
    });

    auto addFirst = [&] {
        auto uses = std::array{nr::renderer::use::colorWrite(first)};
        static_cast<void>(builder.addPass("PassOrder.FirstPass", node, uses, [](const nr::renderer::PassRecordContext&) {}));
    };
    auto addSecond = [&] {
        auto uses = std::array{nr::renderer::use::colorWrite(second)};
        static_cast<void>(builder.addPass("PassOrder.SecondPass", node, uses, [](const nr::renderer::PassRecordContext&) {}));
    };

    if (reversed)
    {
        addSecond();
        addFirst();
    }
    else
    {
        addFirst();
        addSecond();
    }
    return builder.build();
}

[[nodiscard]] nr::renderer::RenderGraphFrameDescription buildSubmitOrderFrame(bool submitAfterBothPasses)
{
    auto builder = nr::renderer::RenderGraphBuilder{};
    auto node = builder.addNode("SubmitOrder", nr::renderer::QueueDomain::Graphics);
    auto first = builder.addResource(nr::renderer::GraphTransientImageDesc{
        .debugName = "SubmitOrder.First",
        .extent = vk::Extent3D{32, 32, 1},
        .format = vk::Format::eR8G8B8A8Unorm,
    });
    auto second = builder.addResource(nr::renderer::GraphTransientImageDesc{
        .debugName = "SubmitOrder.Second",
        .extent = vk::Extent3D{32, 32, 1},
        .format = vk::Format::eR8G8B8A8Unorm,
    });

    auto firstUses = std::array{nr::renderer::use::colorWrite(first)};
    auto secondUses = std::array{nr::renderer::use::colorWrite(second)};
    static_cast<void>(builder.addPass("SubmitOrder.FirstPass", node, firstUses, [](const nr::renderer::PassRecordContext&) {}));
    if (!submitAfterBothPasses)
    {
        static_cast<void>(builder.addSubmitNode("SubmitOrder.Boundary"));
    }
    static_cast<void>(builder.addPass("SubmitOrder.SecondPass", node, secondUses, [](const nr::renderer::PassRecordContext&) {}));
    if (submitAfterBothPasses)
    {
        static_cast<void>(builder.addSubmitNode("SubmitOrder.Boundary"));
    }
    return builder.build();
}

[[nodiscard]] nr::renderer::RenderGraphFrameDescription buildSwapchainFrame(
    std::uint32_t swapchainImageIndex,
    std::string_view debugSuffix)
{
    auto builder = nr::renderer::RenderGraphBuilder{};
    auto node = builder.addNode(std::format("Swapchain.Node.{}", debugSuffix), nr::renderer::QueueDomain::Compute);
    auto swapchainImage = builder.addResource(nr::renderer::GraphImportedSwapchainImageDesc{
        .debugName = std::format("Swapchain.Image.{}", debugSuffix),
        .initialOwnership = nr::renderer::ResourceOwnershipDomain::Compute,
        .swapchainImageIndex = swapchainImageIndex,
        .extent = vk::Extent3D{128, 72, 1},
        .format = vk::Format::eB8G8R8A8Unorm,
    });
    auto uses = std::array{nr::renderer::use::presentRead(swapchainImage, nr::renderer::ResourceOwnershipDomain::Compute)};
    static_cast<void>(builder.addPass(
        std::format("Swapchain.Pass.{}", debugSuffix),
        node,
        uses,
        [](const nr::renderer::PassRecordContext&) {}));
    return builder.build();
}

[[nodiscard]] nr::renderer::RenderGraphFrameDescription buildRetainedStorageWriteFrame(
    nr::renderer::RetainedImageState& state,
    std::string_view debugSuffix)
{
    auto builder = nr::renderer::RenderGraphBuilder{};
    auto node = builder.addNode(std::format("Retained.Node.{}", debugSuffix), nr::renderer::QueueDomain::Compute);
    auto retainedImage = builder.addResource(nr::renderer::GraphImportedImageDesc{
        .debugName = std::format("Retained.Image.{}", debugSuffix),
        .lifetime = nr::renderer::ResourceLifetime::RendererPersistent,
        .initialOwnership = state.initialized
                                ? state.ownership
                                : nr::renderer::ResourceOwnershipDomain::Undefined,
        .extent = vk::Extent3D{128, 72, 1},
        .format = vk::Format::eR16G16B16A16Sfloat,
        .usageIntents = {
            nr::renderer::ImageUsageIntent::StorageWrite,
            nr::renderer::ImageUsageIntent::TransferSrc,
        },
        .initialLayout = state.initialized
                             ? state.layout
                             : nr::renderer::ImageLayoutIntent::Undefined,
        .initialAccessScope = state.initialized
                                  ? state.access
                                  : nr::renderer::AccessScope{},
        .retainedState = std::ref(state),
    });
    auto uses = std::array{nr::renderer::use::storageWrite(retainedImage)};
    static_cast<void>(builder.addPass(
        std::format("Retained.Pass.{}", debugSuffix),
        node,
        uses,
        [](const nr::renderer::PassRecordContext&) {}));
    return builder.build();
}

template <typename TBaseFrameBuilder, typename TVariantFrameBuilder>
void requireCompileCacheMissForStructuralChange(
    std::string_view label,
    TBaseFrameBuilder baseFrameBuilder,
    TVariantFrameBuilder variantFrameBuilder)
{
    auto cache = nr::renderer::RenderGraphCompileCache{};
    auto baseFrame = baseFrameBuilder();
    static_cast<void>(cache.compileConsumingCached(baseFrame));
    auto variantFrame = variantFrameBuilder();
    static_cast<void>(cache.compileConsumingCached(variantFrame));

    auto stats = cache.statistics();
    nr::test::requireEqual(stats.hitCount, std::uint64_t{0}, std::format("{} should not hit", label));
    nr::test::requireEqual(stats.missCount, std::uint64_t{2}, std::format("{} should compile as a miss", label));
}

struct FakeBindlessPipeline
{
    nr::rhi::SlangProgram program{};
    nr::rhi::ShaderDescriptorLayout descriptorLayout{};
    std::array<std::map<std::uint32_t, std::uint32_t>, nr::maxFrameInFlight> variableCountsByFrame{};
    bool forceReallocation = false;

    [[nodiscard]] nr::rhi::ShaderCursor rootCursor() const
    {
        return descriptorLayout.rootCursor();
    }

    [[nodiscard]] bool ensureBindingSetsForFrame(
        std::uint32_t frameIndex,
        const std::map<std::uint32_t, std::uint32_t>& variableDescriptorCountsBySet)
    {
        auto const frameSlot = static_cast<std::size_t>(frameIndex % variableCountsByFrame.size());
        auto const reallocated = forceReallocation ||
                                 variableCountsByFrame[frameSlot] != variableDescriptorCountsBySet;
        variableCountsByFrame[frameSlot] = variableDescriptorCountsBySet;
        forceReallocation = false;
        return reallocated;
    }
};

[[nodiscard]] FakeBindlessPipeline makeFakeUiBindlessPipeline(std::uint32_t runtimeDescriptorCount)
{
    auto& shaderService = nr::rhi::ShaderService::instance();
    shaderService.configure();
    auto pipeline = FakeBindlessPipeline{};
    pipeline.program = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
        .sourcePath = std::filesystem::path{"renderer/appUi"},
    });
    nr::test::require(pipeline.program.valid(), "appUi shader program should compile for bindless cache contract tests");

    pipeline.descriptorLayout = nr::rhi::ShaderDescriptorLayout::create(
        pipeline.program,
        nr::rhi::DescriptorBindingPolicy{
            .defaultRuntimeDescriptorCount = runtimeDescriptorCount,
        });
    nr::test::require(pipeline.descriptorLayout.valid(), "appUi descriptor layout should be valid for bindless cache tests");
    return pipeline;
}

[[nodiscard]] nr::renderer::BindlessImageDescriptor logicalTextureDescriptor(
    std::uint64_t logicalResourceId,
    std::string debugName)
{
    return nr::renderer::BindlessImageDescriptor{
        .layout = vk::ImageLayout::eShaderReadOnlyOptimal,
        .logicalResourceId = logicalResourceId,
        .debugName = std::move(debugName),
    };
}

[[nodiscard]] nr::renderer::BindlessImageTableRequest makeBindlessCacheRequest(
    std::uint64_t tableVersion,
    std::uint32_t descriptorCapacity,
    std::map<std::uint32_t, nr::renderer::BindlessImageDescriptor> descriptorsById,
    std::optional<nr::renderer::BindlessImageDescriptor> fallbackDescriptor =
        logicalTextureDescriptor(100u, "fallback"))
{
    return nr::renderer::BindlessImageTableRequest{
        .tableKey = "test.gUiTextures",
        .shaderSymbol = "gUiTextures",
        .expectedSet = 1u,
        .expectedBinding = 0u,
        .expectedDescriptorType = vk::DescriptorType::eCombinedImageSampler,
        .descriptorCapacity = descriptorCapacity,
        .sampler = vk::Sampler{fakeVkHandle<vk::Sampler::CType>(0x5001u)},
        .tableVersion = tableVersion,
        .descriptorsById = std::move(descriptorsById),
        .fallbackDescriptor = std::move(fallbackDescriptor),
    };
}

[[nodiscard]] std::uint64_t logicalResourceIdForArrayElement(
    const nr::rhi::ShaderBindingSnapshot& snapshot,
    std::uint32_t arrayElement)
{
    auto writeIt = std::ranges::find_if(
        snapshot.descriptorWrites(),
        [arrayElement](const nr::rhi::ShaderBindingRecord& record) {
            return record.arrayElement == arrayElement;
        });
    nr::test::require(writeIt != snapshot.descriptorWrites().end(), "expected descriptor write for requested array element");
    nr::test::require(
        std::holds_alternative<nr::rhi::LogicalResourceDescriptorWrite>(writeIt->payload),
        "bindless cache test descriptors should use logical payloads");
    return std::get<nr::rhi::LogicalResourceDescriptorWrite>(writeIt->payload).logicalResourceId;
}

const nr::test::CaseRegistrar compilerMappingCase{
    "render graph compiler maps usage and access intents",
    [] {
        nr::test::require(nr::renderer::RenderGraphCompiler::mapBufferUsageIntent(nr::renderer::BufferUsageIntent::ShaderBindingTable) ==
                          vk::BufferUsageFlagBits::eShaderBindingTableKHR);
        nr::test::require(nr::renderer::RenderGraphCompiler::mapBufferUsageIntent(nr::renderer::BufferUsageIntent::AccelerationStructureStorage) ==
                          vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR);
        nr::test::require(nr::renderer::RenderGraphCompiler::mapImageUsageIntent(nr::renderer::ImageUsageIntent::PresentSource) ==
                          vk::ImageUsageFlagBits::eTransferDst);
        nr::test::require(nr::renderer::RenderGraphCompiler::mapImageLayoutIntent(nr::renderer::ImageLayoutIntent::PresentSrc) ==
                          vk::ImageLayout::ePresentSrcKHR);
        nr::test::require(nr::renderer::RenderGraphCompiler::mapImageAspectIntent(nr::renderer::ImageAspectIntent::DepthStencil) ==
                          (vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil));

        auto graphicsUniform = nr::renderer::RenderGraphCompiler::mapBufferAccessIntent(
            nr::renderer::BufferAccessIntent::UniformRead,
            nr::renderer::QueueDomain::Graphics);
        nr::test::require(graphicsUniform.stages == vk::PipelineStageFlagBits2::eAllGraphics);
        nr::test::require(graphicsUniform.access == vk::AccessFlagBits2::eUniformRead);

        auto computeSample = nr::renderer::RenderGraphCompiler::mapImageAccessIntent(
            nr::renderer::ImageAccessIntent::SampledRead,
            nr::renderer::QueueDomain::Compute);
        nr::test::require(computeSample.stages ==
                          (vk::PipelineStageFlagBits2::eComputeShader |
                           vk::PipelineStageFlagBits2::eRayTracingShaderKHR));
        nr::test::require(computeSample.access == vk::AccessFlagBits2::eShaderSampledRead);

        auto colorReadWrite = nr::renderer::RenderGraphCompiler::mapImageAccessIntent(
            nr::renderer::ImageAccessIntent::ColorAttachmentReadWrite,
            nr::renderer::QueueDomain::Graphics);
        nr::test::require(colorReadWrite.stages == vk::PipelineStageFlagBits2::eColorAttachmentOutput);
        nr::test::require(colorReadWrite.access ==
                          (vk::AccessFlagBits2::eColorAttachmentRead |
                           vk::AccessFlagBits2::eColorAttachmentWrite));

        auto depthReadWrite = nr::renderer::RenderGraphCompiler::mapImageAccessIntent(
            nr::renderer::ImageAccessIntent::DepthStencilReadWrite,
            nr::renderer::QueueDomain::Graphics);
        nr::test::require(depthReadWrite.stages ==
                          (vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                           vk::PipelineStageFlagBits2::eLateFragmentTests));
        nr::test::require(depthReadWrite.access ==
                          (vk::AccessFlagBits2::eDepthStencilAttachmentRead |
                           vk::AccessFlagBits2::eDepthStencilAttachmentWrite));

        auto storageBufferReadWrite = nr::renderer::RenderGraphCompiler::mapBufferAccessIntent(
            nr::renderer::BufferAccessIntent::ShaderStorageReadWrite,
            nr::renderer::QueueDomain::Compute);
        nr::test::require(storageBufferReadWrite.stages ==
                          (vk::PipelineStageFlagBits2::eComputeShader |
                           vk::PipelineStageFlagBits2::eRayTracingShaderKHR));
        nr::test::require(storageBufferReadWrite.access ==
                          (vk::AccessFlagBits2::eShaderStorageRead |
                           vk::AccessFlagBits2::eShaderStorageWrite));

        auto accelerationStructureRead = nr::renderer::RenderGraphCompiler::mapBufferAccessIntent(
            nr::renderer::BufferAccessIntent::AccelerationStructureRead,
            nr::renderer::QueueDomain::Compute);
        nr::test::require(accelerationStructureRead.stages ==
                          (vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR |
                           vk::PipelineStageFlagBits2::eRayTracingShaderKHR));
        nr::test::require(accelerationStructureRead.access == vk::AccessFlagBits2::eAccelerationStructureReadKHR);

        auto accelerationStructureBuildInputRead = nr::renderer::RenderGraphCompiler::mapBufferAccessIntent(
            nr::renderer::BufferAccessIntent::AccelerationStructureBuildInputRead,
            nr::renderer::QueueDomain::Graphics);
        nr::test::require(accelerationStructureBuildInputRead.stages == vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR);
        nr::test::require(accelerationStructureBuildInputRead.access == vk::AccessFlagBits2::eShaderRead);

        auto accelerationStructureScratchReadWrite = nr::renderer::RenderGraphCompiler::mapBufferAccessIntent(
            nr::renderer::BufferAccessIntent::AccelerationStructureScratchReadWrite,
            nr::renderer::QueueDomain::Graphics);
        nr::test::require(accelerationStructureScratchReadWrite.stages == vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR);
        nr::test::require(accelerationStructureScratchReadWrite.access ==
                          (vk::AccessFlagBits2::eAccelerationStructureReadKHR |
                           vk::AccessFlagBits2::eAccelerationStructureWriteKHR));

        auto shaderBindingTableRead = nr::renderer::RenderGraphCompiler::mapBufferAccessIntent(
            nr::renderer::BufferAccessIntent::ShaderBindingTableRead,
            nr::renderer::QueueDomain::Compute);
        nr::test::require(shaderBindingTableRead.stages == vk::PipelineStageFlagBits2::eRayTracingShaderKHR);
        nr::test::require(shaderBindingTableRead.access == vk::AccessFlagBits2::eShaderBindingTableReadKHR);

        auto asBuildWrite = nr::renderer::RenderGraphCompiler::mapAccelerationStructureAccessIntent(
            nr::renderer::AccelerationStructureAccessIntent::BuildWrite);
        nr::test::require(asBuildWrite.stages == vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR);
        nr::test::require(asBuildWrite.access == vk::AccessFlagBits2::eAccelerationStructureWriteKHR);

        auto asTraceRead = nr::renderer::RenderGraphCompiler::mapAccelerationStructureAccessIntent(
            nr::renderer::AccelerationStructureAccessIntent::TraceRead);
        nr::test::require(asTraceRead.stages == vk::PipelineStageFlagBits2::eRayTracingShaderKHR);
        nr::test::require(asTraceRead.access == vk::AccessFlagBits2::eAccelerationStructureReadKHR);

        auto asCopyWrite = nr::renderer::RenderGraphCompiler::mapAccelerationStructureAccessIntent(
            nr::renderer::AccelerationStructureAccessIntent::CopyWrite);
        nr::test::require(asCopyWrite.stages == vk::PipelineStageFlagBits2::eAccelerationStructureCopyKHR);
        nr::test::require(asCopyWrite.access == vk::AccessFlagBits2::eAccelerationStructureWriteKHR);
    }};

const nr::test::CaseRegistrar compilerCrossQueueCase{
    "render graph compiler emits explicit cross-queue ownership transition",
    [] {
        auto frame = buildCrossQueueFrame();
        nr::test::require(nr::renderer::RenderGraphCompiler::hasExplicitSubmitBoundariesForQueueTransitions(frame),
                          "frame should include explicit submit boundary");

        auto compiled = nr::renderer::RenderGraphCompiler{}.compile(frame);
        nr::test::requireEqual(compiled.resources.size(), std::size_t{2});
        nr::test::requireEqual(compiled.submitBatches.size(), std::size_t{2});
        nr::test::requireEqual(compiled.submitBatches[0].queue, nr::renderer::QueueDomain::Graphics);
        nr::test::requireEqual(compiled.submitBatches[1].queue, nr::renderer::QueueDomain::Compute);
        nr::test::require(compiled.submitBatches[1].openedBySubmitNode.has_value(), "compute batch should be opened by submit boundary");
        nr::test::requireEqual(compiled.submitBatches[1].openedBySubmitNodeDebugName, std::string{"GeometryToResolve"});

        auto const &computePass = compiled.submitBatches[1].passes.front();
        nr::test::requireEqual(computePass.preBarriers.size(), std::size_t{2});
        auto crossQueue = std::ranges::find_if(computePass.preBarriers, [](const nr::renderer::ResourceStateTransition &transition) {
            return transition.strength == nr::renderer::DependencyStrength::ReleaseAcquireRequired;
        });
        nr::test::require(crossQueue != computePass.preBarriers.end(), "compute pass should include a cross-queue transition");
        nr::test::requireEqual(crossQueue->srcQueue, nr::renderer::QueueDomain::Graphics);
        nr::test::requireEqual(crossQueue->dstQueue, nr::renderer::QueueDomain::Compute);
        nr::test::requireEqual(crossQueue->oldLayout, nr::renderer::ImageLayoutIntent::ColorAttachment);
        nr::test::requireEqual(crossQueue->newLayout, nr::renderer::ImageLayoutIntent::ShaderReadOnly);
        nr::test::requireEqual(compiled.ownershipTransitions.size(), std::size_t{1});
        nr::test::require(compiled.debugView.find("ownershipTransition") != std::string::npos,
                          "debug view should include ownership transition diagnostics");
        nr::test::require(compiled.debugView.find("GeometryToResolve") != std::string::npos,
                          "debug view should include submit boundary names");
    }};

const nr::test::CaseRegistrar compilerPassOrderCase{
    "render graph compiler preserves compiled pass order for executor merge",
    [] {
        auto frame = buildMultiPassGraphicsFrame();
        auto compiled = nr::renderer::RenderGraphCompiler{}.compile(frame);

        nr::test::requireEqual(compiled.submitBatches.size(), std::size_t{1});
        nr::test::requireEqual(compiled.submitBatches.front().passes.size(), std::size_t{3});
        nr::test::requireEqual(compiled.submitBatches.front().passes[0].debugName, std::string{"Graphics.First"});
        nr::test::requireEqual(compiled.submitBatches.front().passes[1].debugName, std::string{"Graphics.Second"});
        nr::test::requireEqual(compiled.submitBatches.front().passes[2].debugName, std::string{"Graphics.Third"});
    }};

const nr::test::CaseRegistrar compilerOrderedUseBarrierCase{
    "render graph compiler honors ordered previous-use barrier markers",
    [] {
        auto builder = nr::renderer::RenderGraphBuilder{};
        auto node = builder.addNode("Ordered", nr::renderer::QueueDomain::Graphics);
        auto color = builder.addResource(nr::renderer::GraphTransientImageDesc{
            .debugName = "Ordered.Color",
            .extent = vk::Extent3D{32, 32, 1},
            .format = vk::Format::eR8G8B8A8Unorm,
        });

        auto firstUses = std::array{nr::renderer::use::colorWrite(color)};
        auto secondUses = std::array{nr::renderer::use::orderedAfterPrevious(nr::renderer::use::colorWrite(color))};
        static_cast<void>(builder.addPass("Ordered.First", node, firstUses, [](const nr::renderer::PassRecordContext&) {}));
        static_cast<void>(builder.addPass("Ordered.Second", node, secondUses, [](const nr::renderer::PassRecordContext&) {}));

        auto compiled = nr::renderer::RenderGraphCompiler{}.compile(builder.build());
        nr::test::requireEqual(compiled.submitBatches.size(), std::size_t{1});
        nr::test::requireEqual(compiled.submitBatches.front().passes.size(), std::size_t{2});

        auto const& secondPass = compiled.submitBatches.front().passes[1];
        nr::test::requireEqual(secondPass.preBarriers.size(), std::size_t{1});
        auto const& barrier = secondPass.preBarriers.front();
        nr::test::requireEqual(barrier.strength, nr::renderer::DependencyStrength::BarrierRequired);
        nr::test::requireEqual(barrier.srcQueue, nr::renderer::QueueDomain::Graphics);
        nr::test::requireEqual(barrier.dstQueue, nr::renderer::QueueDomain::Graphics);
        nr::test::requireEqual(barrier.oldLayout, nr::renderer::ImageLayoutIntent::ColorAttachment);
        nr::test::requireEqual(barrier.newLayout, nr::renderer::ImageLayoutIntent::ColorAttachment);
    }};

const nr::test::CaseRegistrar compilerRetainedImageInitializedCase{
    "render graph compiler uses initialized retained image state for first-use barriers",
    [] {
        auto state = nr::renderer::RetainedImageState{
            .initialized = true,
            .layout = nr::renderer::ImageLayoutIntent::TransferSrc,
            .ownership = nr::renderer::ResourceOwnershipDomain::Compute,
            .access = nr::renderer::AccessScope{
                .stages = vk::PipelineStageFlagBits2::eTransfer,
                .access = vk::AccessFlagBits2::eTransferRead,
            },
        };
        auto frame = buildRetainedStorageWriteFrame(state, "initialized");
        auto compiled = nr::renderer::RenderGraphCompiler{}.compile(frame);

        nr::test::requireEqual(compiled.resources.size(), std::size_t{1});
        auto const& resource = compiled.resources.front();
        nr::test::requireEqual(resource.initialLayout, nr::renderer::ImageLayoutIntent::TransferSrc);
        nr::test::requireEqual(resource.initialOwnership, nr::renderer::ResourceOwnershipDomain::Compute);
        nr::test::require(resource.initialAccessScope.stages == vk::PipelineStageFlagBits2::eTransfer);
        nr::test::require(resource.initialAccessScope.access == vk::AccessFlagBits2::eTransferRead);
        nr::test::requireEqual(resource.finalLayout, nr::renderer::ImageLayoutIntent::General);
        nr::test::requireEqual(resource.finalOwnership, nr::renderer::ResourceOwnershipDomain::Compute);
        nr::test::require(resource.finalAccessScope.stages ==
                          (vk::PipelineStageFlagBits2::eComputeShader |
                           vk::PipelineStageFlagBits2::eRayTracingShaderKHR));
        nr::test::require(resource.finalAccessScope.access == vk::AccessFlagBits2::eShaderStorageWrite);
        nr::test::require(resource.retainedState.has_value(), "compiled retained image should keep state ref");
        nr::test::require(
            std::addressof(resource.retainedState->get()) == std::addressof(state),
            "compiled retained image should point at the current retained state");

        auto const& pass = compiled.submitBatches.front().passes.front();
        nr::test::requireEqual(pass.preBarriers.size(), std::size_t{1});
        auto const& barrier = pass.preBarriers.front();
        nr::test::requireEqual(barrier.oldLayout, nr::renderer::ImageLayoutIntent::TransferSrc);
        nr::test::requireEqual(barrier.newLayout, nr::renderer::ImageLayoutIntent::General);
        nr::test::require(barrier.srcScope.stages == vk::PipelineStageFlagBits2::eTransfer);
        nr::test::require(barrier.srcScope.access == vk::AccessFlagBits2::eTransferRead);
        nr::test::require(barrier.dstScope.stages ==
                          (vk::PipelineStageFlagBits2::eComputeShader |
                           vk::PipelineStageFlagBits2::eRayTracingShaderKHR));
        nr::test::require(barrier.dstScope.access == vk::AccessFlagBits2::eShaderStorageWrite);
    }};

const nr::test::CaseRegistrar compilerRetainedImageUninitializedCase{
    "render graph compiler treats uninitialized retained images as undefined",
    [] {
        auto state = nr::renderer::RetainedImageState{};
        auto builder = nr::renderer::RenderGraphBuilder{};
        auto node = builder.addNode("Retained.Uninitialized", nr::renderer::QueueDomain::Compute);
        auto retainedImage = builder.addResource(nr::renderer::GraphImportedImageDesc{
            .debugName = "Retained.Uninitialized.Image",
            .lifetime = nr::renderer::ResourceLifetime::RendererPersistent,
            .initialOwnership = nr::renderer::ResourceOwnershipDomain::Compute,
            .extent = vk::Extent3D{64, 64, 1},
            .format = vk::Format::eR16G16B16A16Sfloat,
            .usageIntents = {
                nr::renderer::ImageUsageIntent::StorageWrite,
                nr::renderer::ImageUsageIntent::TransferSrc,
            },
            .initialLayout = nr::renderer::ImageLayoutIntent::TransferSrc,
            .initialAccessScope = nr::renderer::AccessScope{
                .stages = vk::PipelineStageFlagBits2::eTransfer,
                .access = vk::AccessFlagBits2::eTransferRead,
            },
            .retainedState = std::ref(state),
        });
        auto uses = std::array{nr::renderer::use::storageWrite(retainedImage)};
        static_cast<void>(builder.addPass(
            "Retained.Uninitialized.Pass",
            node,
            uses,
            [](const nr::renderer::PassRecordContext&) {}));

        auto compiled = nr::renderer::RenderGraphCompiler{}.compile(builder.build());
        auto const& resource = compiled.resources.front();
        nr::test::requireEqual(resource.initialLayout, nr::renderer::ImageLayoutIntent::Undefined);
        nr::test::requireEqual(resource.initialOwnership, nr::renderer::ResourceOwnershipDomain::Undefined);
        nr::test::require(!resource.initialAccessScope.resolved(), "uninitialized retained source scope should stay empty");

        auto const& barrier = compiled.submitBatches.front().passes.front().preBarriers.front();
        nr::test::requireEqual(barrier.oldLayout, nr::renderer::ImageLayoutIntent::Undefined);
        nr::test::requireEqual(barrier.newLayout, nr::renderer::ImageLayoutIntent::General);
        nr::test::require(!barrier.srcScope.resolved(), "uninitialized retained first-use barrier should have empty source scope");
        nr::test::require(barrier.dstScope.access == vk::AccessFlagBits2::eShaderStorageWrite);
    }};

const nr::test::CaseRegistrar compilerAccelerationStructureCase{
    "render graph compiler tracks acceleration structure resources",
    [] {
        auto frame = buildAccelerationStructureFrame();
        auto compiled = nr::renderer::RenderGraphCompiler{}.compile(frame);

        nr::test::requireEqual(compiled.resources.size(), std::size_t{1});
        nr::test::require(compiled.resources.front().isAccelerationStructure, "TLAS should compile as an AS resource");
        nr::test::requireEqual(compiled.resources.front().resolvedAccelerationStructureSize, vk::DeviceSize{8192});
        nr::test::requireEqual(compiled.submitBatches.size(), std::size_t{2});

        auto const& tracePass = compiled.submitBatches[1].passes.front();
        nr::test::requireEqual(tracePass.preBarriers.size(), std::size_t{1});
        auto const& barrier = tracePass.preBarriers.front();
        nr::test::requireEqual(barrier.strength, nr::renderer::DependencyStrength::BarrierRequired);
        nr::test::require(barrier.srcScope.stages == vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR);
        nr::test::require(barrier.srcScope.access == vk::AccessFlagBits2::eAccelerationStructureWriteKHR);
        nr::test::require(barrier.dstScope.stages == vk::PipelineStageFlagBits2::eRayTracingShaderKHR);
        nr::test::require(barrier.dstScope.access == vk::AccessFlagBits2::eAccelerationStructureReadKHR);
        nr::test::require(
            compiled.debugView.find("type=AccelerationStructure") != std::string::npos,
            "debug view should identify AS resources");
    }};

const nr::test::CaseRegistrar compilerPrepareRecordSplitCase{
    "render graph compiler keeps prepare and record callbacks separate",
    [] {
        auto builder = nr::renderer::RenderGraphBuilder{};
        auto node = builder.addNode("Bindings", nr::renderer::QueueDomain::Graphics);
        auto color = builder.addResource(nr::renderer::GraphTransientImageDesc{
            .debugName = "Bindings.Color",
            .extent = vk::Extent3D{32, 32, 1},
            .format = vk::Format::eR8G8B8A8Unorm,
        });

        auto uses = std::array{nr::renderer::use::colorWrite(color)};
        auto pass = builder.addPass(
            "Bindings.Split",
            node,
            uses,
            [](const nr::renderer::PassRecordContext&) {},
            [](const nr::renderer::PassPrepareContext&) {});
        nr::test::require(pass.valid(), "split binding pass should be valid");

        auto frame = builder.build();
        nr::test::require(static_cast<bool>(frame.passes.front().prepare), "builder should retain prepare callback");
        nr::test::require(static_cast<bool>(frame.passes.front().record), "builder should retain record callback");

        auto compiled = nr::renderer::RenderGraphCompiler{}.compile(frame);
        auto const& compiledPass = compiled.submitBatches.front().passes.front();
        nr::test::require(static_cast<bool>(compiledPass.prepare), "compiler should retain prepare callback");
        nr::test::require(static_cast<bool>(compiledPass.record), "compiler should retain record callback");
        nr::test::require(!compiledPass.parallelRecord.has_value(), "serial pass should not retain a parallel record desc");
        nr::test::requireEqual(compiledPass.debugName, std::string{"Bindings.Split"});
    }};

const nr::test::CaseRegistrar compilerFrameDataCase{
    "render graph compiler carries typed frame data handles",
    [] {
        auto builder = nr::renderer::RenderGraphBuilder{};
        auto node = builder.addNode("FrameData", nr::renderer::QueueDomain::Graphics);
        auto frameData = builder.addFrameData("SceneBridgeFrame", std::uint32_t{42});
        auto color = builder.addResource(nr::renderer::GraphTransientImageDesc{
            .debugName = "FrameData.Color",
            .extent = vk::Extent3D{16, 16, 1},
            .format = vk::Format::eR8G8B8A8Unorm,
        });

        auto uses = std::array{nr::renderer::use::colorWrite(color)};
        static_cast<void>(builder.addPass(
            "FrameData.Pass",
            node,
            uses,
            [](const nr::renderer::PassRecordContext&) {}));

        auto compiled = nr::renderer::RenderGraphCompiler{}.compileConsuming(builder.mutableFrame());
        nr::test::requireEqual(compiled.frameData.size(), std::size_t{1});
        nr::test::requireEqual(compiled.frameData.front().handle, frameData);

        auto recordContext = nr::renderer::PassRecordContext{
            .resolveFrameDataPayload = [&](nr::renderer::GraphFrameDataHandle handle)
                -> std::optional<std::reference_wrapper<const std::any>> {
                auto frameDataIt = std::ranges::find_if(
                    compiled.frameData,
                    [handle](const nr::renderer::GraphFrameDataDesc& desc) {
                        return desc.handle == handle;
                    });
                if (frameDataIt == compiled.frameData.end())
                {
                    return {};
                }

                return std::cref(frameDataIt->payload);
            },
        };

        nr::test::requireEqual(recordContext.frameData<std::uint32_t>(frameData), std::uint32_t{42});
    }};

const nr::test::CaseRegistrar compileCachePatchesCurrentFramePayloadCase{
    "render graph compile cache hit patches current frame data callbacks and imports",
    [] {
        auto cache = nr::renderer::RenderGraphCompileCache{};
        auto previousBuffer = nr::rhi::Buffer{};
        auto currentBuffer = nr::rhi::Buffer{};
        auto previousRecordedValue = std::uint32_t{0};
        auto currentRecordedValue = std::uint32_t{0};

        auto previousFrame = buildCompileCachePatchFrame(111u, previousBuffer, "previous", previousRecordedValue);
        static_cast<void>(cache.compileConsumingCached(previousFrame));
        auto previousStats = cache.statistics();
        nr::test::requireEqual(previousStats.hitCount, std::uint64_t{0});
        nr::test::requireEqual(previousStats.missCount, std::uint64_t{1});

        auto currentFrame = buildCompileCachePatchFrame(222u, currentBuffer, "current", currentRecordedValue);
        auto compiled = cache.compileConsumingCached(currentFrame);
        auto currentStats = cache.statistics();
        nr::test::requireEqual(currentStats.hitCount, std::uint64_t{1});
        nr::test::requireEqual(currentStats.missCount, std::uint64_t{1});

        auto importedIt = std::ranges::find_if(compiled.resources, [](const nr::renderer::CompiledResourceDesc& resource) {
            return resource.isBuffer;
        });
        nr::test::require(importedIt != compiled.resources.end(), "compiled cache hit should retain imported buffer resource");
        nr::test::require(importedIt->importedBufferResource.has_value(), "compiled imported buffer ref should be patched");
        nr::test::require(
            std::addressof(importedIt->importedBufferResource->get()) == std::addressof(currentBuffer),
            "compiled cache hit should use current imported buffer ref");

        auto const& compiledPass = compiled.submitBatches.front().passes.front();
        nr::test::require(static_cast<bool>(compiledPass.record), "compiled cache hit should patch current record callback");
        compiledPass.record(nr::renderer::PassRecordContext{
            .resolveFrameDataPayload = [&](nr::renderer::GraphFrameDataHandle handle) {
                return findFrameDataPayload(compiled, handle);
            },
        });
        nr::test::requireEqual(currentRecordedValue, std::uint32_t{222});
        nr::test::requireEqual(previousRecordedValue, std::uint32_t{0}, "cached callback from previous frame must not run");
    }};

const nr::test::CaseRegistrar compileCachePatchesRetainedImageStateCase{
    "render graph compile cache keys retained image access and patches current state refs",
    [] {
        auto cache = nr::renderer::RenderGraphCompileCache{};
        auto previousState = nr::renderer::RetainedImageState{
            .initialized = true,
            .layout = nr::renderer::ImageLayoutIntent::TransferSrc,
            .ownership = nr::renderer::ResourceOwnershipDomain::Compute,
            .access = nr::renderer::AccessScope{
                .stages = vk::PipelineStageFlagBits2::eTransfer,
                .access = vk::AccessFlagBits2::eTransferRead,
            },
        };
        auto currentState = previousState;
        auto changedAccessState = previousState;
        changedAccessState.access = nr::renderer::AccessScope{
            .stages = vk::PipelineStageFlagBits2::eTransfer,
            .access = vk::AccessFlagBits2::eMemoryRead,
        };

        auto previousFrame = buildRetainedStorageWriteFrame(previousState, "previous");
        static_cast<void>(cache.compileConsumingCached(previousFrame));
        auto previousStats = cache.statistics();
        nr::test::requireEqual(previousStats.hitCount, std::uint64_t{0});
        nr::test::requireEqual(previousStats.missCount, std::uint64_t{1});

        auto currentFrame = buildRetainedStorageWriteFrame(currentState, "current");
        auto compiled = cache.compileConsumingCached(currentFrame);
        auto currentStats = cache.statistics();
        nr::test::requireEqual(currentStats.hitCount, std::uint64_t{1});
        nr::test::requireEqual(currentStats.missCount, std::uint64_t{1});
        nr::test::require(compiled.resources.front().retainedState.has_value(), "cache hit should keep retained state ref");
        nr::test::require(
            std::addressof(compiled.resources.front().retainedState->get()) == std::addressof(currentState),
            "cache hit should patch retained state to the current frame object");

        auto changedFrame = buildRetainedStorageWriteFrame(changedAccessState, "changedAccess");
        static_cast<void>(cache.compileConsumingCached(changedFrame));
        auto changedStats = cache.statistics();
        nr::test::requireEqual(changedStats.hitCount, std::uint64_t{1});
        nr::test::requireEqual(changedStats.missCount, std::uint64_t{2});
    }};

const nr::test::CaseRegistrar compileCacheStructuralMissCase{
    "render graph compile cache misses structural graph changes",
    [] {
        requireCompileCacheMissForStructuralChange(
            "extent change",
            [] { return buildSingleImageFrame(vk::Extent3D{64, 64, 1}, vk::Format::eR8G8B8A8Unorm); },
            [] { return buildSingleImageFrame(vk::Extent3D{128, 64, 1}, vk::Format::eR8G8B8A8Unorm); });
        requireCompileCacheMissForStructuralChange(
            "format change",
            [] { return buildSingleImageFrame(vk::Extent3D{64, 64, 1}, vk::Format::eR8G8B8A8Unorm); },
            [] { return buildSingleImageFrame(vk::Extent3D{64, 64, 1}, vk::Format::eR16G16B16A16Sfloat); });
        requireCompileCacheMissForStructuralChange(
            "resource use change",
            [] { return buildResourceUseFrame(false); },
            [] { return buildResourceUseFrame(true); });
        requireCompileCacheMissForStructuralChange(
            "pass order change",
            [] { return buildPassOrderFrame(false); },
            [] { return buildPassOrderFrame(true); });
        requireCompileCacheMissForStructuralChange(
            "submit order change",
            [] { return buildSubmitOrderFrame(false); },
            [] { return buildSubmitOrderFrame(true); });
        requireCompileCacheMissForStructuralChange(
            "acceleration structure size change",
            [] { return buildAccelerationStructureFrameWithSize(8192); },
            [] { return buildAccelerationStructureFrameWithSize(16384); });
    }};

const nr::test::CaseRegistrar compileCacheDebugNameSwapchainIndexHitCase{
    "render graph compile cache ignores debug names and swapchain image index",
    [] {
        auto cache = nr::renderer::RenderGraphCompileCache{};
        auto first = buildSwapchainFrame(0u, "first");
        static_cast<void>(cache.compileConsumingCached(first));
        auto second = buildSwapchainFrame(2u, "second");
        auto compiled = cache.compileConsumingCached(second);

        auto stats = cache.statistics();
        nr::test::requireEqual(stats.hitCount, std::uint64_t{1});
        nr::test::requireEqual(stats.missCount, std::uint64_t{1});
        nr::test::requireEqual(compiled.resources.front().debugName, std::string{"Swapchain.Image.second"});
        nr::test::requireEqual(compiled.submitBatches.front().passes.front().debugName, std::string{"Swapchain.Pass.second"});
    }};

const nr::test::CaseRegistrar bindlessImageTableCacheCase{
    "renderer bindless image table cache tracks versions fallback removals and reallocations",
    [] {
        auto pipeline = makeFakeUiBindlessPipeline(8u);
        auto cache = nr::renderer::BindlessImageTableCache{};

        auto firstSnapshot = cache.makeSnapshotForFrame(
            pipeline,
            0u,
            makeBindlessCacheRequest(
                1u,
                4u,
                {
                    {1u, logicalTextureDescriptor(101u, "texture-1")},
                }));
        nr::test::requireEqual(firstSnapshot.descriptorWriteCount(), std::size_t{4});
        nr::test::requireEqual(logicalResourceIdForArrayElement(firstSnapshot, 0u), std::uint64_t{100});
        nr::test::requireEqual(logicalResourceIdForArrayElement(firstSnapshot, 1u), std::uint64_t{101});

        auto cachedSnapshot = cache.makeSnapshotForFrame(
            pipeline,
            0u,
            makeBindlessCacheRequest(
                1u,
                4u,
                {
                    {1u, logicalTextureDescriptor(101u, "texture-1")},
                }));
        nr::test::require(cachedSnapshot.empty(), "same bindless table version should not emit a snapshot");

        auto updatedSnapshot = cache.makeSnapshotForFrame(
            pipeline,
            0u,
            makeBindlessCacheRequest(
                2u,
                4u,
                {
                    {1u, logicalTextureDescriptor(111u, "texture-1-updated")},
                    {2u, logicalTextureDescriptor(102u, "texture-2")},
                }));
        nr::test::requireEqual(updatedSnapshot.descriptorWriteCount(), std::size_t{2});
        nr::test::requireEqual(logicalResourceIdForArrayElement(updatedSnapshot, 1u), std::uint64_t{111});
        nr::test::requireEqual(logicalResourceIdForArrayElement(updatedSnapshot, 2u), std::uint64_t{102});

        auto removedSnapshot = cache.makeSnapshotForFrame(
            pipeline,
            0u,
            makeBindlessCacheRequest(
                3u,
                4u,
                {
                    {1u, logicalTextureDescriptor(111u, "texture-1-updated")},
                }));
        nr::test::requireEqual(removedSnapshot.descriptorWriteCount(), std::size_t{2});
        nr::test::requireEqual(logicalResourceIdForArrayElement(removedSnapshot, 1u), std::uint64_t{111});
        nr::test::requireEqual(logicalResourceIdForArrayElement(removedSnapshot, 2u), std::uint64_t{100});

        pipeline.forceReallocation = true;
        auto reallocatedSnapshot = cache.makeSnapshotForFrame(
            pipeline,
            0u,
            makeBindlessCacheRequest(
                3u,
                6u,
                {
                    {1u, logicalTextureDescriptor(111u, "texture-1-updated")},
                }));
        nr::test::requireEqual(reallocatedSnapshot.descriptorWriteCount(), std::size_t{6});
        nr::test::requireEqual(logicalResourceIdForArrayElement(reallocatedSnapshot, 5u), std::uint64_t{100});
    }};

const nr::test::CaseRegistrar bindlessImageTableOptionalMissingSymbolCase{
    "renderer bindless image table cache accepts optional missing shader symbols",
    [] {
        auto pipeline = makeFakeUiBindlessPipeline(4u);
        auto cache = nr::renderer::BindlessImageTableCache{};
        auto request = makeBindlessCacheRequest(
            1u,
            4u,
            {
                {1u, logicalTextureDescriptor(101u, "texture-1")},
            });
        request.tableKey = "test.missing";
        request.shaderSymbol = "gMissingTextures";
        request.requirement = nr::renderer::BindlessImageTableRequirement::optional;

        auto snapshot = cache.makeSnapshotForFrame(pipeline, 0u, request);
        nr::test::require(snapshot.empty(), "optional missing bindless table should produce no descriptor writes");
    }};
} // namespace
