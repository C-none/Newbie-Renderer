import std;
import dependency.math;
import dependency.vulkan;
import nr.renderPasses;
import nr.rhi;
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

void requirePresent(std::string_view contents, std::string_view token, std::string_view message)
{
    nr::test::require(contents.contains(token), std::string{message});
}

void requireAbsent(std::string_view contents, std::string_view token, std::string_view message)
{
    nr::test::require(!contents.contains(token), std::string{message});
}

[[nodiscard]] std::string collapseWhitespace(std::string_view value)
{
    auto normalized = std::string{};
    auto pendingWhitespace = false;
    std::ranges::for_each(value, [&](char character) {
        if (std::isspace(static_cast<unsigned char>(character)) != 0)
        {
            pendingWhitespace = !normalized.empty();
            return;
        }
        if (pendingWhitespace)
        {
            normalized += ' ';
            pendingWhitespace = false;
        }
        normalized += character;
    });
    return normalized;
}

void requireWhitespaceInsensitivePresent(
    std::string_view contents,
    std::string_view token,
    std::string_view message)
{
    nr::test::require(
        collapseWhitespace(contents).contains(collapseWhitespace(token)),
        std::string{message});
}

[[nodiscard]] std::size_t countOccurrences(std::string_view contents, std::string_view token)
{
    nr::test::require(!token.empty(), "countOccurrences requires a non-empty token");
    auto offsets = std::views::iota(std::size_t{0u}, contents.size());
    return static_cast<std::size_t>(std::ranges::count_if(offsets, [&](std::size_t offset) { return contents.substr(offset).starts_with(token); }));
}

void requireOrdered(std::string_view contents, std::string_view first, std::string_view second, std::string_view message)
{
    auto const firstPosition = contents.find(first);
    auto const secondPosition = firstPosition == std::string_view::npos ? std::string_view::npos : contents.find(second, firstPosition + first.size());
    nr::test::require(firstPosition != std::string_view::npos && secondPosition != std::string_view::npos && firstPosition < secondPosition, std::string{message});
}

[[nodiscard]] nr::rhi::DlssOptimalSettings makeOptimalSettings(
    nr::rhi::DlssDimensions targetSize,
    nr::rhi::DlssQuality quality)
{
    auto divisor = std::uint32_t{2u};
    switch (quality)
    {
    case nr::rhi::DlssQuality::Performance:
        divisor = 4u;
        break;
    case nr::rhi::DlssQuality::Balanced:
        divisor = 3u;
        break;
    case nr::rhi::DlssQuality::Quality:
        divisor = 2u;
        break;
    case nr::rhi::DlssQuality::UltraPerformance:
        divisor = 5u;
        break;
    case nr::rhi::DlssQuality::Dlaa:
        divisor = 1u;
        break;
    case nr::rhi::DlssQuality::Count:
        std::unreachable();
    }
    return nr::rhi::DlssOptimalSettings{
        .optimalRenderSize = nr::rhi::DlssDimensions{
            targetSize.width / divisor,
            targetSize.height / divisor,
        },
        .minimumRenderSize = nr::rhi::DlssDimensions{1u, 1u},
        .maximumRenderSize = targetSize,
    };
}

const nr::test::CaseRegistrar dlssRrPublicContractCase{"renderpasses DLSS RR keeps its complete disabled-by-default contract", [] {
                                                           auto input = nr::renderPasses::makeDefaultDlssRayReconstructionNodeInput();
                                                           nr::test::require(!input.enabled, "DLSS RR must be disabled by default");
                                                           nr::test::requireEqual(nr::rhi::kDlssRayReconstructionResourceSlotCount, std::size_t{64u});
                                                           nr::test::requireEqual(nr::rhi::kDlssRayReconstructionSubrectSlotCount, std::size_t{39u});
                                                           nr::test::require(input.create.flags.hdr, "RR default creation must request HDR");
                                                           nr::test::require(!input.evaluate.visualizeMotionVectors, "RR motion-vector visualization must default to disabled");
                                                           nr::test::requireEqual(input.create.quality, nr::rhi::DlssQuality::Quality);
                                                           nr::test::requireEqual(input.create.roughnessMode, nr::rhi::DlssRoughnessMode::Packed);
                                                           nr::test::requireEqual(std::ranges::count(input.includeResources, true), std::ptrdiff_t{7}, "RR defaults should activate only the six required inputs plus specular hit distance");
                                                           nr::test::require(input.includeResources[static_cast<std::size_t>(nr::rhi::DlssRayReconstructionResourceSlot::SpecularHitDistance)], "RR defaults should request specular hit distance for reflected reprojection");
                                                           nr::test::require(!input.includeResources[static_cast<std::size_t>(nr::rhi::DlssRayReconstructionResourceSlot::DisocclusionMask)], "RR defaults must not request the experimental disocclusion mask");
                                                           nr::test::require(!input.includeResources[static_cast<std::size_t>(nr::rhi::DlssRayReconstructionResourceSlot::MotionVectors3D)], "RR defaults must not request experimental 3D motion vectors");

                                                           auto indices = std::views::iota(std::size_t{0u}, nr::rhi::kDlssRayReconstructionResourceSlotCount);
                                                           std::ranges::for_each(indices, [&](std::size_t index) {
                                                               nr::test::require(!input.resourceKeys[index].empty(), std::format("DLSS RR slot {} needs a key", index));
                                                               nr::test::require(input.resourceKeys[index].starts_with("dlss.rr.input."), "DLSS RR keys must stay node-private");
                                                           });
                                                           nr::test::require(nr::renderPasses::dlssRayReconstructionResourceRequired(nr::rhi::DlssRayReconstructionResourceSlot::Color, input.create.roughnessMode, false), "RR noisy color must be required");
                                                           nr::test::require(!nr::renderPasses::dlssRayReconstructionResourceRequired(nr::rhi::DlssRayReconstructionResourceSlot::Roughness, nr::rhi::DlssRoughnessMode::Packed, false), "packed roughness must not require a separate texture");
                                                           nr::test::require(nr::renderPasses::dlssRayReconstructionResourceRequired(nr::rhi::DlssRayReconstructionResourceSlot::Roughness, nr::rhi::DlssRoughnessMode::Unpacked, false), "unpacked roughness must require a separate texture");
                                                       }};

