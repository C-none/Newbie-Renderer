import std;
import dependency;
import nr.load;
import nr.resource;
import nr.rhi;
import nr.scene;

namespace
{
inline const auto kTriangleRelativePath = std::filesystem::path{"assets/glTF-Sample-Assets/Models/Triangle/glTF/Triangle.gltf"};

[[nodiscard]] bool require(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::println("[fail] {}", message);
        return false;
    }

    return true;
}

[[nodiscard]] std::filesystem::path projectRoot()
{
    return std::filesystem::path{NR_PROJECT_ROOT_DIR};
}

[[nodiscard]] std::expected<std::string, std::string> readTextFile(const std::filesystem::path &path)
{
    auto stream = std::ifstream{path, std::ios::binary};
    if (!stream.is_open())
    {
        return std::unexpected(std::format("failed opening file '{}'", path.generic_string()));
    }

    auto content = std::string{};
    stream.seekg(0, std::ios::end);
    auto const byteSize = stream.tellg();
    if (byteSize < 0)
    {
        return std::unexpected(std::format("failed querying file size for '{}'", path.generic_string()));
    }
    content.resize(static_cast<std::size_t>(byteSize));
    stream.seekg(0, std::ios::beg);
    stream.read(content.data(), static_cast<std::streamsize>(content.size()));
    if (!stream.good() && !stream.eof())
    {
        return std::unexpected(std::format("failed reading file '{}'", path.generic_string()));
    }

    return content;
}

[[nodiscard]] bool printModelRawInput(const std::filesystem::path &relativePath)
{
    auto const absolutePath = projectRoot() / relativePath;
    auto sourceText = readTextFile(absolutePath);
    if (!sourceText.has_value())
    {
        std::println("[fail] {}", sourceText.error());
        return false;
    }

    std::println("[raw.before-load] path='{}' bytes={}", absolutePath.generic_string(), sourceText->size());

    auto lineStream = std::istringstream{*sourceText};
    auto line = std::string{};
    auto lineNumber = std::size_t{1};
    while (std::getline(lineStream, line) && lineNumber <= 30)
    {
        std::println("[raw.before-load][line {:>2}] {}", lineNumber, line);
        ++lineNumber;
    }

    if (lineNumber <= 30)
    {
        std::println("[raw.before-load] file has only {} line(s).", lineNumber - 1);
    }
    else
    {
        std::println("[raw.before-load] ... (preview truncated to 30 lines)");
    }

    return true;
}

[[nodiscard]] auto loadSceneFromRelative(const std::filesystem::path &relativePath)
    -> std::expected<nr::load::SceneAsset, std::string>
{
    auto const absolutePath = projectRoot() / relativePath;
    std::println("[load] source='{}'", absolutePath.generic_string());

    auto request = nr::load::SceneLoadRequest{};
    request.sourcePath = absolutePath;

    auto importResult = nr::load::loadScene(request);
    if (!importResult.has_value())
    {
        auto const &error = importResult.error();
        auto message = std::format(
            "backend='{}' code={} path='{}' message='{}'",
            error.backend,
            static_cast<unsigned>(error.code),
            error.sourcePath.generic_string(),
            error.message);
        return std::unexpected(message);
    }

    return std::move(importResult.value());
}

