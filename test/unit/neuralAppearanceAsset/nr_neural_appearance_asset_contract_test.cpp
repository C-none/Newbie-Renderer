import dependency.crypto;
import nr.neuralAppearanceAsset;
import nr.test;
import nr.utils;
import std;

namespace
{
[[nodiscard]] std::filesystem::path artifactPath()
{
    return std::filesystem::path{std::string{nr::projectRoot}} / "build" / "test" / "neural-appearance-v3.nart";
}

void writeValidArtifact(const nr::neuralAppearance::ArtifactWriteRequest &request)
{
    auto written = nr::neuralAppearance::writeArtifactV3(request);
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
    auto loaded = nr::neuralAppearance::loadArtifactV3(path);
    nr::test::require(!loaded.has_value(), std::format("artifact corruption '{}' must be rejected", diagnostic));
    nr::test::require(loaded.error().contains(diagnostic),
                      std::format("artifact corruption must report '{}', received '{}'", diagnostic, loaded.error()));
}

// Header field offsets fixed by the V3 portable ABI.
inline constexpr auto kProfileDigestOffset = std::size_t{80u};
inline constexpr auto kLayerDescriptorOffset = std::size_t{144u};
inline constexpr auto kReservedOffset = std::size_t{336u};
// The first portable padding range inside the model blob.
inline constexpr auto kFirstModelPaddingOffset = std::size_t{3'472u};

const nr::test::CaseRegistrar v3RoundTripCase{
    "neural appearance V3 writer and reader preserve the fixed portable payload", [] {
        nr::test::requireEqual(nr::neuralAppearance::v3ModelBytes, std::size_t{7'360u});
        nr::test::requireEqual(nr::neuralAppearance::v3ModelLogicalBytes, std::size_t{7'176u});
        nr::test::requireEqual(nr::neuralAppearance::v3TotalBytes, std::size_t{7'872u});
        nr::test::requireEqual(nr::neuralAppearance::v3LayerDescriptorBytes, std::size_t{24u});
        nr::test::requireEqual(nr::neuralAppearance::v3AffineLayerCount, std::size_t{8u});
        nr::test::requireEqual(nr::neuralAppearance::v3TrainableScalarCount, std::uint32_t{3'588u});
        nr::test::requireEqual(nr::neuralAppearance::v3MaterialInputCount, std::uint32_t{12u});
        nr::test::requireEqual(nr::neuralAppearance::v3NoBiasOffset, std::numeric_limits<std::uint32_t>::max());

        auto const descriptors = nr::neuralAppearance::v3LayerDescriptors();
        auto definedBytes = std::size_t{};
        std::ranges::for_each(descriptors, [&](const nr::neuralAppearance::ArtifactLayerDescriptor &descriptor) {
            nr::test::requireEqual(descriptor.rowStrideBytes, descriptor.inputCount * 2u);
            definedBytes += static_cast<std::size_t>(descriptor.outputCount) * descriptor.rowStrideBytes +
                            descriptor.biasCount * 2u;
        });
        nr::test::requireEqual(definedBytes, nr::neuralAppearance::v3ModelLogicalBytes);
        nr::test::require(descriptors[5u].biasOffset == nr::neuralAppearance::v3NoBiasOffset,
                          "the direction layer is the only bias-free affine layer");

        auto model = std::array<std::byte, nr::neuralAppearance::v3ModelBytes>{};
        auto path = artifactPath();
        auto error = std::error_code{};
        std::filesystem::create_directories(path.parent_path(), error);
        std::filesystem::remove(path, error);
        auto request = nr::neuralAppearance::ArtifactWriteRequest{
            .destination = path,
            .model = std::span{model},
        };
        writeValidArtifact(request);
        nr::test::requireEqual(std::filesystem::file_size(path),
                               static_cast<std::uintmax_t>(nr::neuralAppearance::v3TotalBytes));

        auto loaded = nr::neuralAppearance::loadArtifactV3(path);
        nr::test::require(loaded.has_value(), loaded.has_value() ? std::string{} : loaded.error());
        nr::test::requireEqual((*loaded)->modelBytes().size(), nr::neuralAppearance::v3ModelBytes);
        nr::test::requireEqual((*loaded)->completedSteps(), nr::neuralAppearance::v3CompletedSteps);

        overwriteByte(path, kLayerDescriptorOffset + 16u, std::byte{1u});
        requireArtifactRejected(path, "layout does not match this renderer");

        writeValidArtifact(request);
        xorByte(path, kProfileDigestOffset, std::byte{1u});
        requireArtifactRejected(path, "training profile");

        writeValidArtifact(request);
        overwriteByte(path, 0u, std::byte{});
        requireArtifactRejected(path, "earlier artifacts require retraining");

        writeValidArtifact(request);
        std::filesystem::resize_file(path, nr::neuralAppearance::v3TotalBytes - 1u, error);
        nr::test::require(!error, "test artifact must support truncation coverage");
        auto truncated = nr::neuralAppearance::loadArtifactV3(path);
        nr::test::require(!truncated.has_value(), "reader must reject truncated V3 artifacts");

        writeValidArtifact(request);
        std::filesystem::resize_file(path, nr::neuralAppearance::v3TotalBytes + 1u, error);
        nr::test::require(!error, "test artifact must support trailing-data coverage");
        auto trailing = nr::neuralAppearance::loadArtifactV3(path);
        nr::test::require(!trailing.has_value(), "reader must reject V3 artifacts with trailing bytes");

        writeValidArtifact(request);
        overwriteByte(path, nr::neuralAppearance::v3HeaderBytes, std::byte{1u});
        requireArtifactRejected(path, "SHA-256");

        writeValidArtifact(request);
        overwriteByte(path, nr::neuralAppearance::v3HeaderBytes + 1u, std::byte{0x7cu});
        requireArtifactRejected(path, "model storage");

        writeValidArtifact(request);
        overwriteByte(path, kReservedOffset, std::byte{1u});
        requireArtifactRejected(path, "reserved header bytes");

        writeValidArtifact(request);
        overwriteByte(path, nr::neuralAppearance::v3HeaderBytes + kFirstModelPaddingOffset, std::byte{1u});
        requireArtifactRejected(path, "model storage");

        writeValidArtifact(request);
        auto invalidModel = model;
        invalidModel[kFirstModelPaddingOffset] = std::byte{1u};
        auto invalidRequest = request;
        invalidRequest.model = std::span{invalidModel};
        auto rejectedOverwrite = nr::neuralAppearance::writeArtifactV3(invalidRequest);
        nr::test::require(!rejectedOverwrite.has_value(),
                          "writer must reject invalid data before replacing an existing artifact");
        auto preserved = nr::neuralAppearance::loadArtifactV3(path);
        nr::test::require(preserved.has_value(), preserved.has_value() ? std::string{} : preserved.error());

        auto invalidExtension = request;
        invalidExtension.destination = path.parent_path() / "neural-appearance-v3.bin";
        auto rejectedExtension = nr::neuralAppearance::writeArtifactV3(invalidExtension);
        nr::test::require(!rejectedExtension.has_value(), "writer must reject non-.nart artifact destinations");
        auto rejectedLoadExtension = nr::neuralAppearance::loadArtifactV3(invalidExtension.destination);
        nr::test::require(!rejectedLoadExtension.has_value(),
                          "loader must reject non-.nart artifact paths before file access");

        std::filesystem::remove(path, error);
    }};

const nr::test::CaseRegistrar v3TrainingProfileCase{
    "neural appearance V3 training profile digest is stable and describes the eight-affine topology", [] {
        auto const digest = nr::neuralAppearance::v3TrainingProfileDigest();
        nr::test::require(digest.has_value(), "the fixed training profile must hash successfully");
        nr::test::require(std::ranges::any_of(*digest, [](std::byte value) { return value != std::byte{}; }),
                          "the training profile digest must not be all zero");
        nr::test::require(nr::neuralAppearance::v3TrainingProfileText.contains("nr.neural-appearance/v3"),
                          "the profile must identify the V3 artifact generation");
        nr::test::require(
            nr::neuralAppearance::v3TrainingProfileText.contains("projected-diffuse-specular"),
            "the profile must record that both reflective base surface lobes are supervised");
    }};
} // namespace
