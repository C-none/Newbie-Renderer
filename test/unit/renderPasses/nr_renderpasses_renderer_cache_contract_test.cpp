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

void requirePresent(std::string_view contents, std::string_view token, std::string_view message)
{
    nr::test::require(contents.find(token) != std::string::npos, std::string{message});
}

const nr::test::CaseRegistrar renderPassesRendererCacheOwnershipCase{
    "renderpasses no longer own renderer/RDG descriptor table cache state",
    [] {
        auto normalBuffer = readProjectFile("src/renderPasses/NormalBuffer/nrNormalBufferNode.cpp");
        auto pathTracing = readProjectFile("src/renderPasses/PathTracing/nrPathTracingNode.cpp");
        auto accumulate = readProjectFile("src/renderPasses/Accumulate/nrAccumulateNode.cpp");
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
        requirePresent(
            accumulate,
            "ComputePassBuilder",
            "Accumulate must use renderer-side compute pass builder descriptor handling");
        requirePresent(
            accumulate,
            "cameraFrameState.historySampleCount",
            "Accumulate must drive history weight from renderer camera frame state");
        requirePresent(
            accumulate,
            "cameraFrameState.accumulationReset",
            "Accumulate must reset history from renderer camera stability state");
    }};

const nr::test::CaseRegistrar pathTracingShaderOrganizationCase{
    "path tracing shader keeps raygen core separate from material hit shaders",
    [] {
        auto entry = readProjectFile("shader/renderer/pathTracing.slang");
        auto core = readProjectFile("shader/renderer/pathTracing/core.slang");
        auto params = readProjectFile("shader/renderer/pathTracing/params.slang");
        auto pathState = readProjectFile("shader/renderer/pathTracing/pathState.slang");
        auto random = readProjectFile("shader/renderer/pathTracing/random.slang");
        auto scheduler = readProjectFile("shader/renderer/pathTracing/scheduler.slang");
        auto hitShaders = readProjectFile("shader/renderer/pathTracing/hitShaders.slang");
        auto materialPayload = readProjectFile("shader/include/material/payload.slang");

        requirePresent(entry, "runPathTracingScheduler", "PathTracing entry should delegate raygen work to the scheduler");
        requirePresent(entry, "handlePathTracingClosestHit", "PathTracing entry should delegate closest-hit material resolution");
        requireAbsent(entry, "evaluateSceneLightAt", "PathTracing entry should not own lighting logic");
        requireAbsent(entry, "resolveRtMaterialPayload", "PathTracing entry should not own material payload decoding");

        requirePresent(scheduler, "tracePathTracingMaterialRay", "PathTracing scheduler should own material TraceRay scheduling");
        requirePresent(scheduler, "while (pathStateIsActive(path))", "PathTracing scheduler should own the raygen path loop");
        requirePresent(
            scheduler,
            "tracePathTracingSample(pixel, dimensions);",
            "PathTracing scheduler should run exactly one camera sample per pixel");
        requireAbsent(
            scheduler,
            "kPathTracingSamplesPerPixel",
            "PathTracing scheduler must not expose a samples-per-pixel loop");
        requireAbsent(
            scheduler,
            "for (uint sampleIndex",
            "PathTracing scheduler must stay fixed at one camera sample per pixel");
        requirePresent(core, "handlePathTracingHit", "PathTracing core should own hit shading");
        requirePresent(core, "samplePathTracingDirectLighting", "PathTracing core should own direct lighting");
        requirePresent(core, "writePathTracingOutput", "PathTracing core should own output writes");
        requirePresent(
            core,
            "makePathTracingSeed(pixel, dimensions, gFrame.frameState.xy)",
            "PathTracing core should perturb per-thread RNG with the monotonic sample-frame ordinal");
        requirePresent(
            core,
            "kPathTracingLightEnergyScale / lightSample.pdf",
            "PathTracing core should apply the configured direct-light energy scale");
        requirePresent(
            params,
            "kPathTracingLightEnergyScale = 4.0f",
            "PathTracing params should keep v1 light energy at 4x");
        requireAbsent(
            params,
            "kPathTracingSamplesPerPixel",
            "PathTracing params must not expose configurable camera samples per pixel");
        requirePresent(
            random,
            "uint2 sampleFrameOrdinal",
            "PathTracing random seed should receive the 64-bit sample-frame ordinal lanes");
        requirePresent(
            random,
            "sampleFrameOrdinal.x",
            "PathTracing random seed should mix the low sample-frame ordinal lane");
        requirePresent(
            random,
            "sampleFrameOrdinal.y",
            "PathTracing random seed should mix the high sample-frame ordinal lane");
        requirePresent(
            pathState,
            "public uint4 rngState",
            "PathTracing path state should keep a per-pixel/per-frame RNG stream state");
        requireAbsent(
            random,
            "sampleIndex",
            "PathTracing random seed must not keep a configurable camera sample dimension");
        requireAbsent(
            pathState,
            "sampleIndex",
            "PathTracing path state must not carry camera sample state in fixed 1spp mode");

        requirePresent(hitShaders, "resolveRtMaterialPayload", "PathTracing closest-hit should resolve material payloads");
        requireAbsent(hitShaders, "samplePathTracingDirectLighting", "PathTracing hit shaders must not own direct lighting");
        requireAbsent(hitShaders, "evaluateResolvedMaterialDirect", "PathTracing hit shaders must not shade direct light");
        requireAbsent(hitShaders, "outputImage", "PathTracing hit shaders must not write the output image");

        requirePresent(materialPayload, "ResolvedMaterialPayload", "Common material payload helper should define resolved hit material data");
        requirePresent(materialPayload, "evaluateResolvedMaterialDirect", "Common material payload helper should expose direct lighting evaluation");
        requirePresent(materialPayload, "sampleResolvedMaterialScatter", "Common material payload helper should expose v1 scatter sampling");
    }};

const nr::test::CaseRegistrar accumulateShaderCase{
    "accumulate shader owns capped current-frame weight",
    [] {
        auto shader = readProjectFile("shader/renderer/accumulate.slang");
        auto node = readProjectFile("src/renderPasses/Accumulate/nrAccumulateNode.cpp");

        requirePresent(shader, "Texture2D<float4> gCurrentColor", "Accumulate shader should read current frame color");
        requirePresent(shader, "Texture2D<float4> gHistoryColor", "Accumulate shader should read previous history color");
        requirePresent(shader, "RWTexture2D<float4> gAccumulatedColor", "Accumulate shader should write history output");
        requirePresent(shader, "max(exactWeight, cappedWeight)", "Accumulate shader should clamp current-frame weight");
        requirePresent(node, "ImageLayoutIntent::Undefined", "Accumulate output slot should be fully overwritten from undefined layout");
        requirePresent(node, "ImageLayoutIntent::ShaderReadOnly", "Accumulate previous slot should import readable history when valid");
    }};
} // namespace
