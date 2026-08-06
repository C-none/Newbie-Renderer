import std;
import dependency.vulkan;
import nr.rhi;
import nr.renderer;
import nr.test;
import nr.utils;

namespace
{
template <typename VkHandle> [[nodiscard]] VkHandle fakeVkHandle(std::uintptr_t value) noexcept
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

[[nodiscard]] const nr::renderer::ResourceStateTransition &requireBarrierFor(const nr::renderer::CompiledPass &pass,
                                                                             nr::renderer::GraphResourceHandle resource,
                                                                             std::string_view context)
{
    auto barrier = std::ranges::find_if(pass.preBarriers, [&](const nr::renderer::ResourceStateTransition &candidate) {
        return candidate.resource == resource;
    });
    nr::test::require(barrier != pass.preBarriers.end(), context);
    return *barrier;
}

[[nodiscard]] bool hasBarrierFor(const nr::renderer::CompiledPass &pass,
                                 nr::renderer::GraphResourceHandle resource) noexcept
{
    return std::ranges::any_of(pass.preBarriers, [&](const nr::renderer::ResourceStateTransition &candidate) {
        return candidate.resource == resource;
    });
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
    auto graphicsPass =
        builder.addPass("Geometry.Color", graphicsNode, graphicsUses, [](const nr::renderer::PassRecordContext &) {});

    auto submit = builder.addSubmitNode("GeometryToResolve");

    auto computeUses = std::array{
        nr::renderer::use::sampledRead(color),
        nr::renderer::use::storageWrite(output),
    };
    auto computePass = builder.addPass(
        "Resolve.Compose", computeNode, computeUses, [](const nr::renderer::PassRecordContext &) {}, nullptr, false,
        vk::PipelineStageFlagBits2::eComputeShader);

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

    static_cast<void>(
        builder.addPass("Graphics.First", node, firstUses, [](const nr::renderer::PassRecordContext &) {}));
    static_cast<void>(
        builder.addPass("Graphics.Second", node, secondUses, [](const nr::renderer::PassRecordContext &) {}));
    static_cast<void>(
        builder.addPass("Graphics.Third", node, thirdUses, [](const nr::renderer::PassRecordContext &) {}));

    return builder.build();
}

[[nodiscard]] nr::renderer::RenderGraphFrameDescription buildAccelerationStructureFrame()
{
    auto builder = nr::renderer::RenderGraphBuilder{};
    auto buildNode = builder.addNode("AccelerationStructureBuild", nr::renderer::QueueDomain::Compute);
    auto traceNode = builder.addNode("PathTracing", nr::renderer::QueueDomain::Graphics);

    auto tlas = builder.addResource(nr::renderer::GraphImportedAccelerationStructureDesc{
        .debugName = "Scene.TLAS",
        .type = vk::AccelerationStructureTypeKHR::eTopLevel,
        .size = 8192,
    });

    auto buildUses = std::array{nr::renderer::use::accelerationStructureBuildWrite(tlas)};
    static_cast<void>(builder.addPass("AccelerationStructureBuild.Tlas", buildNode, buildUses,
                                      [](const nr::renderer::PassRecordContext &) {}));

    static_cast<void>(builder.addSubmitNode("rtobject.ComputeToGraphics"));

    auto traceUses = std::array{nr::renderer::use::accelerationStructureTraceRead(tlas)};
    static_cast<void>(
        builder.addPass("PathTracing.Trace", traceNode, traceUses, [](const nr::renderer::PassRecordContext &) {}));

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
    static_cast<void>(builder.addPass("RayTracing.Trace", node, uses, [](const nr::renderer::PassRecordContext &) {}));
    return builder.build();
}

[[nodiscard]] nr::renderer::RenderGraphFrameDescription buildCompileCachePatchFrame(
    std::uint32_t frameDataValue, const nr::rhi::Buffer &importedBuffer, std::string_view debugSuffix,
    std::uint32_t &recordedValue)
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
    auto frameDataUses = std::array{frameData, frameData};
    static_cast<void>(
        builder.addPass(std::format("Cache.Pass.{}", debugSuffix), node, uses,
                        [frameData, &recordedValue](const nr::renderer::PassRecordContext &recordContext) {
                            recordedValue = recordContext.frameData<std::uint32_t>(frameData);
                        },
                        nullptr, false, vk::PipelineStageFlags2{}, frameDataUses));
    return builder.build();
}

[[nodiscard]] std::optional<std::reference_wrapper<const std::any>> findFrameDataPayload(
    const nr::renderer::CompiledGraphFrame &compiled, nr::renderer::GraphFrameDataHandle handle)
{
    auto frameDataIt = std::ranges::find_if(
        compiled.frameData, [handle](const nr::renderer::GraphFrameDataDesc &desc) { return desc.handle == handle; });
    if (frameDataIt == compiled.frameData.end())
    {
        return {};
    }
    return std::cref(frameDataIt->payload);
}

[[nodiscard]] nr::renderer::RenderGraphFrameDescription buildSingleImageFrame(vk::Extent3D extent, vk::Format format)
{
    auto builder = nr::renderer::RenderGraphBuilder{};
    auto node = builder.addNode("SingleImage", nr::renderer::QueueDomain::Graphics);
    auto color = builder.addResource(nr::renderer::GraphTransientImageDesc{
        .debugName = "SingleImage.Color",
        .extent = extent,
        .format = format,
    });
    auto uses = std::array{nr::renderer::use::colorWrite(color)};
    static_cast<void>(builder.addPass("SingleImage.Pass", node, uses, [](const nr::renderer::PassRecordContext &) {}));
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
    static_cast<void>(builder.addPass("ResourceUse.Pass", node, uses, [](const nr::renderer::PassRecordContext &) {}));
    return builder.build();
}

[[nodiscard]] nr::renderer::RenderGraphFrameDescription buildSampledUseSignatureFrame(
    nr::renderer::ImageAspectIntent aspect, nr::renderer::ShaderStageIntent shaderStage)
{
    auto builder = nr::renderer::RenderGraphBuilder{};
    auto node = builder.addNode("ResourceUse.Sampled", nr::renderer::QueueDomain::Graphics);
    auto image = builder.addResource(nr::renderer::GraphTransientImageDesc{
        .debugName = "ResourceUse.Sampled.Image",
        .extent = vk::Extent3D{32, 32, 1},
        .format = vk::Format::eD24UnormS8Uint,
        .usageIntents = {nr::renderer::ImageUsageIntent::Sampled},
        .aspect = nr::renderer::ImageAspectIntent::DepthStencil,
    });
    auto uses = std::array{
        nr::renderer::use::withShaderStages(nr::renderer::use::sampledRead(image, aspect), shaderStage),
    };
    static_cast<void>(builder.addPass("ResourceUse.Sampled.Pass", node, uses,
                                      [](const nr::renderer::PassRecordContext &) {}));
    return builder.build();
}

[[nodiscard]] nr::renderer::RenderGraphFrameDescription buildFrameDataUseFrame(bool useSecond)
{
    auto builder = nr::renderer::RenderGraphBuilder{};
    auto node = builder.addNode("FrameDataUse", nr::renderer::QueueDomain::Graphics);
    auto first = builder.addFrameData("FrameDataUse.First", std::uint32_t{1});
    auto second = builder.addFrameData("FrameDataUse.Second", std::uint32_t{2});
    auto color = builder.addResource(nr::renderer::GraphTransientImageDesc{
        .debugName = "FrameDataUse.Color",
        .extent = vk::Extent3D{32, 32, 1},
        .format = vk::Format::eR8G8B8A8Unorm,
    });
    auto uses = std::array{nr::renderer::use::colorWrite(color)};
    auto frameDataUses = std::array{useSecond ? second : first};
    static_cast<void>(builder.addPass("FrameDataUse.Pass", node, uses,
                                      [](const nr::renderer::PassRecordContext &) {}, nullptr, false,
                                      vk::PipelineStageFlags2{}, frameDataUses));
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
        static_cast<void>(
            builder.addPass("PassOrder.FirstPass", node, uses, [](const nr::renderer::PassRecordContext &) {}));
    };
    auto addSecond = [&] {
        auto uses = std::array{nr::renderer::use::colorWrite(second)};
        static_cast<void>(
            builder.addPass("PassOrder.SecondPass", node, uses, [](const nr::renderer::PassRecordContext &) {}));
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
    static_cast<void>(
        builder.addPass("SubmitOrder.FirstPass", node, firstUses, [](const nr::renderer::PassRecordContext &) {}));
    if (!submitAfterBothPasses)
    {
        static_cast<void>(builder.addSubmitNode("SubmitOrder.Boundary"));
    }
    static_cast<void>(
        builder.addPass("SubmitOrder.SecondPass", node, secondUses, [](const nr::renderer::PassRecordContext &) {}));
    if (submitAfterBothPasses)
    {
        static_cast<void>(builder.addSubmitNode("SubmitOrder.Boundary"));
    }
    return builder.build();
}

[[nodiscard]] nr::renderer::RenderGraphFrameDescription buildSwapchainFrame(std::string_view debugSuffix,
                                                                             bool explicitOwnership = true)
{
    auto builder = nr::renderer::RenderGraphBuilder{};
    auto node = builder.addNode(std::format("Swapchain.Node.{}", debugSuffix), nr::renderer::QueueDomain::Compute);
    auto swapchainImage = builder.addResource(nr::renderer::GraphImportedSwapchainImageDesc{
        .debugName = std::format("Swapchain.Image.{}", debugSuffix),
        .initialOwnership = nr::renderer::ResourceOwnershipDomain::Compute,
        .extent = vk::Extent3D{128, 72, 1},
        .format = vk::Format::eB8G8R8A8Unorm,
    });
    auto presentUse = explicitOwnership
                          ? nr::renderer::use::presentRead(swapchainImage,
                                                          nr::renderer::ResourceOwnershipDomain::Compute)
                          : nr::renderer::use::presentRead(swapchainImage);
    auto uses = std::array{presentUse};
    static_cast<void>(builder.addPass(std::format("Swapchain.Pass.{}", debugSuffix), node, uses,
                                      [](const nr::renderer::PassRecordContext &) {}));
    return builder.build();
}

[[nodiscard]] nr::renderer::RenderGraphFrameDescription buildRetainedStorageWriteFrame(
    nr::renderer::RetainedImageState &state, std::string_view debugSuffix)
{
    auto builder = nr::renderer::RenderGraphBuilder{};
    auto node = builder.addNode(std::format("Retained.Node.{}", debugSuffix), nr::renderer::QueueDomain::Compute);
    auto retainedImage = builder.addResource(nr::renderer::GraphImportedImageDesc{
        .debugName = std::format("Retained.Image.{}", debugSuffix),
        .lifetime = nr::renderer::ResourceLifetime::RendererPersistent,
        .initialOwnership =
            state.common.initialized ? state.common.ownership : nr::renderer::ResourceOwnershipDomain::Undefined,
        .extent = vk::Extent3D{128, 72, 1},
        .format = vk::Format::eR16G16B16A16Sfloat,
        .usageIntents =
            {
                nr::renderer::ImageUsageIntent::StorageWrite,
                nr::renderer::ImageUsageIntent::TransferSrc,
            },
        .initialLayout = state.common.initialized ? state.layout : nr::renderer::ImageLayoutIntent::Undefined,
        .initialAccessScope = state.common.initialized ? state.common.access : nr::renderer::AccessScope{},
        .retainedState = std::ref(state),
    });
    auto uses = std::array{nr::renderer::use::storageWrite(retainedImage)};
    static_cast<void>(builder.addPass(std::format("Retained.Pass.{}", debugSuffix), node, uses,
                                      [](const nr::renderer::PassRecordContext &) {}));
    return builder.build();
}

