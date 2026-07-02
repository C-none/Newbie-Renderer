import std;
import nr.test;
import nr.utils;

namespace
{
[[nodiscard]] std::string readProjectFile(std::filesystem::path relativePath)
{
    auto path = std::filesystem::path{std::string{nr::projectRoot}} / relativePath;
    auto file = std::ifstream{path};
    nr::test::require(file.good(), std::format("failed to open {}", path.generic_string()));

    auto contents = std::ostringstream{};
    contents << file.rdbuf();
    return contents.str();
}

void requireAbsent(std::string_view contents, std::string_view token, std::string_view message)
{
    nr::test::require(contents.find(token) == std::string::npos, std::string{message});
}

const nr::test::CaseRegistrar renderPassesRendererCacheOwnershipCase{
    "renderpasses no longer own renderer/RDG descriptor table cache state",
    [] {
        auto normalBuffer = readProjectFile("src/renderPasses/NormalBuffer/nrNormalBufferNode.cpp");
        auto pathTracing = readProjectFile("src/renderPasses/PathTracing/nrPathTracingNode.cpp");
        auto ui = readProjectFile("src/renderPasses/Ui/nrUiNode.cpp");
        auto sceneTextureBinding = readProjectFile("src/renderPasses/nrSceneTextureTableBinding.ixx");

        requireAbsent(
            normalBuffer,
            "SceneTextureTableBindingCache",
            "NormalBuffer must use renderer-owned bindless table cache instead of a node-local scene cache");
        requireAbsent(
            pathTracing,
            "SceneTextureTableBindingCache",
            "PathTracing must use renderer-owned bindless table cache instead of a node-local scene cache");
        requireAbsent(
            ui,
            "appliedTextureTableRevisionByFrame",
            "Ui must not keep per-frame applied texture table revisions");
        requireAbsent(
            ui,
            "ensureBindlessTextureBindingSetsForFrame",
            "Ui texture table binding-set allocation should be owned by renderer bindless cache");
        requireAbsent(
            sceneTextureBinding,
            "resetSceneTextureTableFrameCache",
            "scene texture table helper should not own frame-slot cache reset state");
    }};
} // namespace