const nr::test::CaseRegistrar dlssRrResolutionControllerCase{"renderpasses DLSS RR resolution controller caches quality queries and resets temporal history on extent transitions", [] {
                                                                  auto controller = nr::renderPasses::DlssRayReconstructionResolutionController{};
                                                                  auto queryCount = std::size_t{0u};
                                                                  auto query = nr::renderPasses::DlssRayReconstructionResolutionController::OptimalSettingsQuery{
                                                                      [&](nr::rhi::DlssDimensions targetSize, nr::rhi::DlssQuality quality) {
                                                                          ++queryCount;
                                                                          return makeOptimalSettings(targetSize, quality);
                                                                      }};

                                                                  auto const display = vk::Extent2D{1600u, 900u};
                                                                  auto disabled = controller.resolve({}, display, query);
                                                                  nr::test::requireEqual(disabled.displayExtent, display);
                                                                  nr::test::requireEqual(disabled.renderExtent, display);
                                                                  nr::test::require(!disabled.resetHistory);
                                                                  nr::test::requireEqual(queryCount, std::size_t{0u});

                                                                  auto qualityRequest = nr::renderPasses::DlssRayReconstructionResolutionRequest{
                                                                      .enabled = true,
                                                                      .quality = nr::rhi::DlssQuality::Quality,
                                                                  };
                                                                  auto quality = controller.resolve(qualityRequest, display, query);
                                                                  nr::test::requireEqual(quality.renderExtent, vk::Extent2D{800u, 450u});
                                                                  nr::test::require(quality.resetHistory);
                                                                  nr::test::requireEqual(queryCount, std::size_t{1u});

                                                                  auto qualityCacheHit = controller.resolve(qualityRequest, display, query);
                                                                  nr::test::require(!qualityCacheHit.resetHistory);
                                                                  nr::test::requireEqual(queryCount, std::size_t{1u});

                                                                  auto performanceRequest = qualityRequest;
                                                                  performanceRequest.quality = nr::rhi::DlssQuality::Performance;
                                                                  auto performance = controller.resolve(performanceRequest, display, query);
                                                                  nr::test::requireEqual(performance.renderExtent, vk::Extent2D{400u, 225u});
                                                                  nr::test::require(performance.resetHistory);
                                                                  nr::test::requireEqual(queryCount, std::size_t{2u});

                                                                  auto qualityRoundTrip = controller.resolve(qualityRequest, display, query);
                                                                  nr::test::requireEqual(qualityRoundTrip.renderExtent, quality.renderExtent);
                                                                  nr::test::require(qualityRoundTrip.resetHistory);
                                                                  nr::test::requireEqual(queryCount, std::size_t{2u});

                                                                  auto const resizedDisplay = vk::Extent2D{2000u, 1000u};
                                                                  auto resized = controller.resolve(qualityRequest, resizedDisplay, query);
                                                                  nr::test::requireEqual(resized.renderExtent, vk::Extent2D{1000u, 500u});
                                                                  nr::test::require(resized.resetHistory);
                                                                  nr::test::requireEqual(queryCount, std::size_t{3u});

                                                                  auto disabledAfterActive = controller.resolve({}, resizedDisplay, query);
                                                                  nr::test::requireEqual(disabledAfterActive.renderExtent, resizedDisplay);
                                                                  nr::test::require(disabledAfterActive.resetHistory);
                                                                  nr::test::requireEqual(queryCount, std::size_t{3u});
                                                                  auto disabledCacheHit = controller.resolve({}, resizedDisplay, query);
                                                                  nr::test::require(!disabledCacheHit.resetHistory);
                                                                  nr::test::requireEqual(queryCount, std::size_t{3u});

                                                                  auto dlaaRequest = nr::renderPasses::DlssRayReconstructionResolutionRequest{
                                                                      .enabled = true,
                                                                      .quality = nr::rhi::DlssQuality::Dlaa,
                                                                  };
                                                                  auto dlaa = controller.resolve(dlaaRequest, resizedDisplay, query);
                                                                  nr::test::requireEqual(dlaa.renderExtent, resizedDisplay);
                                                                  nr::test::require(dlaa.resetHistory);
                                                                  nr::test::requireEqual(queryCount, std::size_t{4u});
                                                                  dlaaRequest.bypass = true;
                                                                  auto dlaaBypass = controller.resolve(dlaaRequest, resizedDisplay, query);
                                                                  nr::test::require(!dlaaBypass.resetHistory);
                                                                  nr::test::requireEqual(queryCount, std::size_t{4u});

                                                                  auto snapshot = controller.snapshot();
                                                                  nr::test::require(snapshot.has_value());
                                                                  nr::test::requireEqual(snapshot->request, dlaaRequest);
                                                                  nr::test::requireEqual(snapshot->displayExtent, resizedDisplay);
                                                                  nr::test::requireEqual(snapshot->renderExtent, resizedDisplay);
                                                                  nr::test::require(snapshot->optimalSettings.has_value());
                                                              }};