void printStructuredSceneAsset(const nr::load::SceneAsset &sceneAsset)
{
    std::println(
        "[structured.after-load] source='{}' rootNodeIndex={} nodes={} meshes={} materials={} textures={} cameras={} lights={}",
        sceneAsset.sourcePath.generic_string(),
        sceneAsset.rootNodeIndex,
        sceneAsset.nodes.size(),
        sceneAsset.meshes.size(),
        sceneAsset.materials.size(),
        sceneAsset.textures.size(),
        sceneAsset.cameras.size(),
        sceneAsset.lights.size());

    auto nodeIndices = std::views::iota(std::size_t{0}, sceneAsset.nodes.size());
    std::ranges::for_each(nodeIndices, [&](std::size_t index) {
        auto const &node = sceneAsset.nodes[index];
        std::println(
            "[structured.node#{}] name='{}' parent={} childCount={} meshCount={}",
            index,
            node.name,
            node.parentIndex,
            node.childIndices.size(),
            node.meshIndices.size());
    });

    auto meshIndices = std::views::iota(std::size_t{0}, sceneAsset.meshes.size());
    std::ranges::for_each(meshIndices, [&](std::size_t meshIndex) {
        auto const &mesh = sceneAsset.meshes[meshIndex];
        std::println(
            "[structured.mesh#{}] name='{}' vertices={} indices={} materialIndex={}",
            meshIndex,
            mesh.name,
            mesh.vertices.size(),
            mesh.indices.size(),
            mesh.materialIndex);

        auto previewVertexCount = std::min<std::size_t>(mesh.vertices.size(), 4);
        auto vertexIndices = std::views::iota(std::size_t{0}, previewVertexCount);
        std::ranges::for_each(vertexIndices, [&](std::size_t vertexIndex) {
            auto const &vertex = mesh.vertices[vertexIndex];
            std::println(
                "[structured.mesh#{}][vertex#{}] pos=({}, {}, {}) normal=({}, {}, {}) uv=({}, {})",
                meshIndex,
                vertexIndex,
                vertex.position[0],
                vertex.position[1],
                vertex.position[2],
                vertex.normal[0],
                vertex.normal[1],
                vertex.normal[2],
                vertex.texCoord0[0],
                vertex.texCoord0[1]);
        });

        auto previewIndexCount = std::min<std::size_t>(mesh.indices.size(), 12);
        auto previewIndices = std::views::iota(std::size_t{0}, previewIndexCount) |
                              std::views::transform([&](std::size_t index) {
                                  return mesh.indices[index];
                              });
        auto indexText = std::string{};
        std::ranges::for_each(previewIndices, [&](std::uint32_t value) {
            if (!indexText.empty())
            {
                indexText += ", ";
            }
            indexText += std::to_string(value);
        });
        std::println("[structured.mesh#{}] indexPreview=[{}]", meshIndex, indexText);
    });

    auto materialIndices = std::views::iota(std::size_t{0}, sceneAsset.materials.size());
    std::ranges::for_each(materialIndices, [&](std::size_t materialIndex) {
        auto const &material = sceneAsset.materials[materialIndex];
        std::println(
            "[structured.material#{}] name='{}' baseColor=({}, {}, {}, {}) emissive=({}, {}, {}) metallic={} roughness={} opacity={} textureBindingCount={}",
            materialIndex,
            material.name,
            material.baseColorFactor[0],
            material.baseColorFactor[1],
            material.baseColorFactor[2],
            material.baseColorFactor[3],
            material.emissiveFactor[0],
            material.emissiveFactor[1],
            material.emissiveFactor[2],
            material.metallicFactor,
            material.roughnessFactor,
            material.opacity,
            material.textures.size());

        auto bindingIndices = std::views::iota(std::size_t{0}, material.textures.size());
        std::ranges::for_each(bindingIndices, [&](std::size_t bindingIndex) {
            auto const &binding = material.textures[bindingIndex];
            std::println(
                "[structured.material#{}][binding#{}] textureIndex={} uvChannel={} semantic='{}' textureTypeRaw={}",
                materialIndex,
                bindingIndex,
                binding.textureIndex,
                binding.uvChannel,
                binding.semantic,
                binding.textureTypeRaw);
        });
    });

    auto textureIndices = std::views::iota(std::size_t{0}, sceneAsset.textures.size());
    std::ranges::for_each(textureIndices, [&](std::size_t textureIndex) {
        auto const &texture = sceneAsset.textures[textureIndex];
        auto const hasDecoded = texture.decodedImage.has_value();
        std::println(
            "[structured.texture#{}] key='{}' path='{}' hasDecoded={} payloadKind={}",
            textureIndex,
            texture.key,
            texture.resolvedPath.generic_string(),
            hasDecoded,
            static_cast<std::uint32_t>(texture.payloadKind));

        if (hasDecoded)
        {
            std::println(
                "[structured.texture#{}] decoded={}x{} channels={} pixels={}",
                textureIndex,
                texture.decodedImage->width,
                texture.decodedImage->height,
                texture.decodedImage->channels,
                texture.decodedImage->pixels.size());
        }
    });
}

[[nodiscard]] std::string bytesToHex(std::span<const std::byte> bytes, std::size_t maxBytes = 1024)
{
    auto count = std::min(bytes.size(), maxBytes);
    auto text = std::string{};
    text.reserve(count * 3 + 32);

    auto indices = std::views::iota(std::size_t{0}, count);
    std::ranges::for_each(indices, [&](std::size_t index) {
        if (index != 0)
        {
            text += ' ';
        }

        auto value = std::to_integer<unsigned int>(bytes[index]);
        text += std::format("{:02X}", value);
    });

    if (bytes.size() > count)
    {
        text += std::format(" ... (truncated {}/{})", count, bytes.size());
    }

    return text;
}

void printByteDump(std::string_view label, std::span<const std::byte> bytes)
{
    std::println("[bytes] {} size={} hex={}", label, bytes.size(), bytesToHex(bytes));
}

[[nodiscard]] std::vector<nr::resource::Vertex> decodeVerticesFromBytes(std::span<const std::byte> bytes)
{
    auto const vertexStride = sizeof(nr::resource::Vertex);
    if ((bytes.size() % vertexStride) != 0)
    {
        std::println(
            "[fail] vertex byte stream is not aligned to Vertex stride: bytes={} stride={}",
            bytes.size(),
            vertexStride);
        return {};
    }

    auto const vertexCount = bytes.size() / vertexStride;
    auto vertices = std::vector<nr::resource::Vertex>(vertexCount);
    auto indices = std::views::iota(std::size_t{0}, vertexCount);
    std::ranges::for_each(indices, [&](std::size_t index) {
        auto const *source = bytes.data() + index * vertexStride;
        std::memcpy(std::addressof(vertices[index]), source, vertexStride);
    });

    return vertices;
}

[[nodiscard]] std::vector<std::uint32_t> decodeU32ValuesFromBytes(std::span<const std::byte> bytes)
{
    if ((bytes.size() % sizeof(std::uint32_t)) != 0)
    {
        std::println(
            "[fail] u32 byte stream is misaligned: bytes={} sizeof(u32)={}",
            bytes.size(),
            sizeof(std::uint32_t));
        return {};
    }

    auto const count = bytes.size() / sizeof(std::uint32_t);
    auto values = std::vector<std::uint32_t>(count);
    auto indices = std::views::iota(std::size_t{0}, count);
    std::ranges::for_each(indices, [&](std::size_t index) {
        auto const *source = bytes.data() + index * sizeof(std::uint32_t);
        std::memcpy(std::addressof(values[index]), source, sizeof(std::uint32_t));
    });
    return values;
}