template <typename TBaseFrameBuilder, typename TVariantFrameBuilder>
void requireCompileCacheMissForStructuralChange(std::string_view label, TBaseFrameBuilder baseFrameBuilder,
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

static_assert(std::same_as<nr::renderer::RenderGraphCompileCache::ResourceUseSignature,
                           nr::renderer::PassResourceUseDesc>);

struct FakeBindlessPipeline
{
    struct PassBindingHandle
    {
        std::size_t stateIndex = std::numeric_limits<std::size_t>::max();

        [[nodiscard]] bool valid() const noexcept
        {
            return stateIndex != std::numeric_limits<std::size_t>::max();
        }

        [[nodiscard]] auto operator<=>(const PassBindingHandle &) const = default;
    };

    nr::rhi::SlangProgram program{};
    nr::rhi::ShaderDescriptorLayout descriptorLayout{};
    std::map<std::size_t, std::map<std::size_t, std::map<std::uint32_t, std::uint32_t>>> variableCountsByOwner{};
    std::map<std::size_t, std::map<std::size_t, bool>> bindingSetsInitializedByOwner{};
    std::size_t frameSlotCount = nr::maxFrameInFlight;
    bool forceReallocation = false;
    std::size_t reallocationCount = 0;

    [[nodiscard]] PassBindingHandle passBinding(std::size_t nodeLocalPassOrdinal) const noexcept
    {
        return PassBindingHandle{.stateIndex = nodeLocalPassOrdinal};
    }

    [[nodiscard]] nr::renderer::PipelinePassBindingCacheKey passBindingCacheKey(PassBindingHandle handle,
                                                                                 std::uint32_t frameIndex) const
    {
        nr::test::require(handle.valid(), "fake bindless pipeline requires a valid pass-binding handle");
        nr::test::require(frameSlotCount > 0u, "fake bindless pipeline requires at least one frame slot");
        return nr::renderer::PipelinePassBindingCacheKey{
            .runtimeIdentity = 1u,
            .generation = 1u,
            .stateIndex = handle.stateIndex,
            .frameSlot = static_cast<std::size_t>(frameIndex % frameSlotCount),
        };
    }

    [[nodiscard]] nr::rhi::ShaderCursor rootCursor() const
    {
        return descriptorLayout.rootCursor();
    }

    [[nodiscard]] bool ensureBindingSetsForFrame(
        PassBindingHandle handle, std::uint32_t frameIndex,
        const std::map<std::uint32_t, std::uint32_t> &variableDescriptorCountsBySet)
    {
        nr::test::require(handle.valid(), "fake bindless pipeline requires a valid pass-binding handle");
        auto &variableCountsByFrame = variableCountsByOwner[handle.stateIndex];
        auto &bindingSetsInitializedByFrame = bindingSetsInitializedByOwner[handle.stateIndex];
        nr::test::require(frameSlotCount > 0u, "fake bindless pipeline requires at least one frame slot");
        auto const frameSlot = static_cast<std::size_t>(frameIndex % frameSlotCount);
        auto mergedVariableCounts = variableCountsByFrame[frameSlot];
        std::ranges::for_each(variableDescriptorCountsBySet,
                              [&](const auto &entry) { mergedVariableCounts.insert_or_assign(entry.first, entry.second); });
        auto const reallocated = forceReallocation || !bindingSetsInitializedByFrame[frameSlot] ||
                                 variableCountsByFrame[frameSlot] != mergedVariableCounts;
        variableCountsByFrame[frameSlot] = std::move(mergedVariableCounts);
        bindingSetsInitializedByFrame[frameSlot] = true;
        forceReallocation = false;
        reallocationCount += reallocated ? 1u : 0u;
        return reallocated;
    }
};

[[nodiscard]] FakeBindlessPipeline makeFakeUiBindlessPipeline(std::uint32_t runtimeDescriptorCount)
{
    auto &shaderService = nr::rhi::ShaderService::instance();
    shaderService.configure();
    auto pipeline = FakeBindlessPipeline{};
    pipeline.program = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
        .sourcePath = std::filesystem::path{"renderer/appUi/fragment"},
    });
    nr::test::require(pipeline.program.valid(),
                      "appUi shader program should compile for bindless cache contract tests");

    pipeline.descriptorLayout = nr::rhi::ShaderDescriptorLayout::create(
        pipeline.program, nr::rhi::DescriptorBindingPolicy{
                              .defaultRuntimeDescriptorCount = runtimeDescriptorCount,
                          });
    nr::test::require(pipeline.descriptorLayout.valid(),
                      "appUi descriptor layout should be valid for bindless cache tests");
    return pipeline;
}

[[nodiscard]] FakeBindlessPipeline makeFakeTwoTableBindlessPipeline(std::uint32_t runtimeDescriptorCount)
{
    auto &shaderService = nr::rhi::ShaderService::instance();
    shaderService.configure();
    auto pipeline = FakeBindlessPipeline{};
    pipeline.program = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
        .sourcePath = std::filesystem::path{"test/renderer/bindlessTwoTableContract"},
    });
    nr::test::require(pipeline.program.valid(),
                      "two-table shader should compile for bindless cache contract tests");

    pipeline.descriptorLayout = nr::rhi::ShaderDescriptorLayout::create(
        pipeline.program, nr::rhi::DescriptorBindingPolicy{
                              .defaultRuntimeDescriptorCount = runtimeDescriptorCount,
                          });
    nr::test::require(pipeline.descriptorLayout.valid(),
                      "two-table descriptor layout should be valid for bindless cache tests");
    return pipeline;
}

[[nodiscard]] nr::renderer::BindlessImageDescriptor logicalTextureDescriptor(std::uint64_t logicalResourceId,
                                                                             std::string debugName)
{
    return nr::renderer::BindlessImageDescriptor{
        .layout = vk::ImageLayout::eShaderReadOnlyOptimal,
        .logicalResourceId = logicalResourceId,
        .debugName = std::move(debugName),
    };
}

[[nodiscard]] nr::renderer::BindlessImageTableRequest makeBindlessCacheRequest(
    std::uint64_t tableVersion, std::uint32_t descriptorCapacity,
    std::map<std::uint32_t, nr::renderer::BindlessImageDescriptor> descriptorsById,
    std::optional<nr::renderer::BindlessImageDescriptor> fallbackDescriptor = logicalTextureDescriptor(100u,
                                                                                                       "fallback"))
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

[[nodiscard]] std::uint64_t logicalResourceIdForArrayElement(const nr::rhi::ShaderBindingSnapshot &snapshot,
                                                             std::uint32_t arrayElement)
{
    auto writeIt =
        std::ranges::find_if(snapshot.descriptorWrites(), [arrayElement](const nr::rhi::ShaderBindingRecord &record) {
            return record.arrayElement == arrayElement;
        });
    nr::test::require(writeIt != snapshot.descriptorWrites().end(),
                      "expected descriptor write for requested array element");
    nr::test::require(std::holds_alternative<nr::rhi::LogicalResourceDescriptorWrite>(writeIt->payload),
                      "bindless cache test descriptors should use logical payloads");
    return std::get<nr::rhi::LogicalResourceDescriptorWrite>(writeIt->payload).logicalResourceId;
}

[[nodiscard]] bool forceWriteForArrayElement(const nr::rhi::ShaderBindingSnapshot &snapshot, std::uint32_t arrayElement)
{
    auto writeIt =
        std::ranges::find_if(snapshot.descriptorWrites(), [arrayElement](const nr::rhi::ShaderBindingRecord &record) {
            return record.arrayElement == arrayElement;
        });
    nr::test::require(writeIt != snapshot.descriptorWrites().end(),
                      "expected descriptor write for requested array element");
    return writeIt->forceWrite;
}