const nr::test::CaseRegistrar dlssRrMatrixConventionCase{"renderpasses DLSS RR converts GLM transforms to NGX row-vector matrices", [] {
                                                             auto transform = glm::mat4{1.0f};
                                                             transform[0] = glm::vec4{0.0f, 1.0f, 0.0f, 0.0f};
                                                             transform[1] = glm::vec4{-1.0f, 0.0f, 0.0f, 0.0f};
                                                             transform[2] = glm::vec4{0.0f, 0.0f, 1.0f, 0.0f};
                                                             transform[3] = glm::vec4{4.0f, -2.0f, 7.0f, 1.0f};

                                                             auto const converted = nr::renderPasses::detail::toDlssRowVectorMatrix(transform);
                                                             auto const expected = std::array{
                                                                 0.0f, 1.0f, 0.0f, 0.0f,
                                                                 -1.0f, 0.0f, 0.0f, 0.0f,
                                                                 0.0f, 0.0f, 1.0f, 0.0f,
                                                                 4.0f, -2.0f, 7.0f, 1.0f,
                                                             };

                                                             nr::test::requireEqual(converted, expected, "NGX row-vector matrices must be the mathematical transpose of GLM column-vector matrices");
                                                         }};

const nr::test::CaseRegistrar dlssRrSourceBoundaryCase{"renderpasses DLSS RR is catalog-driven and explicitly installed in rtobject", [] {
                                                           auto const node = readProjectFile("src/renderPasses/DlssRayReconstruction/nrDlssRayReconstructionNode.cpp");
                                                           auto const nodeInterface = readProjectFile("src/renderPasses/DlssRayReconstruction/nrDlssRayReconstructionNode.ixx");
                                                           auto const pathTracing = readProjectFile("src/renderPasses/PathTracing/nrPathTracingNode.cpp");
                                                           auto const debugShader = readProjectFile("shader/renderer/dlssRayReconstructionDebug.slang");
                                                           auto const renderPassExport = readProjectFile("src/renderPasses/exportModule.ixx");
                                                           auto const rtObjectPipeline = readProjectFile("src/pipeline/nrRtObjectPipeline.cpp");
                                                           auto const rendererInterface = readProjectFile("src/renderer/nrRenderer.ixx");
                                                           auto const rendererImplementation = readProjectFile("src/renderer/nrRenderer.cpp");
                                                           auto const executorInterface = readProjectFile("src/renderer/nrRenderGraphExecutor.ixx");
                                                           auto const executorImplementation = readProjectFile("src/renderer/nrRenderGraphExecutor.cpp");
                                                           auto const dependencyInterface = readProjectFile("src/extern/dependencyDlss.ixx");
                                                           auto const dependencyImplementation = readProjectFile("src/extern/dependencyDlss.cpp");
                                                           auto const bridgeInterface = readProjectFile("src/extern/dlssBridge/include/nrDlssBridge.h");
                                                           auto const bridgeImplementation = readProjectFile("src/extern/dlssBridge/nrDlssBridge.cpp");
                                                           auto const rhiInterface = readProjectFile("src/rhi/nrDlss.ixx");
                                                           auto const rhiImplementation = readProjectFile("src/rhi/nrDlss.cpp");
                                                           auto const deviceImplementation = readProjectFile("src/rhi/nrDevice.cpp");

                                                           requirePresent(renderPassExport, "export import :dlssRayReconstruction;", "nr.renderPasses must export the RR node");
                                                           requirePresent(nodeInterface, "return \"render.dlss\";", "RR must expose one actionable semantic for graph singleton preflight");
                                                           requirePresent(nodeInterface, "void declareOptions", "RR must declare controls through the graph option catalog");
                                                           requirePresent(nodeInterface, "void collectOptionAvailability", "RR must publish reset-history availability through the shared snapshot");
                                                           requirePresent(node, "nr::options::makeDlssDefinitions(", "RR must use the canonical DLSS option definitions");
                                                           requirePresent(node, "detail::resolveDlssInput(", "RR must derive its frame configuration from one immutable option snapshot");
                                                           requirePresent(node, "frameParameters.optionSnapshot.get()", "RR structural and materialization paths must read the renderer frame snapshot");
                                                           requirePresent(node, "nr::options::keys::dlssVisualizeMotionVectors", "RR motion-vector visualization must be snapshot-controlled");
                                                           requirePresent(node, "nr::options::keys::dlssResetHistory", "RR reset-history must be represented as a frame effect");
                                                           requirePresent(node, "frameParameters.frameEffectSink->get().claim(*this", "RR reset-history must claim the evaluation pass");
                                                           requireAbsent(node, "context.addSection(", "RR must not expose a node-local actionable UI");
                                                           requireAbsent(nodeInterface, "collectUi", "RR must not expose the removed node UI mutation boundary");
                                                           requireAbsent(node, "uiDraft_", "RR must not retain a second writable UI draft");
                                                           requireAbsent(node, "pendingInput_", "RR must not retain a node-local pending mutation");
                                                           requireAbsent(node, "pendingOneShotReset_", "RR must not retain a node-local one-shot flag");
                                                           requirePresent(node, "activeCreateDesc != createDesc", "only create-config changes should recreate the feature");
                                                           requirePresent(node, "evalDesc.reset =", "evaluation reset should combine the snapshot effect with renderer temporal reset");
                                                           requirePresent(rendererImplementation, "cameraJitter_ = spec.cameraJitter", "installed graphs must retain their configured camera jitter");
                                                           requirePresent(node, "cameraFrameState.jitter.pixelOffset", "automatic RR jitter must use the renderer's current pixel-space offset");
                                                           requirePresent(node, "input.evaluate.automaticJitter", "RR evaluation must preserve automatic versus manual jitter selection");
                                                           requirePresent(node, ": input.evaluate.manualJitter", "RR evaluation must forward programmatic manual jitter");
                                                           requirePresent(node, "auto createDesc = input.create", "RR creation must retain programmatic roughness, depth, flags, and presets");
                                                           requirePresent(node, "createDesc.renderSize = input.overrideRenderSize ? input.renderSizeOverride", "RR creation must retain programmatic render-size overrides");
                                                           requirePresent(node, "createDesc.targetSize = input.overrideTargetSize ? input.targetSizeOverride", "RR creation must retain programmatic target-size overrides");
                                                           requirePresent(node, "evalDesc.motionVectorScale = input.evaluate.motionVectorScale", "RR evaluation must forward programmatic motion-vector scale");
                                                           requirePresent(node, "evalDesc.preExposure = input.evaluate.preExposure", "RR evaluation must forward programmatic pre-exposure");
                                                           requirePresent(node, "evalDesc.exposureScale = input.evaluate.exposureScale", "RR evaluation must forward programmatic exposure scale");
                                                           requirePresent(node, "evalDesc.indicatorInvertXAxis = input.evaluate.indicatorInvertXAxis", "RR evaluation must forward programmatic indicator X inversion");
                                                           requirePresent(node, "evalDesc.indicatorInvertYAxis = input.evaluate.indicatorInvertYAxis", "RR evaluation must forward programmatic indicator Y inversion");
                                                           requirePresent(node, "evalDesc.toneMapper = input.evaluate.toneMapper", "RR evaluation must forward the programmatic tone mapper unchanged");
                                                           requirePresent(node, "evalDesc.worldToViewRowMajor = input.evaluate.worldToViewRowMajor", "RR evaluation must forward programmatic world-to-view matrices");
                                                           requirePresent(node, "evalDesc.viewToClipRowMajor = input.evaluate.viewToClipRowMajor", "RR evaluation must forward programmatic view-to-clip matrices");
                                                           requirePresent(node, "input.evaluate.automaticFrameTimeDelta ? std::clamp(measuredDelta, 0.01f, 1000.0f) : input.evaluate.manualFrameTimeDeltaMilliseconds", "RR evaluation must preserve automatic and programmatic manual frame delta");
                                                           requirePresent(node, "!input.bypass || input.create.quality == nr::rhi::DlssQuality::Dlaa", "RR runtime must enforce DLAA-only bypass");
                                                           requirePresent(node, "!input.overrideRenderSize && !input.overrideTargetSize", "coordinated RR must forbid node-local size overrides");
                                                           requirePresent(node, "frameParameters.resolutionPlan.renderExtent", "coordinated RR must consume the renderer render extent");
                                                           requirePresent(node, "frameParameters.resolutionPlan.displayExtent", "coordinated RR must consume the renderer display extent");
                                                           requirePresent(node, "resolutionSnapshot->request == activeRequest", "coordinated RR must match the early request to active staged input");
                                                           requirePresent(node, "resolutionSnapshot->displayExtent == frameParameters.resolutionPlan.displayExtent", "coordinated RR must match the controller-owned display extent");
                                                           requirePresent(node, "resolutionSnapshot->renderExtent == frameParameters.resolutionPlan.renderExtent", "coordinated RR must match the controller-owned render extent");
                                                           requireAbsent(nodeInterface, "nr::renderer::FrameResolutionPlan plan", "the DLSS snapshot must not copy the renderer-final temporal reset");
                                                           requireAbsent(node, "DLSS always receives a zero jitter offset", "RR must not force jitter off");
                                                           requireAbsent(rendererInterface, "bool reset = false", "renderer camera state must not carry renderer-wide temporal reset state");
                                                           requirePresent(rendererInterface, "struct FrameResolutionPlan", "renderer must expose the generic display/render resolution plan");
                                                           requirePresent(rendererInterface, "std::optional<FrameResolutionResolver> frameResolutionResolver", "renderer graph specs must optionally inject a generic frame resolution resolver");
                                                           requirePresent(rendererImplementation, "frameParameters.resolutionPlan = resolutionPlan", "renderer must publish the resolved plan to node frame parameters");
                                                           requirePresent(rendererImplementation, "frameParameters.resolutionPlan.renderExtent", "renderer camera jitter must use the resolved render extent");
                                                           requirePresent(rendererImplementation, "frameResolutionResolver_.has_value() && static_cast<bool>(*frameResolutionResolver_)", "renderer must early-acquire only for a non-empty resolution resolver");
                                                           requireOrdered(rendererImplementation, "preAcquiredFrameImage = device_->acquireFrameImage(input.acquireTimeout)", "auto const currentDisplayExtent", "resolver graphs must acquire before snapshotting display extent");
                                                           requirePresent(executorInterface, "std::optional<nr::rhi::Device::FrameAcquireResult> preAcquiredFrameImage{}", "executor contexts must optionally carry a pre-acquired frame image");
                                                           requirePresent(executorImplementation, "context.preAcquiredFrameImage.has_value()", "executor must recognize a pre-acquired frame image");
                                                           requirePresent(executorImplementation, "activeSwapchainImageIndex() == acquire.swapchainImageIndex", "executor must validate the pre-acquired image remains active");
                                                           requireOrdered(executorImplementation, "? *context.preAcquiredFrameImage", "resolveSwapchainRuntimeResources(", "executor must reuse the pre-acquired result before resolving the swapchain binding");
                                                           requirePresent(node, "frameParameters.resolutionPlan.resetHistory", "RR evaluation should consume the renderer-final temporal reset directly");
                                                           requireAbsent(node, "cameraFrameState.reset", "RR evaluation must not route renderer-wide reset through camera jitter state");
                                                           requireAbsent(node, "cameraFrameState.accumulationReset", "RR must not consume the standalone Accumulate node's history policy");
                                                           requireWhitespaceInsensitivePresent(rendererImplementation, "makeRendererCameraFrameState(cameraJitter_, sampleFrameOrdinal, frameParameters.resolutionPlan.renderExtent);", "renderer jitter should follow the monotonic sample-frame ordinal and resolved render extent without carrying temporal reset");
                                                           requireAbsent(rendererImplementation, "cameraStableFrameOrdinal_", "camera movement must not restart the Halton sequence");
                                                           requirePresent(node, "runtime_->resetNextEvaluation = true", "disabled RR must request a reset on resume");
                                                           requirePresent(node, "previousBuildTime_ = {};", "disabled RR must reset automatic frame-delta history");
                                                           requireAbsent(node, "!input.enabled || input.bypass", "RR output bypass must not skip evaluation");
                                                           requirePresent(node, "input.bypass ? handles[colorIndex] : outputColor", "RR output bypass must present the PathTracing input color after evaluation");
                                                           requirePresent(node, "runtime->resetNextEvaluation = true", "new or rebuilt RR features must request a reset");
                                                           requirePresent(node, "std::exchange(runtime->resetNextEvaluation, false)", "feature lifecycle reset must be consumed once");
                                                           requirePresent(node, "handles[gBufferSpecularMvIndex].valid()", "GBuffer slot 10 must satisfy reflection motion-vector validation");
                                                           requirePresent(node, "maximum - targetSize.width", "output subrect width arithmetic must be overflow checked");
                                                           requirePresent(node, "subrectBase.x + targetSize.width", "output subrect width must include its base");
                                                           requirePresent(node, "static_assert(subrectResourceMappings.size() == nr::rhi::kDlssRayReconstructionSubrectSlotCount)", "subrect validation must cover exactly the 39 SDK subrect fields");
                                                           requirePresent(node, "SubrectSlot::Translucency, nr::rhi::DlssRayReconstructionResourceSlot::TransparencyMask", "the SDK translucency subrect must map to its transparency-mask resource");
                                                           requirePresent(node, "if (!description.has_value())", "subrect validation must skip resources that are not active");
                                                           requirePresent(node, "resource == Resource::Output || resource == Resource::OutputAlpha", "output subrects must use target dimensions");
                                                           requirePresent(node, "createDesc.flags.motionVectorsLowResolution", "motion-vector bounds must select render or target dimensions from the creation flag");
                                                           requirePresent(node, "required.width <= extent.width - base.x", "subrect width validation must use subtraction after checking the base");
                                                           requirePresent(node, "required.height <= extent.height - base.y", "subrect height validation must use subtraction after checking the base");
                                                           requirePresent(node, "subrect base ({}, {}) with required size {}x{} exceeds image extent {}x{}", "subrect failure diagnostics must identify the base, required size, and actual extent");
                                                           requireOrdered(node, "descriptions[outputAlphaIndex] =", "detail::validateActiveSubrectBounds(", "subrect validation must run after all active output descriptions are known");
                                                           requireOrdered(node, "detail::validateActiveSubrectBounds(", "evalDesc.subrectBases =", "subrect validation must run before evaluation parameters are captured");
                                                           requirePresent(node, "image.extent.width, image.extent.height", "NGX image extents must come from record-time resolution");
                                                           requirePresent(node, "runtime->optimalSettingsQueried = true", "RR runtime must record successful optimal-settings acquisition");
                                                           requirePresent(node, "input.outputColorFormat != vk::Format::eUndefined", "RR color output format must fail fast when undefined");
                                                           requirePresent(node, "input.outputAlphaFormat != vk::Format::eUndefined", "RR alpha output format must fail fast when undefined");
                                                           requirePresent(node, "input.outputAlphaKey != input.outputColorKey", "RR output keys must remain distinct");
                                                           requirePresent(node, "if (coordinatedOptimalSettings.has_value())", "coordinated RR must reuse the early controller optimal-settings snapshot");
                                                           requirePresent(node, "dlssContext->optimalSettings(createDesc.targetSize, createDesc.quality)", "standalone RR must retain its late optimal-settings query");
                                                           requirePresent(node, "detail::validateDlssOptimalSettings(\n            settings", "coordinated resolution must use the shared optimal-settings validation");
                                                           requirePresent(node, "validateDlssOptimalSettings(runtime->optimalSettings, createDesc.targetSize, createDesc.quality)", "standalone and coordinated prepare must validate optimal settings");
                                                           requirePresent(node, "settings.status.success()", "optimal-settings validation must reject failed queries");
                                                           requirePresent(node, "settings.optimalRenderSize.valid() && settings.minimumRenderSize.valid() && settings.maximumRenderSize.valid()", "optimal-settings validation must reject zero dimensions");
                                                           requirePresent(node, "settings.maximumRenderSize.width <= targetSize.width", "optimal-settings validation must reject ranges outside the target width");
                                                           requirePresent(node, "settings.maximumRenderSize.height <= targetSize.height", "optimal-settings validation must reject ranges outside the target height");
                                                           requirePresent(node, "settings.optimalRenderSize == targetSize", "optimal-settings validation must enforce DLAA target equality");
                                                           nr::test::requireEqual(countOccurrences(node, "dlssContext->optimalSettings("), std::size_t{1u}, "RR build must not duplicate the standalone NGX optimal-settings query");
                                                           requireOrdered(node, "if (!runtime->feature || runtime->activeCreateDesc != createDesc)", "auto replacement = device.createDlssRayReconstructionFeature", "RR feature creation must remain inside the create/recreate branch");
                                                           requireOrdered(node, "if (coordinatedOptimalSettings.has_value())", "auto replacement = device.createDlssRayReconstructionFeature", "RR optimal settings must be selected before feature creation");
                                                           requireOrdered(node, "validateDlssOptimalSettings(runtime->optimalSettings, createDesc.targetSize, createDesc.quality)", "auto replacement = device.createDlssRayReconstructionFeature", "RR optimal settings must be validated before feature creation");
                                                           requirePresent(node, "ImageUsageIntent::StorageWrite", "RR output must declare storage-write intent");
                                                           requirePresent(node, "ImageUsageIntent::Sampled", "RR inputs/output must retain sampled intent");
                                                           requirePresent(node, "DLSS.RayReconstruction.VisualizeMotionVectors", "RR must emit the optional post-evaluation MV visualization pass");
                                                           requirePresent(node, ".sampledImage(\"gMotionVectors\"", "RR MV visualization must sample the exact NGX motion-vector input");
                                                           requirePresent(node, ".storageImage(\"gMotionVectorVisualization\", outputColor", "RR MV visualization must replace only the presented RR output");
                                                           requireOrdered(node, "auto pass = context.addPass(", "DLSS.RayReconstruction.VisualizeMotionVectors", "RR evaluation must precede visualization so debug display does not suspend temporal history");
                                                           requirePresent(debugShader, "float2 motionPixels = gMotionVectors.Load", "RR debug shader must visualize the supplied motion-vector buffer");
                                                           requirePresent(debugShader, "abs(motionPixels) * 64.0f", "RR debug shader must retain Messiah's logarithmic pixel-motion amplification");
                                                           requirePresent(debugShader, "float4(1.0f, 0.0f, 1.0f, 1.0f)", "RR debug shader must mark non-finite or out-of-range motion in magenta");
                                                           requirePresent(rendererInterface, "SceneBridgeFrameConstants renderCameraConstants{}", "node frame parameters must expose the actual unjittered render camera");
                                                           requirePresent(rendererImplementation, "frameParameters.renderCameraConstants = globalFrameConstants", "renderer must publish the selected scene or override camera before applying projection jitter");
                                                           requirePresent(node, "frameParameters.renderCameraConstants.view", "automatic RR matrices must use the actual render camera view");
                                                           requirePresent(node, "frameParameters.renderCameraConstants.projection", "automatic RR matrices must use the actual unjittered render projection");
                                                           requireAbsent(node, "frameParameters.primaryCamera", "RR must not ignore renderer camera overrides by reading scene-only primary-camera metadata");
                                                           requirePresent(nodeInterface, "dlss.rr.output.color", "RR output must use an independent frame key");
                                                           requireAbsent(node, "presentSourceColor", "RR must not implicitly connect to the presentation chain");
                                                           requirePresent(rtObjectPipeline, "DlssRayReconstructionNode", "rtobject should install the RR node explicitly");
                                                           requirePresent(rtObjectPipeline, "input.create.quality = context.rtDlssQuality", "rtobject should use the startup DLSS quality only to seed the graph option default");
                                                           requirePresent(rtObjectPipeline, "DlssQuality::Dlaa", "rtobject should begin with full-resolution DLAA reconstruction");
                                                           requirePresent(rtObjectPipeline, "DlssDepthType::Hardware", "rtobject should declare its clip-depth guide as hardware depth");
                                                           requirePresent(rtObjectPipeline, "input.outputColorKey = std::string{nr::renderer::frameResource::presentSourceColor}", "rtobject should explicitly route RR output to presentation");
                                                           requirePresent(rtObjectPipeline, "setResolutionController(dlssResolutionController)", "rtobject RR must attach the shared resolution controller to its node");
                                                           requirePresent(rtObjectPipeline, "graphSpec.frameResolutionResolver", "rtobject RR must install the generic renderer resolution resolver");
                                                           requirePresent(rtObjectPipeline, "dlssResolutionRequestFromSnapshot(snapshot)", "rtobject RR resolver must read the same immutable snapshot as the node");
                                                           requirePresent(rtObjectPipeline, "frameResolutionOptionRequirements", "rtobject RR preflight must declare every option consumed by its resolution resolver");
                                                           requirePresent(rtObjectPipeline, "switch (context.rtPostProcessingMode)", "rtobject should select exactly one post-processing implementation");
                                                           requirePresent(rtObjectPipeline, ".runtime = postProcessing", "rtobject should install the selected Accumulate or RR implementation into one graph slot");
                                                           requireOrdered(rtObjectPipeline, ".instanceName = \"PathTracing\"", ".runtime = postProcessing", "selected post-processing must consume PathTracing output after it is published");
                                                           requireOrdered(rtObjectPipeline, ".runtime = postProcessing", ".instanceName = \"Present\"", "Present must consume the selected post-processing output");
                                                           requirePresent(dependencyInterface, "rayReconstructionResourceSlotCount == 64u", "typed API must retain all SDK image slots");
                                                           requirePresent(dependencyInterface, "rayReconstructionSubrectSlotCount == 39u", "typed API must retain all SDK subrect slots");
                                                           requirePresent(dependencyInterface, "struct ExtensionQueryResult\n{\n    Status status{};\n    std::vector<std::string> names{};\n};", "extension queries must carry status separately from names");
                                                           requirePresent(dependencyInterface, "ExtensionQueryResult rayReconstructionInstanceExtensions()", "instance-extension discovery must expose its status");
                                                           requirePresent(dependencyInterface, "ExtensionQueryResult rayReconstructionDeviceExtensions(", "device-extension discovery must expose its status");
                                                           requireAbsent(dependencyInterface, "nvsdk_ngx", "raw NGX headers must not leak through the module interface");
                                                           requireAbsent(dependencyImplementation, "nvsdk_ngx", "the libc++ host implementation must not include NGX headers");
                                                           requireAbsent(dependencyImplementation, "NVSDK_NGX_", "the libc++ host implementation must not call NGX directly");
                                                           requirePresent(dependencyImplementation, "LoadLibraryExW", "the host must load the bridge with the restricted Windows loader API");
                                                           requirePresent(dependencyImplementation, "GetModuleFileNameW", "the host must resolve the bridge from the absolute executable directory");
                                                           requirePresent(dependencyImplementation, "nrDlssBridgeGetApi", "the host must negotiate the single C ABI function table");
                                                           requirePresent(dependencyImplementation, "bridgeRuntime().api.destroyContext(context);", "Context Impl must release its raw bridge context");
                                                           requirePresent(dependencyImplementation, "bridgeRuntime().api.destroyFeature(feature);", "feature Impl must release its raw bridge feature");
                                                           requirePresent(dependencyImplementation, "Context::~Context() = default;", "Context cleanup must be owned by Impl so move assignment releases the destination");
                                                           requirePresent(dependencyImplementation, "RayReconstructionFeature::~RayReconstructionFeature() = default;", "feature cleanup must be owned by Impl so move assignment releases the destination");
                                                           requireAbsent(dependencyImplementation, "if (impl_ && impl_->context != nullptr)", "Context cleanup must not remain solely in the outer destructor");
                                                           requireAbsent(dependencyImplementation, "if (impl_ && impl_->feature != nullptr)", "feature cleanup must not remain solely in the outer destructor");
                                                           requirePresent(dependencyImplementation, "ExtensionQueryResult{.status = fromBridgeStatus(code, status)}", "extension-query failures must preserve the bridge status");
                                                           requirePresent(dependencyImplementation, "if (count == 0u)", "successful zero-extension results must remain distinct from failures");
                                                           requirePresent(dependencyImplementation, "ExtensionQueryResult{.status = bridgeNotAvailableStatus()}", "unavailable bridge extension queries must report the existing unavailable status");
                                                           requirePresent(bridgeInterface, "NR_DLSS_BRIDGE_ABI_VERSION", "the C bridge must expose an explicit ABI version");
                                                           requirePresent(bridgeInterface, "NR_DLSS_BRIDGE_RR_RESOURCE_COUNT 64u", "the C bridge must retain all SDK image slots");
                                                           requirePresent(bridgeInterface, "NR_DLSS_BRIDGE_RR_SUBRECT_COUNT 39u", "the C bridge must retain all SDK subrect slots");
                                                           requireAbsent(bridgeInterface, "std::", "the C bridge ABI must not expose C++ standard-library types");
                                                           requirePresent(bridgeImplementation, "nvsdk_ngx_helpers_dlssd_vk.h", "raw NGX integration must stay in the MSVC bridge");
                                                           requirePresent(bridgeImplementation, "NGX_VULKAN_EVALUATE_DLSSD_EXT", "the bridge must call real RR evaluation");
                                                           requirePresent(bridgeImplementation, "explicitReflectionMotionVectors != nullptr", "explicit reflection motion vectors must have priority over GBuffer slot 10");
                                                           requirePresent(bridgeImplementation, ": gBufferSpecularMotionVectors;", "reflection motion vectors must fall back to GBuffer slot 10");
                                                           requireAbsent(bridgeImplementation, "NVSDK_NGX_Parameter_GBuffer_Atrrib_10", "slot 10 must not be manually set before the helper overwrites it");
                                                           requirePresent(bridgeImplementation, "\"GBuffer.Attrib.16\"", "slot 16 must retain its explicit SDK parameter");
                                                           requireAbsent(dependencyImplementation, "std::filesystem::current_path()", "DLSS dependency filesystem lookup must use the error-code overload");
                                                           requirePresent(dependencyImplementation, "std::filesystem::create_directories(applicationDataPath, pathError)", "DLSS dependency directory creation must use the error-code overload");
                                                           requirePresent(rhiInterface, "const vk::raii::CommandBuffer& commandBuffer", "nr.rhi DLSS feature creation must expose an RAII command-buffer boundary");
                                                           requireAbsent(rhiInterface, "vk::CommandBuffer commandBuffer", "nr.rhi DLSS feature creation must not expose a raw command buffer");
                                                           requirePresent(rhiImplementation, "static_cast<vk::CommandBuffer>(*commandBuffer)", "nr.rhi must convert to the extern command-buffer type only at the dependency call");
                                                           requirePresent(deviceImplementation, "recording.get(),", "Device must pass its RAII command buffer to the DLSS feature wrapper");
                                                           requireAbsent(deviceImplementation, "static_cast<vk::CommandBuffer>(*recording.get())", "Device must not convert the DLSS creation command buffer to a raw handle");
                                                           requirePresent(deviceImplementation, "std::filesystem::current_path(pathError)", "Device DLSS application-data lookup must use the error-code overload");
                                                           requirePresent(deviceImplementation, "if (nr::dependency::dlss::sdkCompiled())\n        {\n            auto const deviceExtensionQuery", "Device must query DLSS device extensions only when the bridge is available");
                                                           requirePresent(deviceImplementation, "if (nr::dependency::dlss::sdkCompiled())\n        {\n            auto const instanceExtensionQuery", "Device must query DLSS instance extensions only when the bridge is available");
                                                           requireOrdered(deviceImplementation, "deviceExtensionQuery.status.success()", "deviceExtensionQuery.names", "Device must fail fast before consuming device-extension names");
                                                           requireOrdered(deviceImplementation, "instanceExtensionQuery.status.success()", "instanceExtensionQuery.names", "Device must fail fast before consuming instance-extension names");
                                                           requireOrdered(rhiImplementation, "feature_.reset();", "context_.reset();", "RR move assignment must release the old feature before its context");
                                                           requireOrdered(rhiImplementation, "context_.reset();", "context_ = std::move(other.context_);", "RR move assignment must release the old context before replacing it");
                                                           requireAbsent(pathTracing, "presentationContext.swapchainExtent()", "PathTracing initialization must not eagerly allocate display-sized guides");
                                                           requirePresent(pathTracing, "frameParameters.resolutionPlan.renderExtent", "PathTracing must consume the renderer render extent");
                                                           requirePresent(pathTracing, "vk::Extent2D allocatedExtent{}", "each PathTracing frame slot must own independent guide allocation metadata");
                                                           requirePresent(pathTracing, "auto& guideFrameSlot = runtime_->guideFrameSlots[frameSlotIndex]", "PathTracing must select only the completed current frame slot for guide growth");
                                                           requirePresent(pathTracing, "ensurePathTracingGuideImages(device_->get(), guideFrameSlot, frameSlotIndex, renderExtent", "PathTracing guide allocation must grow only the current frame slot at renderer render extent");
                                                           requireAbsent(pathTracing, "std::ranges::all_of(\n        runtime.guideFrameSlots", "PathTracing guide growth must not inspect and replace every in-flight frame slot");
                                                           requirePresent(pathTracing, ".width = renderExtent.width", "PathTracing trace width must use the renderer render extent");
                                                           requirePresent(pathTracing, ".height = renderExtent.height", "PathTracing trace height must use the renderer render extent");
                                                       }};
} // namespace
