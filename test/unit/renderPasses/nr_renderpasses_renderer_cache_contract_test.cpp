import std;
import nr.test;
import nr.utils;

namespace
{
[[nodiscard]] std::string readProjectFile(std::filesystem::path relativePath)
{
    auto const path = std::filesystem::path{std::string{nr::projectRoot}} / relativePath;
    auto file = std::ifstream{path};
    nr::test::require(file.good(), std::format("failed to open {}", path.generic_string()));
    auto contents = std::ostringstream{};
    contents << file.rdbuf();
    return contents.str();
}

void requireAbsent(std::string_view contents, std::string_view token, std::string_view message)
{
    nr::test::require(!contents.contains(token), std::string{message});
}

void requireOrdered(std::string_view contents, std::string_view first, std::string_view second,
                    std::string_view message)
{
    auto const firstPosition = contents.find(first);
    auto const secondPosition = firstPosition == std::string_view::npos
                                    ? std::string_view::npos
                                    : contents.find(second, firstPosition + first.size());
    nr::test::require(firstPosition != std::string_view::npos && secondPosition != std::string_view::npos &&
                          firstPosition < secondPosition,
                      std::string{message});
}

const nr::test::CaseRegistrar singleBuildContractCase{
    "render passes keep one graph declaration path", [] {
        auto const rendererInterface = readProjectFile("src/renderer/nrRenderer.ixx");
        auto const rendererImplementation = readProjectFile("src/renderer/nrRenderer.cpp");
        auto const renderPassExport = readProjectFile("src/renderPasses/exportModule.ixx");
        auto const nodes = std::array{
            readProjectFile("src/renderPasses/AccelerationStructureBuild/nrAccelerationStructureBuildNode.cpp"),
            readProjectFile("src/renderPasses/Accumulate/nrAccumulateNode.cpp"),
            readProjectFile("src/renderPasses/DlssRayReconstruction/nrDlssRayReconstructionNode.cpp"),
            readProjectFile("src/renderPasses/LightPrepare/nrLightPrepareNode.cpp"),
            readProjectFile("src/renderPasses/PathTracing/nrPathTracingNode.cpp"),
            readProjectFile("src/renderPasses/Present/nrPresentNode.cpp"),
            readProjectFile("src/renderPasses/Ui/nrUiNode.cpp"),
        };

        auto sources = std::array{std::string_view{rendererInterface}, std::string_view{rendererImplementation},
                                  std::string_view{renderPassExport}};
        auto const retiredApis = std::array{
            std::string{"RenderGraph"} + std::string(1u, 'S') + "keleton",
            std::string{"Structural"} + "Snapshot",
            std::string{"materializeRenderGraph"} + std::string(1u, 'S') + "keleton",
            std::string{"supportsRenderGraph"} + std::string(1u, 'S') + "keleton",
        };
        auto requireRetiredApisAbsent = [&](std::string_view source) {
            std::ranges::for_each(retiredApis, [&](std::string_view retiredApi) {
                requireAbsent(source, retiredApi, "retired graph-reuse API must be absent");
            });
        };
        std::ranges::for_each(sources, requireRetiredApisAbsent);
        std::ranges::for_each(nodes, requireRetiredApisAbsent);
    }};

const nr::test::CaseRegistrar statefulNodesBuildInOnePassCase{
    "stateful render passes prepare and declare their frame during build", [] {
        auto const asNode = readProjectFile("src/renderPasses/AccelerationStructureBuild/nrAccelerationStructureBuildNode.cpp");
        auto const uiNode = readProjectFile("src/renderPasses/Ui/nrUiNode.cpp");

        requireOrdered(asNode, "void AccelerationStructureBuildNode::build(", "prepareAsFrame(",
                       "AS build must prepare its current frame");
        requireOrdered(asNode, "prepareAsFrame(", "declarePreparedAsFrame(",
                       "AS build must immediately declare the prepared frame");
        requireAbsent(asNode, "preparedFrame", "AS must not transfer prepared state across build paths");

        requireOrdered(uiNode, "void UiNode::build(", "prepareUiDrawFrame(",
                       "UI build must prepare its current draw frame");
        requireAbsent(uiNode, "preparedDrawFrame", "UI must not transfer draw state across build paths");
    }};

const nr::test::CaseRegistrar neuralArtifactStructuralIdentityCase{
    "AS structural plan keys include the immutable neural artifact identity", [] {
        auto const asNode = readProjectFile("src/renderPasses/AccelerationStructureBuild/nrAccelerationStructureBuildNode.cpp");

        nr::test::require(asNode.contains("struct NeuralArtifactStructuralIdentity"),
                          "AS build must declare a dedicated neural artifact structural identity");
        nr::test::require(asNode.contains("std::vector<NeuralArtifactStructuralIdentity> neuralArtifacts"),
                          "AS structural plan key must retain neural artifact identities");
        requireOrdered(asNode, "makeNeuralArtifactStructuralIdentities", "payloadDigest = binding.artifact->payloadDigest()",
                       "AS structural identity must include the immutable artifact payload SHA-256");
        requireOrdered(asNode, "makeAsStructuralPlanKey", "makeNeuralArtifactStructuralIdentities(scene, pendingByMesh)",
                       "AS structural plan construction must capture neural artifact identity before cache comparison");
    }};
} // namespace
