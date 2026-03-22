module;
export module nr.load:assimp;

import dependency;
import :type;
import :backend;
import :decode;
import std;

namespace nr::load::detail
{
inline constexpr unsigned int assimpSceneFlagIncomplete = 0x1u;

[[nodiscard]] inline std::string toStdString(const aiString &value)
{
    auto const *raw = value.C_Str();
    return raw == nullptr ? std::string{} : std::string{raw};
}

[[nodiscard]] inline std::string textureTypeName(aiTextureType textureType)
{
    switch (textureType)
    {
    case aiTextureType_NONE: return "none";
    case aiTextureType_DIFFUSE: return "diffuse";
    case aiTextureType_SPECULAR: return "specular";
    case aiTextureType_AMBIENT: return "ambient";
    case aiTextureType_EMISSIVE: return "emissive";
    case aiTextureType_HEIGHT: return "height";
    case aiTextureType_NORMALS: return "normals";
    case aiTextureType_SHININESS: return "shininess";
    case aiTextureType_OPACITY: return "opacity";
    case aiTextureType_DISPLACEMENT: return "displacement";
    case aiTextureType_LIGHTMAP: return "lightmap";
    case aiTextureType_REFLECTION: return "reflection";
    case aiTextureType_UNKNOWN: return "unknown";
    default: return std::format("type_{}", static_cast<unsigned>(textureType));
    }
}

[[nodiscard]] inline std::string lightSourceTypeName(aiLightSourceType lightType)
{
    switch (lightType)
    {
    case aiLightSource_UNDEFINED: return "undefined";
    case aiLightSource_DIRECTIONAL: return "directional";
    case aiLightSource_POINT: return "point";
    case aiLightSource_SPOT: return "spot";
    case aiLightSource_AMBIENT: return "ambient";
    case aiLightSource_AREA: return "area";
    default: return std::format("type_{}", static_cast<unsigned>(lightType));
    }
}

[[nodiscard]] inline std::string normalizeTextureKey(std::string_view key)
{
    std::string normalized{key};
    std::ranges::transform(normalized, normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return normalized;
}

[[nodiscard]] inline std::string formatHintToString(const aiTexture &texture)
{
    auto rawView = std::string_view{texture.achFormatHint, std::size(texture.achFormatHint)};
    auto terminator = rawView.find('\0');
    if (terminator != std::string_view::npos)
    {
        rawView = rawView.substr(0, terminator);
    }
    return std::string{rawView};
}

[[nodiscard]] inline std::optional<uint32_t> parseEmbeddedTextureIndex(std::string_view reference)
{
    if (reference.size() < 2 || reference.front() != '*')
    {
        return std::nullopt;
    }

    uint64_t value = 0;
    auto digits = reference.substr(1);
    auto *first = digits.data();
    auto *last = digits.data() + digits.size();
    auto [parsedEnd, parseError] = std::from_chars(first, last, value);
    if (parseError != std::errc{} || parsedEnd != last || value > std::numeric_limits<uint32_t>::max())
    {
        return std::nullopt;
    }

    return static_cast<uint32_t>(value);
}

[[nodiscard]] inline std::array<float, 16> toMatrix(const aiMatrix4x4 &matrix)
{
    return {
        matrix.a1,
        matrix.a2,
        matrix.a3,
        matrix.a4,
        matrix.b1,
        matrix.b2,
        matrix.b3,
        matrix.b4,
        matrix.c1,
        matrix.c2,
        matrix.c3,
        matrix.c4,
        matrix.d1,
        matrix.d2,
        matrix.d3,
        matrix.d4,
    };
}

[[nodiscard]] inline std::array<float, 3> toVector3(const aiVector3D &value)
{
    return {value.x, value.y, value.z};
}

[[nodiscard]] inline std::array<float, 3> toColor3(const aiColor3D &value)
{
    return {value.r, value.g, value.b};
}

[[nodiscard]] inline std::filesystem::path resolveTexturePath(const std::filesystem::path &baseDirectory,
                                                              std::string_view sourcePath)
{
    auto path = std::filesystem::path{sourcePath};
    if (path.is_absolute())
    {
        return path;
    }
    return (baseDirectory / path).lexically_normal();
}

[[nodiscard]] inline std::optional<LoadError> appendEmbeddedTexture(const aiTexture &texture,
                                                                     uint32_t textureIndex,
                                                                     SceneAsset &scene)
{
    TextureAsset textureAsset{};
    textureAsset.key = std::format("*{}", textureIndex);

    if (texture.mHeight == 0)
    {
        auto byteCount = static_cast<size_t>(texture.mWidth);
        auto const *byteSource = reinterpret_cast<const std::byte *>(texture.pcData);
        if (byteCount > 0 && byteSource == nullptr)
        {
            return makeLoadError(
                LoadErrorCode::textureDataUnsupported,
                "assimp",
                scene.sourcePath,
                std::format("Embedded compressed texture {} has null data pointer.", textureAsset.key));
        }

        EmbeddedCompressedTexture compressed{};
        compressed.formatHint = formatHintToString(texture);
        compressed.bytes.resize(byteCount);
        if (byteCount > 0)
        {
            std::ranges::copy(byteSource, byteSource + byteCount, compressed.bytes.begin());
        }

        textureAsset.payloadKind = TexturePayloadKind::embeddedCompressedBlob;
        textureAsset.compressed = std::move(compressed);
        scene.textures.push_back(std::move(textureAsset));
        return std::nullopt;
    }

    auto width = static_cast<size_t>(texture.mWidth);
    auto height = static_cast<size_t>(texture.mHeight);
    if (width == 0 || height == 0 || texture.pcData == nullptr)
    {
        return makeLoadError(
            LoadErrorCode::textureDataUnsupported,
            "assimp",
            scene.sourcePath,
            std::format("Embedded raw texture {} has invalid dimensions or null data.", textureAsset.key));
    }

    auto texelCount = width * height;
    if (texelCount > std::numeric_limits<size_t>::max() / 4)
    {
        return makeLoadError(
            LoadErrorCode::textureDataUnsupported,
            "assimp",
            scene.sourcePath,
            std::format("Embedded raw texture {} size overflows host memory limits.", textureAsset.key));
    }

    EmbeddedRawTexture raw{};
    raw.width = static_cast<uint32_t>(width);
    raw.height = static_cast<uint32_t>(height);
    raw.rgba8.resize(texelCount * 4);

    for (auto texelIndex : std::views::iota(size_t{0}, texelCount))
    {
        auto const &texel = texture.pcData[texelIndex];
        auto const byteBase = texelIndex * 4;

        raw.rgba8[byteBase + 0] = static_cast<std::byte>(texel.r);
        raw.rgba8[byteBase + 1] = static_cast<std::byte>(texel.g);
        raw.rgba8[byteBase + 2] = static_cast<std::byte>(texel.b);
        raw.rgba8[byteBase + 3] = static_cast<std::byte>(texel.a);
    }

    textureAsset.payloadKind = TexturePayloadKind::embeddedRawRgba8;
    textureAsset.rawRgba8 = std::move(raw);
    scene.textures.push_back(std::move(textureAsset));

    return std::nullopt;
}

[[nodiscard]] inline std::optional<LoadError> appendEmbeddedTextures(const aiScene &assimpScene, SceneAsset &scene)
{
    auto textureIndices = std::views::iota(0u, assimpScene.mNumTextures);
    for (auto textureIndex : textureIndices)
    {
        auto const *texture = assimpScene.mTextures[textureIndex];
        if (texture == nullptr)
        {
            return makeLoadError(
                LoadErrorCode::invalidScene,
                "assimp",
                scene.sourcePath,
                std::format("Scene texture {} is null.", textureIndex));
        }

        auto error = appendEmbeddedTexture(*texture, textureIndex, scene);
        if (error.has_value())
        {
            return error;
        }
    }

    return std::nullopt;
}

[[nodiscard]] inline uint32_t textureIndexFromKey(const std::map<std::string, uint32_t> &indexByKey,
                                                  std::string_view key)
{
    auto found = indexByKey.find(std::string{key});
    if (found == indexByKey.end())
    {
        return invalidIndex;
    }
    return found->second;
}

[[nodiscard]] inline std::optional<LoadError> appendMaterials(const aiScene &assimpScene,
                                                              const std::filesystem::path &baseDirectory,
                                                              SceneAsset &scene)
{
    auto textureIndexByKey = std::map<std::string, uint32_t>{};
    for (auto textureIndex : std::views::iota(uint32_t{0}, static_cast<uint32_t>(scene.textures.size())))
    {
        textureIndexByKey.emplace(scene.textures[textureIndex].key, textureIndex);
    }

    auto materialIndices = std::views::iota(0u, assimpScene.mNumMaterials);
    for (auto materialIndex : materialIndices)
    {
        auto const *material = assimpScene.mMaterials[materialIndex];
        if (material == nullptr)
        {
            return makeLoadError(
                LoadErrorCode::invalidScene,
                "assimp",
                scene.sourcePath,
                std::format("Scene material {} is null.", materialIndex));
        }

        MaterialAsset materialAsset{};
        materialAsset.name = toStdString(material->GetName());
        if (materialAsset.name.empty())
        {
            materialAsset.name = std::format("material_{}", materialIndex);
        }

        auto textureTypeRange = std::views::iota(0u, static_cast<unsigned>(aiTextureType_UNKNOWN) + 1u);
        for (auto textureTypeRaw : textureTypeRange)
        {
            auto textureType = static_cast<aiTextureType>(textureTypeRaw);
            auto textureCount = material->GetTextureCount(textureType);
            auto textureSlots = std::views::iota(0u, textureCount);
            for (auto slotIndex : textureSlots)
            {
                aiString texturePath{};
                unsigned int uvChannel = 0;
                auto textureQuery = material->GetTexture(textureType, slotIndex, &texturePath, nullptr, &uvChannel, nullptr, nullptr, nullptr);
                if (textureQuery != aiReturn_SUCCESS)
                {
                    continue;
                }

                auto rawTexturePath = toStdString(texturePath);
                if (rawTexturePath.empty())
                {
                    continue;
                }

                uint32_t resolvedTextureIndex = invalidIndex;
                auto embeddedIndex = parseEmbeddedTextureIndex(rawTexturePath);
                if (embeddedIndex.has_value())
                {
                    auto embeddedKey = std::format("*{}", *embeddedIndex);
                    resolvedTextureIndex = textureIndexFromKey(textureIndexByKey, embeddedKey);
                    if (resolvedTextureIndex == invalidIndex)
                    {
                        return makeLoadError(
                            LoadErrorCode::invalidScene,
                            "assimp",
                            scene.sourcePath,
                            std::format("Material '{}' references missing embedded texture '{}'.", materialAsset.name, embeddedKey));
                    }
                }
                else
                {
                    auto resolvedPath = resolveTexturePath(baseDirectory, rawTexturePath);
                    auto normalizedKey = normalizeTextureKey(resolvedPath.generic_string());
                    resolvedTextureIndex = textureIndexFromKey(textureIndexByKey, normalizedKey);
                    if (resolvedTextureIndex == invalidIndex)
                    {
                        TextureAsset externalTexture{};
                        externalTexture.key = normalizedKey;
                        externalTexture.resolvedPath = std::move(resolvedPath);
                        externalTexture.payloadKind = TexturePayloadKind::externalReference;

                        scene.textures.push_back(std::move(externalTexture));
                        resolvedTextureIndex = static_cast<uint32_t>(scene.textures.size() - 1);
                        textureIndexByKey.emplace(normalizedKey, resolvedTextureIndex);
                    }
                }

                materialAsset.textures.push_back(MaterialTextureBinding{
                    .textureIndex = resolvedTextureIndex,
                    .uvChannel = uvChannel,
                    .textureTypeRaw = textureTypeRaw,
                    .semantic = textureTypeName(textureType),
                });
            }
        }

        scene.materials.push_back(std::move(materialAsset));
    }

    return std::nullopt;
}

[[nodiscard]] inline std::optional<LoadError> appendMeshes(const aiScene &assimpScene,
                                                           SceneAsset &scene,
                                                           bool strict)
{
    auto meshIndices = std::views::iota(0u, assimpScene.mNumMeshes);
    for (auto meshIndex : meshIndices)
    {
        auto const *mesh = assimpScene.mMeshes[meshIndex];
        if (mesh == nullptr)
        {
            return makeLoadError(
                LoadErrorCode::invalidScene,
                "assimp",
                scene.sourcePath,
                std::format("Scene mesh {} is null.", meshIndex));
        }

        if ((mesh->mNumVertices == 0 || mesh->mVertices == nullptr) && strict)
        {
            return makeLoadError(
                LoadErrorCode::invalidScene,
                "assimp",
                scene.sourcePath,
                std::format("Mesh {} has no vertex data.", meshIndex));
        }

        MeshAsset meshAsset{};
        meshAsset.name = toStdString(mesh->mName);
        if (meshAsset.name.empty())
        {
            meshAsset.name = std::format("mesh_{}", meshIndex);
        }

        meshAsset.materialIndex = mesh->mMaterialIndex < assimpScene.mNumMaterials
                                      ? mesh->mMaterialIndex
                                      : invalidIndex;

        meshAsset.vertices.reserve(mesh->mNumVertices);
        auto vertexIndices = std::views::iota(0u, mesh->mNumVertices);
        for (auto vertexIndex : vertexIndices)
        {
            VertexAsset vertex{};

            auto const &position = mesh->mVertices[vertexIndex];
            vertex.position = {position.x, position.y, position.z};

            if (mesh->HasNormals() && mesh->mNormals != nullptr)
            {
                auto const &normal = mesh->mNormals[vertexIndex];
                vertex.normal = {normal.x, normal.y, normal.z};
            }

            if (mesh->HasTangentsAndBitangents() && mesh->mTangents != nullptr)
            {
                auto const &tangent = mesh->mTangents[vertexIndex];
                vertex.tangent = {tangent.x, tangent.y, tangent.z, 1.0f};
            }

            if (mesh->HasTextureCoords(0) && mesh->mTextureCoords[0] != nullptr)
            {
                auto const &uv = mesh->mTextureCoords[0][vertexIndex];
                vertex.texCoord0 = {uv.x, uv.y};
            }

            if (mesh->HasVertexColors(0) && mesh->mColors[0] != nullptr)
            {
                auto const &color = mesh->mColors[0][vertexIndex];
                vertex.color0 = {color.r, color.g, color.b, color.a};
            }

            meshAsset.vertices.push_back(vertex);
        }

        meshAsset.indices.reserve(static_cast<size_t>(mesh->mNumFaces) * 3u);
        auto faceIndices = std::views::iota(0u, mesh->mNumFaces);
        for (auto faceIndex : faceIndices)
        {
            auto const &face = mesh->mFaces[faceIndex];
            if (face.mNumIndices == 0)
            {
                continue;
            }

            if (strict && face.mNumIndices < 3)
            {
                return makeLoadError(
                    LoadErrorCode::invalidScene,
                    "assimp",
                    scene.sourcePath,
                    std::format("Mesh '{}' contains a face with fewer than 3 indices.", meshAsset.name));
            }

            auto localIndexRange = std::views::iota(0u, face.mNumIndices);
            for (auto localIndex : localIndexRange)
            {
                meshAsset.indices.push_back(face.mIndices[localIndex]);
            }
        }

        scene.meshes.push_back(std::move(meshAsset));
    }

    return std::nullopt;
}

[[nodiscard]] inline uint32_t appendNodeRecursive(const aiNode &node,
                                                  uint32_t parentIndex,
                                                  SceneAsset &scene)
{
    auto nodeIndex = static_cast<uint32_t>(scene.nodes.size());

    NodeAsset nodeAsset{};
    nodeAsset.name = toStdString(node.mName);
    if (nodeAsset.name.empty())
    {
        nodeAsset.name = std::format("node_{}", nodeIndex);
    }
    nodeAsset.parentIndex = parentIndex;
    nodeAsset.localTransform = toMatrix(node.mTransformation);
    nodeAsset.meshIndices.reserve(node.mNumMeshes);

    auto meshSlots = std::views::iota(0u, node.mNumMeshes);
    for (auto meshSlot : meshSlots)
    {
        nodeAsset.meshIndices.push_back(node.mMeshes[meshSlot]);
    }

    scene.nodes.push_back(std::move(nodeAsset));

    auto childSlots = std::views::iota(0u, node.mNumChildren);
    for (auto childSlot : childSlots)
    {
        auto const *child = node.mChildren[childSlot];
        if (child == nullptr)
        {
            continue;
        }

        auto childIndex = appendNodeRecursive(*child, nodeIndex, scene);
        scene.nodes[nodeIndex].childIndices.push_back(childIndex);
    }

    return nodeIndex;
}

[[nodiscard]] inline std::optional<LoadError> appendNodes(const aiScene &assimpScene, SceneAsset &scene)
{
    if (assimpScene.mRootNode == nullptr)
    {
        return makeLoadError(
            LoadErrorCode::invalidScene,
            "assimp",
            scene.sourcePath,
            "Assimp scene root node is null.");
    }

    scene.rootNodeIndex = appendNodeRecursive(*assimpScene.mRootNode, invalidIndex, scene);
    return std::nullopt;
}

[[nodiscard]] inline std::map<std::string, std::vector<uint32_t>> buildNodeIndicesByName(const SceneAsset &scene)
{
    auto nodeIndicesByName = std::map<std::string, std::vector<uint32_t>>{};

    auto nodeIndices = std::views::iota(uint32_t{0}, static_cast<uint32_t>(scene.nodes.size()));
    for (auto nodeIndex : nodeIndices)
    {
        auto const &node = scene.nodes[nodeIndex];
        if (node.name.empty())
        {
            continue;
        }

        nodeIndicesByName[node.name].push_back(nodeIndex);
    }

    return nodeIndicesByName;
}

[[nodiscard]] inline std::optional<LoadError> resolveNodeIndexByName(
    const std::map<std::string, std::vector<uint32_t>> &nodeIndicesByName,
    std::string_view nodeName,
    std::string_view assetKind,
    uint32_t assetIndex,
    bool strict,
    const SceneAsset &scene,
    uint32_t &resolvedNodeIndex)
{
    resolvedNodeIndex = invalidIndex;
    if (nodeName.empty())
    {
        return std::nullopt;
    }

    auto found = nodeIndicesByName.find(std::string{nodeName});
    if (found == nodeIndicesByName.end())
    {
        if (!strict)
        {
            return std::nullopt;
        }

        return makeLoadError(
            LoadErrorCode::invalidScene,
            "assimp",
            scene.sourcePath,
            std::format("{} {} references missing node '{}'.", assetKind, assetIndex, nodeName));
    }

    auto const &matches = found->second;
    if (matches.empty())
    {
        return std::nullopt;
    }

    resolvedNodeIndex = matches.front();
    if (matches.size() > 1 && strict)
    {
        return makeLoadError(
            LoadErrorCode::invalidScene,
            "assimp",
            scene.sourcePath,
            std::format("{} {} references non-unique node name '{}' ({} matches).", assetKind, assetIndex, nodeName, matches.size()));
    }

    return std::nullopt;
}

[[nodiscard]] inline std::optional<LoadError> appendCameras(
    const aiScene &assimpScene,
    const std::map<std::string, std::vector<uint32_t>> &nodeIndicesByName,
    SceneAsset &scene,
    bool strict)
{
    auto cameraIndices = std::views::iota(0u, assimpScene.mNumCameras);
    for (auto cameraIndex : cameraIndices)
    {
        auto const *camera = assimpScene.mCameras[cameraIndex];
        if (camera == nullptr)
        {
            return makeLoadError(
                LoadErrorCode::invalidScene,
                "assimp",
                scene.sourcePath,
                std::format("Scene camera {} is null.", cameraIndex));
        }

        auto sourceNodeName = toStdString(camera->mName);

        CameraAsset cameraAsset{};
        cameraAsset.name = sourceNodeName.empty() ? std::format("camera_{}", cameraIndex) : sourceNodeName;
        cameraAsset.sourceNodeName = sourceNodeName;
        cameraAsset.position = toVector3(camera->mPosition);
        cameraAsset.lookAt = toVector3(camera->mLookAt);
        cameraAsset.up = toVector3(camera->mUp);
        cameraAsset.horizontalFov = camera->mHorizontalFOV;
        cameraAsset.aspect = camera->mAspect;
        cameraAsset.nearPlane = camera->mClipPlaneNear;
        cameraAsset.farPlane = camera->mClipPlaneFar;
        cameraAsset.orthographicWidth = camera->mOrthographicWidth;

        if (auto error = resolveNodeIndexByName(nodeIndicesByName,
                                                sourceNodeName,
                                                "camera",
                                                cameraIndex,
                                                strict,
                                                scene,
                                                cameraAsset.nodeIndex);
            error.has_value())
        {
            return error;
        }

        scene.cameras.push_back(std::move(cameraAsset));
    }

    return std::nullopt;
}

[[nodiscard]] inline std::optional<LoadError> appendLights(
    const aiScene &assimpScene,
    const std::map<std::string, std::vector<uint32_t>> &nodeIndicesByName,
    SceneAsset &scene,
    bool strict)
{
    auto lightIndices = std::views::iota(0u, assimpScene.mNumLights);
    for (auto lightIndex : lightIndices)
    {
        auto const *light = assimpScene.mLights[lightIndex];
        if (light == nullptr)
        {
            return makeLoadError(
                LoadErrorCode::invalidScene,
                "assimp",
                scene.sourcePath,
                std::format("Scene light {} is null.", lightIndex));
        }

        auto sourceNodeName = toStdString(light->mName);

        LightAsset lightAsset{};
        lightAsset.name = sourceNodeName.empty() ? std::format("light_{}", lightIndex) : sourceNodeName;
        lightAsset.sourceNodeName = sourceNodeName;
        lightAsset.typeRaw = static_cast<uint32_t>(light->mType);
        lightAsset.type = lightSourceTypeName(light->mType);
        lightAsset.position = toVector3(light->mPosition);
        lightAsset.direction = toVector3(light->mDirection);
        lightAsset.up = toVector3(light->mUp);
        lightAsset.colorDiffuse = toColor3(light->mColorDiffuse);
        lightAsset.colorSpecular = toColor3(light->mColorSpecular);
        lightAsset.colorAmbient = toColor3(light->mColorAmbient);
        lightAsset.attenuationConstant = light->mAttenuationConstant;
        lightAsset.attenuationLinear = light->mAttenuationLinear;
        lightAsset.attenuationQuadratic = light->mAttenuationQuadratic;
        lightAsset.innerCone = light->mAngleInnerCone;
        lightAsset.outerCone = light->mAngleOuterCone;
        lightAsset.areaSize = {light->mSize.x, light->mSize.y};

        if (auto error = resolveNodeIndexByName(nodeIndicesByName,
                                                sourceNodeName,
                                                "light",
                                                lightIndex,
                                                strict,
                                                scene,
                                                lightAsset.nodeIndex);
            error.has_value())
        {
            return error;
        }

        scene.lights.push_back(std::move(lightAsset));
    }

    return std::nullopt;
}

[[nodiscard]] inline uint32_t totalVertexCount(const SceneAsset &scene)
{
    auto vertexSizes = scene.meshes | std::views::transform([](const MeshAsset &mesh) {
        return static_cast<uint64_t>(mesh.vertices.size());
    });
    auto total = std::accumulate(vertexSizes.begin(), vertexSizes.end(), uint64_t{0});
    return static_cast<uint32_t>(std::min<uint64_t>(total, std::numeric_limits<uint32_t>::max()));
}

[[nodiscard]] inline uint32_t totalIndexCount(const SceneAsset &scene)
{
    auto indexSizes = scene.meshes | std::views::transform([](const MeshAsset &mesh) {
        return static_cast<uint64_t>(mesh.indices.size());
    });
    auto total = std::accumulate(indexSizes.begin(), indexSizes.end(), uint64_t{0});
    return static_cast<uint32_t>(std::min<uint64_t>(total, std::numeric_limits<uint32_t>::max()));
}

[[nodiscard]] inline aiPostProcessSteps buildPostProcessFlags(const SceneLoadRequest &request)
{
    uint32_t flags = aiProcess_SortByPType;

    if (request.triangulate)
    {
        flags |= aiProcess_Triangulate;
    }
    if (request.joinIdenticalVertices)
    {
        flags |= aiProcess_JoinIdenticalVertices;
    }
    if (request.generateNormals)
    {
        flags |= aiProcess_GenSmoothNormals;
    }
    if (request.generateTangents)
    {
        flags |= aiProcess_CalcTangentSpace;
    }
    if (request.validateDataStructure)
    {
        flags |= aiProcess_ValidateDataStructure;
    }
    if (request.preTransformVertices)
    {
        flags |= aiProcess_PreTransformVertices;
    }
    if (request.optimizeMeshes)
    {
        flags |= aiProcess_OptimizeMeshes;
    }
    if (request.optimizeGraph)
    {
        flags |= aiProcess_OptimizeGraph;
    }

    return static_cast<aiPostProcessSteps>(flags);
}

} // namespace nr::load::detail

export namespace nr::load
{
struct AssimpSceneImporter : SceneImporterBackendBase<AssimpSceneImporter>
{
    static constexpr std::string_view kBackendName = "assimp";
    static constexpr std::array<std::string_view, 4> kSupportedExtensions = {
        ".gltf",
        ".glb",
        ".fbx",
        ".obj",
    };