void printVertexDecimalView(std::string_view label, std::span<const nr::resource::Vertex> vertices, std::size_t maxCount = 8)
{
    auto const count = std::min(vertices.size(), maxCount);
    std::println("[decimal] {} count={} (showing {})", label, vertices.size(), count);

    auto indices = std::views::iota(std::size_t{0}, count);
    std::ranges::for_each(indices, [&](std::size_t index) {
        auto const &v = vertices[index];
        std::println(
            "[decimal] {}[{}] pos=({:.3f}, {:.3f}, {:.3f}) normal=({:.3f}, {:.3f}, {:.3f}) tangent=({:.3f}, {:.3f}, {:.3f}, {:.3f}) uv0=({:.3f}, {:.3f}) uv1=({:.3f}, {:.3f}) color=({:.3f}, {:.3f}, {:.3f}, {:.3f}) joints=({}, {}, {}, {}) weights=({:.3f}, {:.3f}, {:.3f}, {:.3f})",
            label,
            index,
            v.position.x,
            v.position.y,
            v.position.z,
            v.normal.x,
            v.normal.y,
            v.normal.z,
            v.tangent.x,
            v.tangent.y,
            v.tangent.z,
            v.tangent.w,
            v.texCoord0.x,
            v.texCoord0.y,
            v.texCoord1.x,
            v.texCoord1.y,
            v.color0.x,
            v.color0.y,
            v.color0.z,
            v.color0.w,
            v.skin.joints.x,
            v.skin.joints.y,
            v.skin.joints.z,
            v.skin.joints.w,
            v.skin.weights.x,
            v.skin.weights.y,
            v.skin.weights.z,
            v.skin.weights.w);
    });

    if (vertices.size() > count)
    {
        std::println("[decimal] {} ... (truncated {} of {})", label, count, vertices.size());
    }
}

void printIndexDecimalView(std::string_view label, std::span<const std::uint32_t> indices, std::size_t maxCount = 64)
{
    auto const count = std::min(indices.size(), maxCount);
    auto text = std::string{};
    auto visible = std::views::iota(std::size_t{0}, count);
    std::ranges::for_each(visible, [&](std::size_t index) {
        if (!text.empty())
        {
            text += ", ";
        }
        text += std::to_string(indices[index]);
    });
    if (indices.size() > count)
    {
        text += std::format(" ... (truncated {} of {})", count, indices.size());
    }

    std::println("[decimal] {} count={} values=[{}]", label, indices.size(), text);
}

void printIndexTriangleInterpretation(std::string_view label,
                                      std::span<const nr::resource::Vertex> vertices,
                                      std::span<const std::uint32_t> indices,
                                      std::size_t maxTriangleCount = 16)
{
    if ((indices.size() % 3) != 0)
    {
        std::println("[decimal] {} cannot form triangles because index count {} is not divisible by 3", label, indices.size());
        return;
    }

    auto const triangleCount = indices.size() / 3;
    auto const visibleCount = std::min(triangleCount, maxTriangleCount);
    std::println("[decimal] {} triangleCount={} (showing {})", label, triangleCount, visibleCount);

    auto triangleIndices = std::views::iota(std::size_t{0}, visibleCount);
    std::ranges::for_each(triangleIndices, [&](std::size_t triangleIndex) {
        auto const i0 = indices[triangleIndex * 3 + 0];
        auto const i1 = indices[triangleIndex * 3 + 1];
        auto const i2 = indices[triangleIndex * 3 + 2];
        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
        {
            std::println(
                "[decimal] {}[tri#{}] invalid index triplet=({}, {}, {}) for vertexCount={}",
                label,
                triangleIndex,
                i0,
                i1,
                i2,
                vertices.size());
            return;
        }

        auto const &p0 = vertices[i0].position;
        auto const &p1 = vertices[i1].position;
        auto const &p2 = vertices[i2].position;
        std::println(
            "[decimal] {}[tri#{}] indices=({}, {}, {}) => p0=({:.3f}, {:.3f}, {:.3f}) p1=({:.3f}, {:.3f}, {:.3f}) p2=({:.3f}, {:.3f}, {:.3f})",
            label,
            triangleIndex,
            i0,
            i1,
            i2,
            p0.x,
            p0.y,
            p0.z,
            p1.x,
            p1.y,
            p1.z,
            p2.x,
            p2.y,
            p2.z);
    });
}

template <typename T>
[[nodiscard]] std::span<const std::byte> asByteSpan(const T &value)
{
    return std::as_bytes(std::span{std::addressof(value), std::size_t{1}});
}

[[nodiscard]] bool requireBytesEqual(std::string_view label,
                                     std::span<const std::byte> expected,
                                     std::span<const std::byte> actual)
{
    if (expected.size() != actual.size())
    {
        std::println(
            "[fail] {} byte-size mismatch: expected={}, actual={}",
            label,
            expected.size(),
            actual.size());
        return false;
    }

    if (std::ranges::equal(expected, actual))
    {
        return true;
    }

    auto mismatch = std::ranges::mismatch(expected, actual);
    auto mismatchIndex = static_cast<std::size_t>(std::distance(expected.begin(), mismatch.in1));
    std::println(
        "[fail] {} mismatch at byte {}: expected={}, actual={}",
        label,
        mismatchIndex,
        std::to_integer<unsigned int>(*mismatch.in1),
        std::to_integer<unsigned int>(*mismatch.in2));
    return false;
}

