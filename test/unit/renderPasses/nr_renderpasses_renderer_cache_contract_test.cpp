import std;
import nr.renderer;
import nr.renderPasses;
import nr.test;
import nr.utils;

namespace
{
struct StagingTestUiWriter final : nr::renderer::NodeUiWriter
{
    float nextFloat = 0.0f;
    std::uint32_t nextUInt = 0u;
    bool nextBool = false;
    bool nextButton = false;
    std::uint32_t inputUIntCallCount = 0u;
    std::uint32_t sliderUIntCallCount = 0u;
    std::vector<std::string> textCalls{};

    void text(std::string_view value) override
    {
        textCalls.emplace_back(value);
    }
    void separator() override {}
    [[nodiscard]] bool checkbox(std::string_view, bool& value) override
    {
        value = nextBool;
        return true;
    }
    [[nodiscard]] bool button(std::string_view) override { return nextButton; }
    [[nodiscard]] bool beginCombo(std::string_view, std::string_view) override { return false; }
    void endCombo() override {}
    [[nodiscard]] bool selectable(std::string_view, bool) override { return false; }

    [[nodiscard]] bool sliderFloat(std::string_view, float& value, float, float) override
    {
        value = nextFloat;
        return true;
    }

    [[nodiscard]] bool inputUInt(std::string_view, std::uint32_t& value, std::uint32_t, std::uint32_t) override
    {
        ++inputUIntCallCount;
        value = nextUInt;
        return true;
    }

    [[nodiscard]] bool sliderUInt(std::string_view, std::uint32_t& value, std::uint32_t, std::uint32_t) override
    {
        ++sliderUIntCallCount;
        value = nextUInt;
        return true;
    }
};

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
        auto pathTracingInterface = readProjectFile("src/renderPasses/PathTracing/nrPathTracingNode.ixx");
        auto accumulate = readProjectFile("src/renderPasses/Accumulate/nrAccumulateNode.cpp");
        auto ui = readProjectFile("src/renderPasses/Ui/nrUiNode.cpp");
        auto sceneTextureBinding = readProjectFile("src/renderPasses/nrSceneTextureTableBinding.ixx");
        auto rendererInterface = readProjectFile("src/renderer/nrRenderer.ixx");
        auto rendererImplementation = readProjectFile("src/renderer/nrRenderer.cpp");

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
            normalBuffer,
            "sceneTextureTableImmutableSamplerBinding()",
            "NormalBuffer should install the scene texture table immutable sampler before graphics PSO creation");
        requirePresent(
            pathTracing,
            "sceneTextureTableImmutableSamplerBinding()",
            "PathTracing should install the scene texture table immutable sampler before RT PSO creation");
        requirePresent(
            pathTracingInterface,
            "PathTracingVariantKey variant{}",
            "PathTracing input should expose a node-local variant key");
        requirePresent(
            pathTracingInterface,
            "enableRussianRoulette",
            "PathTracing variant key should expose the Russian roulette toggle");
        requirePresent(
            pathTracing,
            "std::map<PathTracingVariantKey, PathTracingVariantRuntime>",
            "PathTracing should keep per-variant PSO runtimes in the node runtime cache");
        requirePresent(
            pathTracing,
            "ensurePathTracingVariantRuntime",
            "PathTracing should lazily create independent PSO runtimes per variant key");
        requireAbsent(
            pathTracing,
            "RendererCacheSuite",
            "PathTracing variant PSOs must not be stored in RendererCacheSuite");
        requirePresent(
            sceneTextureBinding,
            ".usesImmutableSampler = true",
            "scene texture table descriptor writes should rely on the immutable sampler in the PSO layout");
        requireAbsent(
            rendererInterface,
            "sceneTextureSampler",
            "Renderer global resources should not expose a per-frame scene texture sampler");
        requireAbsent(
            rendererImplementation,
            "SceneTextureSampler",
            "Renderer should not create a separate scene texture sampler for gSceneTextures");
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
        requirePresent(
            rendererInterface,
            "kRendererAccumulationMaxSampleCount = 4096u",
            "Renderer camera history count should support the Accumulate 4096-sample cap");
        requirePresent(
            accumulate,
            "ui.inputUInt(\"History Samples\"",
            "Accumulate max history samples should use a direct uint input");
        requireAbsent(
            accumulate,
            "ui.sliderUInt(\"Max Samples\"",
            "Accumulate max history samples should no longer use a slider");
    }};

