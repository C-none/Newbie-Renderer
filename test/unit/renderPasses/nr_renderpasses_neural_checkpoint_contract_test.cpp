import std;
import nr.renderPasses;
import nr.test;

namespace
{
class ScopedTestDirectory
{
  public:
    explicit ScopedTestDirectory(std::filesystem::path path) : path_(std::move(path))
    {
        auto error = std::error_code{};
        std::filesystem::create_directories(path_, error);
        nr::test::require(!error, std::format("failed to create checkpoint test directory: {}", error.message()));
    }

    ~ScopedTestDirectory()
    {
        auto error = std::error_code{};
        static_cast<void>(std::filesystem::remove_all(path_, error));
    }

    [[nodiscard]] const std::filesystem::path &path() const noexcept
    {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

void writeMarker(const std::filesystem::path &path)
{
    auto output = std::ofstream{path, std::ios::binary | std::ios::trunc};
    output.write("checkpoint-slot", 15);
    output.close();
    nr::test::require(!output.fail(), std::format("failed to write {}", path.generic_string()));
}

const nr::test::CaseRegistrar logicalCheckpointCleanupCase{
    "neural checkpoint logical paths preserve an artifact that aliases a physical slot", [] {
        auto const uniqueSuffix = std::format("{}.{}", std::chrono::steady_clock::now().time_since_epoch().count(),
                                              std::hash<std::thread::id>{}(std::this_thread::get_id()));
        auto directory = ScopedTestDirectory{std::filesystem::temp_directory_path() /
                                             std::format("nr-neural-checkpoint-test-{}", uniqueSuffix)};
        auto const basePath = directory.path() / "training.checkpoint";
        auto slot0 = basePath;
        auto slot1 = basePath;
        slot0 += ".0";
        slot1 += ".1";

        writeMarker(slot0);
        nr::test::require(nr::renderPasses::NeuralAppearanceNode::trainingCheckpointExists(basePath),
                          "logical checkpoint presence should include slot zero");
        nr::test::require(nr::renderPasses::NeuralAppearanceNode::removeTrainingCheckpoint(basePath, slot0),
                          "checkpoint cleanup should succeed while preserving an aliased artifact");
        nr::test::require(std::filesystem::exists(slot0), "cleanup must preserve an artifact at slot zero");

        writeMarker(slot1);
        nr::test::require(nr::renderPasses::NeuralAppearanceNode::removeTrainingCheckpoint(basePath, slot0),
                          "checkpoint cleanup should remove the unprotected slot");
        nr::test::require(std::filesystem::exists(slot0) && !std::filesystem::exists(slot1),
                          "cleanup should preserve only the artifact path");

        nr::test::require(nr::renderPasses::NeuralAppearanceNode::removeTrainingCheckpoint(basePath),
                          "unprotected checkpoint cleanup should succeed");
        nr::test::require(!nr::renderPasses::NeuralAppearanceNode::trainingCheckpointExists(basePath),
                          "logical checkpoint presence should clear after both slots are removed");
    }};
} // namespace