[[nodiscard]] nr::scene::detail::MaterialGpuData buildExpectedMaterialGpuData(const nr::resource::Material &material)
{
    auto data = nr::scene::detail::MaterialGpuData{};
    data.baseColorFactor = material.baseColorFactor;
    data.emissiveAndMetallic = glm::vec4{material.emissiveFactor, material.metallicFactor};
    data.roughnessNormalOcclusionAlpha = glm::vec4{
        material.roughnessFactor,
        material.normalScale,
        material.occlusionStrength,
        material.alphaCutoff,
    };
    data.alphaAndFlags = glm::uvec4{
        static_cast<std::uint32_t>(material.alphaMode),
        material.doubleSided ? 1u : 0u,
        0u,
        0u,
    };

    data.specularAndGlossiness = glm::vec4{
        material.specularFactor,
        material.glossinessFactor,
    };

    data.anisotropyAndWorkflow = glm::uvec4{
        static_cast<std::uint32_t>(glm::packSnorm2x16(glm::vec2{material.anisotropyFactor, 0.0f})),
        material.usesMetallicRoughnessWorkflow() ? 1u : 0u,
        material.usesSpecularGlossinessWorkflow() ? 1u : 0u,
        material.usesAnisotropy() ? 1u : 0u,
    };

    auto slots = std::array{
        material.baseColor,
        material.normal,
        material.metallicRoughness,
        material.occlusion,
        material.emissive,
    };

    auto indices = std::views::iota(std::size_t{0}, slots.size());
    std::ranges::for_each(indices, [&](std::size_t index) {
        data.textureHandles[index] = slots[index].texture.packed();
        data.uvSets[index] = slots[index].uvSet;
    });

    return data;
}

[[nodiscard]] nr::scene::detail::CameraGpuData buildExpectedCameraGpuData(const nr::resource::CameraAsset &camera)
{
    return nr::scene::detail::CameraGpuData{
        .projection = static_cast<std::uint32_t>(camera.projection),
        .verticalFovRadians = camera.verticalFovRadians,
        .orthoHeight = camera.orthoHeight,
        .nearPlane = camera.nearPlane,
        .farPlane = camera.farPlane,
    };
}

[[nodiscard]] nr::scene::detail::LightGpuData buildExpectedLightGpuData(const nr::resource::LightAsset &light)
{
    return nr::scene::detail::LightGpuData{
        .type = static_cast<std::uint32_t>(light.type),
        .intensity = light.intensity,
        .color = light.color,
        .range = light.range,
        .innerConeRadians = light.innerConeRadians,
        .outerConeRadians = light.outerConeRadians,
        .castShadow = light.castShadow ? 1u : 0u,
    };
}

[[nodiscard]] nr::rhi::ops::ReadbackSyncPlan graphicsReadbackSyncPlan()
{
    constexpr auto access = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;
    constexpr auto stage = vk::PipelineStageFlagBits2::eAllCommands;
    return nr::rhi::ops::ReadbackSyncPlan{
        .preCopy = nr::rhi::ops::ReadbackSyncScope{
            .stages = stage,
            .access = access,
        },
        .postCopy = nr::rhi::ops::ReadbackSyncScope{
            .stages = stage,
            .access = access,
        },
    };
}

struct ResolvedHandles
{
    std::optional<nr::resource::MeshHandle> mesh{};
    std::optional<nr::resource::MaterialHandle> material{};
    std::optional<nr::resource::TextureHandle> texture{};
    std::optional<nr::resource::CameraAssetHandle> camera{};
    std::optional<nr::resource::LightAssetHandle> light{};
};

struct DeviceWaitIdleGuard
{
    nr::rhi::Device &device;

    ~DeviceWaitIdleGuard()
    {
        device.waitIdle();
    }
};

[[nodiscard]] std::optional<ResolvedHandles> resolveFirstHandles(
    nr::scene::Scene &scene,
    const nr::load::SceneAsset &sceneAsset)
{
    auto bridgePlan = nr::scene::SceneBridge::buildPlan(sceneAsset);
    if (!bridgePlan.valid())
    {
        std::println("[fail] bridge plan is invalid for handle resolution.");
        return std::nullopt;
    }

    auto handles = ResolvedHandles{};

    if (!bridgePlan.meshes.empty())
    {
        auto const &meshKey = bridgePlan.meshes.front().canonicalKey;
        handles.mesh = scene.findMeshHandleByStableKey(meshKey);
        if (!handles.mesh.has_value())
        {
            std::println("[fail] mesh handle not found by key '{}'", meshKey);
            return std::nullopt;
        }
        std::println("[resolve] mesh key='{}' -> slot={} gen={}", meshKey, handles.mesh->slot, handles.mesh->generation);
    }

    if (!bridgePlan.materials.empty())
    {
        auto const &materialKey = bridgePlan.materials.front().canonicalKey;
        handles.material = scene.findMaterialHandleByStableKey(materialKey);
        if (!handles.material.has_value())
        {
            std::println("[fail] material handle not found by key '{}'", materialKey);
            return std::nullopt;
        }
        std::println("[resolve] material key='{}' -> slot={} gen={}", materialKey, handles.material->slot, handles.material->generation);
    }

    if (!bridgePlan.textures.empty())
    {
        auto const &textureKey = bridgePlan.textures.front().canonicalKey;
        handles.texture = scene.findTextureHandleByStableKey(textureKey);
        if (!handles.texture.has_value())
        {
            std::println("[fail] texture handle not found by key '{}'", textureKey);
            return std::nullopt;
        }
        std::println("[resolve] texture key='{}' -> slot={} gen={}", textureKey, handles.texture->slot, handles.texture->generation);
    }

    if (!bridgePlan.cameras.empty())
    {
        auto const &cameraKey = bridgePlan.cameras.front().canonicalKey;
        handles.camera = scene.findCameraHandleByStableKey(cameraKey);
        if (!handles.camera.has_value())
        {
            std::println("[fail] camera handle not found by key '{}'", cameraKey);
            return std::nullopt;
        }
        std::println("[resolve] camera key='{}' -> slot={} gen={}", cameraKey, handles.camera->slot, handles.camera->generation);
    }

    if (!bridgePlan.lights.empty())
    {
        auto const &lightKey = bridgePlan.lights.front().canonicalKey;
        handles.light = scene.findLightHandleByStableKey(lightKey);
        if (!handles.light.has_value())
        {
            std::println("[fail] light handle not found by key '{}'", lightKey);
            return std::nullopt;
        }
        std::println("[resolve] light key='{}' -> slot={} gen={}", lightKey, handles.light->slot, handles.light->generation);
    }

    return handles;
}

