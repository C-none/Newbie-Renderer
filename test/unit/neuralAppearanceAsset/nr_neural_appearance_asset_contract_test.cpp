import dependency.crypto;
import dependency.json;
import nr.load;
import nr.neuralAppearanceAsset;
import nr.test;
import nr.utils;
import std;

namespace
{
[[nodiscard]] std::filesystem::path artifactPath()
{
    return std::filesystem::path{std::string{nr::projectRoot}} / "build" / "test" / "neural-appearance-v2.nart";
}

[[nodiscard]] std::filesystem::path sidecarTestAssetsRoot()
{
    return std::filesystem::path{std::string{nr::projectRoot}} / "build" / "test" / "neural-appearance-sidecar-assets";
}

[[nodiscard]] nr::neuralAppearance::ArtifactWriteRequest makeValidArtifactRequest(
    const std::filesystem::path &path, std::array<std::byte, nr::neuralAppearance::v2ModelBytes> &model,
    std::array<std::byte, nr::neuralAppearance::v2LatentPlaneBytes> &latent0,
    std::array<std::byte, nr::neuralAppearance::v2LatentPlaneBytes> &latent1)
{
    auto contract = nr::neuralAppearance::makeArtifactBindingContract(
        "glTF-Sample-Assets/Models/BoxTextured/glTF/BoxTextured.gltf");
    nr::test::require(contract.has_value(), contract.has_value() ? std::string{} : contract.error());
    return nr::neuralAppearance::ArtifactWriteRequest{
        .destination = path,
        .bindingContract = *contract,
        .model = std::span{model},
        .latentPlanes = {std::span{latent0}, std::span{latent1}},
    };
}

void writeValidArtifact(const nr::neuralAppearance::ArtifactWriteRequest &request)
{
    auto written = nr::neuralAppearance::writeArtifactV2(request);
    nr::test::require(written.has_value(), written.has_value() ? std::string{} : written.error());
}

void overwriteByte(const std::filesystem::path &path, std::size_t offset, std::byte value)
{
    auto output = std::fstream{path, std::ios::binary | std::ios::in | std::ios::out};
    nr::test::require(output.good(), "test artifact must be writable for corruption coverage");
    output.seekp(static_cast<std::streamoff>(offset));
    auto const character = static_cast<char>(std::to_integer<unsigned char>(value));
    output.write(std::addressof(character), 1);
    output.close();
}

void xorByte(const std::filesystem::path &path, std::size_t offset, std::byte mask)
{
    auto output = std::fstream{path, std::ios::binary | std::ios::in | std::ios::out};
    nr::test::require(output.good(), "test artifact must be writable for corruption coverage");
    output.seekg(static_cast<std::streamoff>(offset));
    auto character = char{};
    output.read(std::addressof(character), 1);
    nr::test::require(output.good(), "test artifact byte must be readable for corruption coverage");
    character ^= static_cast<char>(std::to_integer<unsigned char>(mask));
    output.seekp(static_cast<std::streamoff>(offset));
    output.write(std::addressof(character), 1);
    output.close();
}

void requireArtifactRejected(const std::filesystem::path &path, std::string_view diagnostic)
{
    auto loaded = nr::neuralAppearance::loadArtifactV2(path);
    nr::test::require(!loaded.has_value(), std::format("artifact corruption '{}' must be rejected", diagnostic));
    nr::test::require(loaded.error().contains(diagnostic),
                      std::format("artifact corruption must report '{}', received '{}'", diagnostic, loaded.error()));
}

void writeSidecar(const std::filesystem::path &path, std::string_view artifact)
{
    auto serialized = std::string{};
    auto const value = dependency::json::JsonValue{dependency::json::JsonValue::Object{
        {"schema", dependency::json::JsonValue{"nr.neural-material-binding/v2"}},
        {"scene", dependency::json::JsonValue{"scene.gltf"}},
        {"material", dependency::json::JsonValue{std::uint64_t{0u}}},
        {"artifact", dependency::json::JsonValue{artifact}},
    }};
    nr::test::requireEqual(dependency::json::serializeJson(value, serialized, 4u * 1024u),
                           dependency::json::JsonError::none);
    auto output = std::ofstream{path, std::ios::binary | std::ios::trunc};
    nr::test::require(output.good(), "sidecar test file must be writable");
    output.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
    output.close();
}

const nr::test::CaseRegistrar v2RoundTripCase{
    "neural appearance V2 writer and reader preserve the fixed portable payload", [] {
        nr::test::requireEqual(nr::neuralAppearance::v2ModelLogicalBytes, std::size_t{3'870u});
        nr::test::requireEqual(nr::neuralAppearance::v2LayerDescriptorBytes, std::size_t{24u});
        nr::test::requireEqual(nr::neuralAppearance::v2NoBiasOffset,
                               std::numeric_limits<std::uint32_t>::max());
        auto model = std::array<std::byte, nr::neuralAppearance::v2ModelBytes>{};
        auto latent0 = std::array<std::byte, nr::neuralAppearance::v2LatentPlaneBytes>{};
        auto latent1 = std::array<std::byte, nr::neuralAppearance::v2LatentPlaneBytes>{};
        auto path = artifactPath();
        auto error = std::error_code{};
        std::filesystem::create_directories(path.parent_path(), error);
        std::filesystem::remove(path, error);
        auto request = makeValidArtifactRequest(path, model, latent0, latent1);
        writeValidArtifact(request);
        nr::test::requireEqual(std::filesystem::file_size(path),
                               static_cast<std::uintmax_t>(nr::neuralAppearance::v2TotalBytes));

        auto loaded = nr::neuralAppearance::loadArtifactV2(path);
        nr::test::require(loaded.has_value(), loaded.has_value() ? std::string{} : loaded.error());
        nr::test::requireEqual((*loaded)->modelBytes().size(), nr::neuralAppearance::v2ModelBytes);
        nr::test::requireEqual((*loaded)->latentPlane(0u).size(), nr::neuralAppearance::v2LatentPlaneBytes);
        nr::test::requireEqual((*loaded)->latentPlane(1u).size(), nr::neuralAppearance::v2LatentPlaneBytes);
        nr::test::require((*loaded)->latentPlane(2u).empty());
        nr::test::requireEqual((*loaded)->bindingContract().sourceSceneDigest, request.bindingContract.sourceSceneDigest);
        nr::test::requireEqual((*loaded)->bindingContract().trainingProfileDigest,
                               request.bindingContract.trainingProfileDigest);

        overwriteByte(path, 220u, std::byte{1u});
        auto incompatibleDescriptor = nr::neuralAppearance::loadArtifactV2(path);
        nr::test::require(!incompatibleDescriptor.has_value(),
                          "reader must reject a V2 artifact whose descriptor bias offset differs from the fixed topology");

        writeValidArtifact(request);
        xorByte(path, 140u, std::byte{1u});
        auto incompatibleProfile = nr::neuralAppearance::loadArtifactV2(path);
        nr::test::require(!incompatibleProfile.has_value(),
                          "reader must reject a V2 artifact whose profile digest differs from the fixed profile");

        writeValidArtifact(request);
        overwriteByte(path, 0u, std::byte{});
        requireArtifactRejected(path, "V1 artifacts require retraining");

        writeValidArtifact(request);
        std::filesystem::resize_file(path, nr::neuralAppearance::v2TotalBytes - 1u, error);
        nr::test::require(!error, "test artifact must support truncation coverage");
        auto truncated = nr::neuralAppearance::loadArtifactV2(path);
        nr::test::require(!truncated.has_value(), "reader must reject truncated V2 artifacts");

        writeValidArtifact(request);
        std::filesystem::resize_file(path, nr::neuralAppearance::v2TotalBytes + 1u, error);
        nr::test::require(!error, "test artifact must support trailing-data coverage");
        auto trailing = nr::neuralAppearance::loadArtifactV2(path);
        nr::test::require(!trailing.has_value(), "reader must reject V2 artifacts with trailing bytes");

        writeValidArtifact(request);
        overwriteByte(path, nr::neuralAppearance::v2HeaderBytes, std::byte{1u});
        requireArtifactRejected(path, "SHA-256");

        writeValidArtifact(request);
        overwriteByte(path, nr::neuralAppearance::v2HeaderBytes + 1u, std::byte{0x7cu});
        requireArtifactRejected(path, "model storage");

        writeValidArtifact(request);
        overwriteByte(path, nr::neuralAppearance::v2HeaderBytes + nr::neuralAppearance::v2ModelBytes + 1u,
                      std::byte{0x7cu});
        requireArtifactRejected(path, "latent payload");

        writeValidArtifact(request);
        overwriteByte(path, 356u, std::byte{1u});
        requireArtifactRejected(path, "reserved header bytes");

        writeValidArtifact(request);
        overwriteByte(path, nr::neuralAppearance::v2HeaderBytes + 216u, std::byte{1u});
        requireArtifactRejected(path, "model storage");

        writeValidArtifact(request);
        auto invalidModel = model;
        invalidModel[216u] = std::byte{1u};
        auto invalidRequest = request;
        invalidRequest.model = std::span{invalidModel};
        auto rejectedOverwrite = nr::neuralAppearance::writeArtifactV2(invalidRequest);
        nr::test::require(!rejectedOverwrite.has_value(),
                          "writer must reject invalid data before replacing an existing artifact");
        auto preserved = nr::neuralAppearance::loadArtifactV2(path);
        nr::test::require(preserved.has_value(),
                          preserved.has_value() ? std::string{} : preserved.error());

        auto invalidExtension = request;
        invalidExtension.destination = path.parent_path() / "neural-appearance-v2.bin";
        auto rejectedExtension = nr::neuralAppearance::writeArtifactV2(invalidExtension);
        nr::test::require(!rejectedExtension.has_value(), "writer must reject non-.nart artifact destinations");
        auto rejectedLoadExtension = nr::neuralAppearance::loadArtifactV2(invalidExtension.destination);
        nr::test::require(!rejectedLoadExtension.has_value(), "loader must reject non-.nart artifact paths before file access");

        std::filesystem::remove(path, error);
    }};

const nr::test::CaseRegistrar duplicateJsonKeyCase{
    "strict JSON parser rejects duplicate object keys before neural sidecar validation", [] {
        auto parsed = dependency::json::parseJsonRejectingDuplicateKeys(
            R"({"schema":"first","schema":"second"})", 16u);
        nr::test::require(!parsed.valid(), "duplicate sidecar keys must not silently select a last value");
        auto nested = dependency::json::parseJsonRejectingDuplicateKeys(
            R"({"binding":{"artifact":"first","artifact":"second"}})", 16u);
        nr::test::require(!nested.valid(), "duplicate keys in nested objects must also be rejected");
    }};

const nr::test::CaseRegistrar sidecarArtifactExtensionCase{
    "neural material sidecars reject escaped and Windows-special paths", [] {
        auto const assetsRoot = sidecarTestAssetsRoot();
        auto const sidecar = assetsRoot / "neuralAppearance" / "bindings" / "scene.gltf.neural.json";
        auto error = std::error_code{};
        std::filesystem::remove_all(assetsRoot, error);
        std::filesystem::create_directories(sidecar.parent_path(), error);
        nr::test::require(!error, "sidecar test asset root must be creatable");
        for (auto const artifact : std::array<std::string_view, 5u>{
                 "scene.material-0.bin",
                 "../escaped.nart",
                 "scene:alternate-stream.nart",
                 "https://example.invalid/scene.nart",
                 R"(\\server\share\scene.nart)",
             })
        {
            writeSidecar(sidecar, artifact);
            auto request = nr::neuralAppearance::loadBindingRequest(assetsRoot, "scene.gltf");
            nr::test::require(!request.has_value(),
                              std::format("sidecar resolver must reject unsafe artifact '{}'", artifact));
        }
        for (auto const &scene : std::array<std::filesystem::path, 4u>{
                 "../scene.gltf",
                 "scene:alternate-stream.gltf",
                 "https://example.invalid/scene.gltf",
                 R"(\\server\share\scene.gltf)",
             })
        {
            auto request = nr::neuralAppearance::loadBindingRequest(assetsRoot, scene);
            nr::test::require(!request.has_value(),
                              std::format("sidecar lookup must reject unsafe scene '{}'", scene.generic_string()));
        }
        std::filesystem::remove_all(assetsRoot, error);
    }};

const nr::test::CaseRegistrar boxTexturedProductionBindingCase{
    "BoxTextured production sidecar reloads its V2 artifact and matches imported material zero", [] {
        auto const assetsRoot = std::filesystem::path{std::string{nr::projectRoot}} / "assets";
        auto const scenePath =
            std::filesystem::path{"glTF-Sample-Assets/Models/BoxTextured/glTF/BoxTextured.gltf"};

        auto binding = nr::neuralAppearance::loadBindingRequest(assetsRoot, scenePath);
        nr::test::require(binding.has_value(), binding.has_value() ? std::string{} : binding.error());
        nr::test::require(binding->has_value(), "BoxTextured must publish exactly one neural material binding");
        nr::test::requireEqual(binding->value().sourceMaterialIndex, std::uint32_t{0u});
        nr::test::require(static_cast<bool>(binding->value().artifact),
                          "BoxTextured binding must retain its immutable V2 artifact");
        nr::test::requireEqual(binding->value().artifact->modelBytes().size(),
                               nr::neuralAppearance::v2ModelBytes);
        nr::test::requireEqual(binding->value().artifact->latentPlane(0u).size(),
                               nr::neuralAppearance::v2LatentPlaneBytes);
        nr::test::requireEqual(binding->value().artifact->latentPlane(1u).size(),
                               nr::neuralAppearance::v2LatentPlaneBytes);

        auto scene = nr::load::loadScene(nr::load::SceneLoadRequest{
            .sourcePath = assetsRoot / scenePath,
        });
        nr::test::require(scene.has_value(),
                          scene.has_value() ? std::string{} : scene.error().message);
        auto validation = nr::neuralAppearance::validateBindingForScene(binding->value(), *scene);
        nr::test::require(validation.has_value(),
                          validation.has_value() ? std::string{} : validation.error());
    }};
} // namespace