const nr::test::CaseRegistrar presentLinearExrScreenshotCase{
    "present screenshots read back linear source images and write EXR",
    [] {
        auto manifest = readProjectFile("vcpkg.json");
        auto externCMake = readProjectFile("src/extern/CMakeLists.txt");
        auto dependencyAssets = readProjectFile("src/extern/dependencyAssets.ixx");
        auto rendererInterface = readProjectFile("src/renderer/nrRenderer.ixx");
        auto rendererImplementation = readProjectFile("src/renderer/nrRenderer.cpp");
        auto presentInterface = readProjectFile("src/renderPasses/Present/nrPresentNode.ixx");
        auto present = readProjectFile("src/renderPasses/Present/nrPresentNode.cpp");

        requirePresent(manifest, "\"openexr\"", "vcpkg manifest should install OpenEXR");
        requirePresent(externCMake, "find_package(OpenEXR CONFIG REQUIRED)", "dependency boundary should find OpenEXR");
        requirePresent(externCMake, "OpenEXR::OpenEXR", "dependency target should link OpenEXR");
        requirePresent(
            dependencyAssets,
            "namespace nr::dependency::openexr",
            "OpenEXR declarations should be exposed only through dependency.assets");
        requirePresent(
            rendererInterface,
            "describeImageResource(GraphResourceHandle resource)",
            "Present should be able to query source image metadata without owning graph internals");
        requirePresent(
            rendererImplementation,
            "describeGraphImageResource",
            "renderer should implement image resource metadata lookup");

        requireAbsent(present, "stbi_write_png", "Present screenshots should no longer write PNG files");
        requireAbsent(present, "Present.ConvertScreenshot", "Present screenshots should not run a shader conversion pass");
        requireAbsent(present, "kScreenshotFormat", "Present screenshots should not force a fixed RGBA8 format");
        requirePresent(
            present,
            "context.describeImageResource(sourceColor)",
            "Present screenshots should inspect the published source image format");
        requirePresent(
            present,
            "detail::addPresentReadbackCopyPass(\n                context,\n                sourceColor",
            "Present screenshots should copy frameResource::presentSourceColor directly");
        requirePresent(
            present,
            ".format = sourceDesc->format",
            "Pending screenshot save should remember the source format");
        requirePresent(
            presentInterface,
            "vk::Format format = vk::Format::eUndefined",
            "Pending screenshot save should carry the source format across the frame fence");
        requirePresent(
            presentInterface,
            "bool flipY = false",
            "Pending screenshot save should carry flip state for EXR row order");
        requirePresent(
            present,
            "writeLinearScreenshotExr",
            "Present should save the readback payload through the EXR writer");
    }};

