module nr.scene;
import :bridge;
import nr.load;
import nr.utils;
import std;
import :type;
import :utils;

namespace nr::scene
{
[[nodiscard]] bool SceneBridgePlan::valid() const noexcept
{
    return !hasErrors;
}

[[nodiscard]] std::string SceneBridge::makeTextureCanonicalKey(const nr::load::SceneAsset &sceneAsset,
                                                               std::uint32_t textureIndex,
                                                               TextureSamplingColorIntent samplingColorIntent)
{
    if (textureIndex >= sceneAsset.textures.size() || sceneAsset.textures[textureIndex].key.empty())
    {
        return {};
    }

    auto const &baseKey = sceneAsset.textures[textureIndex].key;
    auto const intentLabel =
        samplingColorIntent == TextureSamplingColorIntent::srgb ? std::string_view{"srgb"} : std::string_view{"linear"};
    return std::format("texture[{}]:{}::sampling[{}]", baseKey.size(), baseKey, intentLabel);
}

[[nodiscard]] std::string SceneBridge::makeMaterialCanonicalKey(const nr::load::SceneAsset &sceneAsset,
                                                                std::uint32_t materialIndex)
{
    return makeIndexedCanonicalKey<IndexedCanonicalKeyKind::material>(sceneAsset, materialIndex);
}

[[nodiscard]] std::string SceneBridge::makeMeshCanonicalKey(const nr::load::SceneAsset &sceneAsset,
                                                            std::uint32_t meshIndex)
{
    return makeIndexedCanonicalKey<IndexedCanonicalKeyKind::mesh>(sceneAsset, meshIndex);
}

[[nodiscard]] std::string SceneBridge::makeCameraCanonicalKey(const nr::load::SceneAsset &sceneAsset,
                                                              std::uint32_t cameraIndex)
{
    return makeIndexedCanonicalKey<IndexedCanonicalKeyKind::camera>(sceneAsset, cameraIndex);
}

[[nodiscard]] std::string SceneBridge::makeLightCanonicalKey(const nr::load::SceneAsset &sceneAsset,
                                                             std::uint32_t lightIndex)
{
    return makeIndexedCanonicalKey<IndexedCanonicalKeyKind::light>(sceneAsset, lightIndex);
}

[[nodiscard]] SceneBridgePlan SceneBridge::buildPlan(const nr::load::SceneAsset &sceneAsset)
{
    auto plan = SceneBridgePlan{.sourcePath = sceneAsset.sourcePath};

    auto reportPlanError = [&](std::string message) {
        plan.hasErrors = true;
        nr::nrLog(nr::LogLevel::error, "SCENE", std::format("SceneBridgePlan error: {}", message),
                  std::source_location::current(), false);
    };

    auto const nodeCount = sceneAsset.nodes.size();
    plan.unreachableNodeCount = nodeCount;
    auto const rootValid =
        nodeCount > 0u && sceneAsset.rootNodeIndex != nr::load::invalidIndex && sceneAsset.rootNodeIndex < nodeCount;
    if (nodeCount > 0u && !rootValid)
    {
        reportPlanError(std::format("SceneAsset root node index {} is invalid for {} node(s).",
                                    sceneAsset.rootNodeIndex, nodeCount));
    }

    auto incomingParentCounts = std::vector<std::size_t>(nodeCount);
    auto const nodeIndices = std::views::iota(std::size_t{0}, nodeCount);
    std::ranges::for_each(nodeIndices, [&](std::size_t nodeIndex) {
        auto const &node = sceneAsset.nodes[nodeIndex];
        std::ranges::for_each(node.childIndices, [&](std::uint32_t childIndex) {
            if (childIndex >= nodeCount)
            {
                reportPlanError(std::format("Node {} ('{}') references out-of-range child index {}.", nodeIndex,
                                            node.name, childIndex));
                return;
            }

            ++incomingParentCounts[childIndex];
            if (rootValid && childIndex == sceneAsset.rootNodeIndex)
            {
                reportPlanError(std::format("Root node {} must not appear as a child of node {}.",
                                            sceneAsset.rootNodeIndex, nodeIndex));
            }
        });

        std::ranges::for_each(node.meshIndices, [&](std::uint32_t meshIndex) {
            if (meshIndex >= sceneAsset.meshes.size())
            {
                reportPlanError(std::format("Node {} ('{}') references out-of-range mesh index {}.", nodeIndex,
                                            node.name, meshIndex));
            }
        });
    });

    auto const materialIndices = std::views::iota(std::size_t{0}, sceneAsset.materials.size());
    std::ranges::for_each(materialIndices, [&](std::size_t materialIndex) {
        auto const &material = sceneAsset.materials[materialIndex];
        std::ranges::for_each(material.textures, [&](const nr::load::MaterialTextureBinding &binding) {
            if (binding.textureIndex >= sceneAsset.textures.size())
            {
                reportPlanError(std::format("Material {} ('{}') references out-of-range texture index {}.",
                                            materialIndex, material.name, binding.textureIndex));
            }

            if (nr::resource::materialTextureSlotSemanticValid(binding.semantic) && binding.uvChannel > 1u)
            {
                reportPlanError(std::format(
                    "Material {} ('{}') texture semantic '{}' selects unsupported UV set {}.", materialIndex,
                    material.name, nr::resource::materialTextureSlotSemanticName(binding.semantic), binding.uvChannel));
            }
        });
    });

    auto const meshIndices = std::views::iota(std::size_t{0}, sceneAsset.meshes.size());
    std::ranges::for_each(meshIndices, [&](std::size_t meshIndex) {
        auto const &mesh = sceneAsset.meshes[meshIndex];
        if (mesh.geometries.empty())
        {
            reportPlanError(std::format("Mesh {} ('{}') has no geometry records.", meshIndex, mesh.name));
        }

        std::ranges::for_each(mesh.geometries, [&](const nr::load::MeshGeometryAsset &geometry) {
            if (geometry.materialIndex != nr::load::invalidIndex &&
                geometry.materialIndex >= sceneAsset.materials.size())
            {
                reportPlanError(std::format("Mesh {} ('{}') geometry '{}' references out-of-range material index {}.",
                                            meshIndex, mesh.name, geometry.name, geometry.materialIndex));
            }
        });
    });

    if (rootValid)
    {
        enum class NodeVisitColor : std::uint8_t
        {
            white,
            gray,
            black,
        };
        struct NodeTraversalFrame
        {
            std::uint32_t nodeIndex = nr::load::invalidIndex;
            std::size_t nextChild = 0;
            std::size_t depth = 0;
        };

        auto colors = std::vector<NodeVisitColor>(nodeCount, NodeVisitColor::white);
        auto nodeDepths = std::vector<std::size_t>(nodeCount);
        auto traversal = std::vector<NodeTraversalFrame>{};
        traversal.reserve(nodeCount);
        colors[sceneAsset.rootNodeIndex] = NodeVisitColor::gray;
        nodeDepths[sceneAsset.rootNodeIndex] = 1u;
        traversal.push_back(NodeTraversalFrame{
            .nodeIndex = sceneAsset.rootNodeIndex,
            .depth = 1u,
        });
        plan.reachableNodeCount = 1u;
        plan.maximumReachableTemplateDepth = 1u;

        while (!traversal.empty())
        {
            auto &frame = traversal.back();
            auto const &children = sceneAsset.nodes[frame.nodeIndex].childIndices;
            if (frame.nextChild >= children.size())
            {
                colors[frame.nodeIndex] = NodeVisitColor::black;
                traversal.pop_back();
                continue;
            }

            auto const childIndex = children[frame.nextChild++];
            if (childIndex >= nodeCount)
            {
                continue;
            }

            if (colors[childIndex] == NodeVisitColor::gray)
            {
                reportPlanError(
                    std::format("Node graph contains a cycle through edge {} -> {}.", frame.nodeIndex, childIndex));
                continue;
            }

            if (colors[childIndex] == NodeVisitColor::white)
            {
                colors[childIndex] = NodeVisitColor::gray;
                nodeDepths[childIndex] = frame.depth + 1u;
                ++plan.reachableNodeCount;
                plan.maximumReachableTemplateDepth =
                    std::max(plan.maximumReachableTemplateDepth, nodeDepths[childIndex]);
                traversal.push_back(NodeTraversalFrame{
                    .nodeIndex = childIndex,
                    .depth = nodeDepths[childIndex],
                });
            }
        }

        plan.unreachableNodeCount = nodeCount - plan.reachableNodeCount;
        auto const sharedReachableNode = std::ranges::find_if(nodeIndices, [&](std::size_t nodeIndex) {
            return colors[nodeIndex] != NodeVisitColor::white &&
                   incomingParentCounts[nodeIndex] > (nodeIndex == sceneAsset.rootNodeIndex ? 0u : 1u);
        });
        if (sharedReachableNode != nodeIndices.end())
        {
            reportPlanError(std::format("Reachable node {} has {} incoming parent edges; at most one is allowed.",
                                        *sharedReachableNode, incomingParentCounts[*sharedReachableNode]));
        }

        auto includeBindingDepth = [&](std::uint32_t nodeIndex) {
            if (nodeIndex >= nodeCount || colors[nodeIndex] == NodeVisitColor::white)
            {
                return;
            }

            plan.maximumReachableTemplateDepth =
                std::max(plan.maximumReachableTemplateDepth, nodeDepths[nodeIndex] + 1u);
        };

        std::ranges::for_each(nodeIndices, [&](std::size_t nodeIndex) {
            if (!sceneAsset.nodes[nodeIndex].meshIndices.empty())
            {
                includeBindingDepth(static_cast<std::uint32_t>(nodeIndex));
            }
        });
        std::ranges::for_each(sceneAsset.cameras,
                              [&](const nr::load::CameraAsset &camera) { includeBindingDepth(camera.nodeIndex); });
        std::ranges::for_each(sceneAsset.lights, [&](const nr::load::LightAsset &light) {
            if (detail::mapLightType(light.type).has_value())
            {
                includeBindingDepth(light.nodeIndex);
            }
        });
    }

    auto appendBridgeInputs = [&]<typename InputT>(std::vector<InputT> &inputs, std::uint32_t count,
                                                   auto &&makeCanonicalKey, auto &&formatErrorMessage) {
        auto const sourceIndices = std::views::iota(std::uint32_t{0}, count);
        std::ranges::for_each(sourceIndices, [&](std::uint32_t sourceIndex) {
            auto canonicalKey = makeCanonicalKey(sourceIndex);
            if (canonicalKey.empty())
            {
                reportPlanError(formatErrorMessage(sourceIndex));
                return;
            }

            inputs.push_back(InputT{
                .sourceIndex = sourceIndex,
                .canonicalKey = std::move(canonicalKey),
            });
        });
    };

    auto const colorHints = detail::buildTextureColorSpaceHints(sceneAsset);
    auto const textureIndices =
        std::views::iota(std::uint32_t{0}, static_cast<std::uint32_t>(sceneAsset.textures.size()));
    std::ranges::for_each(textureIndices, [&](std::uint32_t sourceIndex) {
        auto const &hint = colorHints[sourceIndex];
        auto const samplingColorIntent =
            hint.preferSrgb ? TextureSamplingColorIntent::srgb : TextureSamplingColorIntent::linear;
        auto canonicalKey = makeTextureCanonicalKey(sceneAsset, sourceIndex, samplingColorIntent);
        if (canonicalKey.empty())
        {
            reportPlanError(std::format("Texture asset {} is missing a canonical key.", sourceIndex));
            return;
        }

        plan.textures.push_back(TextureBridgeInput{
            .sourceIndex = sourceIndex,
            .canonicalKey = std::move(canonicalKey),
            .samplingColorIntent = samplingColorIntent,
            .mixedColorAndLinearReferences = hint.hasColor && hint.hasLinear,
        });
    });

    appendBridgeInputs(
        plan.materials, static_cast<std::uint32_t>(sceneAsset.materials.size()),
        [&](std::uint32_t sourceIndex) { return makeMaterialCanonicalKey(sceneAsset, sourceIndex); },
        [](std::uint32_t sourceIndex) {
            return std::format("SceneAsset.sourcePath must not be empty when deriving material key {}.", sourceIndex);
        });

    appendBridgeInputs(
        plan.meshes, static_cast<std::uint32_t>(sceneAsset.meshes.size()),
        [&](std::uint32_t sourceIndex) { return makeMeshCanonicalKey(sceneAsset, sourceIndex); },
        [](std::uint32_t sourceIndex) {
            return std::format("SceneAsset.sourcePath must not be empty when deriving mesh key {}.", sourceIndex);
        });

    appendBridgeInputs(
        plan.cameras, static_cast<std::uint32_t>(sceneAsset.cameras.size()),
        [&](std::uint32_t sourceIndex) { return makeCameraCanonicalKey(sceneAsset, sourceIndex); },
        [](std::uint32_t sourceIndex) {
            return std::format("SceneAsset.sourcePath must not be empty when deriving camera key {}.", sourceIndex);
        });

    appendBridgeInputs(
        plan.lights, static_cast<std::uint32_t>(sceneAsset.lights.size()),
        [&](std::uint32_t sourceIndex) { return makeLightCanonicalKey(sceneAsset, sourceIndex); },
        [](std::uint32_t sourceIndex) {
            return std::format("SceneAsset.sourcePath must not be empty when deriving light key {}.", sourceIndex);
        });

    return plan;
}

[[nodiscard]] bool rasterDrawPacketResolved(const RasterDrawPacket &packet,
                                            const SceneBridgeGeometryBuffers &geometryBuffers,
                                            const SceneTextureHandleTable &textureHandlesById) noexcept
{
    if (!packet.mesh.valid() || !packet.material.valid() || !geometryBuffers.valid() || !packet.geometry.valid())
    {
        return false;
    }

    if (packet.geometry.indexed() && !geometryBuffers.hasIndexBuffer())
    {
        return false;
    }

    auto const normalSlotIndex =
        nr::resource::materialTextureSlotIndex(nr::resource::MaterialTextureSlotSemantic::normal);
    if (packet.materialTextures.normal.textureId != packet.materialTextures.ids[normalSlotIndex] ||
        packet.materialTextures.normal.uvSet > 1u)
    {
        return false;
    }

    return std::ranges::all_of(packet.materialTextures.ids, [&](SceneTextureId textureId) {
        if (textureId == kDefaultSceneTextureId)
        {
            return true;
        }

        auto const entry = textureHandlesById.find(static_cast<std::uint32_t>(textureId));
        return entry != textureHandlesById.end() && entry->second.valid() &&
               sceneTextureId(entry->second) == static_cast<std::uint32_t>(textureId);
    });
}

[[nodiscard]] SceneBridgeFrame SceneRenderBridge::buildFrame(const SceneRenderBridgeBuildInput &input)
{
    auto frame = SceneBridgeFrame{};
    auto const &packetSet = input.packetSet.get();
    frame.domain = packetSet.domain;

    if (input.frameConstantsOverride.has_value())
    {
        frame.frameConstants = *input.frameConstantsOverride;
        frame.hasPrimaryCamera = true;
    }

    if (input.primaryCamera.has_value())
    {
        auto const &camera = input.primaryCamera->get();
        frame.hasPrimaryCamera = true;
        frame.frameConstants.view = camera.view;
        frame.frameConstants.projection = camera.projection;
        frame.frameConstants.viewProjection = camera.projection * camera.view;
        frame.frameConstants.cameraWorld = glm::vec3{camera.world[3]};
    }

    if (packetSet.domain != ScenePacketDomain::rasterDraw)
    {
        return frame;
    }

    frame.geometryBuffers = packetSet.rasterGeometryBuffers;
    frame.rasterTextureHandlesById = packetSet.rasterTextureHandlesById;
    frame.rasterDraws = packetSet.rasterDraws;

    if (!frame.rasterDraws.empty())
    {
        nrAssert(frame.geometryBuffers.valid(),
                 "Raster packet extraction emitted draws without a resolved scene geometry atlas binding.");
    }

    std::ranges::for_each(frame.rasterDraws, [&](const RasterDrawPacket &packet) {
        nrAssert(rasterDrawPacketResolved(packet, frame.geometryBuffers, frame.rasterTextureHandlesById),
                 std::format("Raster packet resolution invariant failed for mesh ({}, {}) geometry {} and material "
                             "({}, {}).",
                             packet.mesh.slot, packet.mesh.generation, packet.geometryIndex, packet.material.slot,
                             packet.material.generation));
    });

    frame.frameConstants.drawCount = static_cast<float>(frame.rasterDraws.size());

    return frame;
}
} // namespace nr::scene
