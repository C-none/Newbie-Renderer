export module nr.renderPasses:neuralAppearance;

import nr.renderer;
import nr.rhi;
import std;
import :nodeType;

namespace nr::renderPasses::detail
{
struct NeuralAppearanceRuntimeCache;
} // namespace nr::renderPasses::detail

export namespace nr::renderPasses
{
class NeuralAppearanceNode final : public Node
{
  public:
    explicit NeuralAppearanceNode(bool comparisonEnabled = true, std::uint32_t trainingSeed = 0u) noexcept;
    ~NeuralAppearanceNode() override;

    [[nodiscard]] std::vector<nr::rhi::SlangProgramCompileFileRequest> shaderRequests() const override;
    void initialize(NodeInitContext &context) override;
    void finalizeInitialization() override;
    void build(NodeBuildContext &context, const NodeFrameParameters &frameParameters) override;
    void shutdown(NodeShutdownContext &context) override;

    [[nodiscard]] bool trainingComplete() const noexcept;
    [[nodiscard]] static std::uint32_t totalTrainingStepCount() noexcept;
    [[nodiscard]] std::uint32_t lastScheduledTrainingStep() const noexcept;
    [[nodiscard]] static bool trainingCheckpointExists(const std::filesystem::path &path);
    [[nodiscard]] static bool removeTrainingCheckpoint(const std::filesystem::path &path,
                                                       const std::filesystem::path &preservePath = {});
    [[nodiscard]] std::optional<std::uint32_t> saveTrainingCheckpoint(nr::rhi::Device &device,
                                                                      const std::filesystem::path &path) const;
    [[nodiscard]] bool loadTrainingCheckpoint(nr::rhi::Device &device, const std::filesystem::path &path);
    [[nodiscard]] bool saveTrainingArtifact(nr::rhi::Device &device, const std::filesystem::path &path) const;

  private:
    std::shared_ptr<detail::NeuralAppearanceRuntimeCache> runtime_{};
    bool comparisonEnabled_ = true;
    std::uint32_t trainingSeed_ = 0u;
};
} // namespace nr::renderPasses