[[nodiscard]] nr::load::SceneAsset buildRetentionSyntheticSceneAsset()
{
    auto scene = nr::load::SceneAsset{};
    scene.sourcePath = std::filesystem::path{"manual_upload_retention_scene.gltf"};

    auto image = nr::load::Image{};
    image.width = 2;
    image.height = 2;
    image.channels = 4;
    image.pixels = {
        255, 0, 0, 255,
        0, 255, 0, 255,
        0, 0, 255, 255,
        255, 255, 255, 255,
    };

    auto texture = nr::load::TextureAsset{};
    texture.key = "manual://textures/upload_retention/baseColor";
    texture.decodedImage = image;
    scene.textures.push_back(std::move(texture));

    auto material = nr::load::MaterialAsset{};
    material.name = "retention_material";
    material.textures.push_back(nr::load::MaterialTextureBinding{
        .textureIndex = 0,
        .uvChannel = 0,
        .textureTypeRaw = 0,
        .semantic = "diffuse",
    });
    scene.materials.push_back(std::move(material));

    auto mesh = nr::load::MeshAsset{};
    mesh.name = "retention_mesh";
    mesh.materialIndex = 0;
    mesh.vertices = {
        nr::load::VertexAsset{.position = {-0.5f, -0.5f, 0.0f}},
        nr::load::VertexAsset{.position = {0.5f, -0.5f, 0.0f}},
        nr::load::VertexAsset{.position = {0.0f, 0.5f, 0.0f}},
    };
    mesh.indices = {0, 1, 2};
    scene.meshes.push_back(std::move(mesh));

    auto identity = std::array<float, 16>{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };

    scene.nodes.resize(4);
    scene.rootNodeIndex = 0;

    scene.nodes[0].name = "Root";
    scene.nodes[0].parentIndex = nr::load::invalidIndex;
    scene.nodes[0].childIndices = {1, 2, 3};
    scene.nodes[0].localTransform = identity;

    scene.nodes[1].name = "MeshNode";
    scene.nodes[1].parentIndex = 0;
    scene.nodes[1].meshIndices = {0};
    scene.nodes[1].localTransform = identity;

    scene.nodes[2].name = "CameraNode";
    scene.nodes[2].parentIndex = 0;
    scene.nodes[2].localTransform = identity;

    scene.nodes[3].name = "LightNode";
    scene.nodes[3].parentIndex = 0;
    scene.nodes[3].localTransform = identity;

    scene.cameras.push_back(nr::load::CameraAsset{
        .name = "RetentionCamera",
        .sourceNodeName = "CameraNode",
        .nodeIndex = 2,
        .horizontalFov = glm::radians(70.0f),
        .aspect = 16.0f / 9.0f,
        .nearPlane = 0.1f,
        .farPlane = 400.0f,
        .orthographicWidth = 0.0f,
    });

    scene.lights.push_back(nr::load::LightAsset{
        .name = "RetentionLight",
        .sourceNodeName = "LightNode",
        .nodeIndex = 3,
        .typeRaw = 2,
        .type = "point",
        .colorDiffuse = {1.0f, 0.9f, 0.8f},
        .attenuationLinear = 0.1f,
    });

    scene.stats.nodeCount = static_cast<std::uint32_t>(scene.nodes.size());
    scene.stats.meshCount = static_cast<std::uint32_t>(scene.meshes.size());
    scene.stats.materialCount = static_cast<std::uint32_t>(scene.materials.size());
    scene.stats.textureCount = static_cast<std::uint32_t>(scene.textures.size());
    scene.stats.cameraCount = static_cast<std::uint32_t>(scene.cameras.size());
    scene.stats.lightCount = static_cast<std::uint32_t>(scene.lights.size());
    scene.stats.vertexCount = 3;
    scene.stats.indexCount = 3;

    return scene;
}