const nr::test::CaseRegistrar renderPassNodeUiStagingCase{
    "renderpass node ui callbacks stage changes for the next frame",
    [] {
        auto frameParameters = nr::renderer::NodeFrameParameters{};

        auto present = nr::renderPasses::PresentNode{};
        present.input.uiOpacity = 1.0f;
        auto presentSections = std::vector<nr::renderer::NodeUiSection>{};
        auto presentUiContext = nr::renderer::NodeUiBuildContext{"Present", presentSections};
        present.collectUi(presentUiContext, frameParameters);
        nr::test::requireEqual(presentSections.size(), std::size_t{1u});

        auto presentWriter = StagingTestUiWriter{};
        presentWriter.nextFloat = 0.25f;
        presentWriter.nextButton = true;
        presentSections[0].draw(presentWriter);
        nr::test::requireEqual(present.input.uiOpacity, 1.0f);

        auto nextPresentSections = std::vector<nr::renderer::NodeUiSection>{};
        auto nextPresentUiContext = nr::renderer::NodeUiBuildContext{"Present", nextPresentSections};
        present.collectUi(nextPresentUiContext, frameParameters);
        nr::test::require(std::abs(present.input.uiOpacity - 0.25f) < 0.001f);
        auto nextPresentWriter = StagingTestUiWriter{};
        nextPresentSections[0].draw(nextPresentWriter);
        nr::test::require(
            std::ranges::find(nextPresentWriter.textCalls, "Screenshot queued") != nextPresentWriter.textCalls.end(),
            "Present screenshot button should stage a next-frame queued status");

        auto accumulate = nr::renderPasses::AccumulateNode{};
        accumulate.input.maxHistorySampleCount = nr::renderPasses::kAccumulateDefaultMaxHistorySampleCount;
        auto accumulateSections = std::vector<nr::renderer::NodeUiSection>{};
        auto accumulateUiContext = nr::renderer::NodeUiBuildContext{"Accumulate", accumulateSections};
        accumulate.collectUi(accumulateUiContext, frameParameters);
        nr::test::requireEqual(accumulateSections.size(), std::size_t{1u});

        auto accumulateWriter = StagingTestUiWriter{};
        accumulateWriter.nextUInt = 64u;
        accumulateSections[0].draw(accumulateWriter);
        nr::test::requireEqual(accumulateWriter.inputUIntCallCount, 1u);
        nr::test::requireEqual(accumulateWriter.sliderUIntCallCount, 0u);
        nr::test::requireEqual(
            accumulate.input.maxHistorySampleCount,
            nr::renderPasses::kAccumulateDefaultMaxHistorySampleCount);

        auto nextAccumulateSections = std::vector<nr::renderer::NodeUiSection>{};
        auto nextAccumulateUiContext = nr::renderer::NodeUiBuildContext{"Accumulate", nextAccumulateSections};
        accumulate.collectUi(nextAccumulateUiContext, frameParameters);
        nr::test::requireEqual(accumulate.input.maxHistorySampleCount, 64u);

        auto clampAccumulateWriter = StagingTestUiWriter{};
        clampAccumulateWriter.nextUInt = nr::renderPasses::kAccumulateMaxHistorySampleCount + 256u;
        nextAccumulateSections[0].draw(clampAccumulateWriter);

        auto finalAccumulateSections = std::vector<nr::renderer::NodeUiSection>{};
        auto finalAccumulateUiContext = nr::renderer::NodeUiBuildContext{"Accumulate", finalAccumulateSections};
        accumulate.collectUi(finalAccumulateUiContext, frameParameters);
        nr::test::requireEqual(
            accumulate.input.maxHistorySampleCount,
            nr::renderPasses::kAccumulateMaxHistorySampleCount);

        auto pathTracing = nr::renderPasses::PathTracingNode{};
        pathTracing.input.variant.maxSurfaceBounces = nr::renderPasses::kPathTracingDefaultMaxSurfaceBounces;
        pathTracing.input.variant.enableRussianRoulette = true;
        auto pathTracingSections = std::vector<nr::renderer::NodeUiSection>{};
        auto pathTracingUiContext = nr::renderer::NodeUiBuildContext{"PathTracing", pathTracingSections};
        pathTracing.collectUi(pathTracingUiContext, frameParameters);
        nr::test::requireEqual(pathTracingSections.size(), std::size_t{1u});

        auto pathTracingWriter = StagingTestUiWriter{};
        pathTracingWriter.nextUInt = 0u;
        pathTracingWriter.nextBool = false;
        pathTracingSections[0].draw(pathTracingWriter);
        nr::test::requireEqual(pathTracingWriter.inputUIntCallCount, 1u);
        nr::test::requireEqual(pathTracingWriter.sliderUIntCallCount, 0u);
        nr::test::requireEqual(
            pathTracing.input.variant.maxSurfaceBounces,
            nr::renderPasses::kPathTracingDefaultMaxSurfaceBounces);
        nr::test::require(pathTracing.input.variant.enableRussianRoulette);

        auto nextPathTracingSections = std::vector<nr::renderer::NodeUiSection>{};
        auto nextPathTracingUiContext = nr::renderer::NodeUiBuildContext{"PathTracing", nextPathTracingSections};
        pathTracing.collectUi(nextPathTracingUiContext, frameParameters);
        nr::test::requireEqual(
            pathTracing.input.variant.maxSurfaceBounces,
            nr::renderPasses::kPathTracingMinSurfaceBounces);
        nr::test::require(!pathTracing.input.variant.enableRussianRoulette);

        auto upperClampWriter = StagingTestUiWriter{};
        upperClampWriter.nextUInt = nr::renderPasses::kPathTracingMaxSurfaceBouncesLimit + 128u;
        upperClampWriter.nextBool = true;
        nextPathTracingSections[0].draw(upperClampWriter);

        auto finalPathTracingSections = std::vector<nr::renderer::NodeUiSection>{};
        auto finalPathTracingUiContext = nr::renderer::NodeUiBuildContext{"PathTracing", finalPathTracingSections};
        pathTracing.collectUi(finalPathTracingUiContext, frameParameters);
        nr::test::requireEqual(
            pathTracing.input.variant.maxSurfaceBounces,
            nr::renderPasses::kPathTracingMaxSurfaceBouncesLimit);
        nr::test::require(pathTracing.input.variant.enableRussianRoulette);
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
        auto roulette = readProjectFile("shader/include/pathTracing/roulette.slang");

        requirePresent(entry, "Scheduler scheduler;", "PathTracing entry should construct the raygen scheduler");
        requirePresent(entry, "scheduler.traceSample(pixel, dimensions);", "PathTracing entry should delegate raygen work to the scheduler");
        requirePresent(entry, "handlePathTracingClosestHit", "PathTracing entry should delegate closest-hit material resolution");
        requireAbsent(entry, "evaluateSceneLightAt", "PathTracing entry should not own lighting logic");
        requireAbsent(entry, "resolveRtMaterialPayload", "PathTracing entry should not own material payload decoding");

        requirePresent(scheduler, "public struct Scheduler", "PathTracing scheduler should be a shader-side struct");
        requirePresent(scheduler, "Pt pt = makePathTracingPt(pixel, dimensions);", "PathTracing scheduler should construct the PT path object");
        requirePresent(scheduler, "while (pt.isActive())", "PathTracing scheduler should own the raygen path loop");
        requirePresent(
            scheduler,
            "pt.traceMaterialRay(payload);",
            "PathTracing scheduler should ask Pt to issue material rays");
        requirePresent(
            scheduler,
            "pt.handleTraceResult(payload);",
            "PathTracing scheduler should ask Pt to handle hit or miss results");
        requirePresent(
            scheduler,
            "pt.writeOutput();",
            "PathTracing scheduler should run exactly one camera sample per pixel");
        requireAbsent(
            scheduler,
            "kPathTracingSamplesPerPixel",
            "PathTracing scheduler must not expose a samples-per-pixel loop");
        requireAbsent(
            scheduler,
            "for (uint sampleIndex",
            "PathTracing scheduler must stay fixed at one camera sample per pixel");
        requirePresent(core, "public struct Pt", "PathTracing core should define the PT path object");
        requirePresent(core, "public PathState path", "Pt should hold the per-path state");
        requirePresent(core, "public bool isActive()", "Pt should expose active-state testing");
        requirePresent(core, "public void traceMaterialRay", "Pt should own material TraceRay scheduling");
        requirePresent(core, "public void handleTraceResult", "Pt should own hit or miss dispatch");
        requirePresent(core, "public void handleHit", "Pt should expose hit handling");
        requirePresent(core, "public void handleMiss", "Pt should expose miss handling");
        requirePresent(core, "public void writeOutput", "Pt should expose output writing");
        requirePresent(core, "makePathTracingPt", "PathTracing core should provide Pt construction");
        requirePresent(core, "handlePathTracingHit", "PathTracing core should own hit shading");
        requirePresent(core, "samplePathTracingDirectLighting", "PathTracing core should own direct lighting");
        requirePresent(core, "writePathTracingOutput", "PathTracing core should own output writes");
        requirePresent(
            core,
            "makePathTracingSeed(pixel, dimensions, gFrame.frameState.xy)",
            "PathTracing core should perturb per-thread RNG with the monotonic sample-frame ordinal");
        requireAbsent(
            params,
            "kPathTracingSamplesPerPixel",
            "PathTracing params must not expose configurable camera samples per pixel");
        requirePresent(
            params,
            "public extern static const uint kPathTracingMaxSurfaceBounces;",
            "PathTracing max bounce variant must be provided by C++ VariantDesc");
        requireAbsent(
            params,
            "kPathTracingMaxSurfaceBounces =",
            "PathTracing max bounce variant must not have a shader-side default");
        requirePresent(
            roulette,
            "public extern struct PathTracingRussianRoulettePolicy : IPathTracingRussianRoulettePolicy;",
            "PathTracing roulette policy variant must be provided by C++ VariantDesc");
        requireAbsent(
            roulette,
            "PathTracingRussianRoulettePolicy : IPathTracingRussianRoulettePolicy =",
            "PathTracing roulette policy variant must not rely on a shader-side default");
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
        requirePresent(
            node,
            "std::array<nr::renderer::RetainedImageState, 2u> historyStates{}",
            "Accumulate history slots should use retained image state tracking");
        requirePresent(
            node,
            ".retainedState = std::ref(state)",
            "Accumulate history imports should attach retained state to the graph");
        requireAbsent(
            node,
            "ImageLayoutIntent::ShaderReadOnly",
            "Accumulate should not hard-code previous-slot layout outside retained state");
    }};