    [[nodiscard]] static bool supportsExtension(std::string_view extension)
    {
        return std::ranges::find(kSupportedExtensions, extension) != kSupportedExtensions.end();
    }

    [[nodiscard]] static SceneImportResult importScene(const SceneLoadRequest &request)
    {
        if (request.sourcePath.empty())
        {
            return SceneImportResult{
                std::unexpected(makeError(
                    LoadErrorCode::invalidArgument,
                    request.sourcePath,
                    "Scene source path is empty."))};
        }

        auto extension = normalizedExtension(request.sourcePath);
        if (!supportsExtension(extension))
        {
            return SceneImportResult{
                std::unexpected(makeError(
                    LoadErrorCode::unsupportedFormat,
                    request.sourcePath,
                    std::format("Unsupported file extension '{}' for assimp backend.", extension)))};
        }

        if (!std::filesystem::exists(request.sourcePath))
        {
            return SceneImportResult{
                std::unexpected(makeError(
                    LoadErrorCode::fileNotFound,
                    request.sourcePath,
                    "Source asset file does not exist."))};
        }

        Assimp::Importer importer{};
        auto flags = detail::buildPostProcessFlags(request);
        auto const *assimpScene = importer.ReadFile(request.sourcePath.string(), flags);
        if (assimpScene == nullptr)
        {
            return SceneImportResult{
                std::unexpected(makeError(
                    LoadErrorCode::importFailed,
                    request.sourcePath,
                    std::format("Assimp import failed: {}", importer.GetErrorString())))};
        }

        if ((assimpScene->mFlags & detail::assimpSceneFlagIncomplete) != 0 || assimpScene->mRootNode == nullptr)
        {
            return SceneImportResult{
                std::unexpected(makeError(
                    LoadErrorCode::invalidScene,
                    request.sourcePath,
                    "Assimp returned an incomplete scene graph."))};
        }

        SceneAsset scene{};
        scene.sourcePath = request.sourcePath;

        if (auto error = detail::appendEmbeddedTextures(*assimpScene, scene); error.has_value())
        {
            return SceneImportResult{std::unexpected(std::move(*error))};
        }

        auto baseDirectory = request.searchRoot.empty() ? request.sourcePath.parent_path() : request.searchRoot;
        if (auto error = detail::appendMaterials(*assimpScene, baseDirectory, scene); error.has_value())
        {
            return SceneImportResult{std::unexpected(std::move(*error))};
        }

        if (auto decodeResult = decodeSceneTextureImages(scene); !decodeResult.has_value())
        {
            return SceneImportResult{std::unexpected(std::move(decodeResult.error()))};
        }

        if (auto error = detail::appendMeshes(*assimpScene, scene, request.strict); error.has_value())
        {
            return SceneImportResult{std::unexpected(std::move(*error))};
        }

        if (auto error = detail::appendNodes(*assimpScene, scene); error.has_value())
        {
            return SceneImportResult{std::unexpected(std::move(*error))};
        }

        auto nodeIndicesByName = detail::buildNodeIndicesByName(scene);
        if (auto error = detail::appendCameras(*assimpScene, nodeIndicesByName, scene, request.strict); error.has_value())
        {
            return SceneImportResult{std::unexpected(std::move(*error))};
        }

        if (auto error = detail::appendLights(*assimpScene, nodeIndicesByName, scene, request.strict); error.has_value())
        {
            return SceneImportResult{std::unexpected(std::move(*error))};
        }

        scene.stats.nodeCount = static_cast<uint32_t>(scene.nodes.size());
        scene.stats.meshCount = static_cast<uint32_t>(scene.meshes.size());
        scene.stats.materialCount = static_cast<uint32_t>(scene.materials.size());
        scene.stats.textureCount = static_cast<uint32_t>(scene.textures.size());
        scene.stats.cameraCount = static_cast<uint32_t>(scene.cameras.size());
        scene.stats.lightCount = static_cast<uint32_t>(scene.lights.size());
        scene.stats.vertexCount = detail::totalVertexCount(scene);
        scene.stats.indexCount = detail::totalIndexCount(scene);

        return scene;
    }
};

} // namespace nr::load
