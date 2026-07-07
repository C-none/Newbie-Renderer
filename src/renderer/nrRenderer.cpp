module nr.renderer;
import :renderer;
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

[[nodiscard]] bool finiteVec3(const glm::vec3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool finiteVec4(const glm::vec4& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
}

[[nodiscard]] bool finiteMat4(const glm::mat4& value) noexcept
{
    auto columns = std::views::iota(std::size_t{0}, std::size_t{4});
    return std::ranges::all_of(columns, [&](std::size_t column) {
        return finiteVec4(value[column]);
    });
}

[[nodiscard]] bool nearlyEqual(float left, float right, float epsilon) noexcept
{
    return std::abs(left - right) <= epsilon;
}

[[nodiscard]] RendererGlobalFrameUniforms makeGlobalFrameUniforms(
    const nr::scene::SceneBridgeFrameConstants& frameConstants,
    const nr::scene::SceneBridgeFrameConstants& previousFrameConstants,
    std::uint32_t frameIndex,
    std::uint64_t sampleFrameOrdinal) noexcept
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

[[nodiscard]] double elapsedMilliseconds(
    std::chrono::steady_clock::time_point begin,
    std::chrono::steady_clock::time_point end) noexcept
{
        return std::chrono::duration<double, std::milli>(end - begin).count();
    }

void accumulateCpuTimings(RendererCpuFrameTimings& target, const RendererCpuFrameTimings& sample) noexcept
{
        target.cpuWaitGpuMilliseconds += sample.cpuWaitGpuMilliseconds;
        target.frameSetupMilliseconds += sample.frameSetupMilliseconds;
        target.sceneMilliseconds += sample.sceneMilliseconds;
        target.buildMilliseconds += sample.buildMilliseconds;
        target.compileMilliseconds += sample.compileMilliseconds;
        target.prepareMilliseconds += sample.prepareMilliseconds;
        target.executeMilliseconds += sample.executeMilliseconds;
        target.presentMilliseconds += sample.presentMilliseconds;
        target.totalMilliseconds += sample.totalMilliseconds;
    }

[[nodiscard]] RendererCpuFrameTimings averageCpuTimings(
    const RendererCpuFrameTimings& total,
    std::uint32_t frameCount) noexcept
{
        auto const divisor = static_cast<double>(std::max(frameCount, 1u));
        return RendererCpuFrameTimings{
            .cpuWaitGpuMilliseconds = total.cpuWaitGpuMilliseconds / divisor,
            .frameSetupMilliseconds = total.frameSetupMilliseconds / divisor,
            .sceneMilliseconds = total.sceneMilliseconds / divisor,
            .buildMilliseconds = total.buildMilliseconds / divisor,
            .compileMilliseconds = total.compileMilliseconds / divisor,
            .prepareMilliseconds = total.prepareMilliseconds / divisor,
            .executeMilliseconds = total.executeMilliseconds / divisor,
            .presentMilliseconds = total.presentMilliseconds / divisor,
            .totalMilliseconds = total.totalMilliseconds / divisor,
        };
    }

[[nodiscard]] std::vector<RendererGpuPassAverage> averageGpuPassTimings(
        const std::map<std::pair<std::uint32_t, std::string>, RendererGpuPassAverage>& totals)
{
        auto averages = totals |
                        std::views::values |
                        std::views::transform([](const RendererGpuPassAverage& total) {
                            auto average = total;
                            auto const divisor = static_cast<double>(std::max(average.sampleCount, 1u));
                            average.milliseconds /= divisor;
                            return average;
                        }) |
                        std::ranges::to<std::vector>();

        std::ranges::sort(averages, [](const RendererGpuPassAverage& lhs, const RendererGpuPassAverage& rhs) {
            return std::tie(lhs.pass.value, lhs.debugName) < std::tie(rhs.pass.value, rhs.debugName);
        });
        return averages;
    }

[[nodiscard]] std::optional<nr::scene::SceneMaterialTextureIds> collectSceneMaterialTextureIds(
    const nr::scene::Scene& scene,
    nr::resource::MaterialHandle materialHandle,
    std::map<std::uint32_t, nr::resource::TextureHandle>& sceneTextureHandlesById)
{
    auto materialRecordRef = scene.tryGetMaterialAsset(materialHandle);
    nrAssert(
        materialRecordRef.has_value(),
        std::format(
            "Renderer scene texture collection expected material handle (slot={}, generation={}) to resolve.",
            materialHandle.slot,
            materialHandle.generation));

    auto const& materialRecord = materialRecordRef->get();
    nrAssert(
        materialRecord.cpuReady,
        std::format(
            "Renderer scene texture collection expected material '{}' to be CPU ready.",
            materialRecord.cpu.name));

    auto textureIds = nr::scene::SceneMaterialTextureIds{};
    auto slotIndices = std::views::iota(std::size_t{0}, materialRecord.cpu.textureSlots.size());
    std::ranges::for_each(slotIndices, [&](std::size_t slotIndex) {
        auto textureHandle = materialRecord.cpu.textureSlots[slotIndex].texture;
        if (!textureHandle.valid())
        {
            return;
        }

        auto binding = scene.tryGetSampledTextureBinding(textureHandle);
        nrAssert(
            binding.has_value(),
            std::format(
                "Renderer scene texture collection expected resident sampled texture for material '{}' slot {}.",
                materialRecord.cpu.name,
                slotIndex));
        nrAssert(
            binding->descriptorIndex < kSceneTextureDescriptorCapacity,
            std::format(
                "Scene texture descriptor id {} exceeds capacity {}.",
                binding->descriptorIndex,
                kSceneTextureDescriptorCapacity));
        nrAssert(
            binding->descriptorIndex <= nr::scene::kMaxSceneTextureId,
            std::format(
                "Scene texture descriptor id {} exceeds packed uint16 id capacity {}.",
                binding->descriptorIndex,
                nr::scene::kMaxSceneTextureId));

        auto const textureId = static_cast<nr::scene::SceneTextureId>(binding->descriptorIndex);
        textureIds[slotIndex] = textureId;
        sceneTextureHandlesById.insert_or_assign(binding->descriptorIndex, textureHandle);
    });
    return textureIds;
}

void collectTlasSceneTextureHandles(
    const nr::scene::Scene& scene,
    std::span<const nr::scene::TlasBuildInputPacket> tlasPackets,
    std::map<std::uint32_t, nr::resource::TextureHandle>& sceneTextureHandlesById)
{
    std::ranges::for_each(tlasPackets, [&](const nr::scene::TlasBuildInputPacket& packet) {
        auto meshRecordRef = scene.tryGetMeshAsset(packet.mesh);
        nrAssert(
            meshRecordRef.has_value(),
            std::format(
                "Renderer TLAS texture collection expected mesh handle (slot={}, generation={}) to resolve.",
                packet.mesh.slot,
                packet.mesh.generation));
        auto const& meshRecord = meshRecordRef->get();
        nrAssert(
            meshRecord.cpuReady,
            std::format(
                "Renderer TLAS texture collection expected mesh handle (slot={}, generation={}) to be CPU ready.",
                packet.mesh.slot,
                packet.mesh.generation));

        std::ranges::for_each(meshRecord.cpu.geometries, [&](const nr::resource::MeshGeometry& geometry) {
            if (geometry.material.valid())
            {
                static_cast<void>(collectSceneMaterialTextureIds(scene, geometry.material, sceneTextureHandlesById));
            }
        });
    });
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

[[nodiscard]] RendererCameraJitterSample makeHalton23CameraJitterSample(
    std::uint64_t stableFrameOrdinal,
    vk::Extent2D viewportExtent,
    std::uint32_t cycleLength) noexcept
{
    auto const extent = sanitizeViewportExtent(viewportExtent);
    auto const cycle = std::max(1u, cycleLength);
    auto const sampleIndex =
        static_cast<std::uint32_t>(stableFrameOrdinal % static_cast<std::uint64_t>(cycle)) + 1u;
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

[[nodiscard]] glm::mat4 applyCameraProjectionJitter(
    const glm::mat4& projection,
    glm::vec2 ndcOffset) noexcept
{
    auto result = projection;
    result[2][0] -= ndcOffset.x;
    result[2][1] -= ndcOffset.y;
    return result;
}

[[nodiscard]] RendererCameraStabilityKey makeRendererCameraStabilityKey(
    const nr::scene::SceneBridgeFrameConstants& frameConstants,
    vk::Extent2D viewportExtent) noexcept
{
    return RendererCameraStabilityKey{
        .view = frameConstants.view,
        .projection = frameConstants.projection,
        .cameraWorld = frameConstants.cameraWorld,
        .viewportExtent = sanitizeViewportExtent(viewportExtent),
    };
}

[[nodiscard]] bool rendererCameraStabilityKeysEquivalent(
    const RendererCameraStabilityKey& left,
    const RendererCameraStabilityKey& right,
    float epsilon) noexcept
{
    if (left.viewportExtent != right.viewportExtent)
    {
        return false;
    }

    if (!finiteMat4(left.view) || !finiteMat4(right.view) ||
        !finiteMat4(left.projection) || !finiteMat4(right.projection) ||
        !finiteVec3(left.cameraWorld) || !finiteVec3(right.cameraWorld))
    {
        return false;
    }

    auto rows = std::views::iota(std::size_t{0}, std::size_t{4});
    auto columns = std::views::iota(std::size_t{0}, std::size_t{4});
    auto const matricesMatch = std::ranges::all_of(columns, [&](std::size_t column) {
        return std::ranges::all_of(rows, [&](std::size_t row) {
            return nearlyEqual(left.view[column][row], right.view[column][row], epsilon) &&
                   nearlyEqual(left.projection[column][row], right.projection[column][row], epsilon);
        });
    });
    if (!matricesMatch)
    {
        return false;
    }

    return nearlyEqual(left.cameraWorld.x, right.cameraWorld.x, epsilon) &&
           nearlyEqual(left.cameraWorld.y, right.cameraWorld.y, epsilon) &&
           nearlyEqual(left.cameraWorld.z, right.cameraWorld.z, epsilon);
}

[[nodiscard]] std::optional<NodeImageResourceDesc> describeGraphImageResource(const GraphResourceDesc& resource)
{
    return std::visit(
        [](const auto& desc) -> std::optional<NodeImageResourceDesc> {
            using DescT = std::remove_cvref_t<decltype(desc)>;
            if constexpr (std::same_as<DescT, GraphImportedImageDesc> ||
                          std::same_as<DescT, GraphTransientImageDesc>)
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

NodeUiBuildContext::NodeUiBuildContext(
    std::string_view runtimeName,
    std::vector<NodeUiSection>& sections)
    : runtimeName_{runtimeName},
      sections_{sections}
{
    nrAssert(!runtimeName_.empty(), "NodeUiBuildContext requires a non-empty runtime name.");
}

[[nodiscard]] std::string_view NodeUiBuildContext::runtimeName() const noexcept
{
    return runtimeName_;
}

[[nodiscard]] std::string NodeUiBuildContext::makeSectionId(std::string_view localId) const
{
    nrAssert(!localId.empty(), "NodeUiBuildContext::makeSectionId requires a non-empty local id.");
    return std::format("{}.{}", runtimeName_, localId);
}

void NodeUiBuildContext::addSection(
    std::string_view title,
    NodeUiSectionDrawCallback draw,
    bool defaultOpen,
    std::string_view localId)
{
    nrAssert(!title.empty(), "NodeUiBuildContext::addSection requires a non-empty title.");
    nrAssert(static_cast<bool>(draw), "NodeUiBuildContext::addSection requires a draw callback.");

    auto const sectionOrdinal = nextSectionOrdinal_++;
    auto sectionId = localId.empty()
                         ? std::format("{}.{}", runtimeName_, sectionOrdinal)
                         : makeSectionId(localId);

    sections_.get().push_back(NodeUiSection{
        .id = std::move(sectionId),
        .title = std::string{title},
        .draw = std::move(draw),
        .defaultOpen = defaultOpen,
    });
}

namespace
{
template <typename TValue>
[[nodiscard]] TValue variantSnapshotValueOr(
    const VariantItemSnapshot& snapshot,
    TValue fallback)
{
    auto const* value = std::get_if<TValue>(std::addressof(snapshot.value));
    return value != nullptr ? *value : fallback;
}

[[nodiscard]] std::string variantChoicePreview(const VariantItemSnapshot& snapshot)
{
    auto const value = variantSnapshotValueOr<std::string>(snapshot, {});
    auto choiceIt = std::ranges::find_if(snapshot.desc.shader.stringChoices, [&](const nr::rhi::ShaderVariantStringChoice& choice) {
        return choice.value == value;
    });
    if (choiceIt == std::ranges::end(snapshot.desc.shader.stringChoices))
    {
        return value;
    }
    return choiceIt->label.empty() ? choiceIt->value : choiceIt->label;
}

void drawVariantSnapshot(
    VariantStateRegistry& variants,
    std::string_view runtimeName,
    const VariantItemSnapshot& snapshot,
    NodeUiWriter& ui)
{
    auto const& shader = snapshot.desc.shader;
    auto const label = shader.label.empty() ? shader.id : shader.label;
    switch (shader.kind)
    {
    case nr::rhi::ShaderVariantValueKind::Bool:
    {
        auto value = variantSnapshotValueOr<bool>(snapshot, false);
        if (ui.checkbox(label, value))
        {
            static_cast<void>(variants.submitPatch(runtimeName, shader.id, value, VariantWriteSource::Ui));
        }
        return;
    }
    case nr::rhi::ShaderVariantValueKind::Int32:
    {
        auto value = variantSnapshotValueOr<std::int32_t>(snapshot, {});
        auto const minValue = shader.numericRange.bounded
                                  ? static_cast<std::int32_t>(shader.numericRange.minValue)
                                  : std::numeric_limits<std::int32_t>::lowest();
        auto const maxValue = shader.numericRange.bounded
                                  ? static_cast<std::int32_t>(shader.numericRange.maxValue)
                                  : std::numeric_limits<std::int32_t>::max();
        if (ui.inputInt32(label, value, minValue, maxValue))
        {
            static_cast<void>(variants.submitPatch(runtimeName, shader.id, value, VariantWriteSource::Ui));
        }
        return;
    }
    case nr::rhi::ShaderVariantValueKind::UInt32:
    {
        auto value = variantSnapshotValueOr<std::uint32_t>(snapshot, {});
        auto const minValue = shader.numericRange.bounded
                                  ? static_cast<std::uint32_t>(std::max(0.0, shader.numericRange.minValue))
                                  : std::uint32_t{0};
        auto const maxValue = shader.numericRange.bounded
                                  ? static_cast<std::uint32_t>(std::max(0.0, shader.numericRange.maxValue))
                                  : std::numeric_limits<std::uint32_t>::max();
        auto const changed = snapshot.desc.effect != VariantItemEffect::RuntimeOnly &&
                             shader.numericRange.bounded &&
                             maxValue <= static_cast<std::uint32_t>(std::numeric_limits<int>::max())
                                 ? ui.sliderUInt(label, value, minValue, maxValue)
                                 : ui.inputUInt(label, value, minValue, maxValue);
        if (changed)
        {
            static_cast<void>(variants.submitPatch(runtimeName, shader.id, value, VariantWriteSource::Ui));
        }
        return;
    }
    case nr::rhi::ShaderVariantValueKind::Float32:
    {
        auto value = variantSnapshotValueOr<float>(snapshot, {});
        auto const minValue = shader.numericRange.bounded
                                  ? static_cast<float>(shader.numericRange.minValue)
                                  : std::numeric_limits<float>::lowest();
        auto const maxValue = shader.numericRange.bounded
                                  ? static_cast<float>(shader.numericRange.maxValue)
                                  : std::numeric_limits<float>::max();
        auto const changed = shader.numericRange.bounded
                                 ? ui.sliderFloat(label, value, minValue, maxValue)
                                 : ui.inputFloat(label, value, minValue, maxValue);
        if (changed)
        {
            static_cast<void>(variants.submitPatch(runtimeName, shader.id, value, VariantWriteSource::Ui));
        }
        return;
    }
    case nr::rhi::ShaderVariantValueKind::String:
    {
        auto current = variantSnapshotValueOr<std::string>(snapshot, {});
        if (!ui.beginCombo(label, variantChoicePreview(snapshot)))
        {
            return;
        }
        std::ranges::for_each(shader.stringChoices, [&](const nr::rhi::ShaderVariantStringChoice& choice) {
            auto const choiceLabel = choice.label.empty() ? choice.value : choice.label;
            auto const selected = choice.value == current;
            if (ui.selectable(choiceLabel, selected))
            {
                current = choice.value;
                static_cast<void>(variants.submitPatch(runtimeName, shader.id, current, VariantWriteSource::Ui));
            }
        });
        ui.endCombo();
        return;
    }
    }
}

void collectVariantUiSections(
    VariantStateRegistry& variants,
    std::string_view runtimeName,
    NodeUiBuildContext& context)
{
    auto snapshots = variants.snapshot(runtimeName);
    std::erase_if(snapshots, [](const VariantItemSnapshot& snapshot) {
        return !snapshot.desc.uiVisible;
    });
    if (snapshots.empty())
    {
        return;
    }

    context.addSection(
        runtimeName,
        [runtimeName = std::string{runtimeName}, &variants](NodeUiWriter& ui) {
            auto currentSnapshots = variants.snapshot(runtimeName);
            std::erase_if(currentSnapshots, [](const VariantItemSnapshot& snapshot) {
                return !snapshot.desc.uiVisible;
            });
            std::ranges::for_each(currentSnapshots, [&](const VariantItemSnapshot& snapshot) {
                drawVariantSnapshot(variants, runtimeName, snapshot, ui);
            });
        },
        true,
        "variants");
}
} // namespace

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

[[nodiscard]] GraphResourceHandle NodeBuildContext::requireFrameResource(
        std::string_view key,
        std::string_view consumerDebugName) const
{
        auto resource = resolveFrameResource(key);
        nrAssert(
            resource.valid(),
            std::format("{} requires frame resource '{}', but it has not been published.", consumerDebugName, key));
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

[[nodiscard]] GraphFrameDataHandle NodeBuildContext::requireFrameData(
        std::string_view key,
        std::string_view consumerDebugName) const
{
        auto frameData = resolveFrameData(key);
        nrAssert(
            frameData.valid(),
            std::format("{} requires frame data '{}', but it has not been published.", consumerDebugName, key));
        return frameData;
    }

[[nodiscard]] std::optional<std::reference_wrapper<const std::any>> NodeBuildContext::resolveFrameDataPayload(
        GraphFrameDataHandle handle) const
{
        if (!handle.valid())
        {
            return {};
        }

        auto const& frameData = graphBuilder.get().frame().frameData;
        auto const frameDataIt = std::ranges::find_if(frameData, [handle](const GraphFrameDataDesc& desc) {
            return desc.handle == handle;
        });
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

        auto const& resources = graphBuilder.get().frame().resources;
        auto const resourceIt = std::ranges::find_if(resources, [resource](const GraphResourceDesc& desc) {
            return desc.handle == resource;
        });
        if (resourceIt == resources.end())
        {
            return std::nullopt;
        }

        return describeGraphImageResource(*resourceIt);
    }

[[nodiscard]] GraphResourceHandle NodeBuildContext::transientColor(
        std::string_view debugName,
        vk::Extent2D extent,
        vk::Format format)
{
        return addResource(GraphTransientImageDesc{
            .debugName = std::string(debugName),
            .extent = vk::Extent3D{extent.width, extent.height, 1},
            .format = format,
            .usageIntents = {
                ImageUsageIntent::ColorAttachment,
                ImageUsageIntent::TransferSrc,
                ImageUsageIntent::Sampled,
            },
            .initialLayout = ImageLayoutIntent::ColorAttachment,
        });
    }

[[nodiscard]] GraphResourceHandle NodeBuildContext::importColor(
        const nr::rhi::Image& image,
        std::string_view debugName,
        vk::Extent2D extent,
        vk::Format format,
        ResourceLifetime lifetime)
{
        return importImage(
            image,
            debugName,
            extent,
            format,
            lifetime,
            {
                ImageUsageIntent::ColorAttachment,
                ImageUsageIntent::TransferSrc,
                ImageUsageIntent::Sampled,
            });
    }

[[nodiscard]] GraphResourceHandle NodeBuildContext::importStorageColor(
        const nr::rhi::Image& image,
        std::string_view debugName,
        vk::Extent2D extent,
        vk::Format format,
        ResourceLifetime lifetime)
{
        return importImage(
            image,
            debugName,
            extent,
            format,
            lifetime,
            {
                ImageUsageIntent::StorageWrite,
                ImageUsageIntent::TransferSrc,
            });
    }

[[nodiscard]] GraphResourceHandle NodeBuildContext::importRetainedStorageColor(
        const nr::rhi::Image& image,
        RetainedImageState& state,
        std::string_view debugName,
        vk::Extent2D extent,
        vk::Format format,
        ResourceLifetime lifetime)
{
        nrAssert(image.valid(), std::format("{} image is invalid.", debugName));

        return addResource(GraphImportedImageDesc{
            .debugName = std::string(debugName),
            .lifetime = lifetime,
            .initialOwnership = state.initialized ? state.ownership : ResourceOwnershipDomain::Undefined,
            .extent = vk::Extent3D{extent.width, extent.height, 1},
            .format = format,
            .usageIntents = {
                ImageUsageIntent::StorageWrite,
                ImageUsageIntent::TransferSrc,
            },
            .initialLayout = state.initialized ? state.layout : ImageLayoutIntent::Undefined,
            .initialAccessScope = state.initialized ? state.access : AccessScope{},
            .importedResource = std::cref(image),
            .retainedState = std::ref(state),
        });
    }

[[nodiscard]] GraphResourceHandle NodeBuildContext::importSampledColor(
        const nr::rhi::Image& image,
        std::string_view debugName,
        vk::Extent2D extent,
        vk::Format format,
        ResourceLifetime lifetime)
{
        return importImage(
            image,
            debugName,
            extent,
            format,
            lifetime,
            {
                ImageUsageIntent::ColorAttachment,
                ImageUsageIntent::Sampled,
            });
    }

[[nodiscard]] GraphResourceHandle NodeBuildContext::importSampledImage(
        const nr::rhi::Image& image,
        std::string_view debugName,
        vk::Extent3D extent,
        vk::Format format,
        ResourceLifetime lifetime,
        ResourceOwnershipDomain initialOwnership)
{
        nrAssert(image.valid(), std::format("{} image is invalid.", debugName));

        return addResource(GraphImportedImageDesc{
            .debugName = std::string(debugName),
            .lifetime = lifetime,
            .initialOwnership = initialOwnership,
            .extent = extent,
            .format = format,
            .usageIntents = {
                ImageUsageIntent::Sampled,
            },
            .initialLayout = ImageLayoutIntent::ShaderReadOnly,
            .importedResource = std::cref(image),
        });
    }

[[nodiscard]] GraphResourceHandle NodeBuildContext::importDepth(
        const nr::rhi::Image& image,
        std::string_view debugName,
        vk::Extent2D extent,
        vk::Format format,
        ResourceLifetime lifetime)
{
        return importImage(
            image,
            debugName,
            extent,
            format,
            lifetime,
            {
                ImageUsageIntent::DepthStencilAttachment,
            },
            ImageAspectIntent::Depth);
    }

[[nodiscard]] GraphResourceHandle NodeBuildContext::importBuffer(
        const nr::rhi::Buffer& buffer,
        std::string_view debugName,
        ResourceLifetime lifetime,
        std::initializer_list<BufferUsageIntent> usageIntents,
        ResourceOwnershipDomain initialOwnership)
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

[[nodiscard]] GraphResourceHandle NodeBuildContext::importAccelerationStructure(
        const nr::rhi::AccelerationStructureResource& accelerationStructure,
        std::string_view debugName,
        ResourceLifetime lifetime,
        ResourceOwnershipDomain initialOwnership)
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

[[nodiscard]] GraphResourceHandle NodeBuildContext::importSwapchain(
        std::string_view debugName,
        const NodeFrameParameters& frameParameters)
{
        return addResource(GraphImportedSwapchainImageDesc{
            .debugName = std::string(debugName),
            .swapchainImageIndex = frameParameters.swapchainImageIndex,
            .extent = vk::Extent3D{
                frameParameters.swapchainExtent.width,
                frameParameters.swapchainExtent.height,
                1,
            },
            .format = frameParameters.swapchainFormat,
        });
    }

[[nodiscard]] GraphPassHandle NodeBuildContext::addPass(
        std::span<const PassResourceUseDesc> intentList,
        std::string_view debugName,
        PassRecordCallback executeLambda,
        PassPrepareCallback prepareCallback,
        bool isCopyPass,
        vk::PipelineStageFlags2 shaderStages)
{
        return graphBuilder.get().addPass(
            debugName,
            nodeHandle,
            intentList,
            std::move(executeLambda),
            std::move(prepareCallback),
            isCopyPass,
            shaderStages);
    }

[[nodiscard]] GraphPassHandle NodeBuildContext::addPass(
        std::span<const PassResourceUseDesc> intentList,
        std::string_view debugName,
        PassParallelRecordDesc parallelRecord,
        PassPrepareCallback prepareCallback,
        vk::PipelineStageFlags2 shaderStages)
{
        return graphBuilder.get().addPass(
            debugName,
            nodeHandle,
            intentList,
            std::move(parallelRecord),
            std::move(prepareCallback),
            shaderStages);
    }

[[nodiscard]] GraphSubmitHandle NodeBuildContext::addSubmitNode(
        std::string_view debugName)
{
        return graphBuilder.get().addSubmitNode(debugName);
    }

[[nodiscard]] GraphResourceHandle NodeBuildContext::importImage(
        const nr::rhi::Image& image,
        std::string_view debugName,
        vk::Extent2D extent,
        vk::Format format,
        ResourceLifetime lifetime,
        std::initializer_list<ImageUsageIntent> usageIntents,
        ImageAspectIntent aspect)
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

void FrameUniformArena::initialize(nr::rhi::Device& device, vk::DeviceSize bytesPerFrame, std::string_view debugName)
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

        buffer_ = device.resourceFactory.createBuffer(
            bufferInfo,
            nr::rhi::MemoryUsage::CpuToGpu,
            debugName_);
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

[[nodiscard]] FrameUniformBinding FrameUniformArena::uploadBytes(
        RenderGraphBuilder& graphBuilder,
        std::string_view debugName,
        std::span<const std::byte> bytes)
{
        nrAssert(valid(), "FrameUniformArena::uploadBytes requires initialized uniform buffer.");
        nrAssert(!bytes.empty(), "FrameUniformArena::uploadBytes requires a non-empty payload.");

        auto const range = static_cast<vk::DeviceSize>(bytes.size_bytes());
        nrAssert(
            range <= maxUniformBufferRange_,
            std::format(
                "FrameUniformArena payload exceeds maxUniformBufferRange. debugName='{}' range={} max={}",
                debugName,
                range,
                maxUniformBufferRange_));
        auto const allocationSize = alignUp(range, uniformOffsetAlignment_);
        nrAssert(
            currentFrameCursor_ + allocationSize <= frameSliceSize_,
            std::format(
                "FrameUniformArena frame slice overflow. debugName='{}' cursor={} allocation={} frameSliceSize={}",
                debugName,
                currentFrameCursor_,
                allocationSize,
                frameSliceSize_));

        auto const offset = currentFrameBaseOffset_ + currentFrameCursor_;
        buffer_.writeMappedAndFlush(bytes, offset);
        currentFrameCursor_ += allocationSize;

        auto resource = graphBuilder.addResource(GraphImportedBufferDesc{
            .debugName = std::format("{}@{}", debugName, offset),
            .lifetime = ResourceLifetime::FrameLocal,
            .size = buffer_.size(),
            .usageIntents = {
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

[[nodiscard]] vk::DeviceSize FrameUniformArena::alignUp(vk::DeviceSize value, vk::DeviceSize alignment) noexcept
{
        if (alignment <= 1u)
        {
            return value;
        }
        auto const remainder = value % alignment;
        return remainder == 0u ? value : value + (alignment - remainder);
    }

RasterPassBuilder::RasterPassBuilder(
        NodeBuildContext& context,
        std::string_view debugName,
        std::shared_ptr<PipelineRuntime<nr::rhi::GraphicsPipeline>> runtime)
        : Base(context, debugName, std::move(runtime), "RasterPassBuilder")
{
    }

RasterPassBuilder& RasterPassBuilder::viewport(vk::Extent2D extent)
{
        viewportExtent_ = extent;
        return *this;
    }

RasterPassBuilder& RasterPassBuilder::viewportYMode(RasterViewportYMode mode)
{
        viewportYMode_ = mode;
        return *this;
    }

RasterPassBuilder& RasterPassBuilder::colorAttachment(GraphResourceHandle resource, vk::ClearValue clearValue)
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

RasterPassBuilder& RasterPassBuilder::depthAttachment(GraphResourceHandle resource)
{
        nrAssert(resource.valid(), "RasterPassBuilder::depthAttachment requires a valid graph resource.");
        depthAttachment_ = RasterDepthAttachment{
            .resource = resource,
            .loadOp = vk::AttachmentLoadOp::eClear,
        };
        Base::resourceUse(use::depthReadWrite(resource));
        return *this;
    }

RasterPassBuilder& RasterPassBuilder::rasterState(nr::rhi::MeshRasterState state)
{
        rasterState_ = state;
        return *this;
    }

RasterPassBuilder& RasterPassBuilder::primitiveTopology(vk::PrimitiveTopology topology)
{
        primitiveTopology_ = topology;
        return *this;
    }

RasterPassBuilder& RasterPassBuilder::record(RasterPassRecordCallback callback)
{
        nrAssert(
            !parallelItemCountCallback_ && !parallelRangeRecordCallback_,
            "RasterPassBuilder::record conflicts with recordParallel.");
        recordCallback_ = std::move(callback);
        return *this;
    }

RasterPassBuilder& RasterPassBuilder::recordParallel(
        RasterPassItemCountCallback itemCountCallback,
        RasterPassRangeRecordCallback rangeRecordCallback)
{
        nrAssert(static_cast<bool>(itemCountCallback), "RasterPassBuilder::recordParallel requires an item-count callback.");
        nrAssert(static_cast<bool>(rangeRecordCallback), "RasterPassBuilder::recordParallel requires a range-record callback.");
        nrAssert(!recordCallback_, "RasterPassBuilder::recordParallel conflicts with record.");
        parallelItemCountCallback_ = std::move(itemCountCallback);
        parallelRangeRecordCallback_ = std::move(rangeRecordCallback);
        return *this;
    }

[[nodiscard]] RasterPassBuilder::RasterPassRenderingSetup RasterPassBuilder::makeRenderingSetup(
        const PassRecordContext& recordContext,
        std::span<const RasterColorAttachment> colorAttachments,
        const std::optional<RasterDepthAttachment>& depthAttachment,
        std::optional<vk::Extent2D> viewportExtent,
        std::string_view debugName)
{
        nrAssert(static_cast<bool>(recordContext.resolveImage), "RasterPassBuilder record requires image resolver callback.");

        auto setup = RasterPassRenderingSetup{};
        setup.resolvedColors = colorAttachments |
                               std::views::transform([&](const RasterColorAttachment& attachment) {
                                   auto image = recordContext.resolveImage(attachment.resource);
                                   nrAssert(
                                       image.has_value(),
                                       std::format("RasterPassBuilder failed to resolve color image for pass '{}'.", debugName));
                                   nrAssert(
                                       image->view != vk::ImageView{},
                                       std::format("RasterPassBuilder pass '{}' requires a valid color image view.", debugName));
                                   return *image;
                               }) |
                               std::ranges::to<std::vector>();

        if (depthAttachment.has_value())
        {
            auto depthImage = recordContext.resolveImage(depthAttachment->resource);
            nrAssert(
                depthImage.has_value(),
                std::format("RasterPassBuilder failed to resolve depth image for pass '{}'.", debugName));
            nrAssert(
                depthImage->view != vk::ImageView{},
                std::format("RasterPassBuilder pass '{}' requires a valid depth image view.", debugName));
            setup.resolvedDepth = *depthImage;
        }

        setup.targetExtent = resolveTargetExtent(viewportExtent, setup.resolvedColors, setup.resolvedDepth);
        setup.colorAttachments = std::views::iota(std::size_t{0}, colorAttachments.size()) |
                                 std::views::transform([&](std::size_t attachmentIndex) {
                                     auto const& attachment = colorAttachments[attachmentIndex];
                                     auto const& image = setup.resolvedColors[attachmentIndex];
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

[[nodiscard]] PassPrimaryRecordScope RasterPassBuilder::makeDynamicRenderingSecondaryScope(
        const RasterPassRenderingSetup& setup,
        const PipelineRuntime<nr::rhi::GraphicsPipeline>& runtime,
        std::string_view debugName)
{
        auto const& graphicsDesc = runtime.state().graphicsDesc;
        nrAssert(
            graphicsDesc.has_value(),
            std::format("RasterPassBuilder pass '{}' requires retained graphics pipeline dynamic-rendering state.", debugName));

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

void RasterPassBuilder::bindGraphicsSetup(
        const vk::raii::CommandBuffer& commandBuffer,
        const PipelineRuntime<nr::rhi::GraphicsPipeline>& runtime,
        const nr::rhi::ShaderBindingSnapshot& bindingSnapshot,
        std::uint32_t frameIndex,
        vk::Extent2D targetExtent,
        RasterViewportYMode viewportYMode,
        nr::rhi::MeshRasterState rasterState,
        vk::PrimitiveTopology primitiveTopology)
{
        Base::bindPipelinePreparedResourcesAndPushConstants(
            commandBuffer,
            runtime,
            bindingSnapshot,
            frameIndex);

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
        nrAssert(
            hasSerialRecord != hasParallelRecord,
            "RasterPassBuilder::build requires exactly one serial record or parallel record callback.");
        nrAssert(
            !hasParallelRecord || (parallelItemCountCallback_ && parallelRangeRecordCallback_),
            "RasterPassBuilder::build parallel record requires both item-count and range-record callbacks.");

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
                .primaryScope = [runtime,
                                 colorAttachments,
                                 depthAttachment,
                                 viewportExtent,
                                 debugName](const PassRecordContext& recordContext) {
                    nrAssert(static_cast<bool>(runtime), "RasterPassBuilder primary scope requires initialized runtime state.");
                    auto setup = makeRenderingSetup(
                        recordContext,
                        std::span<const RasterColorAttachment>{colorAttachments.data(), colorAttachments.size()},
                        depthAttachment,
                        viewportExtent,
                        debugName);
                    return makeDynamicRenderingSecondaryScope(setup, *runtime, debugName);
                },
                .recordRange = [runtime,
                                colorAttachments,
                                depthAttachment,
                                bindingSnapshot,
                                viewportExtent,
                                viewportYMode,
                                rasterState,
                                primitiveTopology,
                                debugName,
                                rangeRecordCallback = std::move(parallelRangeRecordCallback)](
                                   const PassRangeRecordContext& rangeContext) {
                    nrAssert(static_cast<bool>(runtime), "RasterPassBuilder range record requires initialized runtime state.");

                    auto setup = makeRenderingSetup(
                        rangeContext.pass,
                        std::span<const RasterColorAttachment>{colorAttachments.data(), colorAttachments.size()},
                        depthAttachment,
                        viewportExtent,
                        debugName);
                    auto& commandBuffer = rangeContext.commandBuffer.get();
                    bindGraphicsSetup(
                        commandBuffer,
                        *runtime,
                        bindingSnapshot,
                        rangeContext.pass.frameIndex,
                        setup.targetExtent,
                        viewportYMode,
                        rasterState,
                        primitiveTopology);

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

            return context_.get().addPass(
                std::span<const PassResourceUseDesc>{resourceUses.data(), resourceUses.size()},
                debugName,
                std::move(parallelRecord),
                std::move(prepareCallback),
                vk::PipelineStageFlagBits2::eAllGraphics);
        }

        return context_.get().addPass(
            std::span<const PassResourceUseDesc>{resourceUses.data(), resourceUses.size()},
            debugName,
            [runtime,
             colorAttachments,
             depthAttachment,
             bindingSnapshot,
             viewportExtent,
             viewportYMode,
             rasterState,
             primitiveTopology,
             debugName,
             recordCallback = std::move(recordCallback)](const PassRecordContext& recordContext) {
                nrAssert(recordContext.commandBuffer.has_value(), "RasterPassBuilder record requires RAII command buffer access.");
                nrAssert(static_cast<bool>(runtime), "RasterPassBuilder record requires initialized runtime state.");

                auto setup = makeRenderingSetup(
                    recordContext,
                    std::span<const RasterColorAttachment>{colorAttachments.data(), colorAttachments.size()},
                    depthAttachment,
                    viewportExtent,
                    debugName);

                auto renderingScope = nr::rhi::ops::RenderingScopeDesc{
                    .renderArea = vk::Rect2D{vk::Offset2D{0, 0}, setup.targetExtent},
                    .colorAttachments = std::span<const nr::rhi::ops::RenderingAttachmentDesc>{
                        setup.colorAttachments.data(),
                        setup.colorAttachments.size()},
                    .depthAttachment = setup.depthAttachment,
                    .stencilAttachment = setup.stencilAttachment,
                };

                auto& commandBuffer = recordContext.commandBuffer->get();
                auto scopedRendering = nr::rhi::ops::ScopedRendering(commandBuffer, renderingScope);
                bindGraphicsSetup(
                    commandBuffer,
                    *runtime,
                    bindingSnapshot,
                    recordContext.frameIndex,
                    setup.targetExtent,
                    viewportYMode,
                    rasterState,
                    primitiveTopology);

                recordCallback(RasterPassRecordContext{
                    .pass = recordContext,
                    .commandBuffer = commandBuffer,
                    .descriptorLayout = runtime->state().descriptorLayout,
                    .pipelineLayout = runtime->state().layout,
                    .extent = setup.targetExtent,
                });
            },
            std::move(prepareCallback),
            false,
            vk::PipelineStageFlagBits2::eAllGraphics);
    }

[[nodiscard]] vk::Extent2D RasterPassBuilder::resolveTargetExtent(
        std::optional<vk::Extent2D> viewportExtent,
        std::span<const PassImageResource> resolvedColors,
        const std::optional<PassImageResource>& resolvedDepth)
{
        nrAssert(!resolvedColors.empty(), "RasterPassBuilder::resolveTargetExtent requires at least one resolved color image.");

        auto targetExtent = viewportExtent.value_or(vk::Extent2D{
            resolvedColors.front().extent.width,
            resolvedColors.front().extent.height,
        });

        std::ranges::for_each(resolvedColors, [&](const PassImageResource& image) {
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

ComputePassBuilder::ComputePassBuilder(
        NodeBuildContext& context,
        std::string_view debugName,
        std::shared_ptr<PipelineRuntime<nr::rhi::ComputePipeline>> runtime)
        : Base(context, debugName, std::move(runtime), "ComputePassBuilder")
{
    }

ComputePassBuilder& ComputePassBuilder::record(ComputePassRecordCallback callback)
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
            std::span<const PassResourceUseDesc>{resourceUses.data(), resourceUses.size()},
            debugName,
            [runtime,
             bindingSnapshot,
             recordCallback = std::move(recordCallback)](const PassRecordContext& recordContext) {
                nrAssert(recordContext.commandBuffer.has_value(), "ComputePassBuilder record requires RAII command buffer access.");
                nrAssert(static_cast<bool>(runtime), "ComputePassBuilder record requires initialized runtime state.");

                auto& commandBuffer = recordContext.commandBuffer->get();
                Base::bindPipelinePreparedResourcesAndPushConstants(
                    commandBuffer,
                    *runtime,
                    bindingSnapshot,
                    recordContext.frameIndex);

                recordCallback(ComputePassRecordContext{
                    .pass = recordContext,
                    .commandBuffer = commandBuffer,
                    .descriptorLayout = runtime->state().descriptorLayout,
                    .pipelineLayout = runtime->state().layout,
                });
            },
            std::move(prepareCallback),
            false,
            vk::PipelineStageFlagBits2::eComputeShader);
    }

RayTracingPassBuilder::RayTracingPassBuilder(
        NodeBuildContext& context,
        std::string_view debugName,
        std::shared_ptr<PipelineRuntime<nr::rhi::RayTracingPipeline>> runtime)
        : Base(context, debugName, std::move(runtime), "RayTracingPassBuilder")
{
    }

RayTracingPassBuilder& RayTracingPassBuilder::record(RayTracingPassRecordCallback callback)
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
            std::span<const PassResourceUseDesc>{resourceUses.data(), resourceUses.size()},
            debugName,
            [runtime,
             bindingSnapshot,
             recordCallback = std::move(recordCallback)](const PassRecordContext& recordContext) {
                nrAssert(recordContext.commandBuffer.has_value(), "RayTracingPassBuilder record requires RAII command buffer access.");
                nrAssert(static_cast<bool>(runtime), "RayTracingPassBuilder record requires initialized runtime state.");

                auto& commandBuffer = recordContext.commandBuffer->get();
                Base::bindPipelinePreparedResourcesAndPushConstants(
                    commandBuffer,
                    *runtime,
                    bindingSnapshot,
                    recordContext.frameIndex);

                recordCallback(RayTracingPassRecordContext{
                    .pass = recordContext,
                    .commandBuffer = commandBuffer,
                    .descriptorLayout = runtime->state().descriptorLayout,
                    .pipelineLayout = runtime->state().layout,
                });
            },
            std::move(prepareCallback),
            false,
            vk::PipelineStageFlagBits2::eRayTracingShaderKHR);
    }

void NodeRuntime::initialize(NodeInitContext&)
{
    }

void NodeRuntime::collectUi(NodeUiBuildContext&, const NodeFrameParameters&)
{
    }

void NodeRuntime::shutdown(NodeShutdownContext&)
{
    }

void Renderer::ensureSceneTextureFallback()
{
        nrAssert(static_cast<bool>(device_), "Renderer::ensureSceneTextureFallback requires initialized device.");

        if (sceneTextureFallback_.valid())
        {
            return;
        }

        auto imageInfo = nr::rhi::makeImageCreateInfo(
            vk::Format::eR8G8B8A8Unorm,
            vk::Extent2D{1u, 1u},
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled);
        sceneTextureFallback_ = device_->resourceFactory.createImage(
            imageInfo,
            nr::rhi::MemoryUsage::GpuOnly,
            "Renderer.SceneTextureFallback.Purple");
        nrAssert(sceneTextureFallback_.valid(), "Renderer failed to create purple scene texture fallback.");

        uploadSceneTextureFallback();
    }

[[nodiscard]] nr::rhi::ops::BufferUploadOwnershipPlan Renderer::makeSceneTextureFallbackUploadPlan() const
{
        nrAssert(static_cast<bool>(device_), "Renderer::makeSceneTextureFallbackUploadPlan requires initialized device.");

        auto const transferQueueFamily = device_->queueManager.transfer().queueFamilyIndex();
        auto const graphicsQueueFamily = device_->queueManager.graphics().queueFamilyIndex();

        auto plan = nr::rhi::ops::BufferUploadOwnershipPlan{};
        plan.releaseToDestination = nr::rhi::ops::makeQueueOwnershipTransfer(
            transferQueueFamily,
            graphicsQueueFamily,
            nr::rhi::ops::QueueAccessScope{
                .stages = vk::PipelineStageFlagBits2::eTransfer,
                .access = vk::AccessFlagBits2::eTransferWrite,
            },
            nr::rhi::ops::QueueAccessScope{
                .stages = vk::PipelineStageFlagBits2::eAllCommands,
                .access = vk::AccessFlagBits2::eShaderRead,
            });
        return plan;
    }

void Renderer::uploadSceneTextureFallback()
{
        nrAssert(static_cast<bool>(device_), "Renderer::uploadSceneTextureFallback requires initialized device.");
        nrAssert(sceneTextureFallback_.valid(), "Renderer::uploadSceneTextureFallback requires a valid fallback image.");

        auto purplePixel = std::array{
            static_cast<std::byte>(0xFFu),
            static_cast<std::byte>(0x00u),
            static_cast<std::byte>(0xFFu),
            static_cast<std::byte>(0xFFu),
        };

        auto& uploadContext = device_->uploadReadback();
        auto const uploadPlan = makeSceneTextureFallbackUploadPlan();
        auto uploadTicket = uploadContext.uploadImage(
            std::span<const std::byte>{purplePixel},
            sceneTextureFallback_,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            uploadPlan);
        nrAssert(uploadTicket.valid(), "Renderer failed to upload purple scene texture fallback.");

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
        auto& commandBuffer = commandBuffers.front();

        nr::rhi::CommandRecorder::beginPrimary(commandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        uploadContext.recordImageAcquireBarrier(commandBuffer, uploadTicket);
        nr::rhi::CommandRecorder::end(commandBuffer);

        auto syncBatch = nr::rhi::CommandBatch{};
        syncBatch.addWait(
            uploadContext.uploadTimelineSemaphore(),
            vk::PipelineStageFlagBits2::eAllCommands,
            uploadTicket.signalValue);
        syncBatch.addCommandBuffer(commandBuffer);

        auto fence = vk::raii::Fence(device_->device, vk::FenceCreateInfo{});
        device_->queueManager.graphics().submit(std::move(syncBatch), std::cref(fence));
        auto const waitResult = device_->device.waitForFences(*fence, vk::True, std::numeric_limits<std::uint64_t>::max());
        nrAssert(waitResult == vk::Result::eSuccess, "Renderer failed waiting for purple scene texture fallback upload synchronization.");
        uploadContext.reclaimCompletedUploads();
    }

[[nodiscard]] RendererSceneTextureDescriptorTable Renderer::buildSceneTextureDescriptorTable(
    const NodeFrameParameters& frameParameters,
    const std::map<std::uint32_t, nr::resource::TextureHandle>& sceneTextureHandlesById)
{
    ensureSceneTextureFallback();
    return cacheSuite_.globalDescriptorTableCache.buildSceneTextureDescriptorTable(
        RendererSceneTextureDescriptorTableInput{
            .fallbackImage = std::cref(sceneTextureFallback_),
            .scene = frameParameters.scene,
            .sceneTextureHandlesById = sceneTextureHandlesById,
        });
}

void Renderer::initialize(const RendererCreateInfo& info)
{
        if (device_)
        {
            return;
        }

        auto& shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        device_ = std::make_unique<nr::rhi::Device>();
        device_->initialize(info.appName, info.engineName, info.pipelineCache);
        frameUniformArena_.initialize(*device_, info.frameUniformBytesPerFrame, "Renderer.FrameUniformArena");
        submissionTimeline_.initialize(device_->device, 0);
        ensureSceneTextureFallback();
    }

void Renderer::installGraph(const RendererGraphSpec& spec)
{
        nrAssert(static_cast<bool>(device_), "Renderer::installGraph requires initialize() before graph installation.");
        teardownInstalledGraph();
        cacheSuite_.clear();

        auto installed = std::vector<InstalledNode>{};
        installed.reserve(spec.nodes.size());

        auto knownNames = std::set<std::string>{};

        std::ranges::for_each(spec.nodes, [&](const NodeCreateInfo& createInfo) {
            nrAssert(static_cast<bool>(createInfo.runtime), "Renderer::installGraph requires a valid node runtime in NodeCreateInfo.");

            auto description = createInfo.runtime->describe();
            auto runtimeName = createInfo.config.instanceName.empty()
                                   ? description.name
                                   : createInfo.config.instanceName;

            nrAssert(!runtimeName.empty(), "Renderer::installGraph requires each node to have a non-empty runtime name.");
            auto [_, inserted] = knownNames.insert(runtimeName);
            nrAssert(inserted, "Renderer::installGraph found duplicate node names in RendererGraphSpec.");

            cacheSuite_.variantRegistry.clearRuntime(runtimeName);
            auto initContext = NodeInitContext{
                .device = std::ref(*device_),
                .variants = std::ref(cacheSuite_.variantRegistry),
                .runtimeName = runtimeName,
            };
            createInfo.runtime->initialize(initContext);

            installed.push_back(InstalledNode{
                .runtime = createInfo.runtime,
                .description = std::move(description),
                .config = createInfo.config,
                .runtimeName = std::move(runtimeName),
            });
        });

        auto submitNodesByAfterIndex = std::multimap<std::size_t, SubmitNodeSpec>{};
        std::ranges::for_each(spec.submitNodes, [&](const SubmitNodeSpec& submitSpec) {
            nrAssert(
                submitSpec.afterNodeIndex < installed.size(),
                "Renderer::installGraph submit node index is out of range for installed nodes.");
            submitNodesByAfterIndex.emplace(submitSpec.afterNodeIndex, submitSpec);
        });

        installedNodes_ = std::move(installed);
        submitNodesByAfterIndex_ = std::move(submitNodesByAfterIndex);
        cameraJitter_ = spec.cameraJitter;
        resetCameraFrameTracking();
        previousGlobalFrameConstants_.reset();
        graphInstalled_ = true;
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
        resetCameraFrameTracking();
        previousGlobalFrameConstants_.reset();
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
        submissionTimeline_ = RendererSubmissionTimeline{};
        frameUniformArena_ = FrameUniformArena{};
        sceneTextureFallback_ = nr::rhi::Image{};
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
        cacheSuite_.clear();
        resetCameraFrameTracking();
        previousGlobalFrameConstants_.reset();
    }

void Renderer::resetSceneBinding() noexcept
{
        activeScene_.reset();
        sceneExtractProfile_.reset();
        sceneTlasExtractProfile_.reset();
        resetCameraFrameTracking();
        previousGlobalFrameConstants_.reset();
    }

void Renderer::resetCameraFrameTracking() noexcept
{
        previousCameraStabilityKey_.reset();
        cameraStableFrameOrdinal_ = 0u;
    }

[[nodiscard]] RendererCameraFrameState Renderer::beginCameraFrameState(
        const nr::scene::SceneBridgeFrameConstants& frameConstants,
        vk::Extent2D viewportExtent) noexcept
{
        auto const extent = sanitizeViewportExtent(viewportExtent);
        auto const stabilityKey = makeRendererCameraStabilityKey(frameConstants, extent);
        auto const cameraStable = previousCameraStabilityKey_.has_value() &&
                                  rendererCameraStabilityKeysEquivalent(*previousCameraStabilityKey_, stabilityKey);
        if (!cameraStable)
        {
            cameraStableFrameOrdinal_ = 0u;
        }

        auto state = RendererCameraFrameState{
            .jitterEnabled = cameraJitter_.enabled(),
            .accumulationReset = !cameraStable,
            .stableFrameOrdinal = cameraStableFrameOrdinal_,
            .historySampleCount = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(cameraStableFrameOrdinal_, kRendererAccumulationMaxSampleCount)),
            .viewportExtent = extent,
        };

        if (state.jitterEnabled)
        {
            switch (cameraJitter_.sequence)
            {
            case RendererCameraJitterSequence::Halton23:
                state.jitter = makeHalton23CameraJitterSample(
                    state.stableFrameOrdinal,
                    extent,
                    cameraJitter_.cycleLength);
                break;
            case RendererCameraJitterSequence::None:
                break;
            }
        }

        previousCameraStabilityKey_ = stabilityKey;
        if (cameraStableFrameOrdinal_ < std::numeric_limits<std::uint64_t>::max())
        {
            ++cameraStableFrameOrdinal_;
        }
        return state;
    }

[[nodiscard]] RendererFrameResult Renderer::renderFrame(const RendererFrameInput& input)
{
        if (!device_ || !graphInstalled_)
        {
            return RendererFrameResult{};
        }

        auto const totalStart = std::chrono::steady_clock::now();
        auto const beginFrameStart = std::chrono::steady_clock::now();
        auto begin = device_->beginFrame(input.acquireTimeout);
        auto const sampleFrameOrdinal = sampleFrameOrdinal_;
        if (sampleFrameOrdinal_ < std::numeric_limits<std::uint64_t>::max())
        {
            ++sampleFrameOrdinal_;
        }
        frameUniformArena_.beginFrame(begin.frameIndex);
        auto cpuTimings = RendererCpuFrameTimings{
            .cpuWaitGpuMilliseconds = begin.cpuWaitGpuMilliseconds,
            .frameSetupMilliseconds = std::max(
                0.0,
                elapsedMilliseconds(beginFrameStart, std::chrono::steady_clock::now()) -
                    begin.cpuWaitGpuMilliseconds),
        };

        auto scenePackets = std::optional<nr::scene::ScenePacketSet>{};
        auto sceneTlasPackets = std::optional<nr::scene::ScenePacketSet>{};
        auto primaryCamera = std::optional<nr::scene::SceneResolvedCamera>{};
        auto sceneBridgeFrame = std::optional<nr::scene::SceneBridgeFrame>{};
        auto sceneTextureHandlesById = std::map<std::uint32_t, nr::resource::TextureHandle>{};
        auto sceneExtractProfileCreated = false;
        auto sceneCameraOverride = input.cameraOverride;

        auto const sceneStart = std::chrono::steady_clock::now();
        if (input.scene.has_value())
        {
            auto& scene = input.scene->get();
            scene.beginFrame(begin.frameIndex);
            scene.uploadPending();

            auto [profile, created] = ensureSceneExtractProfile(scene);
            sceneExtractProfileCreated = created;
            auto [tlasProfile, tlasProfileCreated] = ensureSceneTlasExtractProfile(scene);
            sceneExtractProfileCreated = sceneExtractProfileCreated || tlasProfileCreated;

            auto extractInput = input.sceneExtractInput.value_or(nr::scene::SceneExtractInput{});
            if (!extractInput.viewportExtent.has_value())
            {
                auto extent = device_->presentationContext.swapchainExtent();
                extractInput.viewportExtent = glm::uvec2{extent.width, extent.height};
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
            if (!sceneCameraOverride.has_value())
            {
                primaryCamera = scene.tryGetPrimaryCamera(extractInput.viewportExtent);
            }

            auto bridgeBuildInput = nr::scene::SceneRenderBridgeBuildInput{
                .packetSet = std::cref(*scenePackets),
            };
            auto rasterGeometryBuffers = scene.tryGetRasterGeometryBuffers();
            bridgeBuildInput.resolveGeometryBuffers =
                [rasterGeometryBuffers]() -> std::optional<nr::scene::SceneBridgeGeometryBuffers> {
                return rasterGeometryBuffers;
            };

            bridgeBuildInput.resolveMaterialTextureIds =
                [&](nr::resource::MaterialHandle materialHandle)
                -> std::optional<nr::scene::SceneMaterialTextureIds> {
                return collectSceneMaterialTextureIds(scene, materialHandle, sceneTextureHandlesById);
            };

            if (sceneCameraOverride.has_value())
            {
                bridgeBuildInput.frameConstantsOverride = sceneCameraOverride->frameConstants;
            }

            bridgeBuildInput.resolveMaterialRasterState =
                [&](nr::resource::MaterialHandle materialHandle)
                -> std::optional<nr::scene::SceneBridgeMaterialRasterState> {
                auto materialRecordRef = scene.tryGetMaterialAsset(materialHandle);
                if (!materialRecordRef.has_value())
                {
                    return std::nullopt;
                }

                auto const& materialRecord = materialRecordRef->get();
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

            bridgeBuildInput.resolveRasterDrawGeometry =
                [&](nr::resource::MeshHandle meshHandle, std::uint32_t geometryIndex)
                -> std::optional<nr::scene::SceneBridgeDrawGeometry> {
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
                auto checkedAddUint32 =
                    [](std::uint32_t base, std::uint32_t offset, std::string_view label) {
                    auto const value = static_cast<std::uint64_t>(base) + static_cast<std::uint64_t>(offset);
                    nrAssert(
                        value <= std::numeric_limits<std::uint32_t>::max(),
                        std::format("{} value {} exceeds uint32_t range.", label, value));
                    return static_cast<std::uint32_t>(value);
                };
                auto checkedAddInt32 =
                    [](std::uint32_t base, std::uint32_t offset, std::string_view label) {
                    auto const value = static_cast<std::uint64_t>(base) + static_cast<std::uint64_t>(offset);
                    nrAssert(
                        value <= static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()),
                        std::format("{} value {} exceeds int32_t range.", label, value));
                    return static_cast<std::int32_t>(value);
                };

                auto drawGeometry = nr::scene::SceneBridgeDrawGeometry{};
                drawGeometry.vertexBuffer = rasterGeometryBuffers->vertexBuffer;
                drawGeometry.frontFace = meshRecord.cpu.clockwiseFrontFace
                                             ? vk::FrontFace::eClockwise
                                             : vk::FrontFace::eCounterClockwise;

                auto const indexedGeometry = atlas.indexCount > 0u;
                if (indexedGeometry)
                {
                    if (!rasterGeometryBuffers->hasIndexBuffer())
                    {
                        return std::nullopt;
                    }

                    drawGeometry.indexBuffer = rasterGeometryBuffers->indexBuffer;
                    drawGeometry.indexType = rasterGeometryBuffers->indexType;
                    drawGeometry.firstIndex = checkedAddUint32(
                        atlas.indexBase,
                        meshGeometry.firstIndex,
                        "Scene raster draw firstIndex");
                    drawGeometry.indexCount = meshGeometry.indexCount > 0
                                              ? meshGeometry.indexCount
                                              : atlas.indexCount;
                    drawGeometry.vertexOffset = checkedAddInt32(
                        atlas.vertexBase,
                        meshGeometry.vertexOffset,
                        "Scene raster draw vertexOffset");
                    return drawGeometry;
                }

                drawGeometry.firstVertex = checkedAddUint32(
                    atlas.vertexBase,
                    meshGeometry.firstIndex,
                    "Scene raster draw firstVertex");
                drawGeometry.vertexCount = meshGeometry.indexCount > 0
                                               ? meshGeometry.indexCount
                                               : atlas.vertexCount;
                return drawGeometry;
            };

            if (primaryCamera.has_value())
            {
                bridgeBuildInput.primaryCamera = std::cref(*primaryCamera);
            }

            sceneBridgeFrame = nr::scene::SceneRenderBridge::buildFrame(bridgeBuildInput);
        }
        cpuTimings.sceneMilliseconds = elapsedMilliseconds(
            sceneStart,
            std::chrono::steady_clock::now());

        auto frameParameters = NodeFrameParameters{};
        frameParameters.frameIndex = begin.frameIndex;
        frameParameters.swapchainImageIndex = begin.swapchainImageIndex;
        frameParameters.swapchainExtent = device_->presentationContext.swapchainExtent();
        frameParameters.swapchainFormat = device_->presentationContext.swapchainFormat();
        frameParameters.swapchainColorSpace = device_->presentationContext.swapchainColorSpace();
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
        }

        if (input.scene.has_value() && sceneTlasPackets.has_value())
        {
            collectTlasSceneTextureHandles(
                input.scene->get(),
                std::span<const nr::scene::TlasBuildInputPacket>{sceneTlasPackets->tlasBuildInputs},
                sceneTextureHandlesById);
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

        auto const cameraFrameState = beginCameraFrameState(globalFrameConstants, frameParameters.swapchainExtent);
        auto renderingFrameConstants = globalFrameConstants;
        if (cameraFrameState.jitterEnabled)
        {
            renderingFrameConstants.projection = applyCameraProjectionJitter(
                globalFrameConstants.projection,
                cameraFrameState.jitter.ndcOffset);
            renderingFrameConstants.viewProjection = renderingFrameConstants.projection * renderingFrameConstants.view;
        }

        auto const buildStart = std::chrono::steady_clock::now();
        buildInstalledGraph(
            frameParameters,
            renderingFrameConstants,
            cameraFrameState,
            sampleFrameOrdinal,
            sceneBridgeFrameRef,
            sceneTextureHandlesById);
        cpuTimings.buildMilliseconds = elapsedMilliseconds(
            buildStart,
            std::chrono::steady_clock::now());

        auto const compileStart = std::chrono::steady_clock::now();
        auto compiled = cacheSuite_.compileCache.compileConsumingCached(builder_.mutableFrame());
        cpuTimings.compileMilliseconds = elapsedMilliseconds(
            compileStart,
            std::chrono::steady_clock::now());

        auto executeContext = RenderGraphExecutor::ExecuteContext{
            .device = *device_,
            .frameIndex = begin.frameIndex,
            .swapchainImageIndex = begin.swapchainImageIndex,
            .submissionTimeline = submissionTimeline_.valid()
                                    ? std::optional<std::reference_wrapper<RendererSubmissionTimeline>>(std::ref(submissionTimeline_))
                                    : std::nullopt,
        };

        auto const prepareStart = std::chrono::steady_clock::now();
        auto prepared = executor_.prepareFrame(std::move(compiled), executeContext);
        cpuTimings.prepareMilliseconds = elapsedMilliseconds(
            prepareStart,
            std::chrono::steady_clock::now());

        auto const executeStart = std::chrono::steady_clock::now();
        auto executeReport = executor_.executePrepared(prepared, executeContext);
        cpuTimings.executeMilliseconds = elapsedMilliseconds(
            executeStart,
            std::chrono::steady_clock::now());
        if (executeReport.completedGpuPassTimingFrame.has_value())
        {
            recordGpuPassTimingSample(*executeReport.completedGpuPassTimingFrame);
        }

        auto const presentStart = std::chrono::steady_clock::now();
        auto present = device_->presentFrame();
        cpuTimings.presentMilliseconds = elapsedMilliseconds(
            presentStart,
            std::chrono::steady_clock::now());
        cpuTimings.totalMilliseconds = elapsedMilliseconds(
            totalStart,
            std::chrono::steady_clock::now());
        recordCpuTimingSample(cpuTimings);

        return RendererFrameResult{
            .rendered = true,
            .frameIndex = begin.frameIndex,
            .swapchainImageIndex = begin.swapchainImageIndex,
            .presentResult = present.result,
            .compiledSubmitBatchCount = prepared.compiled.submitBatches.size(),
            .submittedBatchCount = executeReport.submittedBatchCount,
            .invokedPassPrepareCount = executeReport.invokedPassPrepareCount,
            .invokedPassRecordCount = executeReport.invokedPassRecordCount,
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

[[nodiscard]] nr::rhi::Device& Renderer::device()
{
        return *device_;
    }

[[nodiscard]] const nr::rhi::Device& Renderer::device() const
{
        return *device_;
    }

[[nodiscard]] RenderGraphExecutor& Renderer::graphExecutor() noexcept
{
        return executor_;
    }

[[nodiscard]] const RenderGraphExecutor& Renderer::graphExecutor() const noexcept
{
        return executor_;
    }

[[nodiscard]] const RendererCpuStatistics& Renderer::cpuStatistics() const noexcept
{
        return cpuStatistics_;
    }

[[nodiscard]] const RendererGpuPassStatistics& Renderer::gpuPassStatistics() const noexcept
{
        return gpuPassStatistics_;
    }

void Renderer::buildInstalledGraph(
        const NodeFrameParameters& frameParameters,
        const nr::scene::SceneBridgeFrameConstants& frameConstants,
        const RendererCameraFrameState& cameraFrameState,
        std::uint64_t sampleFrameOrdinal,
        std::optional<std::reference_wrapper<const nr::scene::SceneBridgeFrame>> sceneBridgeFrame,
        const std::map<std::uint32_t, nr::resource::TextureHandle>& sceneTextureHandlesById)
{
        nrAssert(graphInstalled_, "Renderer::buildInstalledGraph requires installGraph() before rendering.");

        cacheSuite_.variantRegistry.commitFramePatches();
        builder_.clear();
        auto const previousFrameConstants = previousGlobalFrameConstants_.value_or(frameConstants);
        auto const globalFrameUniforms =
            makeGlobalFrameUniforms(frameConstants, previousFrameConstants, frameParameters.frameIndex, sampleFrameOrdinal);
        previousGlobalFrameConstants_ = frameConstants;
        auto globalResources = FrameGlobalResources{
            .frameUniform = frameUniformArena_.upload(builder_, "Renderer.GlobalFrameUniforms", globalFrameUniforms),
            .bindlessImageTableCache = std::ref(cacheSuite_.bindlessImageTableCache),
            .cameraFrameState = cameraFrameState,
        };
        auto sceneTextureDescriptorTable = buildSceneTextureDescriptorTable(frameParameters, sceneTextureHandlesById);
        globalResources.sceneTextureDescriptorsById = std::move(sceneTextureDescriptorTable.descriptorsById);
        globalResources.sceneTextureDescriptorVersion = sceneTextureDescriptorTable.version;

        auto nodeFrameParameters = frameParameters;
        if (sceneBridgeFrame.has_value())
        {
            nodeFrameParameters.sceneBridgeFrameHandle =
                builder_.addFrameData("SceneBridgeFrame", sceneBridgeFrame->get());
        }

        auto nodeUiSections = std::vector<NodeUiSection>{};
        nodeUiSections.reserve(installedNodes_.size());
        std::ranges::for_each(installedNodes_, [&](InstalledNode& installedNode) {
            auto uiContext = NodeUiBuildContext{installedNode.runtimeName, nodeUiSections};
            installedNode.runtime->collectUi(uiContext, nodeFrameParameters);
            collectVariantUiSections(cacheSuite_.variantRegistry, installedNode.runtimeName, uiContext);
        });
        nodeFrameParameters.nodeUiSections =
            std::span<const NodeUiSection>{nodeUiSections.data(), nodeUiSections.size()};

        auto frameResources = std::map<std::string, GraphResourceHandle>{};
        auto frameDataResources = std::map<std::string, GraphFrameDataHandle>{};

        auto nodeOrdinals = std::views::iota(std::size_t{0}, installedNodes_.size());
        std::ranges::for_each(nodeOrdinals, [&](std::size_t nodeIndex) {
            auto& installedNode = installedNodes_[nodeIndex];

            auto nodeHandle = builder_.addNode(
                installedNode.runtimeName,
                installedNode.config.queue);

            auto buildContext = NodeBuildContext{
                .graphBuilder = std::ref(builder_),
                .nodeHandle = nodeHandle,
                .queue = installedNode.config.queue,
                .frameIndex = frameParameters.frameIndex,
                .runtimeName = installedNode.runtimeName,
                .globalResources = std::cref(globalResources),
                .frameResources = std::ref(frameResources),
                .frameDataResources = std::ref(frameDataResources),
                .variants = std::ref(cacheSuite_.variantRegistry),
            };

            installedNode.runtime->build(buildContext, nodeFrameParameters);

            auto boundaries = submitNodesByAfterIndex_.equal_range(nodeIndex);
            std::ranges::for_each(std::ranges::subrange(boundaries.first, boundaries.second), [&](const auto& entry) {
                auto debugName = entry.second.debugName.empty()
                                     ? std::format("Submit.After.{}", installedNode.runtimeName)
                                     : entry.second.debugName;
                auto submitHandle = builder_.addSubmitNode(debugName);
                nrAssert(submitHandle.valid(), "Renderer::buildInstalledGraph failed to add a valid submit node.");
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
            std::ranges::for_each(installedNodes_, [&](InstalledNode& installedNode) {
                if (installedNode.runtime)
                {
                    installedNode.runtime->shutdown(shutdownContext);
                }
            });
        }

        installedNodes_.clear();
        submitNodesByAfterIndex_.clear();
        graphInstalled_ = false;
    }

[[nodiscard]] std::pair<nr::scene::SceneExtractProfileHandle, bool> Renderer::ensureSceneExtractProfile(nr::scene::Scene& scene)
{
        auto sameScene = activeScene_.has_value() && std::addressof(activeScene_->get()) == std::addressof(scene);
        if (!sameScene)
        {
            sceneExtractProfile_.reset();
            sceneTlasExtractProfile_.reset();
            resetCameraFrameTracking();
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

[[nodiscard]] std::pair<nr::scene::SceneExtractProfileHandle, bool> Renderer::ensureSceneTlasExtractProfile(nr::scene::Scene& scene)
{
        auto sameScene = activeScene_.has_value() && std::addressof(activeScene_->get()) == std::addressof(scene);
        if (!sameScene)
        {
            sceneExtractProfile_.reset();
            sceneTlasExtractProfile_.reset();
            resetCameraFrameTracking();
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

void Renderer::recordCpuTimingSample(const RendererCpuFrameTimings& timings) noexcept
{
        accumulateCpuTimings(cpuTimingAccumulator_, timings);
        ++cpuStatistics_.pendingSampleFrameCount;

        if (cpuStatistics_.pendingSampleFrameCount < nr::statisticsSampleFrameCount)
        {
            return;
        }

        cpuStatistics_.average = averageCpuTimings(
            cpuTimingAccumulator_,
            cpuStatistics_.pendingSampleFrameCount);
        cpuStatistics_.averagedFrameCount = cpuStatistics_.pendingSampleFrameCount;
        cpuStatistics_.pendingSampleFrameCount = 0u;
        cpuStatistics_.valid = true;
        cpuTimingAccumulator_ = {};
    }

void Renderer::recordGpuPassTimingSample(const GpuPassTimingFrame& timings)
{
        std::ranges::for_each(timings.passes, [&](const GpuPassTimingSample& sample) {
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
        if (gpuPassStatistics_.pendingSampleFrameCount < nr::statisticsSampleFrameCount)
        {
            return;
        }

        gpuPassStatistics_.averages = averageGpuPassTimings(gpuPassTimingAccumulator_);
        gpuPassStatistics_.averagedFrameCount = gpuPassStatistics_.pendingSampleFrameCount;
        gpuPassStatistics_.pendingSampleFrameCount = 0u;
        gpuPassStatistics_.valid = true;
        gpuPassTimingAccumulator_.clear();
    }
} // namespace nr::renderer