[[nodiscard]] bool checkDiscardUploadSourceRetentionPolicy()
{
    std::println("\n=== Case: checkDiscardUploadSourceRetentionPolicy ===");

    auto sceneAsset = buildRetentionSyntheticSceneAsset();

    auto device = nr::rhi::Device{};
    device.initialize("nr_scene_upload_readback_discard", "nrrhi_test");
    auto waitIdleOnExit = DeviceWaitIdleGuard{.device = device};

    auto scene = nr::scene::Scene(nr::scene::SceneCreateInfo{
        .device = device,
        .cpuRetention = nr::scene::CpuRetentionPolicy::discardUploadSourceAfterResident,
    });

    auto templateHandle = scene.registerTemplate(sceneAsset);
    if (!require(templateHandle.valid(), "registerTemplate should succeed for retention synthetic scene."))
    {
        return false;
    }

    auto handles = resolveFirstHandles(scene, sceneAsset);
    if (!handles.has_value())
    {
        return false;
    }

    scene.beginFrame(0);
    scene.uploadPending();

    if (handles->mesh.has_value())
    {
        auto meshRecordRef = scene.tryGetMeshAsset(*handles->mesh);
        if (!require(meshRecordRef.has_value(), "Mesh record should exist for retention check."))
        {
            return false;
        }

        auto const &meshRecord = meshRecordRef->get();
        if (!require(meshRecord.gpuState == nr::scene::GpuResidencyState::resident,
                     "Mesh should be resident after uploadPending in retention check."))
        {
            return false;
        }
        if (!require(meshRecord.cpu.vertices.empty() && meshRecord.cpu.indices.empty(),
                     "Mesh CPU upload sources should be discarded under discardUploadSourceAfterResident policy."))
        {
            return false;
        }
    }

    if (handles->texture.has_value())
    {
        auto textureRecordRef = scene.tryGetTextureAsset(*handles->texture);
        if (!require(textureRecordRef.has_value(), "Texture record should exist for retention check."))
        {
            return false;
        }

        auto const &textureRecord = textureRecordRef->get();
        if (!require(textureRecord.gpuState == nr::scene::GpuResidencyState::resident,
                     "Texture should be resident after uploadPending in retention check."))
        {
            return false;
        }

        auto levelsCleared = std::ranges::all_of(textureRecord.cpu.levels, [](const nr::resource::ImageLevel &level) {
            return level.bytes.empty();
        });
        if (!require(levelsCleared,
                     "Texture CPU upload source bytes should be discarded under discardUploadSourceAfterResident policy."))
        {
            return false;
        }
    }

    if (handles->material.has_value())
    {
        auto materialRecordRef = scene.tryGetMaterialAsset(*handles->material);
        if (!require(materialRecordRef.has_value(), "Material record should exist for retention check."))
        {
            return false;
        }

        auto const &materialRecord = materialRecordRef->get();
        if (!require(materialRecord.gpuState == nr::scene::GpuResidencyState::resident,
                     "Material should be resident after uploadPending in retention check."))
        {
            return false;
        }
        if (!require(materialRecord.cpu.name == "retention_material",
                     "Material CPU data should be retained under discardUploadSourceAfterResident policy."))
        {
            return false;
        }
    }

    if (handles->camera.has_value())
    {
        auto cameraRecordRef = scene.tryGetCameraAsset(*handles->camera);
        if (!require(cameraRecordRef.has_value(), "Camera record should exist for retention check."))
        {
            return false;
        }

        auto const &cameraRecord = cameraRecordRef->get();
        if (!require(cameraRecord.gpuState == nr::scene::GpuResidencyState::resident,
                     "Camera should be resident after uploadPending in retention check."))
        {
            return false;
        }
        if (!require(cameraRecord.cpu.name == "RetentionCamera",
                     "Camera CPU data should be retained under discardUploadSourceAfterResident policy."))
        {
            return false;
        }
    }

    if (handles->light.has_value())
    {
        auto lightRecordRef = scene.tryGetLightAsset(*handles->light);
        if (!require(lightRecordRef.has_value(), "Light record should exist for retention check."))
        {
            return false;
        }

        auto const &lightRecord = lightRecordRef->get();
        if (!require(lightRecord.gpuState == nr::scene::GpuResidencyState::resident,
                     "Light should be resident after uploadPending in retention check."))
        {
            return false;
        }
        if (!require(lightRecord.cpu.name == "RetentionLight",
                     "Light CPU data should be retained under discardUploadSourceAfterResident policy."))
        {
            return false;
        }
    }

    return true;
}