const nr::test::CaseRegistrar retainedImportedImageStateCase{
    "renderpasses use retained state for renderer-persistent imported images",
    [] {
        auto present = readProjectFile("src/renderPasses/Present/nrPresentNode.cpp");
        auto accumulate = readProjectFile("src/renderPasses/Accumulate/nrAccumulateNode.cpp");
        auto ui = readProjectFile("src/renderPasses/Ui/nrUiNode.cpp");
        auto rendererInterface = readProjectFile("src/renderer/nrRenderer.ixx");

        requirePresent(
            rendererInterface,
            "importRetainedStorageColor",
            "NodeBuildContext should expose a retained imported storage-color helper");
        requirePresent(
            present,
            "RetainedImageState convertedColorState",
            "Present should retain converted-color image state across frames");
        requirePresent(
            present,
            "convertedColorState.reset()",
            "Present should reset converted-color state when the image is recreated");
        requirePresent(
            present,
            "context.importRetainedStorageColor",
            "Present.ConvertedColor should use retained import tracking");
        requirePresent(
            accumulate,
            "std::array<nr::renderer::RetainedImageState, 2u> historyStates{}",
            "Accumulate history ping-pong images should each have retained state");
        requirePresent(
            accumulate,
            "runtime.historyStates[slot].reset()",
            "Accumulate should reset retained history state on image recreation");
        requirePresent(
            ui,
            "nr::renderer::RetainedImageState state{}",
            "Ui texture entries should use the shared retained image state object");
        requirePresent(
            ui,
            "textureEntry.state.layout = nr::renderer::ImageLayoutIntent::ShaderReadOnly",
            "Ui upload completion should seed texture retained layout");
        requireAbsent(ui, "currentLayout", "Ui should not keep an ad-hoc currentLayout field");
    }};
} // namespace
