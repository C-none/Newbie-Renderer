import std;
import dependency.math;
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

const nr::test::CaseRegistrar dlssRrSourceBoundaryCase{"renderpasses DLSS RR is exported, staged, and explicitly installed in rtobject", [] {
                                                           auto const node = readProjectFile("src/renderPasses/DlssRayReconstruction/nrDlssRayReconstructionNode.cpp");
                                                           auto const nodeInterface = readProjectFile("src/renderPasses/DlssRayReconstruction/nrDlssRayReconstructionNode.ixx");
                                                           auto const debugShader = readProjectFile("shader/renderer/dlssRayReconstructionDebug.slang");
                                                           auto const renderPassExport = readProjectFile("src/renderPasses/exportModule.ixx");
                                                           auto const pipeline = readProjectFile("src/pipeline/nrPipeline.cpp");
                                                           auto const rendererInterface = readProjectFile("src/renderer/nrRenderer.ixx");
                                                           auto const rendererImplementation = readProjectFile("src/renderer/nrRenderer.cpp");
                                                           auto const dependencyInterface = readProjectFile("src/extern/dependencyDlss.ixx");
                                                           auto const dependencyImplementation = readProjectFile("src/extern/dependencyDlss.cpp");
                                                           auto const bridgeInterface = readProjectFile("src/extern/dlssBridge/include/nrDlssBridge.h");
                                                           auto const bridgeImplementation = readProjectFile("src/extern/dlssBridge/nrDlssBridge.cpp");
                                                           auto const rhiInterface = readProjectFile("src/rhi/nrDlss.ixx");
                                                           auto const rhiImplementation = readProjectFile("src/rhi/nrDlss.cpp");
                                                           auto const deviceImplementation = readProjectFile("src/rhi/nrDevice.cpp");

                                                           requirePresent(renderPassExport, "export import :dlssRayReconstruction;", "nr.renderPasses must export the RR node");
                                                           nr::test::requireEqual(countOccurrences(node, "context.addSection("), std::size_t{1u}, "RR must expose all controls and status through one node UI session");
                                                           requirePresent(node, "\"dlss-rr\"", "RR must use one stable UI session id");
                                                           requirePresent(node, "uiDraft_.evaluate.manualJitter[0], -0.5f, 0.5f", "RR manual jitter X must stay within the SDK pixel-offset range");
                                                           requirePresent(node, "uiDraft_.evaluate.manualJitter[1], -0.5f, 0.5f", "RR manual jitter Y must stay within the SDK pixel-offset range");
                                                           requirePresent(node, "Visualize Motion Vectors", "RR must expose motion-vector visualization in its existing UI session");
                                                           requirePresent(node, "pendingInput_", "RR UI edits must be staged to the next frame");
                                                           requirePresent(node, "pendingOneShotReset_", "RR UI reset must be a staged one-shot");
                                                           requirePresent(node, "activeCreateDesc != createDesc", "only create-config changes should recreate the feature");
                                                           requirePresent(node, "evalDesc.reset = consumeOneShotReset_", "evaluation reset should consume the one-shot state");
                                                           requirePresent(rendererImplementation, "cameraJitter_ = spec.cameraJitter", "installed graphs must retain their configured camera jitter");
                                                           requirePresent(node, "cameraFrameState.jitter.pixelOffset", "automatic RR jitter must use the renderer's current pixel-space offset");
                                                           requirePresent(node, "input.evaluate.automaticJitter", "RR evaluation must preserve automatic versus manual jitter selection");
                                                           requireAbsent(node, "DLSS always receives a zero jitter offset", "RR must not force jitter off");
                                                           requirePresent(rendererInterface, "bool reset = false", "renderer camera reset should remain false until a camera-mutation interface exists");
                                                           requirePresent(node, "cameraFrameState.reset", "RR evaluation should consume the renderer-owned camera reset flag");
                                                           requireAbsent(node, "cameraFrameState.accumulationReset", "RR must not consume the standalone Accumulate node's history policy");
                                                           requirePresent(rendererImplementation, "makeRendererCameraFrameState(\n            cameraJitter_,\n            sampleFrameOrdinal,", "renderer jitter should follow the monotonic sample-frame ordinal");
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
                                                           requirePresent(node, "runtime->optimalSettingsQueried = true", "optimal settings status must record that a query occurred");
                                                           requirePresent(node, "runtime->optimalSettingsQueried &&", "status UI must hide default optimal settings before a query");
                                                           requirePresent(node, "input.outputColorFormat != vk::Format::eUndefined", "RR color output format must fail fast when undefined");
                                                           requirePresent(node, "input.outputAlphaFormat != vk::Format::eUndefined", "RR alpha output format must fail fast when undefined");
                                                           requirePresent(node, "input.outputAlphaKey != input.outputColorKey", "RR output keys must remain distinct");
                                                           requireOrdered(node, "if (!runtime->feature || runtime->activeCreateDesc != createDesc)", "runtime->optimalSettings = context->optimalSettings", "RR optimal settings must be queried only inside the create/recreate branch");
                                                           requireOrdered(node, "runtime->optimalSettings = context->optimalSettings", "auto replacement = device.createDlssRayReconstructionFeature", "RR optimal settings must be queried before feature creation");
                                                           requirePresent(node, "ImageUsageIntent::StorageWrite", "RR output must declare storage-write intent");
                                                           requirePresent(node, "ImageUsageIntent::Sampled", "RR inputs/output must retain sampled intent");
                                                           requirePresent(node, "DLSS.RayReconstruction.VisualizeMotionVectors", "RR must emit the optional post-evaluation MV visualization pass");
                                                           requirePresent(node, ".sampledImage(\"gMotionVectors\"", "RR MV visualization must sample the exact NGX motion-vector input");
                                                           requirePresent(node, ".storageImage(\"gMotionVectorVisualization\", outputColor", "RR MV visualization must replace only the presented RR output");
                                                           requireOrdered(node, "context.addPass(intents, \"DLSS.RayReconstruction\"", "DLSS.RayReconstruction.VisualizeMotionVectors", "RR evaluation must precede visualization so debug display does not suspend temporal history");
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
                                                           requirePresent(pipeline, "DlssRayReconstructionNode", "rtobject should install the RR node explicitly");
                                                           requirePresent(pipeline, "input.enabled = true", "rtobject should enable its explicitly installed RR node");
                                                           requirePresent(pipeline, "DlssQuality::Dlaa", "rtobject should begin with full-resolution DLAA reconstruction");
                                                           requirePresent(pipeline, "DlssDepthType::Hardware", "rtobject should declare its clip-depth guide as hardware depth");
                                                           requirePresent(pipeline, "input.outputColorKey = std::string{nr::renderer::frameResource::presentSourceColor}", "rtobject should explicitly route RR output to presentation");
                                                           requirePresent(pipeline, "switch (context.rtPostProcessingMode)", "rtobject should select exactly one post-processing implementation");
                                                           requirePresent(pipeline, ".runtime = postProcessing", "rtobject should install the selected Accumulate or RR implementation into one graph slot");
                                                           requireOrdered(pipeline, ".instanceName = \"PathTracing\"", ".runtime = postProcessing", "selected post-processing must consume PathTracing output after it is published");
                                                           requireOrdered(pipeline, ".runtime = postProcessing", ".instanceName = \"Present\"", "Present must consume the selected post-processing output");
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
                                                       }};
} // namespace
