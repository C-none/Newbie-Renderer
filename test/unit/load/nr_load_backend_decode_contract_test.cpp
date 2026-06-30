import std;
import nr.load;
import nr.resource;
import nr.test;

namespace
{
struct FooImporter : nr::load::SceneImporterBackendBase<FooImporter>
{
    [[nodiscard]] static bool supportsExtension(std::string_view extension)
    {
        return extension == ".foo";
    }

    [[nodiscard]] static nr::load::SceneImportResult importScene(const nr::load::SceneLoadRequest &request)
    {
        auto scene = nr::load::SceneAsset{};
        scene.sourcePath = request.sourcePath;
        scene.stats.nodeCount = 1;
        return scene;
    }
};

[[nodiscard]] nr::load::TextureAsset rawTexture(std::string key, std::uint32_t width, std::uint32_t height)
{
    auto texture = nr::load::TextureAsset{};
    texture.key = std::move(key);
    texture.payloadKind = nr::load::TexturePayloadKind::embeddedRawRgba8;
    texture.rawRgba8 = nr::load::EmbeddedRawTexture{
        .width = width,
        .height = height,
        .rgba8 = std::vector<std::byte>(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u),
    };
    return texture;
}

const nr::test::CaseRegistrar backendDispatchCase{
    "load backend registry validates path and extension",
    [] {
        using Registry = std::tuple<FooImporter>;

        auto empty = nr::load::SceneImporterRegistry<Registry>::import(nr::load::SceneLoadRequest{});
        nr::test::require(!empty.has_value(), "empty source path should fail");
        nr::test::require(empty.error().code == nr::load::LoadErrorCode::invalidArgument, "empty source path should be invalidArgument");
        nr::test::requireEqual(empty.error().backend, std::string{"registry"});

        auto unsupported = nr::load::SceneImporterRegistry<Registry>::import(nr::load::SceneLoadRequest{
            .sourcePath = std::filesystem::path{"asset.bar"},
        });
        nr::test::require(!unsupported.has_value(), "unsupported extension should fail");
        nr::test::require(unsupported.error().code == nr::load::LoadErrorCode::unsupportedFormat, "unsupported extension should be unsupportedFormat");

        auto imported = nr::load::SceneImporterRegistry<Registry>::import(nr::load::SceneLoadRequest{
            .sourcePath = std::filesystem::path{"Asset.FOO"},
        });
        nr::test::require(imported.has_value(), "case-normalized .foo extension should dispatch");
        nr::test::requireEqual(imported->sourcePath.generic_string(), std::string{"Asset.FOO"});
    }};

const nr::test::CaseRegistrar assimpTextureSemanticCase{
    "assimp material texture semantics map to resource slots",
    [] {
        using enum nr::resource::MaterialTextureSlotSemantic;

        struct SemanticCase
        {
            std::uint32_t textureTypeRaw = 0;
            std::uint32_t textureSlot = 0;
            nr::resource::MaterialTextureSlotSemantic expected = unsupported;
        };

        auto cases = std::array{
            SemanticCase{.textureTypeRaw = 1u, .expected = baseColor},
            SemanticCase{.textureTypeRaw = 12u, .expected = baseColor},
            SemanticCase{.textureTypeRaw = 22u, .expected = baseColor},
            SemanticCase{.textureTypeRaw = 6u, .expected = normal},
            SemanticCase{.textureTypeRaw = 13u, .expected = normal},
            SemanticCase{.textureTypeRaw = 5u, .expected = normal},
            SemanticCase{.textureTypeRaw = 9u, .expected = normal},
            SemanticCase{.textureTypeRaw = 10u, .expected = occlusion},
            SemanticCase{.textureTypeRaw = 3u, .expected = occlusion},
            SemanticCase{.textureTypeRaw = 17u, .expected = occlusion},
            SemanticCase{.textureTypeRaw = 4u, .expected = emissive},
            SemanticCase{.textureTypeRaw = 14u, .expected = emissive},
            SemanticCase{.textureTypeRaw = 15u, .expected = unsupported},
            SemanticCase{.textureTypeRaw = 16u, .expected = unsupported},
            SemanticCase{.textureTypeRaw = 27u, .expected = metallicRoughness},
            SemanticCase{.textureTypeRaw = 2u, .expected = unsupported},
            SemanticCase{.textureTypeRaw = 7u, .expected = unsupported},
            SemanticCase{.textureTypeRaw = 23u, .expected = unsupported},
            SemanticCase{.textureTypeRaw = 24u, .expected = unsupported},
            SemanticCase{.textureTypeRaw = 25u, .expected = unsupported},
            SemanticCase{.textureTypeRaw = 20u, .textureSlot = 0u, .expected = clearcoat},
            SemanticCase{.textureTypeRaw = 20u, .textureSlot = 1u, .expected = clearcoatRoughness},
            SemanticCase{.textureTypeRaw = 20u, .textureSlot = 2u, .expected = clearcoatNormal},
            SemanticCase{.textureTypeRaw = 19u, .textureSlot = 0u, .expected = sheenColor},
            SemanticCase{.textureTypeRaw = 19u, .textureSlot = 1u, .expected = sheenRoughness},
            SemanticCase{.textureTypeRaw = 21u, .textureSlot = 0u, .expected = transmission},
            SemanticCase{.textureTypeRaw = 21u, .textureSlot = 1u, .expected = unsupported},
            SemanticCase{.textureTypeRaw = 26u, .textureSlot = 0u, .expected = anisotropy},
            SemanticCase{.textureTypeRaw = 8u, .expected = unsupported},
            SemanticCase{.textureTypeRaw = 11u, .expected = unsupported},
            SemanticCase{.textureTypeRaw = 18u, .expected = unsupported},
        };

        std::ranges::for_each(cases, [](SemanticCase semanticCase) {
            nr::test::require(nr::load::assimpTextureSlotSemantic(semanticCase.textureTypeRaw, semanticCase.textureSlot) ==
                                  semanticCase.expected,
                              std::format("unexpected semantic mapping for Assimp type {} slot {}",
                                          semanticCase.textureTypeRaw,
                                          semanticCase.textureSlot));
        });
    }};

const nr::test::CaseRegistrar embeddedRawDecodeCase{
    "load texture decode only touches material-referenced textures",
    [] {
        auto scene = nr::load::SceneAsset{};
        scene.sourcePath = std::filesystem::path{"decode_contract.gltf"};
        scene.textures.push_back(rawTexture("referenced", 2u, 1u));

        auto unreferenced = nr::load::TextureAsset{};
        unreferenced.key = "unreferenced-invalid";
        unreferenced.payloadKind = nr::load::TexturePayloadKind::embeddedRawRgba8;
        scene.textures.push_back(std::move(unreferenced));

        auto material = nr::load::MaterialAsset{};
        material.textures.push_back(nr::load::MaterialTextureBinding{.textureIndex = 0});
        scene.materials.push_back(std::move(material));

        auto result = nr::load::decodeSceneTextureImages(scene, nr::load::TextureDecodeOptions{.workerCount = 1});
        nr::test::require(result.has_value(), "referenced embedded raw texture should decode");
        nr::test::require(scene.textures[0].decodedImage.has_value(), "referenced texture should receive decoded image");
        nr::test::requireEqual(scene.textures[0].decodedImage->width, 2u);
        nr::test::requireEqual(scene.textures[0].decodedImage->height, 1u);
        nr::test::requireEqual(scene.textures[0].decodedImage->channels, 4u);
        nr::test::requireEqual(scene.textures[0].decodedImage->pixels.size(), std::size_t{8});
        nr::test::requireEqual(scene.textures[0].decodeBackend, std::string{"assimp-raw-copy"});
        nr::test::require(!scene.textures[1].decodedImage.has_value(), "unreferenced invalid texture should not be decoded");
    }};

const nr::test::CaseRegistrar embeddedRawAutoWorkerDecodeCase{
    "load texture decode supports automatic multi-worker scheduling",
    [] {
        auto scene = nr::load::SceneAsset{};
        scene.sourcePath = std::filesystem::path{"decode_auto_workers.gltf"};
        scene.textures.push_back(rawTexture("referenced-a", 1u, 1u));
        scene.textures.push_back(rawTexture("referenced-b", 2u, 2u));

        auto material = nr::load::MaterialAsset{};
        material.textures.push_back(nr::load::MaterialTextureBinding{.textureIndex = 0});
        material.textures.push_back(nr::load::MaterialTextureBinding{.textureIndex = 1});
        scene.materials.push_back(std::move(material));

        auto result = nr::load::decodeSceneTextureImages(scene, nr::load::TextureDecodeOptions{});
        nr::test::require(result.has_value(), "referenced embedded raw textures should decode with automatic workers");
        nr::test::require(scene.textures[0].decodedImage.has_value(), "first referenced texture should receive decoded image");
        nr::test::require(scene.textures[1].decodedImage.has_value(), "second referenced texture should receive decoded image");
        nr::test::requireEqual(scene.textures[0].decodedImage->pixels.size(), std::size_t{4});
        nr::test::requireEqual(scene.textures[1].decodedImage->pixels.size(), std::size_t{16});
        nr::test::requireEqual(scene.textures[0].decodeBackend, std::string{"assimp-raw-copy"});
        nr::test::requireEqual(scene.textures[1].decodeBackend, std::string{"assimp-raw-copy"});
    }};

const nr::test::CaseRegistrar decodeFailureCase{
    "load texture decode reports referenced payload failures",
    [] {
        auto scene = nr::load::SceneAsset{};
        scene.sourcePath = std::filesystem::path{"decode_failure.gltf"};

        auto texture = rawTexture("bad-raw", 2u, 2u);
        texture.rawRgba8->rgba8.pop_back();
        scene.textures.push_back(std::move(texture));

        auto material = nr::load::MaterialAsset{};
        material.textures.push_back(nr::load::MaterialTextureBinding{.textureIndex = 0});
        scene.materials.push_back(std::move(material));

        auto result = nr::load::decodeSceneTextureImages(scene, nr::load::TextureDecodeOptions{.workerCount = 1});
        nr::test::require(!result.has_value(), "bad referenced raw texture should fail decode");
        nr::test::require(result.error().code == nr::load::LoadErrorCode::textureDataUnsupported, "decode failure should use textureDataUnsupported");
        nr::test::requireEqual(result.error().backend, std::string{"decode"});
        nr::test::require(result.error().message.find("bad-raw") != std::string::npos, "decode failure should include texture key");
    }};
} // namespace