const nr::test::CaseRegistrar compilerMappingCase{
    "render graph compiler maps usage and access intents", [] {
        nr::test::require(nr::renderer::RenderGraphCompiler::mapBufferUsageIntent(
                              nr::renderer::BufferUsageIntent::ShaderBindingTable) ==
                          vk::BufferUsageFlagBits::eShaderBindingTableKHR);
        nr::test::require(nr::renderer::RenderGraphCompiler::mapBufferUsageIntent(
                              nr::renderer::BufferUsageIntent::AccelerationStructureStorage) ==
                          vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR);
        nr::test::require(nr::renderer::RenderGraphCompiler::mapImageUsageIntent(
                              nr::renderer::ImageUsageIntent::PresentSource) == vk::ImageUsageFlagBits::eTransferDst);
        nr::test::require(nr::renderer::RenderGraphCompiler::mapImageLayoutIntent(
                              nr::renderer::ImageLayoutIntent::PresentSrc) == vk::ImageLayout::ePresentSrcKHR);
        nr::test::require(
            nr::renderer::RenderGraphCompiler::mapImageAspectIntent(nr::renderer::ImageAspectIntent::DepthStencil) ==
            (vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil));

        auto graphicsUniform = nr::renderer::RenderGraphCompiler::mapBufferAccessIntent(
            nr::renderer::BufferAccessIntent::UniformRead, nr::renderer::QueueDomain::Graphics);
        nr::test::require(graphicsUniform.stages == vk::PipelineStageFlagBits2::eAllGraphics);
        nr::test::require(graphicsUniform.access == vk::AccessFlagBits2::eUniformRead);

        auto computeSample = nr::renderer::RenderGraphCompiler::mapImageAccessIntent(
            nr::renderer::ImageAccessIntent::SampledRead, vk::PipelineStageFlagBits2::eComputeShader);
        nr::test::require(computeSample.stages == vk::PipelineStageFlagBits2::eComputeShader);
        nr::test::require(computeSample.access == vk::AccessFlagBits2::eShaderSampledRead);

        auto rayTracingUniform = nr::renderer::RenderGraphCompiler::mapBufferAccessIntent(
            nr::renderer::BufferAccessIntent::UniformRead, vk::PipelineStageFlagBits2::eRayTracingShaderKHR);
        nr::test::require(rayTracingUniform.stages == vk::PipelineStageFlagBits2::eRayTracingShaderKHR);
        nr::test::require(rayTracingUniform.access == vk::AccessFlagBits2::eUniformRead);

        auto colorReadWrite = nr::renderer::RenderGraphCompiler::mapImageAccessIntent(
            nr::renderer::ImageAccessIntent::ColorAttachmentReadWrite, nr::renderer::QueueDomain::Graphics);
        nr::test::require(colorReadWrite.stages == vk::PipelineStageFlagBits2::eColorAttachmentOutput);
        nr::test::require(colorReadWrite.access ==
                          (vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite));

        auto depthReadWrite = nr::renderer::RenderGraphCompiler::mapImageAccessIntent(
            nr::renderer::ImageAccessIntent::DepthStencilReadWrite, nr::renderer::QueueDomain::Graphics);
        nr::test::require(depthReadWrite.stages == (vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                                                    vk::PipelineStageFlagBits2::eLateFragmentTests));
        nr::test::require(depthReadWrite.access == (vk::AccessFlagBits2::eDepthStencilAttachmentRead |
                                                    vk::AccessFlagBits2::eDepthStencilAttachmentWrite));

        auto storageBufferReadWrite = nr::renderer::RenderGraphCompiler::mapBufferAccessIntent(
            nr::renderer::BufferAccessIntent::ShaderStorageReadWrite, vk::PipelineStageFlagBits2::eComputeShader);
        nr::test::require(storageBufferReadWrite.stages == vk::PipelineStageFlagBits2::eComputeShader);
        nr::test::require(storageBufferReadWrite.access ==
                          (vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite));

        auto accelerationStructureRead = nr::renderer::RenderGraphCompiler::mapBufferAccessIntent(
            nr::renderer::BufferAccessIntent::AccelerationStructureRead, nr::renderer::QueueDomain::Compute);
        nr::test::require(accelerationStructureRead.stages ==
                          (vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR |
                           vk::PipelineStageFlagBits2::eRayTracingShaderKHR));
        nr::test::require(accelerationStructureRead.access == vk::AccessFlagBits2::eAccelerationStructureReadKHR);

        auto accelerationStructureBuildInputRead = nr::renderer::RenderGraphCompiler::mapBufferAccessIntent(
            nr::renderer::BufferAccessIntent::AccelerationStructureBuildInputRead, nr::renderer::QueueDomain::Graphics);
        nr::test::require(accelerationStructureBuildInputRead.stages ==
                          vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR);
        nr::test::require(accelerationStructureBuildInputRead.access == vk::AccessFlagBits2::eShaderRead);

        auto accelerationStructureScratchReadWrite = nr::renderer::RenderGraphCompiler::mapBufferAccessIntent(
            nr::renderer::BufferAccessIntent::AccelerationStructureScratchReadWrite,
            nr::renderer::QueueDomain::Graphics);
        nr::test::require(accelerationStructureScratchReadWrite.stages ==
                          vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR);
        nr::test::require(
            accelerationStructureScratchReadWrite.access ==
            (vk::AccessFlagBits2::eAccelerationStructureReadKHR | vk::AccessFlagBits2::eAccelerationStructureWriteKHR));

        auto shaderBindingTableRead = nr::renderer::RenderGraphCompiler::mapBufferAccessIntent(
            nr::renderer::BufferAccessIntent::ShaderBindingTableRead, nr::renderer::QueueDomain::Compute);
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

const nr::test::CaseRegistrar compilerPassShaderScopeCase{
    "render graph compiler uses pass shader scope for shader access", [] {
        auto builder = nr::renderer::RenderGraphBuilder{};
        auto computeNode = builder.addNode("Compute.Typed", nr::renderer::QueueDomain::Compute);
        auto rtNode = builder.addNode("RayTracing.Typed", nr::renderer::QueueDomain::Compute);

        auto storageImage = builder.addResource(nr::renderer::GraphTransientImageDesc{
            .debugName = "Compute.StorageImage",
            .extent = vk::Extent3D{32, 32, 1},
            .format = vk::Format::eR16G16B16A16Sfloat,
            .usageIntents = {nr::renderer::ImageUsageIntent::StorageWrite},
        });
        auto uniformBuffer = builder.addResource(nr::renderer::GraphTransientBufferDesc{
            .debugName = "RayTracing.UniformBuffer",
            .size = 256,
            .usageIntents = {nr::renderer::BufferUsageIntent::Uniform},
        });

        auto computeUses = std::array{nr::renderer::use::storageWrite(storageImage)};
        static_cast<void>(builder.addPass(
            "Compute.Typed.StorageWrite", computeNode, computeUses, [](const nr::renderer::PassRecordContext &) {},
            nullptr, false, vk::PipelineStageFlagBits2::eComputeShader));

        auto rtUses = std::array{nr::renderer::use::uniformRead(uniformBuffer)};
        static_cast<void>(builder.addPass(
            "RayTracing.Typed.UniformRead", rtNode, rtUses, [](const nr::renderer::PassRecordContext &) {}, nullptr,
            false, vk::PipelineStageFlagBits2::eRayTracingShaderKHR));

        auto compiled = nr::renderer::RenderGraphCompiler{}.compile(builder.build());
        auto resourceByHandle =
            [&](nr::renderer::GraphResourceHandle handle) -> const nr::renderer::CompiledResourceDesc & {
            auto it =
                std::ranges::find_if(compiled.resources, [handle](const nr::renderer::CompiledResourceDesc &resource) {
                    return resource.handle == handle;
                });
            nr::test::require(it != compiled.resources.end(), "expected compiled resource by handle");
            return *it;
        };

        nr::test::require(resourceByHandle(storageImage).finalAccessScope.stages ==
                          vk::PipelineStageFlagBits2::eComputeShader);
        nr::test::require(resourceByHandle(uniformBuffer).finalAccessScope.stages ==
                          vk::PipelineStageFlagBits2::eRayTracingShaderKHR);
    }};

const nr::test::CaseRegistrar compilerResourceShaderOverrideCase{
    "render graph compiler honors resource shader stage overrides", [] {
        auto builder = nr::renderer::RenderGraphBuilder{};
        auto node = builder.addNode("Raster.Typed", nr::renderer::QueueDomain::Graphics);

        auto uniformBuffer = builder.addResource(nr::renderer::GraphTransientBufferDesc{
            .debugName = "Raster.VertexUniform",
            .size = 256,
            .usageIntents = {nr::renderer::BufferUsageIntent::Uniform},
        });
        auto sampledImage = builder.addResource(nr::renderer::GraphTransientImageDesc{
            .debugName = "Raster.FragmentSampled",
            .extent = vk::Extent3D{16, 16, 1},
            .format = vk::Format::eR8G8B8A8Unorm,
            .usageIntents = {nr::renderer::ImageUsageIntent::Sampled},
        });

        auto uses = std::array{
            nr::renderer::use::withShaderStages(nr::renderer::use::uniformRead(uniformBuffer),
                                                nr::renderer::ShaderStageIntent::Vertex),
            nr::renderer::use::withShaderStages(nr::renderer::use::sampledRead(sampledImage),
                                                nr::renderer::ShaderStageIntent::Fragment),
        };
        static_cast<void>(builder.addPass(
            "Raster.Typed.Resources", node, uses, [](const nr::renderer::PassRecordContext &) {}, nullptr, false,
            vk::PipelineStageFlagBits2::eAllGraphics));

        auto compiled = nr::renderer::RenderGraphCompiler{}.compile(builder.build());
        auto resourceByHandle =
            [&](nr::renderer::GraphResourceHandle handle) -> const nr::renderer::CompiledResourceDesc & {
            auto it =
                std::ranges::find_if(compiled.resources, [handle](const nr::renderer::CompiledResourceDesc &resource) {
                    return resource.handle == handle;
                });
            nr::test::require(it != compiled.resources.end(), "expected compiled resource by handle");
            return *it;
        };

        nr::test::require(resourceByHandle(uniformBuffer).finalAccessScope.stages ==
                          vk::PipelineStageFlagBits2::eVertexShader);
        nr::test::require(resourceByHandle(sampledImage).finalAccessScope.stages ==
                          vk::PipelineStageFlagBits2::eFragmentShader);
    }};

const nr::test::CaseRegistrar compilerCrossQueueCase{
    "render graph compiler emits explicit cross-queue ownership transition", [] {
        auto frame = buildCrossQueueFrame();
        nr::test::require(nr::renderer::RenderGraphCompiler::hasExplicitSubmitBoundariesForQueueTransitions(frame),
                          "frame should include explicit submit boundary");

        auto compiled = nr::renderer::RenderGraphCompiler{}.compile(frame);
        nr::test::requireEqual(compiled.resources.size(), std::size_t{2});
        nr::test::requireEqual(compiled.submitBatches.size(), std::size_t{2});
        nr::test::requireEqual(compiled.submitBatches[0].queue, nr::renderer::QueueDomain::Graphics);
        nr::test::requireEqual(compiled.submitBatches[1].queue, nr::renderer::QueueDomain::Compute);
        nr::test::require(compiled.submitBatches[1].openedBySubmitNode.has_value(),
                          "compute batch should be opened by submit boundary");
        nr::test::requireEqual(compiled.submitBatches[1].openedBySubmitNodeDebugName, std::string{"GeometryToResolve"});

        auto const &computePass = compiled.submitBatches[1].passes.front();
        nr::test::requireEqual(computePass.preBarriers.size(), std::size_t{2});
        auto crossQueue =
            std::ranges::find_if(computePass.preBarriers, [](const nr::renderer::ResourceStateTransition &transition) {
                return transition.strength == nr::renderer::DependencyStrength::ReleaseAcquireRequired;
            });
        nr::test::require(crossQueue != computePass.preBarriers.end(),
                          "compute pass should include a cross-queue transition");
        nr::test::requireEqual(crossQueue->srcQueue, nr::renderer::QueueDomain::Graphics);
        nr::test::requireEqual(crossQueue->dstQueue, nr::renderer::QueueDomain::Compute);
        nr::test::requireEqual(crossQueue->oldLayout, nr::renderer::ImageLayoutIntent::ColorAttachment);
        nr::test::requireEqual(crossQueue->newLayout, nr::renderer::ImageLayoutIntent::ShaderReadOnly);
        nr::test::requireEqual(compiled.ownershipTransitions.size(), std::size_t{1});
        nr::test::require(compiled.debugView.find("ownershipTransition") != std::string::npos,
                          "debug view should include ownership transition diagnostics");
        nr::test::require(compiled.debugView.find("GeometryToResolve") != std::string::npos,
                          "debug view should include submit boundary names");

        auto plan = nr::renderer::RenderGraphExecutor{}.buildPlan(compiled);
        nr::test::requireEqual(plan.batches.size(), std::size_t{2});
        nr::test::require(plan.batches[1].waitStageMask == vk::PipelineStageFlagBits2::eComputeShader,
                          "graphics-to-compute wait should begin at the actual compute shader consumer stage");
    }};

const nr::test::CaseRegistrar executorSwapchainAcquireBoundaryCase{
    "render graph executor acquires swapchain immediately before the transfer copy batch", [] {
        auto builder = nr::renderer::RenderGraphBuilder{};
        auto computeNode = builder.addNode("Present", nr::renderer::QueueDomain::Compute);
        auto convertedColor = builder.addResource(nr::renderer::GraphTransientImageDesc{
            .debugName = "Present.ConvertedColor",
            .extent = vk::Extent3D{128, 72, 1},
            .format = vk::Format::eB8G8R8A8Unorm,
        });
        auto swapchainImage = builder.addResource(nr::renderer::GraphImportedSwapchainImageDesc{
            .debugName = "Swapchain.Image",
            .initialOwnership = nr::renderer::ResourceOwnershipDomain::Compute,
            .extent = vk::Extent3D{128, 72, 1},
            .format = vk::Format::eB8G8R8A8Unorm,
        });

        auto convertUses = std::array{nr::renderer::use::storageWrite(convertedColor)};
        static_cast<void>(builder.addPass(
            "Present.Convert", computeNode, convertUses, [](const nr::renderer::PassRecordContext &) {}, nullptr, false,
            vk::PipelineStageFlagBits2::eComputeShader));
        static_cast<void>(
            builder.addSubmitNode("Present.AcquireSwapchainImage", nr::renderer::SubmitBoundaryKind::SwapchainAcquire));
        static_cast<void>(builder.addCopyPass("Present.CopyToSwapchain", computeNode,
                                              nr::renderer::CopyImageToImagePassDesc{
                                                  .source = convertedColor,
                                                  .destination = swapchainImage,
                                                  .presentDestination = true,
                                              }));

        auto compiled = nr::renderer::RenderGraphCompiler{}.compile(builder.build());
        auto plan = nr::renderer::RenderGraphExecutor{}.buildPlan(compiled);
        nr::test::requireEqual(compiled.submitBatches.size(), std::size_t{2});
        nr::test::requireEqual(compiled.submitBatches[1].openedBySubmitNodeKind,
                               nr::renderer::SubmitBoundaryKind::SwapchainAcquire);
        nr::test::require(plan.batches[1].acquiresSwapchainBeforeSubmit);
        nr::test::require(plan.batches[1].waitStageMask == vk::PipelineStageFlagBits2::eTransfer);
        nr::test::require(plan.batches[1].signalsPresent);
    }};

const nr::test::CaseRegistrar executorCrossQueueWaitUnionCase{
    "render graph executor unions exact cross-batch consumer wait stages", [] {
        auto builder = nr::renderer::RenderGraphBuilder{};
        auto graphicsNode = builder.addNode("Graphics", nr::renderer::QueueDomain::Graphics);
        auto computeNode = builder.addNode("Compute", nr::renderer::QueueDomain::Compute);
        auto transferSource = builder.addResource(nr::renderer::GraphTransientImageDesc{
            .debugName = "Graphics.TransferSource",
            .extent = vk::Extent3D{32, 32, 1},
            .format = vk::Format::eR8G8B8A8Unorm,
        });
        auto shaderSource = builder.addResource(nr::renderer::GraphTransientImageDesc{
            .debugName = "Graphics.ShaderSource",
            .extent = vk::Extent3D{32, 32, 1},
            .format = vk::Format::eR8G8B8A8Unorm,
        });

        auto graphicsUses = std::array{
            nr::renderer::use::colorWrite(transferSource),
            nr::renderer::use::colorWrite(shaderSource),
        };
        static_cast<void>(builder.addPass("Graphics.Write", graphicsNode, graphicsUses,
                                          [](const nr::renderer::PassRecordContext &) {}));
        static_cast<void>(builder.addSubmitNode("GraphicsToCompute"));

        auto transferUses = std::array{nr::renderer::use::imageTransferSrc(transferSource)};
        static_cast<void>(
            builder.addPass("Compute.Copy", computeNode, transferUses, [](const nr::renderer::PassRecordContext &) {}));
        auto computeUses = std::array{nr::renderer::use::sampledRead(shaderSource)};
        static_cast<void>(builder.addPass(
            "Compute.Read", computeNode, computeUses, [](const nr::renderer::PassRecordContext &) {}, nullptr, false,
            vk::PipelineStageFlagBits2::eComputeShader));

        auto compiled = nr::renderer::RenderGraphCompiler{}.compile(builder.build());
        auto plan = nr::renderer::RenderGraphExecutor{}.buildPlan(compiled);
        auto expected = vk::PipelineStageFlagBits2::eTransfer | vk::PipelineStageFlagBits2::eComputeShader;
        nr::test::requireEqual(plan.batches.size(), std::size_t{2});
        nr::test::require(plan.batches[1].waitStageMask == expected,
                          "cross-batch wait should include both transfer and compute consumers");
    }};

const nr::test::CaseRegistrar compilerPassOrderCase{
    "render graph compiler preserves compiled pass order for executor merge", [] {
        auto frame = buildMultiPassGraphicsFrame();
        auto compiled = nr::renderer::RenderGraphCompiler{}.compile(frame);

        nr::test::requireEqual(compiled.submitBatches.size(), std::size_t{1});
        nr::test::requireEqual(compiled.submitBatches.front().passes.size(), std::size_t{3});
        nr::test::requireEqual(compiled.submitBatches.front().passes[0].debugName, std::string{"Graphics.First"});
        nr::test::requireEqual(compiled.submitBatches.front().passes[1].debugName, std::string{"Graphics.Second"});
        nr::test::requireEqual(compiled.submitBatches.front().passes[2].debugName, std::string{"Graphics.Third"});
    }};

const nr::test::CaseRegistrar compilerBufferHazardCase{
    "render graph compiler emits precise same-batch buffer hazard barriers and skips RAR", [] {
        auto builder = nr::renderer::RenderGraphBuilder{};
        auto node = builder.addNode("BufferHazards", nr::renderer::QueueDomain::Compute);
        auto makeBuffer = [&](std::string_view name) {
            return builder.addResource(nr::renderer::GraphTransientBufferDesc{
                .debugName = std::string{name},
                .size = 256,
                .usageIntents = {nr::renderer::BufferUsageIntent::StorageReadWrite},
            });
        };
        auto raw = makeBuffer("BufferHazards.RAW");
        auto war = makeBuffer("BufferHazards.WAR");
        auto waw = makeBuffer("BufferHazards.WAW");
        auto rar = makeBuffer("BufferHazards.RAR");

        auto firstUses = std::array{
            nr::renderer::use::storageBufferWrite(raw),
            nr::renderer::use::storageBufferRead(war),
            nr::renderer::use::storageBufferWrite(waw),
            nr::renderer::use::storageBufferRead(rar),
        };
        auto secondUses = std::array{
            nr::renderer::use::storageBufferRead(raw),
            nr::renderer::use::storageBufferWrite(war),
            nr::renderer::use::storageBufferWrite(waw),
            nr::renderer::use::storageBufferRead(rar),
        };
        static_cast<void>(builder.addPass(
            "BufferHazards.First", node, firstUses, [](const nr::renderer::PassRecordContext &) {}, nullptr, false,
            vk::PipelineStageFlagBits2::eComputeShader));
        static_cast<void>(builder.addPass(
            "BufferHazards.Second", node, secondUses, [](const nr::renderer::PassRecordContext &) {}, nullptr, false,
            vk::PipelineStageFlagBits2::eComputeShader));

        auto compiled = nr::renderer::RenderGraphCompiler{}.compile(builder.build());
        auto const &second = compiled.submitBatches.front().passes[1];
        nr::test::requireEqual(second.preBarriers.size(), std::size_t{3});

        auto const &rawBarrier = requireBarrierFor(second, raw, "buffer RAW should require a barrier");
        nr::test::require(rawBarrier.srcScope.stages == vk::PipelineStageFlagBits2::eComputeShader);
        nr::test::require(rawBarrier.srcScope.access == vk::AccessFlagBits2::eShaderStorageWrite);
        nr::test::require(rawBarrier.dstScope.access == vk::AccessFlagBits2::eShaderStorageRead);

        auto const &warBarrier = requireBarrierFor(second, war, "buffer WAR should require a barrier");
        nr::test::require(warBarrier.srcScope.access == vk::AccessFlagBits2::eShaderStorageRead);
        nr::test::require(warBarrier.dstScope.access == vk::AccessFlagBits2::eShaderStorageWrite);

        auto const &wawBarrier = requireBarrierFor(second, waw, "buffer WAW should require a barrier");
        nr::test::require(wawBarrier.srcScope.access == vk::AccessFlagBits2::eShaderStorageWrite);
        nr::test::require(wawBarrier.dstScope.access == vk::AccessFlagBits2::eShaderStorageWrite);
        nr::test::require(!hasBarrierFor(second, rar), "buffer RAR should not create an automatic barrier");
    }};

const nr::test::CaseRegistrar compilerImageHazardCase{
    "render graph compiler emits precise same-batch image hazard barriers and skips RAR", [] {
        auto builder = nr::renderer::RenderGraphBuilder{};
        auto node = builder.addNode("ImageHazards", nr::renderer::QueueDomain::Compute);
        auto makeImage = [&](std::string_view name) {
            return builder.addResource(nr::renderer::GraphTransientImageDesc{
                .debugName = std::string{name},
                .extent = vk::Extent3D{16, 16, 1},
                .format = vk::Format::eR8G8B8A8Unorm,
                .usageIntents = {nr::renderer::ImageUsageIntent::StorageReadWrite},
            });
        };
        auto raw = makeImage("ImageHazards.RAW");
        auto war = makeImage("ImageHazards.WAR");
        auto waw = makeImage("ImageHazards.WAW");
        auto rar = makeImage("ImageHazards.RAR");

        auto firstUses = std::array{
            nr::renderer::use::storageWrite(raw),
            nr::renderer::use::storageRead(war),
            nr::renderer::use::storageWrite(waw),
            nr::renderer::use::storageRead(rar),
        };
        auto secondUses = std::array{
            nr::renderer::use::storageRead(raw),
            nr::renderer::use::storageWrite(war),
            nr::renderer::use::storageWrite(waw),
            nr::renderer::use::storageRead(rar),
        };
        static_cast<void>(builder.addPass(
            "ImageHazards.First", node, firstUses, [](const nr::renderer::PassRecordContext &) {}, nullptr, false,
            vk::PipelineStageFlagBits2::eComputeShader));
        static_cast<void>(builder.addPass(
            "ImageHazards.Second", node, secondUses, [](const nr::renderer::PassRecordContext &) {}, nullptr, false,
            vk::PipelineStageFlagBits2::eComputeShader));

        auto compiled = nr::renderer::RenderGraphCompiler{}.compile(builder.build());
        auto const &second = compiled.submitBatches.front().passes[1];
        nr::test::requireEqual(second.preBarriers.size(), std::size_t{3});
        auto const &rawBarrier = requireBarrierFor(second, raw, "image RAW should require a barrier");
        nr::test::require(rawBarrier.oldLayout == nr::renderer::ImageLayoutIntent::General);
        nr::test::require(rawBarrier.newLayout == nr::renderer::ImageLayoutIntent::General);
        nr::test::require(rawBarrier.srcScope.access == vk::AccessFlagBits2::eShaderStorageWrite);
        nr::test::require(rawBarrier.dstScope.access == vk::AccessFlagBits2::eShaderStorageRead);
        static_cast<void>(requireBarrierFor(second, war, "image WAR should require a barrier"));
        static_cast<void>(requireBarrierFor(second, waw, "image WAW should require a barrier"));
        nr::test::require(!hasBarrierFor(second, rar), "image RAR should not create an automatic barrier");
    }};

const nr::test::CaseRegistrar compilerAccelerationStructureHazardCase{
    "render graph compiler emits precise same-batch AS hazard barriers and skips RAR", [] {
        auto builder = nr::renderer::RenderGraphBuilder{};
        auto node = builder.addNode("AccelerationStructureHazards", nr::renderer::QueueDomain::Graphics);
        auto makeAccelerationStructure = [&](std::string_view name) {
            return builder.addResource(nr::renderer::GraphImportedAccelerationStructureDesc{
                .debugName = std::string{name},
                .type = vk::AccelerationStructureTypeKHR::eTopLevel,
                .size = 4096,
            });
        };
        auto raw = makeAccelerationStructure("AccelerationStructureHazards.RAW");
        auto war = makeAccelerationStructure("AccelerationStructureHazards.WAR");
        auto waw = makeAccelerationStructure("AccelerationStructureHazards.WAW");
        auto rar = makeAccelerationStructure("AccelerationStructureHazards.RAR");

        auto firstUses = std::array{
            nr::renderer::use::accelerationStructureBuildWrite(raw),
            nr::renderer::use::accelerationStructureTraceRead(war),
            nr::renderer::use::accelerationStructureBuildWrite(waw),
            nr::renderer::use::accelerationStructureTraceRead(rar),
        };
        auto secondUses = std::array{
            nr::renderer::use::accelerationStructureTraceRead(raw),
            nr::renderer::use::accelerationStructureBuildWrite(war),
            nr::renderer::use::accelerationStructureBuildWrite(waw),
            nr::renderer::use::accelerationStructureTraceRead(rar),
        };
        static_cast<void>(builder.addPass("AccelerationStructureHazards.First", node, firstUses,
                                          [](const nr::renderer::PassRecordContext &) {}));
        static_cast<void>(builder.addPass("AccelerationStructureHazards.Second", node, secondUses,
                                          [](const nr::renderer::PassRecordContext &) {}));

        auto compiled = nr::renderer::RenderGraphCompiler{}.compile(builder.build());
        auto const &second = compiled.submitBatches.front().passes[1];
        nr::test::requireEqual(second.preBarriers.size(), std::size_t{3});
        auto const &rawBarrier = requireBarrierFor(second, raw, "AS RAW should require a barrier");
        nr::test::require(rawBarrier.srcScope.stages == vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR);
        nr::test::require(rawBarrier.srcScope.access == vk::AccessFlagBits2::eAccelerationStructureWriteKHR);
        nr::test::require(rawBarrier.dstScope.stages == vk::PipelineStageFlagBits2::eRayTracingShaderKHR);
        nr::test::require(rawBarrier.dstScope.access == vk::AccessFlagBits2::eAccelerationStructureReadKHR);
        static_cast<void>(requireBarrierFor(second, war, "AS WAR should require a barrier"));
        static_cast<void>(requireBarrierFor(second, waw, "AS WAW should require a barrier"));
        nr::test::require(!hasBarrierFor(second, rar), "AS RAR should not create an automatic barrier");
    }};

const nr::test::CaseRegistrar compilerOrderedUseBarrierCase{
    "render graph compiler honors ordered previous-use barrier markers", [] {
        auto builder = nr::renderer::RenderGraphBuilder{};
        auto node = builder.addNode("Ordered", nr::renderer::QueueDomain::Graphics);
        auto color = builder.addResource(nr::renderer::GraphTransientImageDesc{
            .debugName = "Ordered.Color",
            .extent = vk::Extent3D{32, 32, 1},
            .format = vk::Format::eR8G8B8A8Unorm,
        });

        auto firstUses = std::array{nr::renderer::use::sampledRead(color)};
        auto secondUses = std::array{nr::renderer::use::orderedAfterPrevious(nr::renderer::use::sampledRead(color))};
        static_cast<void>(
            builder.addPass("Ordered.First", node, firstUses, [](const nr::renderer::PassRecordContext &) {}));
        static_cast<void>(
            builder.addPass("Ordered.Second", node, secondUses, [](const nr::renderer::PassRecordContext &) {}));

        auto compiled = nr::renderer::RenderGraphCompiler{}.compile(builder.build());
        nr::test::requireEqual(compiled.submitBatches.size(), std::size_t{1});
        nr::test::requireEqual(compiled.submitBatches.front().passes.size(), std::size_t{2});

        auto const &secondPass = compiled.submitBatches.front().passes[1];
        nr::test::requireEqual(secondPass.preBarriers.size(), std::size_t{1});
        auto const &barrier = secondPass.preBarriers.front();
        nr::test::requireEqual(barrier.strength, nr::renderer::DependencyStrength::BarrierRequired);
        nr::test::requireEqual(barrier.srcQueue, nr::renderer::QueueDomain::Graphics);
        nr::test::requireEqual(barrier.dstQueue, nr::renderer::QueueDomain::Graphics);
        nr::test::requireEqual(barrier.oldLayout, nr::renderer::ImageLayoutIntent::ShaderReadOnly);
        nr::test::requireEqual(barrier.newLayout, nr::renderer::ImageLayoutIntent::ShaderReadOnly);
        nr::test::require(barrier.srcScope.access == vk::AccessFlagBits2::eShaderSampledRead);
        nr::test::require(barrier.dstScope.access == vk::AccessFlagBits2::eShaderSampledRead);
    }};

const nr::test::CaseRegistrar compilerRetainedBufferFirstUseHazardCase{
    "render graph compiler synchronizes initialized retained first use without inventing fresh hazards", [] {
        auto writeState = nr::renderer::RetainedBufferState{
            .common =
                nr::renderer::RetainedExternalResourceState{
                    .initialized = true,
                    .ownership = nr::renderer::ResourceOwnershipDomain::Compute,
                    .access =
                        nr::renderer::AccessScope{
                            .stages = vk::PipelineStageFlagBits2::eComputeShader,
                            .access = vk::AccessFlagBits2::eShaderStorageWrite,
                        },
                },
        };
        auto readState = nr::renderer::RetainedBufferState{
            .common =
                nr::renderer::RetainedExternalResourceState{
                    .initialized = true,
                    .ownership = nr::renderer::ResourceOwnershipDomain::Compute,
                    .access =
                        nr::renderer::AccessScope{
                            .stages = vk::PipelineStageFlagBits2::eComputeShader,
                            .access = vk::AccessFlagBits2::eShaderStorageRead,
                        },
                },
        };
        auto markerState = readState;
        auto incompleteState = nr::renderer::RetainedBufferState{
            .common =
                nr::renderer::RetainedExternalResourceState{
                    .initialized = true,
                    .ownership = nr::renderer::ResourceOwnershipDomain::Compute,
                },
        };
        auto freshState = nr::renderer::RetainedBufferState{};

        auto builder = nr::renderer::RenderGraphBuilder{};
        auto node = builder.addNode("RetainedFirstUse", nr::renderer::QueueDomain::Compute);
        auto addBuffer = [&](std::string_view name, nr::renderer::RetainedBufferState &state) {
            return builder.addResource(nr::renderer::GraphImportedBufferDesc{
                .debugName = std::string{name},
                .lifetime = nr::renderer::ResourceLifetime::RendererPersistent,
                .initialOwnership = nr::renderer::ResourceOwnershipDomain::Compute,
                .size = 256,
                .usageIntents = {nr::renderer::BufferUsageIntent::StorageReadWrite},
                .retainedState = std::ref(state),
            });
        };
        auto raw = addBuffer("RetainedFirstUse.RAW", writeState);
        auto rar = addBuffer("RetainedFirstUse.RAR", readState);
        auto marker = addBuffer("RetainedFirstUse.Marker", markerState);
        auto incomplete = addBuffer("RetainedFirstUse.Incomplete", incompleteState);
        auto fresh = addBuffer("RetainedFirstUse.Fresh", freshState);
        auto uses = std::array{
            nr::renderer::use::storageBufferRead(raw),
            nr::renderer::use::storageBufferRead(rar),
            nr::renderer::use::orderedAfterPrevious(nr::renderer::use::storageBufferRead(marker)),
            nr::renderer::use::storageBufferRead(incomplete),
            nr::renderer::use::storageBufferWrite(fresh),
        };
        static_cast<void>(builder.addPass(
            "RetainedFirstUse.Pass", node, uses, [](const nr::renderer::PassRecordContext &) {}, nullptr, false,
            vk::PipelineStageFlagBits2::eComputeShader));

        auto compiled = nr::renderer::RenderGraphCompiler{}.compile(builder.build());
        auto const &pass = compiled.submitBatches.front().passes.front();
        nr::test::requireEqual(pass.preBarriers.size(), std::size_t{3});

        auto const &rawBarrier = requireBarrierFor(pass, raw, "retained first-use RAW should require a barrier");
        nr::test::require(rawBarrier.srcScope.stages == vk::PipelineStageFlagBits2::eComputeShader);
        nr::test::require(rawBarrier.srcScope.access == vk::AccessFlagBits2::eShaderStorageWrite);
        nr::test::require(rawBarrier.dstScope.access == vk::AccessFlagBits2::eShaderStorageRead);

        auto const &markerBarrier =
            requireBarrierFor(pass, marker, "retained first-use explicit RAR marker should require a barrier");
        nr::test::require(markerBarrier.srcScope.access == vk::AccessFlagBits2::eShaderStorageRead);
        nr::test::require(markerBarrier.dstScope.access == vk::AccessFlagBits2::eShaderStorageRead);

        auto const &incompleteBarrier =
            requireBarrierFor(pass, incomplete, "incomplete retained source scope should synchronize conservatively");
        nr::test::require(incompleteBarrier.srcScope.stages == vk::PipelineStageFlagBits2::eAllCommands);
        nr::test::require(incompleteBarrier.srcScope.access ==
                          (vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite));
        nr::test::require(!hasBarrierFor(pass, rar), "retained first-use RAR should not create an automatic barrier");
        nr::test::require(!hasBarrierFor(pass, fresh),
                          "uninitialized retained write should not invent a previous access barrier");
    }};

const nr::test::CaseRegistrar compilerRetainedImageAndAccelerationStructureFirstUseHazardCase{
    "render graph compiler synchronizes same-layout retained image and AS first-use RAW hazards", [] {
        auto imageState = nr::renderer::RetainedImageState{
            .common =
                nr::renderer::RetainedExternalResourceState{
                    .initialized = true,
                    .ownership = nr::renderer::ResourceOwnershipDomain::Graphics,
                    .access =
                        nr::renderer::AccessScope{
                            .stages = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                            .access = vk::AccessFlagBits2::eShaderStorageWrite,
                        },
                },
            .layout = nr::renderer::ImageLayoutIntent::General,
        };
        auto accelerationStructureState = nr::renderer::RetainedAccelerationStructureState{
            .common =
                nr::renderer::RetainedExternalResourceState{
                    .initialized = true,
                    .ownership = nr::renderer::ResourceOwnershipDomain::Graphics,
                    .access =
                        nr::renderer::AccessScope{
                            .stages = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
                            .access = vk::AccessFlagBits2::eAccelerationStructureWriteKHR,
                        },
                },
        };
        auto builder = nr::renderer::RenderGraphBuilder{};
        auto node = builder.addNode("RetainedImageAndAS", nr::renderer::QueueDomain::Graphics);
        auto image = builder.addResource(nr::renderer::GraphImportedImageDesc{
            .debugName = "RetainedImageAndAS.Image",
            .lifetime = nr::renderer::ResourceLifetime::RendererPersistent,
            .initialOwnership = nr::renderer::ResourceOwnershipDomain::Graphics,
            .extent = vk::Extent3D{16, 16, 1},
            .format = vk::Format::eR8G8B8A8Unorm,
            .usageIntents = {nr::renderer::ImageUsageIntent::StorageRead},
            .initialLayout = nr::renderer::ImageLayoutIntent::General,
            .retainedState = std::ref(imageState),
        });
        auto accelerationStructure = builder.addResource(nr::renderer::GraphImportedAccelerationStructureDesc{
            .debugName = "RetainedImageAndAS.TLAS",
            .lifetime = nr::renderer::ResourceLifetime::RendererPersistent,
            .initialOwnership = nr::renderer::ResourceOwnershipDomain::Graphics,
            .type = vk::AccelerationStructureTypeKHR::eTopLevel,
            .size = 4096,
            .usageIntents = {nr::renderer::AccelerationStructureUsageIntent::TraceInput},
            .retainedState = std::ref(accelerationStructureState),
        });
        auto uses = std::array{
            nr::renderer::use::storageRead(image),
            nr::renderer::use::accelerationStructureTraceRead(accelerationStructure),
        };
        static_cast<void>(builder.addPass(
            "RetainedImageAndAS.Read", node, uses, [](const nr::renderer::PassRecordContext &) {}, nullptr, false,
            vk::PipelineStageFlagBits2::eRayTracingShaderKHR));

        auto compiled = nr::renderer::RenderGraphCompiler{}.compile(builder.build());
        auto const &pass = compiled.submitBatches.front().passes.front();
        nr::test::requireEqual(pass.preBarriers.size(), std::size_t{2});
        auto const &imageBarrier =
            requireBarrierFor(pass, image, "same-layout retained image first-use RAW should require a barrier");
        nr::test::require(imageBarrier.oldLayout == nr::renderer::ImageLayoutIntent::General);
        nr::test::require(imageBarrier.newLayout == nr::renderer::ImageLayoutIntent::General);
        nr::test::require(imageBarrier.srcScope.access == vk::AccessFlagBits2::eShaderStorageWrite);
        nr::test::require(imageBarrier.dstScope.access == vk::AccessFlagBits2::eShaderStorageRead);

        auto const &asBarrier =
            requireBarrierFor(pass, accelerationStructure, "retained AS first-use RAW should require a barrier");
        nr::test::require(asBarrier.srcScope.access == vk::AccessFlagBits2::eAccelerationStructureWriteKHR);
        nr::test::require(asBarrier.dstScope.access == vk::AccessFlagBits2::eAccelerationStructureReadKHR);
    }};

const nr::test::CaseRegistrar compilerRetainedImageInitializedCase{
    "render graph compiler uses initialized retained image state for first-use barriers", [] {
        auto state = nr::renderer::RetainedImageState{
            .common =
                nr::renderer::RetainedExternalResourceState{
                    .initialized = true,
                    .ownership = nr::renderer::ResourceOwnershipDomain::Compute,
                    .access =
                        nr::renderer::AccessScope{
                            .stages = vk::PipelineStageFlagBits2::eTransfer,
                            .access = vk::AccessFlagBits2::eTransferRead,
                        },
                },
            .layout = nr::renderer::ImageLayoutIntent::TransferSrc,
        };
        auto frame = buildRetainedStorageWriteFrame(state, "initialized");
        auto compiled = nr::renderer::RenderGraphCompiler{}.compile(frame);

        nr::test::requireEqual(compiled.resources.size(), std::size_t{1});
        auto const &resource = compiled.resources.front();
        nr::test::requireEqual(resource.initialLayout, nr::renderer::ImageLayoutIntent::TransferSrc);
        nr::test::requireEqual(resource.initialOwnership, nr::renderer::ResourceOwnershipDomain::Compute);
        nr::test::require(resource.initialAccessScope.stages == vk::PipelineStageFlagBits2::eTransfer);
        nr::test::require(resource.initialAccessScope.access == vk::AccessFlagBits2::eTransferRead);
        nr::test::requireEqual(resource.finalLayout, nr::renderer::ImageLayoutIntent::General);
        nr::test::requireEqual(resource.finalOwnership, nr::renderer::ResourceOwnershipDomain::Compute);
        nr::test::require(resource.finalAccessScope.stages == (vk::PipelineStageFlagBits2::eComputeShader |
                                                               vk::PipelineStageFlagBits2::eRayTracingShaderKHR));
        nr::test::require(resource.finalAccessScope.access == vk::AccessFlagBits2::eShaderStorageWrite);
        nr::test::require(resource.retainedState.has_value(), "compiled retained image should keep state ref");
        nr::test::require(std::addressof(resource.retainedState->get()) == std::addressof(state),
                          "compiled retained image should point at the current retained state");

        auto const &pass = compiled.submitBatches.front().passes.front();
        nr::test::requireEqual(pass.preBarriers.size(), std::size_t{1});
        auto const &barrier = pass.preBarriers.front();
        nr::test::requireEqual(barrier.oldLayout, nr::renderer::ImageLayoutIntent::TransferSrc);
        nr::test::requireEqual(barrier.newLayout, nr::renderer::ImageLayoutIntent::General);
        nr::test::require(barrier.srcScope.stages == vk::PipelineStageFlagBits2::eTransfer);
        nr::test::require(barrier.srcScope.access == vk::AccessFlagBits2::eTransferRead);
        nr::test::require(barrier.dstScope.stages == (vk::PipelineStageFlagBits2::eComputeShader |
                                                      vk::PipelineStageFlagBits2::eRayTracingShaderKHR));
        nr::test::require(barrier.dstScope.access == vk::AccessFlagBits2::eShaderStorageWrite);
    }};

const nr::test::CaseRegistrar compilerRetainedImageInitialOwnershipCase{
    "render graph compiler transfers initialized retained image ownership before first use", [] {
        auto state = nr::renderer::RetainedImageState{
            .common =
                nr::renderer::RetainedExternalResourceState{
                    .initialized = true,
                    .ownership = nr::renderer::ResourceOwnershipDomain::Graphics,
                    .access =
                        nr::renderer::AccessScope{
                            .stages = vk::PipelineStageFlagBits2::eTransfer,
                            .access = vk::AccessFlagBits2::eTransferRead,
                        },
                },
            .layout = nr::renderer::ImageLayoutIntent::TransferSrc,
        };
        auto frame = buildRetainedStorageWriteFrame(state, "initial-ownership");
        auto compiled = nr::renderer::RenderGraphCompiler{}.compile(frame);

        auto const &resource = compiled.resources.front();
        nr::test::requireEqual(resource.initialOwnership, nr::renderer::ResourceOwnershipDomain::Graphics);
        nr::test::requireEqual(resource.finalOwnership, nr::renderer::ResourceOwnershipDomain::Compute);

        auto const &pass = compiled.submitBatches.front().passes.front();
        nr::test::requireEqual(pass.preBarriers.size(), std::size_t{1});
        auto const &transition = pass.preBarriers.front();
        nr::test::requireEqual(transition.strength, nr::renderer::DependencyStrength::ReleaseAcquireRequired);
        nr::test::requireEqual(transition.srcQueue, nr::renderer::QueueDomain::Graphics);
        nr::test::requireEqual(transition.dstQueue, nr::renderer::QueueDomain::Compute);
        nr::test::requireEqual(transition.oldLayout, nr::renderer::ImageLayoutIntent::TransferSrc);
        nr::test::requireEqual(transition.newLayout, nr::renderer::ImageLayoutIntent::General);
        nr::test::requireEqual(compiled.ownershipTransitions.size(), std::size_t{1});

        auto plan = nr::renderer::RenderGraphExecutor{}.buildPlan(compiled);
        nr::test::requireEqual(plan.initialReleaseBatches.size(), std::size_t{1});
        auto const &initialRelease = plan.initialReleaseBatches.front();
        nr::test::requireEqual(initialRelease.queue, nr::renderer::QueueDomain::Graphics);
        nr::test::requireEqual(initialRelease.tailReleaseTransitions.size(), std::size_t{1});
        nr::test::require(!initialRelease.waitsForPreviousBatch, "first synthetic release should not wait");
        nr::test::require(initialRelease.signalsNextBatch, "synthetic release should signal its consumer chain");

        nr::test::requireEqual(plan.batches.size(), std::size_t{1});
        auto const &consumerBatch = plan.batches.front();
        nr::test::requireEqual(consumerBatch.headAcquireTransitions.size(), std::size_t{1});
        nr::test::require(consumerBatch.waitsForPreviousBatch,
                          "first-use consumer should wait for the synthetic release");
        nr::test::require(consumerBatch.waitStageMask == (vk::PipelineStageFlagBits2::eComputeShader |
                                                          vk::PipelineStageFlagBits2::eRayTracingShaderKHR),
                          "first-use acquire should wait at the retained resource consumer stages");
    }};

const nr::test::CaseRegistrar compilerRetainedImageUninitializedCase{
    "render graph compiler treats uninitialized retained images as undefined", [] {
        auto state = nr::renderer::RetainedImageState{};
        auto builder = nr::renderer::RenderGraphBuilder{};
        auto node = builder.addNode("Retained.Uninitialized", nr::renderer::QueueDomain::Compute);
        auto retainedImage = builder.addResource(nr::renderer::GraphImportedImageDesc{
            .debugName = "Retained.Uninitialized.Image",
            .lifetime = nr::renderer::ResourceLifetime::RendererPersistent,
            .initialOwnership = nr::renderer::ResourceOwnershipDomain::Compute,
            .extent = vk::Extent3D{64, 64, 1},
            .format = vk::Format::eR16G16B16A16Sfloat,
            .usageIntents =
                {
                    nr::renderer::ImageUsageIntent::StorageWrite,
                    nr::renderer::ImageUsageIntent::TransferSrc,
                },
            .initialLayout = nr::renderer::ImageLayoutIntent::TransferSrc,
            .initialAccessScope =
                nr::renderer::AccessScope{
                    .stages = vk::PipelineStageFlagBits2::eTransfer,
                    .access = vk::AccessFlagBits2::eTransferRead,
                },
            .retainedState = std::ref(state),
        });
        auto uses = std::array{nr::renderer::use::storageWrite(retainedImage)};
        static_cast<void>(
            builder.addPass("Retained.Uninitialized.Pass", node, uses, [](const nr::renderer::PassRecordContext &) {}));

        auto compiled = nr::renderer::RenderGraphCompiler{}.compile(builder.build());
        auto const &resource = compiled.resources.front();
        nr::test::requireEqual(resource.initialLayout, nr::renderer::ImageLayoutIntent::Undefined);
        nr::test::requireEqual(resource.initialOwnership, nr::renderer::ResourceOwnershipDomain::Undefined);
        nr::test::require(!resource.initialAccessScope.resolved(),
                          "uninitialized retained source scope should stay empty");

        auto const &barrier = compiled.submitBatches.front().passes.front().preBarriers.front();
        nr::test::requireEqual(barrier.oldLayout, nr::renderer::ImageLayoutIntent::Undefined);
        nr::test::requireEqual(barrier.newLayout, nr::renderer::ImageLayoutIntent::General);
        nr::test::require(!barrier.srcScope.resolved(),
                          "uninitialized retained first-use barrier should have empty source scope");
        nr::test::require(barrier.dstScope.access == vk::AccessFlagBits2::eShaderStorageWrite);
    }};

const nr::test::CaseRegistrar compilerAccelerationStructureCase{
    "render graph compiler tracks acceleration structure resources", [] {
        auto frame = buildAccelerationStructureFrame();
        auto compiled = nr::renderer::RenderGraphCompiler{}.compile(frame);

        nr::test::requireEqual(compiled.resources.size(), std::size_t{1});
        nr::test::require(compiled.resources.front().isAccelerationStructure, "TLAS should compile as an AS resource");
        nr::test::requireEqual(compiled.resources.front().resolvedAccelerationStructureSize, vk::DeviceSize{8192});
        nr::test::requireEqual(compiled.submitBatches.size(), std::size_t{2});
        nr::test::requireEqual(compiled.submitBatches[0].queue, nr::renderer::QueueDomain::Compute);
        nr::test::requireEqual(compiled.submitBatches[1].queue, nr::renderer::QueueDomain::Graphics);
        nr::test::require(compiled.submitBatches[1].openedBySubmitNode.has_value(),
                          "graphics RT batch should be opened by the explicit submit boundary");
        nr::test::requireEqual(compiled.submitBatches[1].openedBySubmitNodeDebugName,
                               std::string{"rtobject.ComputeToGraphics"});

        auto const &tracePass = compiled.submitBatches[1].passes.front();
        nr::test::requireEqual(tracePass.preBarriers.size(), std::size_t{1});
        auto const &barrier = tracePass.preBarriers.front();
        nr::test::requireEqual(barrier.strength, nr::renderer::DependencyStrength::ReleaseAcquireRequired);
        nr::test::requireEqual(barrier.srcQueue, nr::renderer::QueueDomain::Compute);
        nr::test::requireEqual(barrier.dstQueue, nr::renderer::QueueDomain::Graphics);
        nr::test::require(barrier.srcScope.stages == vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR);
        nr::test::require(barrier.srcScope.access == vk::AccessFlagBits2::eAccelerationStructureWriteKHR);
        nr::test::require(barrier.dstScope.stages == vk::PipelineStageFlagBits2::eRayTracingShaderKHR);
        nr::test::require(barrier.dstScope.access == vk::AccessFlagBits2::eAccelerationStructureReadKHR);
        nr::test::requireEqual(compiled.ownershipTransitions.size(), std::size_t{1});
        auto plan = nr::renderer::RenderGraphExecutor{}.buildPlan(compiled);
        nr::test::require(plan.batches[1].waitStageMask == vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                          "AS-to-RT wait should begin at the ray tracing shader consumer stage");
        nr::test::require(compiled.debugView.find("type=AccelerationStructure") != std::string::npos,
                          "debug view should identify AS resources");
    }};

const nr::test::CaseRegistrar compilerPrepareRecordSplitCase{
    "render graph compiler keeps prepare and record callbacks separate", [] {
        auto builder = nr::renderer::RenderGraphBuilder{};
        auto node = builder.addNode("Bindings", nr::renderer::QueueDomain::Graphics);
        auto color = builder.addResource(nr::renderer::GraphTransientImageDesc{
            .debugName = "Bindings.Color",
            .extent = vk::Extent3D{32, 32, 1},
            .format = vk::Format::eR8G8B8A8Unorm,
        });

        auto uses = std::array{nr::renderer::use::colorWrite(color)};
        auto pass = builder.addPass(
            "Bindings.Split", node, uses, [](const nr::renderer::PassRecordContext &) {},
            [](const nr::renderer::PassPrepareContext &) {});
        nr::test::require(pass.valid(), "split binding pass should be valid");

        auto frame = builder.build();
        nr::test::require(static_cast<bool>(frame.passes.front().prepare), "builder should retain prepare callback");
        nr::test::require(static_cast<bool>(frame.passes.front().record), "builder should retain record callback");

        auto compiled = nr::renderer::RenderGraphCompiler{}.compile(frame);
        auto const &compiledPass = compiled.submitBatches.front().passes.front();
        nr::test::require(static_cast<bool>(compiledPass.prepare), "compiler should retain prepare callback");
        nr::test::require(static_cast<bool>(compiledPass.record), "compiler should retain record callback");
        nr::test::require(!compiledPass.parallelRecord.has_value(),
                          "serial pass should not retain a parallel record desc");
        nr::test::requireEqual(compiledPass.debugName, std::string{"Bindings.Split"});
    }};

const nr::test::CaseRegistrar compilerFrameDataCase{
    "render graph compiler owns canonical typed frame data uses", [] {
        auto builder = nr::renderer::RenderGraphBuilder{};
        auto node = builder.addNode("FrameData", nr::renderer::QueueDomain::Graphics);
        auto frameData = builder.addFrameData("SceneBridgeFrame", std::uint32_t{42});
        auto secondaryFrameData = builder.addFrameData("SecondaryFrame", std::uint32_t{84});
        auto color = builder.addResource(nr::renderer::GraphTransientImageDesc{
            .debugName = "FrameData.Color",
            .extent = vk::Extent3D{16, 16, 1},
            .format = vk::Format::eR8G8B8A8Unorm,
        });

        auto uses = std::array{nr::renderer::use::colorWrite(color)};
        auto frameDataUses = std::vector{secondaryFrameData, frameData, secondaryFrameData};
        static_cast<void>(builder.addPass("FrameData.Pass", node, uses,
                                          [](const nr::renderer::PassRecordContext &) {}, nullptr, false,
                                          vk::PipelineStageFlags2{}, frameDataUses));
        frameDataUses.clear();

        auto frame = builder.build();
        nr::test::requireEqual(frame.passes.front().frameDataUses.size(), std::size_t{2});
        nr::test::requireEqual(frame.passes.front().frameDataUses[0], secondaryFrameData);
        nr::test::requireEqual(frame.passes.front().frameDataUses[1], frameData);

        auto compiled = nr::renderer::RenderGraphCompiler{}.compileConsuming(frame);
        nr::test::requireEqual(compiled.frameData.size(), std::size_t{2});
        nr::test::requireEqual(compiled.frameData.front().handle, frameData);
        auto const &compiledPass = compiled.submitBatches.front().passes.front();
        nr::test::requireEqual(compiledPass.frameDataUses.size(), std::size_t{2});
        nr::test::requireEqual(compiledPass.frameDataUses[0], secondaryFrameData);
        nr::test::requireEqual(compiledPass.frameDataUses[1], frameData);

        auto recordContext = nr::renderer::PassRecordContext{
            .resolveFrameDataPayload = [&](nr::renderer::GraphFrameDataHandle handle)
                -> std::optional<std::reference_wrapper<const std::any>> {
                auto frameDataIt =
                    std::ranges::find_if(compiled.frameData, [handle](const nr::renderer::GraphFrameDataDesc &desc) {
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
    "render graph compile cache hit patches current frame data callbacks and imports", [] {
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

        auto importedIt = std::ranges::find_if(
            compiled.resources, [](const nr::renderer::CompiledResourceDesc &resource) { return resource.isBuffer; });
        nr::test::require(importedIt != compiled.resources.end(),
                          "compiled cache hit should retain imported buffer resource");
        nr::test::require(importedIt->importedBufferResource.has_value(),
                          "compiled imported buffer ref should be patched");
        nr::test::require(std::addressof(importedIt->importedBufferResource->get()) == std::addressof(currentBuffer),
                          "compiled cache hit should use current imported buffer ref");

        auto const &compiledPass = compiled.submitBatches.front().passes.front();
        nr::test::require(static_cast<bool>(compiledPass.record),
                          "compiled cache hit should patch current record callback");
        nr::test::requireEqual(compiledPass.frameDataUses.size(), std::size_t{1});
        nr::test::requireEqual(compiledPass.frameDataUses.front(), compiled.frameData.front().handle);
        compiledPass.record(nr::renderer::PassRecordContext{
            .resolveFrameDataPayload =
                [&](nr::renderer::GraphFrameDataHandle handle) { return findFrameDataPayload(compiled, handle); },
        });
        nr::test::requireEqual(currentRecordedValue, std::uint32_t{222});
        nr::test::requireEqual(previousRecordedValue, std::uint32_t{0},
                               "cached callback from previous frame must not run");
    }};

const nr::test::CaseRegistrar compileCachePatchesRetainedImageStateCase{
    "render graph compile cache keys retained image access and patches current state refs", [] {
        auto cache = nr::renderer::RenderGraphCompileCache{};
        auto previousState = nr::renderer::RetainedImageState{
            .common =
                nr::renderer::RetainedExternalResourceState{
                    .initialized = true,
                    .ownership = nr::renderer::ResourceOwnershipDomain::Compute,
                    .access =
                        nr::renderer::AccessScope{
                            .stages = vk::PipelineStageFlagBits2::eTransfer,
                            .access = vk::AccessFlagBits2::eTransferRead,
                        },
                },
            .layout = nr::renderer::ImageLayoutIntent::TransferSrc,
        };
        auto currentState = previousState;
        auto changedAccessState = previousState;
        changedAccessState.common.access = nr::renderer::AccessScope{
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
        nr::test::require(compiled.resources.front().retainedState.has_value(),
                          "cache hit should keep retained state ref");
        nr::test::require(std::addressof(compiled.resources.front().retainedState->get()) ==
                              std::addressof(currentState),
                          "cache hit should patch retained state to the current frame object");

        auto changedFrame = buildRetainedStorageWriteFrame(changedAccessState, "changedAccess");
        static_cast<void>(cache.compileConsumingCached(changedFrame));
        auto changedStats = cache.statistics();
        nr::test::requireEqual(changedStats.hitCount, std::uint64_t{1});
        nr::test::requireEqual(changedStats.missCount, std::uint64_t{2});
    }};

const nr::test::CaseRegistrar compileCacheCanonicalUseAndTimelineCase{
    "render graph compile cache keys canonical uses and ignores retained timeline values", [] {
        auto buildFrame = [](nr::renderer::RetainedBufferState &state, bool duplicateUse, bool orderedUse) {
            auto builder = nr::renderer::RenderGraphBuilder{};
            auto node = builder.addNode("Cache.Canonical", nr::renderer::QueueDomain::Compute);
            auto buffer = builder.addResource(nr::renderer::GraphImportedBufferDesc{
                .debugName = "Cache.Canonical.Buffer",
                .lifetime = nr::renderer::ResourceLifetime::RendererPersistent,
                .initialOwnership = nr::renderer::ResourceOwnershipDomain::Compute,
                .size = 256,
                .usageIntents = {nr::renderer::BufferUsageIntent::StorageRead},
                .retainedState = std::ref(state),
            });
            auto use = nr::renderer::use::storageBufferRead(buffer);
            if (orderedUse)
            {
                use = nr::renderer::use::orderedAfterPrevious(use);
            }
            auto uses = std::vector{use};
            if (duplicateUse)
            {
                uses.push_back(use);
            }
            static_cast<void>(builder.addPass(
                "Cache.Canonical.Pass", node, uses, [](const nr::renderer::PassRecordContext &) {}, nullptr, false,
                vk::PipelineStageFlagBits2::eComputeShader));
            return builder.build();
        };

        auto previousState = nr::renderer::RetainedBufferState{
            .common =
                nr::renderer::RetainedExternalResourceState{
                    .initialized = true,
                    .ownership = nr::renderer::ResourceOwnershipDomain::Compute,
                    .access =
                        nr::renderer::AccessScope{
                            .stages = vk::PipelineStageFlagBits2::eComputeShader,
                            .access = vk::AccessFlagBits2::eShaderStorageRead,
                        },
                    .lastSubmissionTimelineValue = 11u,
                },
        };
        auto currentState = previousState;
        currentState.common.lastSubmissionTimelineValue = 29u;

        auto cache = nr::renderer::RenderGraphCompileCache{};
        auto first = buildFrame(previousState, false, false);
        static_cast<void>(cache.compileConsumingCached(first));

        auto duplicate = buildFrame(currentState, true, false);
        auto compiled = cache.compileConsumingCached(duplicate);
        auto hitStats = cache.statistics();
        nr::test::requireEqual(hitStats.hitCount, std::uint64_t{1});
        nr::test::requireEqual(hitStats.missCount, std::uint64_t{1});
        nr::test::requireEqual(compiled.submitBatches.front().passes.front().resourceUses.size(), std::size_t{1});
        nr::test::require(compiled.resources.front().retainedBufferState.has_value());
        nr::test::requireEqual(compiled.resources.front().retainedBufferState->get().common.lastSubmissionTimelineValue,
                               std::uint64_t{29},
                               "cache hits should patch the current retained timeline source without keying it");

        auto ordered = buildFrame(currentState, false, true);
        static_cast<void>(cache.compileConsumingCached(ordered));
        auto markerStats = cache.statistics();
        nr::test::requireEqual(markerStats.hitCount, std::uint64_t{1});
        nr::test::requireEqual(markerStats.missCount, std::uint64_t{2});
    }};

const nr::test::CaseRegistrar compileCacheStructuralMissCase{
    "render graph compile cache misses structural graph changes", [] {
        requireCompileCacheMissForStructuralChange(
            "extent change", [] { return buildSingleImageFrame(vk::Extent3D{64, 64, 1}, vk::Format::eR8G8B8A8Unorm); },
            [] { return buildSingleImageFrame(vk::Extent3D{128, 64, 1}, vk::Format::eR8G8B8A8Unorm); });
        requireCompileCacheMissForStructuralChange(
            "format change", [] { return buildSingleImageFrame(vk::Extent3D{64, 64, 1}, vk::Format::eR8G8B8A8Unorm); },
            [] { return buildSingleImageFrame(vk::Extent3D{64, 64, 1}, vk::Format::eR16G16B16A16Sfloat); });
        requireCompileCacheMissForStructuralChange(
            "resource use change", [] { return buildResourceUseFrame(false); },
            [] { return buildResourceUseFrame(true); });
        requireCompileCacheMissForStructuralChange(
            "resource use aspect change",
            [] {
                return buildSampledUseSignatureFrame(nr::renderer::ImageAspectIntent::Depth,
                                                     nr::renderer::ShaderStageIntent::Fragment);
            },
            [] {
                return buildSampledUseSignatureFrame(nr::renderer::ImageAspectIntent::Stencil,
                                                     nr::renderer::ShaderStageIntent::Fragment);
            });
        requireCompileCacheMissForStructuralChange(
            "resource use shader stage change",
            [] {
                return buildSampledUseSignatureFrame(nr::renderer::ImageAspectIntent::Depth,
                                                     nr::renderer::ShaderStageIntent::Vertex);
            },
            [] {
                return buildSampledUseSignatureFrame(nr::renderer::ImageAspectIntent::Depth,
                                                     nr::renderer::ShaderStageIntent::Fragment);
            });
        requireCompileCacheMissForStructuralChange(
            "resource use ownership-domain change", [] { return buildSwapchainFrame("implicit", false); },
            [] { return buildSwapchainFrame("explicit", true); });
        requireCompileCacheMissForStructuralChange(
            "frame data use change", [] { return buildFrameDataUseFrame(false); },
            [] { return buildFrameDataUseFrame(true); });
        requireCompileCacheMissForStructuralChange(
            "pass order change", [] { return buildPassOrderFrame(false); }, [] { return buildPassOrderFrame(true); });
        requireCompileCacheMissForStructuralChange(
            "submit order change", [] { return buildSubmitOrderFrame(false); },
            [] { return buildSubmitOrderFrame(true); });
        requireCompileCacheMissForStructuralChange(
            "acceleration structure size change", [] { return buildAccelerationStructureFrameWithSize(8192); },
            [] { return buildAccelerationStructureFrameWithSize(16384); });
    }};

const nr::test::CaseRegistrar compileCacheDebugNameSwapchainIndexHitCase{
    "render graph compile cache ignores debug names for late-bound swapchain resources", [] {
        auto cache = nr::renderer::RenderGraphCompileCache{};
        auto first = buildSwapchainFrame("first");
        static_cast<void>(cache.compileConsumingCached(first));
        auto second = buildSwapchainFrame("second");
        auto compiled = cache.compileConsumingCached(second);

        auto stats = cache.statistics();
        nr::test::requireEqual(stats.hitCount, std::uint64_t{1});
        nr::test::requireEqual(stats.missCount, std::uint64_t{1});
        nr::test::requireEqual(compiled.resources.front().debugName, std::string{"Swapchain.Image.second"});
        nr::test::requireEqual(compiled.submitBatches.front().passes.front().debugName,
                               std::string{"Swapchain.Pass.second"});
    }};

const nr::test::CaseRegistrar compileCacheCopyPayloadPatchCase{
    "render graph compile cache patches copy pass payloads on cache hit", [] {
        auto makeCopyFrame = [](std::string_view debugSuffix, vk::DeviceSize sourceOffset) {
            auto builder = nr::renderer::RenderGraphBuilder{};
            auto node = builder.addNode(std::format("Copy.Node.{}", debugSuffix), nr::renderer::QueueDomain::Compute);
            auto source = builder.addResource(nr::renderer::GraphTransientBufferDesc{
                .debugName = std::format("Copy.Source.{}", debugSuffix),
                .size = 256,
            });
            auto destination = builder.addResource(nr::renderer::GraphTransientBufferDesc{
                .debugName = std::format("Copy.Destination.{}", debugSuffix),
                .size = 256,
            });
            static_cast<void>(builder.addCopyPass(std::format("Copy.Pass.{}", debugSuffix), node,
                                                  nr::renderer::CopyBufferToBufferPassDesc{
                                                      .source = source,
                                                      .destination = destination,
                                                      .region = vk::BufferCopy{sourceOffset, 0, 64},
                                                  }));
            return builder.build();
        };

        auto cache = nr::renderer::RenderGraphCompileCache{};
        auto first = makeCopyFrame("first", 0);
        static_cast<void>(cache.compileConsumingCached(first));

        auto second = makeCopyFrame("second", 0);
        auto compiledHit = cache.compileConsumingCached(second);
        auto statsAfterHit = cache.statistics();
        nr::test::requireEqual(statsAfterHit.hitCount, std::uint64_t{1});
        nr::test::requireEqual(statsAfterHit.missCount, std::uint64_t{1});
        auto const &hitPass = compiledHit.submitBatches.front().passes.front();
        nr::test::requireEqual(hitPass.debugName, std::string{"Copy.Pass.second"});
        nr::test::require(hitPass.copy.has_value(), "cached copy pass should keep current copy metadata");
        auto const *hitCopy = std::get_if<nr::renderer::CopyBufferToBufferPassDesc>(&*hitPass.copy);
        nr::test::require(hitCopy != nullptr, "cached copy payload should retain buffer-to-buffer type");
        nr::test::requireEqual(hitCopy->region.size, vk::DeviceSize{64});

        auto changedRegion = makeCopyFrame("changed", 16);
        static_cast<void>(cache.compileConsumingCached(changedRegion));
        auto statsAfterMiss = cache.statistics();
        nr::test::requireEqual(statsAfterMiss.hitCount, std::uint64_t{1});
        nr::test::requireEqual(statsAfterMiss.missCount, std::uint64_t{2});
    }};

const nr::test::CaseRegistrar bindlessImageTableCacheCase{
    "renderer bindless image table cache tracks versions fallback removals and reallocations", [] {
        auto pipeline = makeFakeUiBindlessPipeline(8u);
        auto cache = nr::renderer::BindlessImageTableCache{};
        auto const passBinding = pipeline.passBinding(0u);

        auto firstSnapshot =
            cache.makeSnapshotForFrame(pipeline, passBinding, 0u,
                                       makeBindlessCacheRequest(1u, 4u,
                                                                {
                                                                    {1u, logicalTextureDescriptor(101u, "texture-1")},
                                                                }));
        nr::test::requireEqual(firstSnapshot.descriptorWriteCount(), std::size_t{4});
        nr::test::requireEqual(logicalResourceIdForArrayElement(firstSnapshot, 0u), std::uint64_t{100});
        nr::test::requireEqual(logicalResourceIdForArrayElement(firstSnapshot, 1u), std::uint64_t{101});

        auto cachedSnapshot =
            cache.makeSnapshotForFrame(pipeline, passBinding, 0u,
                                       makeBindlessCacheRequest(1u, 4u,
                                                                {
                                                                    {1u, logicalTextureDescriptor(101u, "texture-1")},
                                                                }));
        nr::test::require(cachedSnapshot.empty(), "same bindless table version should not emit a snapshot");

        auto refreshedRequest = makeBindlessCacheRequest(1u, 4u,
                                                         {
                                                             {1u, logicalTextureDescriptor(101u, "texture-1")},
                                                         });
        refreshedRequest.refreshActiveDescriptorsOnCacheHit = true;
        auto refreshedSnapshot = cache.makeSnapshotForFrame(pipeline, passBinding, 0u, refreshedRequest);
        nr::test::requireEqual(refreshedSnapshot.descriptorWriteCount(), std::size_t{1});
        nr::test::requireEqual(logicalResourceIdForArrayElement(refreshedSnapshot, 1u), std::uint64_t{101});
        nr::test::require(forceWriteForArrayElement(refreshedSnapshot, 1u),
                          "cache-hit active descriptor refresh should force descriptor writes");

        auto updatedSnapshot = cache.makeSnapshotForFrame(
            pipeline, passBinding, 0u,
            makeBindlessCacheRequest(2u, 4u,
                                     {
                                         {1u, logicalTextureDescriptor(111u, "texture-1-updated")},
                                         {2u, logicalTextureDescriptor(102u, "texture-2")},
                                     }));
        nr::test::requireEqual(updatedSnapshot.descriptorWriteCount(), std::size_t{2});
        nr::test::requireEqual(logicalResourceIdForArrayElement(updatedSnapshot, 1u), std::uint64_t{111});
        nr::test::requireEqual(logicalResourceIdForArrayElement(updatedSnapshot, 2u), std::uint64_t{102});

        auto removedSnapshot = cache.makeSnapshotForFrame(
            pipeline, passBinding, 0u,
            makeBindlessCacheRequest(3u, 4u,
                                     {
                                         {1u, logicalTextureDescriptor(111u, "texture-1-updated")},
                                     }));
        nr::test::requireEqual(removedSnapshot.descriptorWriteCount(), std::size_t{2});
        nr::test::requireEqual(logicalResourceIdForArrayElement(removedSnapshot, 1u), std::uint64_t{111});
        nr::test::requireEqual(logicalResourceIdForArrayElement(removedSnapshot, 2u), std::uint64_t{100});

        pipeline.forceReallocation = true;
        auto reallocatedSnapshot = cache.makeSnapshotForFrame(
            pipeline, passBinding, 0u,
            makeBindlessCacheRequest(3u, 6u,
                                     {
                                         {1u, logicalTextureDescriptor(111u, "texture-1-updated")},
                                     }));
        nr::test::requireEqual(reallocatedSnapshot.descriptorWriteCount(), std::size_t{6});
        nr::test::requireEqual(logicalResourceIdForArrayElement(reallocatedSnapshot, 5u), std::uint64_t{100});
    }};

const nr::test::CaseRegistrar bindlessImageTablePassOwnerIsolationCase{
    "renderer bindless image table cache isolates applied versions by pass owner", [] {
        auto pipeline = makeFakeUiBindlessPipeline(8u);
        auto cache = nr::renderer::BindlessImageTableCache{};
        auto const passA = pipeline.passBinding(0u);
        auto const passB = pipeline.passBinding(1u);
        auto request = makeBindlessCacheRequest(7u, 4u,
                                                {
                                                    {1u, logicalTextureDescriptor(101u, "texture-1")},
                                                });

        auto passAFirst = cache.makeSnapshotForFrame(pipeline, passA, 0u, request);
        auto passAHit = cache.makeSnapshotForFrame(pipeline, passA, 0u, request);
        auto passBFirst = cache.makeSnapshotForFrame(pipeline, passB, 0u, request);

        nr::test::requireEqual(passAFirst.descriptorWriteCount(), std::size_t{4});
        nr::test::require(passAHit.empty(), "one pass owner should suppress its own applied table version");
        nr::test::requireEqual(passBFirst.descriptorWriteCount(), std::size_t{4});
    }};

const nr::test::CaseRegistrar bindlessImageTableFrameSlotIsolationCase{
    "renderer bindless image table cache follows the pipeline frame-slot domain", [] {
        auto pipeline = makeFakeUiBindlessPipeline(8u);
        pipeline.frameSlotCount = nr::maxFrameInFlight + 1u;
        auto cache = nr::renderer::BindlessImageTableCache{};
        auto const passBinding = pipeline.passBinding(0u);
        auto request = makeBindlessCacheRequest(7u, 4u,
                                                {
                                                    {1u, logicalTextureDescriptor(101u, "texture-1")},
                                                });

        auto frameZero = cache.makeSnapshotForFrame(pipeline, passBinding, 0u, request);
        auto extendedFrameSlot = cache.makeSnapshotForFrame(
            pipeline, passBinding, static_cast<std::uint32_t>(nr::maxFrameInFlight), request);
        auto extendedFrameSlotHit = cache.makeSnapshotForFrame(
            pipeline, passBinding, static_cast<std::uint32_t>(nr::maxFrameInFlight), request);
        auto frameZeroHit = cache.makeSnapshotForFrame(pipeline, passBinding, 0u, request);

        nr::test::requireEqual(frameZero.descriptorWriteCount(), std::size_t{4});
        nr::test::requireEqual(extendedFrameSlot.descriptorWriteCount(), std::size_t{4});
        nr::test::require(extendedFrameSlotHit.empty(), "one extended frame slot should suppress its own version");
        nr::test::require(frameZeroHit.empty(), "another frame slot should retain its independent applied version");
    }};

const nr::test::CaseRegistrar bindlessImageTableOwnerWideReallocationInvalidationCase{
    "renderer bindless image table cache invalidates every table for a reallocated pass owner frame", [] {
        auto pipeline = makeFakeTwoTableBindlessPipeline(8u);
        auto cache = nr::renderer::BindlessImageTableCache{};
        auto const passBinding = pipeline.passBinding(0u);
        auto tableA = makeBindlessCacheRequest(3u, 4u,
                                               {
                                                   {1u, logicalTextureDescriptor(101u, "texture-a")},
                                               });
        tableA.tableKey = "test.table-a";
        tableA.shaderSymbol = "gTableA";
        auto tableB = makeBindlessCacheRequest(5u, 4u,
                                               {
                                                   {2u, logicalTextureDescriptor(102u, "texture-b")},
                                               });
        tableB.tableKey = "test.table-b";
        tableB.shaderSymbol = "gTableB";
        tableB.expectedSet = 2u;
        tableB.expectedDescriptorType = vk::DescriptorType::eStorageImage;
        tableB.sampler = vk::Sampler{};
        std::ranges::for_each(tableB.descriptorsById,
                              [](auto &entry) { entry.second.layout = vk::ImageLayout::eGeneral; });
        tableB.fallbackDescriptor->layout = vk::ImageLayout::eGeneral;

        auto tableAFirst = cache.makeSnapshotForFrame(pipeline, passBinding, 0u, tableA);
        auto tableBFirst = cache.makeSnapshotForFrame(pipeline, passBinding, 0u, tableB);
        nr::test::requireEqual(tableAFirst.descriptorWriteCount(), std::size_t{4});
        nr::test::requireEqual(tableBFirst.descriptorWriteCount(), std::size_t{4});

        tableA.descriptorCapacity = 6u;
        auto tableAReallocated = cache.makeSnapshotForFrame(pipeline, passBinding, 0u, tableA);
        nr::test::requireEqual(tableAReallocated.descriptorWriteCount(), std::size_t{6});

        auto tableBAfterOwnerReallocation = cache.makeSnapshotForFrame(pipeline, passBinding, 0u, tableB);
        nr::test::requireEqual(tableBAfterOwnerReallocation.descriptorWriteCount(), std::size_t{4});
        auto tableAStableHit = cache.makeSnapshotForFrame(pipeline, passBinding, 0u, tableA);
        nr::test::require(tableAStableHit.empty(),
                          "consecutive two-table ensures should retain both variable descriptor counts without churn");
        nr::test::requireEqual(pipeline.reallocationCount, std::size_t{3});
        nr::test::require(pipeline.variableCountsByOwner[passBinding.stateIndex][0u] ==
                              std::map<std::uint32_t, std::uint32_t>{{1u, 6u}, {2u, 4u}},
                          "both variable descriptor counts should remain active for the pass owner frame");
    }};

const nr::test::CaseRegistrar bindlessImageTableOptionalMissingSymbolCase{
    "renderer bindless image table cache accepts optional missing shader symbols", [] {
        auto pipeline = makeFakeUiBindlessPipeline(4u);
        auto cache = nr::renderer::BindlessImageTableCache{};
        auto const passBinding = pipeline.passBinding(0u);
        auto request = makeBindlessCacheRequest(1u, 4u,
                                                {
                                                    {1u, logicalTextureDescriptor(101u, "texture-1")},
                                                });
        request.tableKey = "test.missing";
        request.shaderSymbol = "gMissingTextures";
        request.requirement = nr::renderer::BindlessImageTableRequirement::optional;

        auto snapshot = cache.makeSnapshotForFrame(pipeline, passBinding, 0u, request);
        nr::test::require(snapshot.empty(), "optional missing bindless table should produce no descriptor writes");
    }};

} // namespace