[[nodiscard]] bool checkSceneUploadAndReadbackFromRealTriangle()
{
    std::println("\n=== Case: checkSceneUploadAndReadbackFromRealTriangle ===");

    if (!printModelRawInput(kTriangleRelativePath))
    {
        return false;
    }

    auto importedScene = loadSceneFromRelative(kTriangleRelativePath);
    if (!importedScene.has_value())
    {
        std::println("[fail] loadScene failed: {}", importedScene.error());
        return false;
    }

    auto sceneAsset = std::move(importedScene.value());
    printStructuredSceneAsset(sceneAsset);

    auto device = nr::rhi::Device{};
    device.initialize("nr_scene_upload_readback_test", "nrrhi_test");
    auto waitIdleOnExit = DeviceWaitIdleGuard{.device = device};

    auto scene = nr::scene::Scene(nr::scene::SceneCreateInfo{
        .device = device,
    });

    auto templateHandle = scene.registerTemplate(sceneAsset);
    if (!require(templateHandle.valid(), "registerTemplate should succeed for Triangle.gltf."))
    {
        return false;
    }

    auto handles = resolveFirstHandles(scene, sceneAsset);
    if (!handles.has_value())
    {
        return false;
    }

    scene.beginFrame(0);
    scene.uploadPending();

    auto &uploadReadback = device.uploadReadback();
    auto readbackSync = graphicsReadbackSyncPlan();

    if (handles->mesh.has_value())
    {
        auto meshRecordRef = scene.tryGetMeshAsset(*handles->mesh);
        if (!require(meshRecordRef.has_value(), "Mesh record should exist after uploadPending."))
        {
            return false;
        }

        auto const &meshRecord = meshRecordRef->get();
        if (!require(meshRecord.gpuState == nr::scene::GpuResidencyState::resident, "Mesh should be resident after uploadPending."))
        {
            return false;
        }
        if (!require(meshRecord.gpu.has_value(), "Mesh gpu payload should exist."))
        {
            return false;
        }

        std::println(
            "[structured.scene-cpu.mesh] stableKey='{}' vertices={} indices={} submeshes={}",
            meshRecord.stableKey,
            meshRecord.cpu.vertices.size(),
            meshRecord.cpu.indices.size(),
            meshRecord.cpu.submeshes.size());

        auto expectedVertexBytes = std::as_bytes(std::span{meshRecord.cpu.vertices});
        printByteDump("upload.memory.mesh.vertex.expected", expectedVertexBytes);
        printVertexDecimalView("upload.memory.mesh.vertex.expected.decimal", meshRecord.cpu.vertices);

        auto meshReadbackSupported =
            (meshRecord.gpu->vertexBuffer.usage() & vk::BufferUsageFlagBits::eTransferSrc) != vk::BufferUsageFlags{};
        if (!require(meshReadbackSupported, "Mesh vertex buffer lacks eTransferSrc usage; mesh readback is unsupported."))
        {
            return false;
        }

        auto vertexTicket = uploadReadback.readbackBuffer(
            meshRecord.gpu->vertexBuffer,
            0,
            expectedVertexBytes.size_bytes(),
            nr::rhi::QueueRole::Graphics,
            readbackSync);
        auto vertexBytes = uploadReadback.readbackBytes(vertexTicket);
        printByteDump("readback.mesh.vertex.actual", vertexBytes);

        auto readbackVertices = decodeVerticesFromBytes(vertexBytes);
        if (!require(!readbackVertices.empty(), "Decoded readback vertex list should not be empty."))
        {
            return false;
        }
        printVertexDecimalView("readback.mesh.vertex.actual.decimal", readbackVertices);

        if (!requireBytesEqual("mesh vertex readback", expectedVertexBytes, vertexBytes))
        {
            return false;
        }

        auto expectedIndexBytes = std::as_bytes(std::span{meshRecord.cpu.indices});
        printByteDump("upload.memory.mesh.index.expected", expectedIndexBytes);
        printIndexDecimalView("upload.memory.mesh.index.expected.decimal", meshRecord.cpu.indices);
        printIndexTriangleInterpretation("upload.memory.mesh.index.expected.interpretation", meshRecord.cpu.vertices, meshRecord.cpu.indices);

        if (meshRecord.gpu->indexBuffer.valid())
        {
            auto indexTicket = uploadReadback.readbackBuffer(
                meshRecord.gpu->indexBuffer,
                0,
                expectedIndexBytes.size_bytes(),
                nr::rhi::QueueRole::Graphics,
                readbackSync);
            auto indexBytes = uploadReadback.readbackBytes(indexTicket);
            printByteDump("readback.mesh.index.actual", indexBytes);

            auto readbackIndices = decodeU32ValuesFromBytes(indexBytes);
            if (!require(!readbackIndices.empty(), "Decoded readback index list should not be empty."))
            {
                return false;
            }
            printIndexDecimalView("readback.mesh.index.actual.decimal", readbackIndices);
            printIndexTriangleInterpretation("readback.mesh.index.actual.interpretation", readbackVertices, readbackIndices);

            if (!requireBytesEqual("mesh index readback", expectedIndexBytes, indexBytes))
            {
                return false;
            }
        }
    }

    if (handles->material.has_value())
    {
        auto materialRecordRef = scene.tryGetMaterialAsset(*handles->material);
        if (!require(materialRecordRef.has_value(), "Material record should exist after uploadPending."))
        {
            return false;
        }

        auto const &materialRecord = materialRecordRef->get();
        if (!require(materialRecord.gpuState == nr::scene::GpuResidencyState::resident, "Material should be resident after uploadPending."))
        {
            return false;
        }
        if (!require(materialRecord.gpu.has_value(), "Material gpu payload should exist."))
        {
            return false;
        }

        std::println(
            "[structured.scene-cpu.material] stableKey='{}' name='{}' baseColor=({}, {}, {}, {}) texture(baseColor)={}",
            materialRecord.stableKey,
            materialRecord.cpu.name,
            materialRecord.cpu.baseColorFactor.x,
            materialRecord.cpu.baseColorFactor.y,
            materialRecord.cpu.baseColorFactor.z,
            materialRecord.cpu.baseColorFactor.w,
            materialRecord.cpu.baseColor.texture.packed());

        auto expectedMaterialData = buildExpectedMaterialGpuData(materialRecord.cpu);
        auto expectedMaterialBytes = asByteSpan(expectedMaterialData);
        printByteDump("upload.memory.material.expected", expectedMaterialBytes);

        auto materialReadbackSupported =
            (materialRecord.gpu->buffer.usage() & vk::BufferUsageFlagBits::eTransferSrc) != vk::BufferUsageFlags{};
        if (!require(materialReadbackSupported, "Material buffer lacks eTransferSrc usage; material readback is unsupported."))
        {
            return false;
        }

        auto materialTicket = uploadReadback.readbackBuffer(
            materialRecord.gpu->buffer,
            0,
            expectedMaterialBytes.size_bytes(),
            nr::rhi::QueueRole::Graphics,
            readbackSync);
        auto materialBytes = uploadReadback.readbackBytes(materialTicket);
        printByteDump("readback.material.actual", materialBytes);

        if (!requireBytesEqual("material readback", expectedMaterialBytes, materialBytes))
        {
            return false;
        }
    }

    if (handles->texture.has_value())
    {
        auto textureRecordRef = scene.tryGetTextureAsset(*handles->texture);
        if (!require(textureRecordRef.has_value(), "Texture record should exist after uploadPending."))
        {
            return false;
        }

        auto const &textureRecord = textureRecordRef->get();
        if (!require(textureRecord.gpuState == nr::scene::GpuResidencyState::resident, "Texture should be resident after uploadPending."))
        {
            return false;
        }
        if (!require(textureRecord.gpu.has_value(), "Texture gpu payload should exist."))
        {
            return false;
        }

        auto textureReadbackSupported =
            (textureRecord.gpu->image.usage() & vk::ImageUsageFlagBits::eTransferSrc) != vk::ImageUsageFlags{};
        if (!require(textureReadbackSupported, "Texture image lacks eTransferSrc usage; texture readback is unsupported."))
        {
            return false;
        }

        if (!textureRecord.cpu.levels.empty())
        {
            auto expectedTextureBytes = std::span{textureRecord.cpu.levels.front().bytes};
            std::println(
                "[structured.scene-cpu.texture] stableKey='{}' format={} extent={}x{} levels={}",
                textureRecord.stableKey,
                vk::to_string(textureRecord.cpu.format),
                textureRecord.cpu.width,
                textureRecord.cpu.height,
                textureRecord.cpu.levels.size());
            printByteDump("upload.memory.texture.expected", expectedTextureBytes);

            auto textureTicket = uploadReadback.readbackImage(
                textureRecord.gpu->image,
                textureRecord.gpu->layout,
                nr::rhi::QueueRole::Graphics,
                readbackSync);
            auto textureBytes = uploadReadback.readbackBytes(textureTicket);
            printByteDump("readback.texture.actual", textureBytes);

            if (!requireBytesEqual("texture readback", expectedTextureBytes, textureBytes))
            {
                return false;
            }
        }
        else
        {
            std::println("[info] texture exists but has no CPU pixel levels; skip texture byte compare.");
        }
    }
    else
    {
        std::println("[info] Triangle scene has no texture bridge entries; texture upload/readback section skipped.");
    }

    if (handles->camera.has_value())
    {
        auto cameraRecordRef = scene.tryGetCameraAsset(*handles->camera);
        if (!require(cameraRecordRef.has_value(), "Camera record should exist after uploadPending."))
        {
            return false;
        }

        auto const &cameraRecord = cameraRecordRef->get();
        if (!require(cameraRecord.gpuState == nr::scene::GpuResidencyState::resident, "Camera should be resident after uploadPending."))
        {
            return false;
        }
        if (!require(cameraRecord.gpu.has_value(), "Camera gpu payload should exist."))
        {
            return false;
        }

        auto expectedCameraData = buildExpectedCameraGpuData(cameraRecord.cpu);
        auto expectedCameraBytes = asByteSpan(expectedCameraData);
        printByteDump("upload.memory.camera.expected", expectedCameraBytes);

        if (!require(cameraRecord.gpu->buffer.mapped() != nullptr, "Camera buffer should be host mapped for direct verification."))
        {
            return false;
        }

        auto observedCameraBytes = std::span{
            static_cast<const std::byte *>(cameraRecord.gpu->buffer.mapped()),
            sizeof(nr::scene::detail::CameraGpuData)};
        printByteDump("readback.camera.direct-mapped.actual", observedCameraBytes);
        if (!requireBytesEqual("camera direct mapped", expectedCameraBytes, observedCameraBytes))
        {
            return false;
        }
    }
    else
    {
        std::println("[info] Triangle scene has no camera bridge entries; camera upload/readback section skipped.");
    }

    if (handles->light.has_value())
    {
        auto lightRecordRef = scene.tryGetLightAsset(*handles->light);
        if (!require(lightRecordRef.has_value(), "Light record should exist after uploadPending."))
        {
            return false;
        }

        auto const &lightRecord = lightRecordRef->get();
        if (!require(lightRecord.gpuState == nr::scene::GpuResidencyState::resident, "Light should be resident after uploadPending."))
        {
            return false;
        }
        if (!require(lightRecord.gpu.has_value(), "Light gpu payload should exist."))
        {
            return false;
        }

        auto expectedLightData = buildExpectedLightGpuData(lightRecord.cpu);
        auto expectedLightBytes = asByteSpan(expectedLightData);
        printByteDump("upload.memory.light.expected", expectedLightBytes);

        if (!require(lightRecord.gpu->buffer.mapped() != nullptr, "Light buffer should be host mapped for direct verification."))
        {
            return false;
        }

        auto observedLightBytes = std::span{
            static_cast<const std::byte *>(lightRecord.gpu->buffer.mapped()),
            sizeof(nr::scene::detail::LightGpuData)};
        printByteDump("readback.light.direct-mapped.actual", observedLightBytes);
        if (!requireBytesEqual("light direct mapped", expectedLightBytes, observedLightBytes))
        {
            return false;
        }
    }
    else
    {
        std::println("[info] Triangle scene has no light bridge entries; light upload/readback section skipped.");
    }

    return true;
}

} // namespace

int main()
{
    try
    {
        auto const cases = std::array{
            std::pair{"checkSceneUploadAndReadbackFromRealTriangle", &checkSceneUploadAndReadbackFromRealTriangle},
            std::pair{"checkDiscardUploadSourceRetentionPolicy", &checkDiscardUploadSourceRetentionPolicy},
        };

        std::size_t passedCount = 0;
        for (auto const &[name, fn] : cases)
        {
            std::println("\n[run] {}", name);
            auto const ok = fn();
            std::println("[result] {} => {}", name, ok ? "PASS" : "FAIL");
            if (ok)
            {
                ++passedCount;
            }
        }

        std::println("\n[summary] passed={} failed={}", passedCount, cases.size() - passedCount);
        if (passedCount != cases.size())
        {
            std::println("[FAIL] scene upload/readback validation failed");
            return 1;
        }

        std::println("[OK] scene upload/readback validation passed");
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::println("[error] exception: {}", exception.what());
        return 2;
    }
}
