import dependency.json;
import dependency.vulkan;
import std;
import nr.options;
import nr.rhi;
import nr.renderer;
import nr.renderPasses;
import nr.resource;
import nr.scene;
import nr.test;
import nr.test.options;
import nr.utils;

namespace
{
const nr::test::CaseRegistrar migratedRenderPassBranchSnapshotsCase{
    "all rtobject nodes opt into exact Skeleton branch snapshots", [] {
        auto pathTracing = nr::renderPasses::PathTracingNode{};
        nr::test::require(pathTracing.supportsRenderGraphSkeleton());
        auto pathDefaultSnapshot = nr::test::options::makeDefaultSnapshot(
            nr::test::options::buildCatalog(nr::options::makePathTracingDefinitions()), "renderpasses-cache-contract");
        auto const pathDefault = pathTracing.structuralSnapshot(nr::renderer::NodeFrameParameters{
            .optionSnapshot = std::cref(pathDefaultSnapshot),
        });
        nr::test::require(pathDefault.has_value());
        auto pathVariantSnapshot = pathDefaultSnapshot;
        pathVariantSnapshot.values.insert_or_assign(
            nr::options::optionId(nr::options::keys::pathTracingMaxSurfaceBounces),
            nr::options::OptionWireValue{std::uint64_t{7u}});
        auto const pathVariant = pathTracing.structuralSnapshot(nr::renderer::NodeFrameParameters{
            .optionSnapshot = std::cref(pathVariantSnapshot),
        });
        nr::test::require(pathVariant.has_value());
        nr::test::require(pathDefault->branchKey != pathVariant->branchKey);

        auto pathFilterAfterShadingSnapshot = pathDefaultSnapshot;
        pathFilterAfterShadingSnapshot.values.insert_or_assign(
            nr::options::optionId(nr::options::keys::pathTracingFilterAfterShadingEnabled),
            nr::options::OptionWireValue{true});
        auto const pathFilterAfterShading = pathTracing.structuralSnapshot(nr::renderer::NodeFrameParameters{
            .optionSnapshot = std::cref(pathFilterAfterShadingSnapshot),
        });
        nr::test::require(pathFilterAfterShading.has_value());
        nr::test::require(pathDefault->branchKey != pathFilterAfterShading->branchKey,
                          "Filter After Shading must select a distinct PathTracing structural branch");

        auto dlss = nr::renderPasses::DlssRayReconstructionNode{};
        nr::test::require(dlss.supportsRenderGraphSkeleton());
        auto dlssDefaultSnapshot = nr::test::options::makeDefaultSnapshot(
            nr::test::options::buildCatalog(nr::options::makeDlssDefinitions()), "renderpasses-cache-contract");
        auto const dlssDefault = dlss.structuralSnapshot(nr::renderer::NodeFrameParameters{
            .optionSnapshot = std::cref(dlssDefaultSnapshot),
        });
        nr::test::require(dlssDefault.has_value());
        nr::test::require(dlssDefault->branchKey.contains(";bypass=0;alpha=0;hdr=1;debug=0;"));

        auto dlssBypassSnapshot = dlssDefaultSnapshot;
        dlssBypassSnapshot.values.insert_or_assign(nr::options::optionId(nr::options::keys::dlssBypass),
                                                   nr::options::OptionWireValue{true});
        auto const dlssBypass = dlss.structuralSnapshot(nr::renderer::NodeFrameParameters{
            .optionSnapshot = std::cref(dlssBypassSnapshot),
        });
        nr::test::require(dlssBypass.has_value());
        nr::test::require(dlssBypass->branchKey.contains(";bypass=1;alpha=0;hdr=1;debug=0;"));
        nr::test::require(dlssDefault->branchKey != dlssBypass->branchKey);

        auto dlssDebugSnapshot = dlssDefaultSnapshot;
        dlssDebugSnapshot.values.insert_or_assign(nr::options::optionId(nr::options::keys::dlssVisualizeMotionVectors),
                                                  nr::options::OptionWireValue{true});
        auto const dlssDebug = dlss.structuralSnapshot(nr::renderer::NodeFrameParameters{
            .optionSnapshot = std::cref(dlssDebugSnapshot),
        });
        nr::test::require(dlssDebug.has_value());
        nr::test::require(dlssDebug->branchKey.contains(";bypass=0;alpha=0;hdr=1;debug=1;"));
        nr::test::require(dlssDefault->branchKey != dlssDebug->branchKey);

        dlss.input.create.flags.alphaUpscaling = true;
        auto const dlssAlpha = dlss.structuralSnapshot(nr::renderer::NodeFrameParameters{
            .optionSnapshot = std::cref(dlssDefaultSnapshot),
        });
        nr::test::require(dlssAlpha.has_value());
        nr::test::require(dlssAlpha->branchKey.contains(";bypass=0;alpha=1;hdr=1;debug=0;"));
        nr::test::require(dlssDefault->branchKey != dlssAlpha->branchKey);

        auto dlssBypassDebugSnapshot = dlssDebugSnapshot;
        dlssBypassDebugSnapshot.values.insert_or_assign(nr::options::optionId(nr::options::keys::dlssBypass),
                                                        nr::options::OptionWireValue{true});
        auto const dlssBypassAlphaDebug = dlss.structuralSnapshot(nr::renderer::NodeFrameParameters{
            .optionSnapshot = std::cref(dlssBypassDebugSnapshot),
        });
        nr::test::require(dlssBypassAlphaDebug.has_value());
        nr::test::require(dlssBypassAlphaDebug->branchKey.contains(";bypass=1;alpha=1;hdr=1;debug=1;"));
        nr::test::require(dlssAlpha->branchKey != dlssBypassAlphaDebug->branchKey);

        auto present = nr::renderPasses::PresentNode{};
        nr::test::require(present.supportsRenderGraphSkeleton());
        auto presentDefaultSnapshot = nr::test::options::makeDefaultSnapshot(
            nr::test::options::buildCatalog(nr::options::makePresentDefinitions()), "renderpasses-cache-contract");
        auto presentSnapshot = [&](const nr::options::OptionFrameSnapshot &optionSnapshot,
                                   vk::Format format = vk::Format::eUndefined,
                                   vk::ColorSpaceKHR colorSpace = vk::ColorSpaceKHR::eSrgbNonlinear) {
            return present.structuralSnapshot(nr::renderer::NodeFrameParameters{
                .optionSnapshot = std::cref(optionSnapshot),
                .swapchainFormat = format,
                .swapchainColorSpace = colorSpace,
            });
        };
        auto const presentDefault = presentSnapshot(presentDefaultSnapshot);
        nr::test::require(presentDefault.has_value());
        auto requireSamePresentTopology = [&](const auto &candidate, std::string_view message) {
            nr::test::require(candidate.has_value() &&
                                  candidate->configurationRevision == presentDefault->configurationRevision &&
                                  candidate->branchKey == presentDefault->branchKey,
                              std::string{message});
        };

        auto presentToneSnapshot = presentDefaultSnapshot;
        presentToneSnapshot.values.insert_or_assign(nr::options::optionId(nr::options::keys::presentToneMapping),
                                                     nr::options::OptionWireValue{"reinhard"});
        requireSamePresentTopology(presentSnapshot(presentToneSnapshot),
                                   "Present tone mapping must not change Skeleton topology");
        auto presentOpacitySnapshot = presentDefaultSnapshot;
        presentOpacitySnapshot.values.insert_or_assign(nr::options::optionId(nr::options::keys::presentUiOpacity),
                                                        nr::options::OptionWireValue{0.25});
        requireSamePresentTopology(presentSnapshot(presentOpacitySnapshot),
                                   "Present UI opacity must not change Skeleton topology");
        present.input.format = vk::Format::eB8G8R8A8Srgb;
        requireSamePresentTopology(presentSnapshot(presentDefaultSnapshot),
                                   "Present fallback format must not duplicate the renderer Skeleton key");
        requireSamePresentTopology(
            presentSnapshot(presentDefaultSnapshot, vk::Format::eA2B10G10R10UnormPack32,
                            vk::ColorSpaceKHR::eHdr10St2084EXT),
            "Present swapchain format and color space must not duplicate the renderer Skeleton key");

        auto captureSnapshot = presentDefaultSnapshot;
        captureSnapshot.effect = nr::options::FrameEffect{
            .sequence = 1u,
            .id = nr::options::optionId(nr::options::keys::presentCaptureExr),
        };
        nr::test::require(!presentSnapshot(captureSnapshot).has_value(),
                          "Present capture effects must keep using cold graph materialization");

        auto readbackBuffer = nr::rhi::Buffer{};
        present.input.readback = nr::renderPasses::PresentReadbackTarget{
            .buffer = std::cref(readbackBuffer),
        };
        auto const presentReadback = presentSnapshot(presentDefaultSnapshot);
        nr::test::require(presentReadback.has_value() && presentReadback->branchKey != presentDefault->branchKey,
                          "Present readback presence must select distinct Skeleton topology");
        present.input.readback->offset = 256u;
        auto const presentReadbackOffset = presentSnapshot(presentDefaultSnapshot);
        nr::test::require(
            presentReadbackOffset.has_value() &&
                presentReadbackOffset->configurationRevision == presentReadback->configurationRevision &&
                presentReadbackOffset->branchKey == presentReadback->branchKey,
            "Present readback offset must patch the existing readback topology");

        auto ui = nr::renderPasses::UiNode{};
        nr::test::require(ui.supportsRenderGraphSkeleton());

        auto accelerationStructure = nr::renderPasses::AccelerationStructureBuildNode{};
        nr::test::require(accelerationStructure.supportsRenderGraphSkeleton());
    }};

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

[[nodiscard]] std::string removeWhitespace(std::string_view value)
{
    return value |
           std::views::filter([](char character) { return std::isspace(static_cast<unsigned char>(character)) == 0; }) |
           std::ranges::to<std::string>();
}

void requirePresent(std::string_view contents, std::string_view token, std::string_view message)
{
    nr::test::require(removeWhitespace(contents).contains(removeWhitespace(token)), std::string{message});
}

const nr::test::CaseRegistrar singleEntryPointShaderFileCase{
    "each Slang source file declares at most one shader entry point", [] {
        auto shaderSources =
            std::filesystem::recursive_directory_iterator{std::filesystem::path{std::string{nr::projectRoot}} /
                                                          "shader"} |
            std::views::filter([](const std::filesystem::directory_entry &entry) {
                return entry.is_regular_file() && entry.path().extension() == ".slang";
            });

        std::ranges::for_each(shaderSources, [](const auto &entry) {
            auto const relativePath =
                std::filesystem::relative(entry.path(), std::filesystem::path{std::string{nr::projectRoot}});
            auto const source = readProjectFile(relativePath);
            auto const entryPointAttribute =
                std::regex{R"(\[\s*shader\s*\()", std::regex_constants::ECMAScript | std::regex_constants::optimize};
            auto const matches = std::ranges::subrange{
                std::sregex_iterator{source.begin(), source.end(), entryPointAttribute}, std::sregex_iterator{}};
            auto const entryPointCount = std::ranges::distance(matches);
            nr::test::require(
                entryPointCount <= 1,
                std::format("shader source '{}' declares {} entry points; each file may declare at most one",
                            relativePath.generic_string(), entryPointCount));
        });
    }};

const nr::test::CaseRegistrar renderPassShaderRequestCollectionCase{
    "render passes declare ordered static single-entry shader requests", [] {
        auto requireRequests = []<std::size_t Count>(const nr::renderer::NodeRuntime &node,
                                                     const std::array<std::string_view, Count> &expectedPaths) {
            auto const requests = node.shaderRequests();
            nr::test::requireEqual(requests.size(), expectedPaths.size());
            std::ranges::for_each(std::views::iota(std::size_t{0}, requests.size()), [&](std::size_t index) {
                nr::test::requireEqual(requests[index].sourcePath.generic_string(), std::string{expectedPaths[index]});
                nr::test::require(requests[index].variant.assignments.empty(),
                                  "static render-pass shaders should not carry unrelated variants");
            });
        };

        requireRequests(nr::renderPasses::AccumulateNode{}, std::array{std::string_view{"renderer/accumulate"}});
        requireRequests(nr::renderPasses::DlssRayReconstructionNode{},
                        std::array{std::string_view{"renderer/dlssRayReconstructionDebug"}});
        requireRequests(nr::renderPasses::EmbeddedTriangleNode{},
                        std::array{
                            std::string_view{"renderer/embeddedTriangle/vertex"},
                            std::string_view{"renderer/embeddedTriangle/fragment"},
                        });
        requireRequests(nr::renderPasses::NormalBufferNode{}, std::array{
                                                                  std::string_view{"renderer/normalBuffer/vertex"},
                                                                  std::string_view{"renderer/normalBuffer/fragment"},
                                                              });
        requireRequests(nr::renderPasses::PresentNode{}, std::array{std::string_view{"renderer/presentConvert"}});
        requireRequests(nr::renderPasses::UiNode{}, std::array{
                                                        std::string_view{"renderer/appUi/vertex"},
                                                        std::string_view{"renderer/appUi/fragment"},
                                                    });
        requireRequests(nr::renderPasses::PathTracingNode{}, std::array<std::string_view, 0>{});
    }};

void requireOrdered(std::string_view contents, std::string_view beforeToken, std::string_view afterToken,
                    std::string_view message)
{
    auto const before = contents.find(beforeToken);
    auto const after = contents.find(afterToken);
    nr::test::require(before != std::string_view::npos && after != std::string_view::npos && before < after,
                      std::string{message});
}

void requireExactlyOne(std::string_view contents, std::string_view token, std::string_view message)
{
    auto const first = contents.find(token);
    nr::test::require(first != std::string_view::npos &&
                          contents.find(token, first + token.size()) == std::string_view::npos,
                      std::string{message});
}

[[nodiscard]] std::string_view sourceSection(std::string_view contents, std::string_view beginToken,
                                             std::string_view endToken)
{
    auto const begin = contents.find(beginToken);
    nr::test::require(begin != std::string_view::npos, "source section begin token is missing");
    auto const end = contents.find(endToken, begin + beginToken.size());
    nr::test::require(end != std::string_view::npos, "source section end token is missing");
    return contents.substr(begin, end - begin);
}

const nr::test::CaseRegistrar embeddedTriangleFixedVertexDomainCase{
    "EmbeddedTriangle shader consumes the fixed three-vertex draw domain directly", [] {
        auto node = readProjectFile("src/renderPasses/EmbeddedTriangle/nrEmbeddedTriangleNode.cpp");
        auto vertexShader = readProjectFile("shader/renderer/embeddedTriangle/vertex.slang");
        auto build = sourceSection(node, "void EmbeddedTriangleNode::build(",
                                   "void EmbeddedTriangleNode::shutdown(");

        requirePresent(build, "rasterContext.commandBuffer.draw(3, 1, 0, 0)",
                       "EmbeddedTriangle must issue exactly the shader's three-vertex domain");
        requireExactlyOne(node, "commandBuffer.draw(",
                          "EmbeddedTriangle production code must retain one non-indexed draw call");
        requirePresent(vertexShader, "float4(positions[vertexId], 1.0f)",
                       "EmbeddedTriangle positions must use the fixed SV_VertexID directly");
        requirePresent(vertexShader, "output.color = colors[vertexId]",
                       "EmbeddedTriangle colors must use the same fixed SV_VertexID directly");
        requireAbsent(vertexShader, "% 3u",
                      "The fixed three-vertex draw contract must not retain a modulo fallback");
        requireAbsent(vertexShader, "uint index",
                      "The fixed three-vertex draw contract must not retain a redundant local index");
        requireAbsent(vertexShader, "positions[index]",
                      "EmbeddedTriangle position lookup must not hide the fixed vertex domain");
        requireAbsent(vertexShader, "colors[index]",
                      "EmbeddedTriangle color lookup must not hide the fixed vertex domain");
        requireAbsent(vertexShader, "clamp(",
                      "The fixed production draw contract must not add a shader-side bounds fallback");
    }};

const nr::test::CaseRegistrar uiTextureRetirementFrameSlotCase{
    "UI texture retirement follows completed RHI frame slots", [] {
        auto ui = readProjectFile("src/renderPasses/Ui/nrUiNode.cpp");
        auto retiredTexture = sourceSection(ui, "struct UiRetiredTexture", "struct UiRuntimeCache");
        auto retireTexture = sourceSection(ui, "void retireUiTexture(", "void createOrUpdateUiTexture(");
        auto replaceTexture = sourceSection(ui, "void createOrUpdateUiTexture(", "void destroyUiTexture(");
        auto destroyTexture = sourceSection(ui, "void destroyUiTexture(",
                                            "[[nodiscard]] bool runtimeHasValidTextureFor(");
        auto cleanupTextures = sourceSection(ui, "void cleanupRetiredTextures(",
                                             "void synchronizeUiTextures(");
        auto synchronizeTextures = sourceSection(ui, "void synchronizeUiTextures(",
                                                 "[[nodiscard]] std::vector<nr::renderer::GraphResourceHandle>");
        auto drawPreflight = sourceSection(ui, "[[nodiscard]] UiDrawFramePayload prepareUiDrawFrame(",
                                          "} // namespace nr::renderPasses::detail");

        requirePresent(retiredTexture, "std::bitset<nr::maxFrameInFlight> pendingFrameSlots",
                       "Retired UI textures must track the exact in-flight RHI slots that may still reference them");
        requireAbsent(ui, "retiredFrameIndex",
                      "UI retirement must not treat the circular RHI frame slot as a monotonic frame index");
        requireAbsent(ui, "kRetirementFrameCount",
                      "UI retirement must not guess GPU completion from an elapsed frame count");
        requireOrdered(retireTexture, "pendingFrameSlots.set()", "pendingFrameSlots.reset(currentFrameSlot)",
                       "UI retirement must mark every slot before removing the already-reclaimed current slot");
        requireOrdered(retireTexture, "pendingFrameSlots.reset(currentFrameSlot)",
                       "runtime.retiredTextures.push_back",
                       "The retired image must capture its exact pending-slot mask");
        requirePresent(replaceTexture,
                       "retireUiTexture(runtime, std::move(textureEntry.image), textureSlot, currentFrameSlot, false)",
                       "Texture replacement must retain the old image without retiring its live descriptor slot");
        requirePresent(destroyTexture,
                       "retireUiTexture(runtime, std::move(textureEntry.image), slot, currentFrameSlot, true)",
                       "Texture destruction must defer both the old image and its descriptor slot");
        requireAbsent(replaceTexture, "pendingFrameSlots.set()",
                      "Texture replacement must share the canonical retirement-mask helper");
        requireAbsent(destroyTexture, "pendingFrameSlots.set()",
                      "Texture destruction must share the canonical retirement-mask helper");
        requireOrdered(cleanupTextures, "retired.pendingFrameSlots.reset(currentFrameSlot)",
                       "retired.pendingFrameSlots.any()",
                       "Each completed preflight slot must be cleared before testing retirement completion");
        requireOrdered(cleanupTextures, "retired.pendingFrameSlots.any()", "return false",
                       "A retired image must remain alive while any in-flight slot is pending");
        requireOrdered(cleanupTextures, "if (retired.releaseSlot", "runtime.freeTextureSlots.push_back(retired.slot)",
                       "Only destroy retirement may return an empty descriptor slot to the free list");
        requirePresent(cleanupTextures, "!runtime.texturesBySlot[retired.slot].image.valid()",
                       "A destroy retirement must not reuse a slot that has acquired a current image");
        requireAbsent(synchronizeTextures, "cleanupRetiredTextures(",
                      "UI retirement cleanup must not depend on finalized draw data or a texture list");
        requireOrdered(drawPreflight, "cleanupRetiredTextures(runtime, currentFrameSlot)",
                       "tryGetUiOverlaySystem(frameParameters)",
                       "Clear-only frames must advance texture retirement before the missing-UiSystem early return");
        requireOrdered(drawPreflight, "cleanupRetiredTextures(runtime, currentFrameSlot)",
                       "uiSystem->get().drawData()",
                       "Texture retirement must advance before the missing-draw-data early return");
        requireOrdered(drawPreflight, "uiSystem->get().drawData()", "synchronizeUiTextures(",
                       "Texture synchronization must remain conditional on finalized draw data");

        static_assert(nr::maxFrameInFlight > 1u);
        constexpr auto retirementFrameSlot = std::size_t{0u};
        auto pendingFrameSlots = std::bitset<nr::maxFrameInFlight>{};
        pendingFrameSlots.set();
        pendingFrameSlots.reset(retirementFrameSlot);
        nr::test::require(!pendingFrameSlots.test(retirementFrameSlot),
                          "The slot whose fence completed before retirement must never be pending");
        nr::test::requireEqual(pendingFrameSlots.count(), nr::maxFrameInFlight - 1u,
                               "Every other in-flight slot must remain pending at retirement");

        auto remainingSlots = std::views::iota(std::size_t{1u}, std::size_t{nr::maxFrameInFlight}) |
                              std::ranges::to<std::vector>();
        auto retiredImageAlive = true;
        auto freeSlots = std::vector<std::uint32_t>{};
        std::ranges::for_each(remainingSlots | std::views::take(remainingSlots.size() - 1u),
                              [&](std::size_t completedSlot) {
                                  pendingFrameSlots.reset(completedSlot);
                                  nr::test::require(pendingFrameSlots.any() && retiredImageAlive && freeSlots.empty(),
                                                    "Neither image nor slot may release before the last pending fence");
                              });
        pendingFrameSlots.reset(remainingSlots.back());
        nr::test::require(pendingFrameSlots.none(),
                          "The final distinct slot preflight must complete image retirement");
        retiredImageAlive = false;
        freeSlots.push_back(7u);
        nr::test::require(!retiredImageAlive && freeSlots == std::vector<std::uint32_t>{7u},
                          "Destroy retirement may reuse its empty slot only after every pending fence completes");

        pendingFrameSlots.set();
        pendingFrameSlots.reset(retirementFrameSlot);
        std::ranges::for_each(remainingSlots,
                              [&](std::size_t completedSlot) { pendingFrameSlots.reset(completedSlot); });
        retiredImageAlive = false;
        freeSlots.clear();
        nr::test::require(pendingFrameSlots.none() && !retiredImageAlive && freeSlots.empty(),
                          "Replacement retirement must release the old image without freeing its live slot");
    }};

const nr::test::CaseRegistrar uiFrozenFormatAndResizeTransitionCase{
    "UI freezes its PSO format and clears full resize transition frames", [] {
        auto uiInterface = readProjectFile("src/renderPasses/Ui/nrUiNode.ixx");
        auto ui = readProjectFile("src/renderPasses/Ui/nrUiNode.cpp");
        auto resolveFormat = sourceSection(ui, "[[nodiscard]] constexpr vk::Format resolveUiBufferFormat(",
                                           "/// Initial vertex buffer capacity");
        auto runtime = sourceSection(ui, "struct UiRuntimeCache", "[[nodiscard]] vk::Format validatedFrozenUiBufferFormat(");
        auto formatInvariant = sourceSection(ui, "[[nodiscard]] vk::Format validatedFrozenUiBufferFormat(",
                                             "[[nodiscard]] vk::PipelineColorBlendAttachmentState");
        auto ensureBuffer = sourceSection(ui, "void ensureUiBufferImage(",
                                          "[[nodiscard]] std::shared_ptr<UiRuntimeCache> ensureUiRuntime(");
        auto ensureRuntime = sourceSection(ui, "[[nodiscard]] std::shared_ptr<UiRuntimeCache> ensureUiRuntime(",
                                           "[[nodiscard]] std::uint64_t makeManagedTextureKey(");
        auto copyDrawData = sourceSection(ui, "[[nodiscard]] UiFrameDrawData copyUiDrawData(",
                                          "[[nodiscard]] UiDrawFramePayload prepareUiDrawFrame(");
        auto drawPreflight = sourceSection(ui, "[[nodiscard]] UiDrawFramePayload prepareUiDrawFrame(",
                                           "} // namespace nr::renderPasses::detail");
        auto callbacks = sourceSection(ui, "struct UiDrawCommandRecorder", "struct UiValidatedDrawCounts");
        auto initialization = sourceSection(ui, "void UiNode::initialize(", "void UiNode::finalizeInitialization(");
        auto finalization = sourceSection(ui, "void UiNode::finalizeInitialization(", "void UiNode::build(");
        auto structuralSnapshot = sourceSection(
            ui, "std::optional<nr::renderer::NodeRuntime::StructuralSnapshot> UiNode::structuralSnapshot(",
            "bool UiNode::materializeRenderGraphSkeleton(");
        auto skeleton = sourceSection(ui, "bool UiNode::materializeRenderGraphSkeleton(",
                                      "void UiNode::materializeCurrentFrame(");
        auto coldBuild = sourceSection(ui, "void UiNode::materializeCurrentFrame(", "void UiNode::shutdown(");

        requirePresent(uiInterface, "struct UiNodeInput",
                       "UiNodeInput must remain part of the exported render-pass ABI");
        requirePresent(uiInterface, "vk::Format bufferFormat = vk::Format::eR8G8B8A8Unorm",
                       "UiNodeInput must retain its public bufferFormat field and default");
        requirePresent(uiInterface, "UiNodeInput input{}",
                       "UiNode must retain its public input member for ABI compatibility");
        requirePresent(resolveFormat, "input.bufferFormat == vk::Format::eUndefined",
                       "The cpp-local resolver must preserve the legacy undefined-format default");
        requirePresent(runtime, "vk::Format frozenBufferFormat = vk::Format::eUndefined",
                       "Ui runtime must own the format frozen at PSO initialization");
        requireAbsent(ui, "allocatedUiFormat",
                      "Ui image allocation must not duplicate the immutable PSO format as mutable allocation state");
        requireOrdered(formatInvariant, "resolveUiBufferFormat(input)",
                       "resolvedFormat == runtime.frozenBufferFormat",
                       "The single format invariant must reject public-input mutation after initialization");
        requirePresent(formatInvariant, "return runtime.frozenBufferFormat",
                       "Validated callers must consume the frozen runtime format, not the mutable input value");
        requireOrdered(initialization, "resolveUiBufferFormat(input)", "ensureUiRuntime(",
                       "Ui initialization must resolve its public input exactly once before runtime creation");
        requireOrdered(ensureRuntime, "runtime->frozenBufferFormat = frozenBufferFormat",
                       "pipelineDesc.colorAttachmentFormats = {runtime->frozenBufferFormat}",
                       "Ui PSO creation must consume the runtime's frozen format");
        requirePresent(ensureBuffer, "makeImageCreateInfo(runtime.frozenBufferFormat, extent",
                       "Ui overlay images must use the same frozen PSO attachment format");
        requireAbsent(ensureBuffer, "vk::Format format",
                      "Ui overlay image allocation must not accept a second format source");
        requirePresent(finalization, "validatedFrozenUiBufferFormat(input, *runtime_)",
                       "Ui PSO finalization must reject a format mutation during asynchronous construction");
        requirePresent(structuralSnapshot, "validatedFrozenUiBufferFormat(input, *runtime_)",
                       "Ui structural preflight must reject format mutation before preparing textures or keys");
        requirePresent(skeleton, "validatedFrozenUiBufferFormat(input, *runtime_)",
                       "Ui Skeleton materialization must reject format mutation before image patching");
        requirePresent(coldBuild, "validatedFrozenUiBufferFormat(input, *runtime_)",
                       "Ui cold materialization must reject format mutation before image import");
        requireAbsent(structuralSnapshot, "input.bufferFormat",
                      "Ui structural keys must not read mutable input as a production format source");
        requireAbsent(skeleton, "input.bufferFormat",
                      "Ui Skeleton image patching must not read mutable input as a production format source");
        requireAbsent(coldBuild, "input.bufferFormat",
                      "Ui cold image imports must not read mutable input as a production format source");
        requirePresent(skeleton, ".format = bufferFormat",
                       "Ui Skeleton imports must use the validated frozen format");
        requirePresent(coldBuild, "bufferExtent, bufferFormat, nr::renderer::ResourceLifetime::FrameLocal",
                       "Ui cold imports must use the validated frozen format");

        requirePresent(copyDrawData, "std::max(1u, swapchainExtent.width)",
                       "Ui draw frames must start with the complete non-zero swapchain width");
        requirePresent(copyDrawData, "std::max(1u, swapchainExtent.height)",
                       "Ui draw frames must start with the complete non-zero swapchain height");
        requireOrdered(copyDrawData, "!std::isfinite(framebufferWidth) || !std::isfinite(framebufferHeight)",
                       "return output",
                       "Invalid finalized ImDraw framebuffer dimensions must produce a clear-only frame");
        requireOrdered(copyDrawData, "std::numeric_limits<std::uint32_t>::max()",
                       "static_cast<std::uint32_t>(framebufferWidth)",
                       "Ui resize validation must reject out-of-range dimensions before integer conversion");
        requireAbsent(copyDrawData, "static_cast<int>(drawData.DisplaySize",
                      "Ui resize validation must not convert unvalidated floating dimensions to int");
        requirePresent(copyDrawData,
                       "finalizedFramebufferExtent != output.framebufferExtent",
                       "Ui resize transitions must require complete converted extent agreement");
        requireOrdered(copyDrawData, "finalizedFramebufferExtent != output.framebufferExtent",
                       "output.vertices.reserve", "A framebuffer mismatch must return before any draw payload is copied");
        requireAbsent(copyDrawData, "effectiveWidth",
                      "Ui resize transitions must not clamp draw data into a partial-width overlay");
        requireAbsent(copyDrawData, "effectiveHeight",
                      "Ui resize transitions must not clamp draw data into a partial-height overlay");
        requireOrdered(drawPreflight, "cleanupRetiredTextures(runtime, currentFrameSlot)",
                       "uiSystem->get().drawData()",
                       "Ui retirement cleanup must remain first in structural preflight");
        requireOrdered(drawPreflight, "uiSystem->get().drawData()", "synchronizeUiTextures(",
                       "Ui texture requests must be synchronized from finalized draw data");
        requireOrdered(drawPreflight, "synchronizeUiTextures(", "copyUiDrawData(",
                       "Ui texture requests must complete even when extent validation yields a clear-only frame");
        requirePresent(skeleton, ".viewport(drawFrame->framebufferExtent)",
                       "Ui Skeleton clear-only frames must retain the full-frame viewport and render area");
        requirePresent(coldBuild, ".viewport(drawFrame->framebufferExtent)",
                       "Ui cold clear-only frames must retain the full-frame viewport and render area");
        requirePresent(callbacks, "if (drawFrame->commands.empty())",
                       "Shared Ui recording must skip draws while preserving the attachment clear");
        requirePresent(skeleton, "makeUiOverlayCallbacks(",
                       "Ui Skeleton materialization must use the shared clear-preserving callbacks");
        requirePresent(coldBuild, "makeUiOverlayCallbacks(",
                       "Ui cold materialization must use the shared clear-preserving callbacks");
    }};

const nr::test::CaseRegistrar uiTexturePayloadBoundaryCase{
    "UI validates external ImGui texture payload bounds before byte access", [] {
        auto ui = readProjectFile("src/renderPasses/Ui/nrUiNode.cpp");
        auto payloadLayout = sourceSection(ui, "struct UiTexturePayloadLayout",
                                           "[[nodiscard]] UiTextureUploadPayload makeTextureUploadPayload(");
        auto uploadBytes = sourceSection(ui, "[[nodiscard]] UiTextureUploadPayload makeTextureUploadPayload(",
                                         "[[nodiscard]] nr::rhi::ops::BufferUploadOwnershipPlan");
        auto createTexture = sourceSection(ui, "void createOrUpdateUiTexture(", "void destroyUiTexture(");

        requireOrdered(payloadLayout, "textureData.Width > 0", "std::in_range<std::size_t>(textureData.Width)",
                       "Texture width must be positive before conversion to a host byte count");
        requireOrdered(payloadLayout, "textureData.Height > 0", "std::in_range<std::size_t>(textureData.Height)",
                       "Texture height must be positive before conversion to a host byte count");
        requireOrdered(payloadLayout, "textureHeight <= maxByteCount / textureWidth",
                       "textureWidth * textureHeight",
                       "Pixel-count multiplication must be guarded against size_t overflow");
        requireOrdered(payloadLayout, "pixelCount <= maxByteCount / kUiRgbaBytesPerPixel",
                       "pixelCount * kUiRgbaBytesPerPixel",
                       "RGBA byte-count multiplication must be guarded against size_t overflow");
        requirePresent(payloadLayout, "std::in_range<std::uint32_t>(textureWidth)",
                       "Texture width must fit the Vulkan extent before conversion");
        requirePresent(payloadLayout, "std::in_range<std::uint32_t>(textureHeight)",
                       "Texture height must fit the Vulkan extent before conversion");
        requirePresent(uploadBytes, "auto const payloadLayout = checkedUiTexturePayloadLayout(textureData)",
                       "Extent construction and upload sizing must share one checked calculation");
        requirePresent(createTexture, "auto uploadPayload = makeTextureUploadPayload(textureData)",
                       "The texture boundary must produce extent and bytes in one validated operation");
        requireAbsent(createTexture, "checkedUiTexturePayloadLayout(",
                      "The upload caller must not duplicate the payload boundary calculation");
        requireAbsent(ui, "makeTextureExtent(",
                      "Texture extent must not retain an independently validated construction path");
        requireAbsent(ui, "makeTextureUploadBytes(",
                      "Texture bytes must not retain an independently validated construction path");

        requireOrdered(uploadBytes,
                       "textureData.Format == ImTextureFormat_RGBA32 || textureData.Format == ImTextureFormat_Alpha8",
                       "textureData.GetSizeInBytes()",
                       "Unsupported texture formats must fail before consulting their byte payload");
        requirePresent(uploadBytes,
                       "rgbaSource ? payloadLayout.rgbaByteCount : payloadLayout.pixelCount",
                       "RGBA32 and Alpha8 must select exact width*height*4 and width*height source sizes");
        requireOrdered(uploadBytes, "textureData.BytesPerPixel == expectedBytesPerPixel",
                       "textureData.GetSizeInBytes()",
                       "The external bytes-per-pixel field must match the supported format before its int multiplication");
        requireOrdered(uploadBytes, "std::numeric_limits<int>::max()", "textureData.GetSizeInBytes()",
                       "The expected payload must fit ImGui's int byte-count API before calling it");
        requireOrdered(uploadBytes, "textureData.GetSizeInBytes()", "textureData.Pixels != nullptr",
                       "The exact reported source size must be checked before any pixel access");
        requirePresent(uploadBytes,
                       "static_cast<std::size_t>(reportedSourceByteCount) == expectedSourceByteCount",
                       "ImGui's reported payload size must exactly match the format-derived byte count");
        requireOrdered(uploadBytes, "textureData.Pixels != nullptr", "textureData.GetPixels()",
                       "Empty ImGui pixel storage must fail through the project diagnostic before its asserting accessor");
        requirePresent(uploadBytes, "sourcePixels != nullptr",
                       "The external pixel accessor must yield non-empty storage before reads");
        requirePresent(uploadBytes, "sourceFirst + payloadLayout.rgbaByteCount",
                       "RGBA32 copies must use the exact checked RGBA byte count");
        requirePresent(uploadBytes, "uploadBytes.resize(payloadLayout.rgbaByteCount)",
                       "Alpha8 expansion must allocate the exact checked RGBA output size");
        requirePresent(uploadBytes,
                       "std::views::iota(std::size_t{0u}, payloadLayout.pixelCount)",
                       "Alpha8 expansion must read exactly one source byte per checked pixel");
        requireAbsent(uploadBytes, "sourceFirst + reportedSourceByteCount",
                      "A reported external byte count must not independently determine the copy range");
    }};

const nr::test::CaseRegistrar uiDrawDataAndUploadBoundaryCase{
    "UI validates ImDrawData structure and upload arithmetic before use", [] {
        auto ui = readProjectFile("src/renderPasses/Ui/nrUiNode.cpp");
        auto uploadByteSize = sourceSection(ui, "[[nodiscard]] vk::DeviceSize checkedUiUploadByteSize(",
                                            "void ensureFrameUploadBuffer(");
        auto ensureUploadBuffer = sourceSection(ui, "void ensureFrameUploadBuffer(",
                                                "void uploadUiDrawFrameBuffers(");
        auto uploadDrawFrame = sourceSection(ui, "void uploadUiDrawFrameBuffers(",
                                             "[[nodiscard]] std::optional<std::reference_wrapper");
        auto callbacks = sourceSection(ui, "[[nodiscard]] UiOverlayCallbacks makeUiOverlayCallbacks(",
                                       "struct UiValidatedDrawCounts");
        auto validateDrawData = sourceSection(ui, "struct UiValidatedDrawCounts",
                                              "[[nodiscard]] UiFrameDrawData copyUiDrawData(");
        auto copyDrawData = sourceSection(ui, "[[nodiscard]] UiFrameDrawData copyUiDrawData(",
                                          "[[nodiscard]] UiDrawFramePayload prepareUiDrawFrame(");
        auto skeleton = sourceSection(ui, "bool UiNode::materializeRenderGraphSkeleton(",
                                      "void UiNode::materializeCurrentFrame(");
        auto coldBuild = sourceSection(ui, "void UiNode::materializeCurrentFrame(", "void UiNode::shutdown(");

        requireOrdered(validateDrawData, "drawData.Valid", "drawData.CmdListsCount >= 0",
                       "Finalized ImDrawData validity must be checked before consuming its counts");
        requireOrdered(validateDrawData, "drawData.CmdListsCount >= 0",
                       "std::views::iota(std::size_t{0u}, counts.commandListCount)",
                       "A negative command-list count must not enter an iota range");
        requireOrdered(validateDrawData, "drawData.TotalVtxCount >= 0",
                       "static_cast<std::size_t>(drawData.TotalVtxCount)",
                       "The total vertex count must be non-negative before size_t conversion");
        requireOrdered(validateDrawData, "drawData.TotalIdxCount >= 0",
                       "static_cast<std::size_t>(drawData.TotalIdxCount)",
                       "The total index count must be non-negative before size_t conversion");
        requirePresent(validateDrawData, "drawData.CmdLists.Size == drawData.CmdListsCount",
                       "The ImVector size must exactly match ImDrawData's legacy list count");
        requirePresent(validateDrawData, "drawData.CmdLists.Size == 0 || drawData.CmdLists.Data != nullptr",
                       "Non-empty ImDrawData command-list storage requires a data pointer");
        requireOrdered(validateDrawData, "commandList != nullptr", "commandList->VtxBuffer.Size >= 0",
                       "Every draw list must be non-null before inspecting its buffers");
        requireOrdered(validateDrawData, "commandList->VtxBuffer.Size >= 0",
                       "static_cast<std::size_t>(commandList->VtxBuffer.Size)",
                       "Negative list vertex counts must fail before size_t conversion");
        requireOrdered(validateDrawData, "commandList->IdxBuffer.Size >= 0",
                       "static_cast<std::size_t>(commandList->IdxBuffer.Size)",
                       "Negative list index counts must fail before size_t conversion");
        requireOrdered(validateDrawData, "commandList->CmdBuffer.Size >= 0",
                       "static_cast<std::size_t>(commandList->CmdBuffer.Size)",
                       "Negative list command counts must fail before size_t conversion");
        requirePresent(validateDrawData,
                       "commandList->VtxBuffer.Size == 0 || commandList->VtxBuffer.Data != nullptr",
                       "Non-empty vertex buffers require external storage");
        requirePresent(validateDrawData,
                       "commandList->IdxBuffer.Size == 0 || commandList->IdxBuffer.Data != nullptr",
                       "Non-empty index buffers require external storage");
        requirePresent(validateDrawData,
                       "commandList->CmdBuffer.Size == 0 || commandList->CmdBuffer.Data != nullptr",
                       "Non-empty command buffers require external storage");

        requirePresent(validateDrawData,
                       "counts.vertexCount = checkedUiDrawSizeAdd(counts.vertexCount, listVertexCount",
                       "List vertex totals must use the Ui-local checked addition");
        requirePresent(validateDrawData,
                       "counts.indexCount = checkedUiDrawSizeAdd(counts.indexCount, listIndexCount",
                       "List index totals must use the Ui-local checked addition");
        requirePresent(validateDrawData,
                       "counts.vertexCount == static_cast<std::size_t>(drawData.TotalVtxCount)",
                       "Scanned vertex totals must exactly match ImDrawData metadata");
        requirePresent(validateDrawData,
                       "counts.indexCount == static_cast<std::size_t>(drawData.TotalIdxCount)",
                       "Scanned index totals must exactly match ImDrawData metadata");
        requireOrdered(validateDrawData, "command.UserCallback == nullptr", "indexOffset <= listIndexCount",
                       "Every callback must fail before draw-command geometry validation");
        requireAbsent(ui, "command.UserCallback != nullptr",
                      "UiNode must not silently skip or retain borrowed ImDrawCmd callbacks");
        requireOrdered(copyDrawData, "validateUiDrawData(drawData)",
                       "validatedCounts.vertexCount == 0u || validatedCounts.indexCount == 0u",
                       "Zero-total frames must still scan every command and reject callbacks");

        requireOrdered(validateDrawData, "indexOffset <= listIndexCount",
                       "elementCount <= listIndexCount - indexOffset",
                       "Index offset must be proven in range before forming the remaining span");
        requirePresent(validateDrawData, "vertexOffset <= listVertexCount",
                       "Each command vertex offset must fit its own draw list");
        requirePresent(validateDrawData, "std::in_range<std::size_t>(localIndex)",
                       "ImDrawIdx values must be checked before size_t conversion");
        requirePresent(validateDrawData,
                       "vertexOffset, static_cast<std::size_t>(localIndex), \"draw-command vertex reference\"",
                       "Each 16/32-bit index must combine with VtxOffset through checked addition");
        requirePresent(validateDrawData, "referencedVertex < listVertexCount",
                       "Every command index must resolve inside its current list vertex buffer");
        requireOrdered(copyDrawData, "global draw-command first index", "auto const clipMinX",
                       "Global first-index validation must precede degenerate-clip early return");
        requireOrdered(copyDrawData, "global draw-command vertex offset", "auto const clipMinX",
                       "Global vertex-offset validation must precede degenerate-clip early return");
        requireOrdered(copyDrawData, "std::in_range<std::uint32_t>(firstIndex)",
                       "static_cast<std::uint32_t>(firstIndex)",
                       "Global first indices must fit uint32 before conversion");
        requireOrdered(copyDrawData, "std::in_range<std::int32_t>(vertexOffset)",
                       "static_cast<std::int32_t>(vertexOffset)",
                       "Global vertex offsets must fit int32 before conversion");

        requireOrdered(uploadByteSize,
                       "elementCount <= std::numeric_limits<std::size_t>::max() / elementByteSize",
                       "elementCount * elementByteSize",
                       "Upload byte multiplication must be guarded against size_t overflow");
        requireOrdered(uploadByteSize, "std::in_range<vk::DeviceSize>(byteSize)",
                       "static_cast<vk::DeviceSize>(byteSize)",
                       "Checked host byte sizes must fit Vulkan DeviceSize before conversion");
        requirePresent(uploadDrawFrame,
                       "checkedUiUploadByteSize(drawFrame.vertices.size(), sizeof(ImDrawVert), \"vertex\")",
                       "Vertex uploads must use the single checked byte-size function");
        requirePresent(uploadDrawFrame,
                       "checkedUiUploadByteSize(drawFrame.indices.size(), sizeof(ImDrawIdx), \"index\")",
                       "Index uploads must use the single checked byte-size function");
        requireAbsent(ui, "drawFrame.vertices.size() * sizeof(ImDrawVert)",
                      "Prepare callbacks must not retain unchecked vertex byte multiplication");
        requireAbsent(ui, "drawFrame.indices.size() * sizeof(ImDrawIdx)",
                      "Prepare callbacks must not retain unchecked index byte multiplication");
        requirePresent(uploadDrawFrame, "writeMappedAndFlush(std::span<const ImDrawVert>{drawFrame.vertices})",
                       "The shared helper must preserve direct mapped vertex uploads");
        requirePresent(uploadDrawFrame, "writeMappedAndFlush(std::span<const ImDrawIdx>{drawFrame.indices})",
                       "The shared helper must preserve direct mapped index uploads");
        requirePresent(callbacks, "uploadUiDrawFrameBuffers(device, *runtime, frameSlot, *drawFrame)",
                       "The shared prepare callback must reuse the checked upload path");
        requirePresent(skeleton, "makeUiOverlayCallbacks(",
                       "Skeleton prepare must reuse the shared checked upload callback");
        requirePresent(coldBuild, "makeUiOverlayCallbacks(",
                       "Cold prepare must reuse the shared checked upload callback");

        requireOrdered(ensureUploadBuffer, "capacity > maximumCapacity / 2u",
                       "capacity * 2u",
                       "Upload-buffer doubling must be capped before multiplication");
        requirePresent(ensureUploadBuffer, "capacity = std::max(requiredSize, cappedDoubledCapacity)",
                       "Growth must choose the required size or the safely capped double in one step");
        requireAbsent(ensureUploadBuffer, "while (capacity < requiredSize)",
                      "Upload-buffer growth no longer needs an overflow-prone loop");
        requirePresent(ensureUploadBuffer, "nr::rhi::MemoryUsage::CpuToGpu",
                       "Ui frame buffers must remain directly mapped CPU-to-GPU allocations");
    }};

const nr::test::CaseRegistrar uiCpuAbiAndIndexTypeCase{
    "UI locks its CPU push, vertex, and index ABI", [] {
        auto ui = readProjectFile("src/renderPasses/Ui/nrUiNode.cpp");
        auto abi = sourceSection(ui, "struct UiPushConstants",
                                 "[[nodiscard]] constexpr vk::Format resolveUiBufferFormat(");
        auto callbacks = sourceSection(ui, "struct UiDrawCommandRecorder", "struct UiValidatedDrawCounts");
        auto skeleton = sourceSection(ui, "bool UiNode::materializeRenderGraphSkeleton(",
                                      "void UiNode::materializeCurrentFrame(");
        auto coldBuild = sourceSection(ui, "void UiNode::materializeCurrentFrame(", "void UiNode::shutdown(");

        requirePresent(abi, "std::is_standard_layout_v<UiPushConstants>",
                       "Ui push constants must remain standard layout");
        requirePresent(abi, "sizeof(UiPushConstants) == 32u",
                       "Ui push constants must retain their exact CPU/shader byte size");
        auto const pushOffsets = std::array{
            std::pair{"scale", std::size_t{0u}},
            std::pair{"translate", std::size_t{8u}},
            std::pair{"textureIndex", std::size_t{16u}},
            std::pair{"padding", std::size_t{20u}},
        };
        std::ranges::for_each(pushOffsets, [&](auto const &field) {
            requirePresent(abi, std::format("offsetof(UiPushConstants, {}) == {}u", field.first, field.second),
                           std::format("Ui push field '{}' must retain its exact offset", field.first));
        });
        requirePresent(abi, "sizeof(UiPushConstants) <= nr::rhi::kMaxPushConstantBytes",
                       "The exact Ui ABI must also remain within the RHI push-constant ceiling");

        requirePresent(abi, "std::is_standard_layout_v<ImDrawVert>",
                       "ImDrawVert must remain standard layout before offsetof checks");
        requirePresent(abi, "sizeof(ImDrawVert) == 20u", "ImDrawVert must retain its renderer vertex stride");
        auto const drawVertexOffsets = std::array{
            std::tuple{"pos", "drawVertPosOffset", std::size_t{0u}},
            std::tuple{"uv", "drawVertUvOffset", std::size_t{8u}},
            std::tuple{"col", "drawVertColorOffset", std::size_t{16u}},
        };
        std::ranges::for_each(drawVertexOffsets, [&](auto const &field) {
            auto const &[memberName, adapterName, offset] = field;
            requirePresent(abi, std::format("imgui::{} == {}u", adapterName, offset),
                           std::format("The dependency.ui '{}' adapter offset must remain exact", memberName));
            requirePresent(abi, std::format("offsetof(ImDrawVert, {}) == {}u", memberName, offset),
                           std::format("The actual ImDrawVert '{}' offset must remain exact", memberName));
        });

        requirePresent(abi, "std::is_unsigned_v<ImDrawIdx>", "ImDrawIdx must remain an unsigned index type");
        requirePresent(abi, "sizeof(ImDrawIdx) == 2u || sizeof(ImDrawIdx) == 4u",
                       "Ui rendering supports only Vulkan's 16-bit and 32-bit index widths");
        requirePresent(abi, "inline constexpr vk::IndexType kUiIndexType",
                       "Ui index-width selection must have one cpp-local source of truth");
        auto constexpr indexTypeExpression =
            std::string_view{"sizeof(ImDrawIdx) == 2u ? vk::IndexType::eUint16 : vk::IndexType::eUint32"};
        auto const firstIndexTypeExpression = ui.find(indexTypeExpression);
        nr::test::require(firstIndexTypeExpression != std::string::npos &&
                              ui.find(indexTypeExpression, firstIndexTypeExpression + indexTypeExpression.size()) ==
                                  std::string::npos,
                          "Ui index-width selection must occur exactly once");
        requirePresent(callbacks, "bindIndexBuffer(indexBuffer.handle(), 0u, kUiIndexType)",
                       "The shared record callback must reuse the canonical Ui index type");
        requireAbsent(skeleton, "bindIndexBuffer(", "Skeleton recording must use the shared callback implementation");
        requireAbsent(coldBuild, "bindIndexBuffer(", "Cold recording must use the shared callback implementation");
    }};

const nr::test::CaseRegistrar uiImmutableFramePayloadAndSharedCallbacksCase{
    "UI shares one immutable frame payload across Skeleton and cold callbacks", [] {
        auto ui = readProjectFile("src/renderPasses/Ui/nrUiNode.cpp");
        auto drawTypes = sourceSection(ui, "struct UiFrameDrawData", "struct UiTextureEntry");
        auto runtime = sourceSection(ui, "struct UiRuntimeCache",
                                     "[[nodiscard]] vk::Format validatedFrozenUiBufferFormat");
        auto framePreparation = sourceSection(ui, "[[nodiscard]] UiDrawFramePayload makeUiDrawFramePayload(",
                                              "} // namespace nr::renderPasses::detail");
        auto callbackTypes = sourceSection(ui, "struct UiDrawCommandRecorder",
                                           "[[nodiscard]] UiOverlayCallbacks makeUiOverlayCallbacks(");
        auto callbackFactory = sourceSection(ui, "[[nodiscard]] UiOverlayCallbacks makeUiOverlayCallbacks(",
                                             "struct UiValidatedDrawCounts");
        auto prepareCallback = sourceSection(callbackFactory, ".prepare =", ".dynamicBindingSnapshot =");
        auto snapshotCallback = sourceSection(callbackFactory, ".dynamicBindingSnapshot =", ".record =");
        auto const recordBegin = callbackFactory.find(".record =");
        nr::test::require(recordBegin != std::string_view::npos,
                          "Ui shared callback factory must define a record callback");
        auto recordCallback = callbackFactory.substr(recordBegin);
        auto structuralSnapshot = sourceSection(
            ui, "std::optional<nr::renderer::NodeRuntime::StructuralSnapshot> UiNode::structuralSnapshot(",
            "bool UiNode::materializeRenderGraphSkeleton(");
        auto skeleton = sourceSection(ui, "bool UiNode::materializeRenderGraphSkeleton(",
                                      "void UiNode::materializeCurrentFrame(");
        auto coldBuild = sourceSection(ui, "void UiNode::materializeCurrentFrame(", "void UiNode::shutdown(");

        requirePresent(drawTypes, "using UiDrawFramePayload = std::shared_ptr<const UiFrameDrawData>",
                       "Delayed Ui callbacks must share one immutable draw-frame payload");
        requirePresent(runtime, "UiDrawFramePayload preparedDrawFrame{}",
                       "The runtime cache must retain the structural preflight payload by shared identity");
        requireAbsent(runtime, "std::optional<UiFrameDrawData>",
                      "The runtime cache must not retain a second value-owned draw frame");
        requireAbsent(ui, "preparedDrawFrame.has_value()",
                      "Shared payload presence must use pointer semantics without optional wrapping");
        requireAbsent(ui, "preparedDrawFrame.reset()",
                      "Prepared payload consumption must use one atomic-looking exchange operation");
        requireAbsent(drawTypes, "UiRuntimeCache",
                      "The immutable payload must not own or point back to its runtime cache");
        requireAbsent(drawTypes, "vertexByteSize",
                      "The immutable payload must not cache upload-derived vertex byte counts");
        requireAbsent(drawTypes, "indexByteSize",
                      "The immutable payload must not cache upload-derived index byte counts");

        requirePresent(framePreparation, "std::make_shared<const UiFrameDrawData>(std::move(drawFrame))",
                       "Frame preparation must publish immutable storage without copying its vectors");
        requireOrdered(framePreparation, "drawFrame.framebufferExtent = bufferExtent", "if (!uiSystem.has_value())",
                       "Every clear-only return must inherit the complete frame extent first");
        requirePresent(framePreparation, "return makeUiDrawFramePayload(std::move(drawFrame))",
                       "Clear-only preparation paths must return an immutable payload");
        requirePresent(framePreparation,
                       "return makeUiDrawFramePayload(copyUiDrawData(drawData->get(), bufferExtent))",
                       "Populated draw data must be moved directly into the immutable payload");

        requireOrdered(structuralSnapshot, "detail::prepareUiDrawFrame", "static_cast<bool>(drawFrame)",
                       "Structural preflight must validate the newly prepared payload");
        requireOrdered(structuralSnapshot, "static_cast<bool>(drawFrame)",
                       "runtime_->preparedDrawFrame = std::move(drawFrame)",
                       "Only a validated immutable payload may enter the runtime cache");
        requireOrdered(structuralSnapshot, "runtime_->preparedDrawFrame = std::move(drawFrame)",
                       "detail::uiSkeletonBranchKey",
                       "Structural preflight must store the exact payload paired with its branch snapshot");

        requireOrdered(skeleton, "auto const expectedBranch", "snapshot.branchKey != expectedBranch",
                       "Skeleton materialization must derive the current branch before deciding payload ownership");
        requireOrdered(skeleton, "snapshot.branchKey != expectedBranch", "return false",
                       "A branch mismatch must exit without consuming the prepared payload");
        requireOrdered(skeleton, "return false", "std::exchange(runtime_->preparedDrawFrame",
                       "Only an exact Skeleton hit may exchange the prepared payload out of the runtime cache");
        requireOrdered(skeleton, "std::exchange(runtime_->preparedDrawFrame", "static_cast<bool>(drawFrame)",
                       "Skeleton payload exchange must be followed by an explicit invariant check");

        requireOrdered(coldBuild, "std::exchange(runtime_->preparedDrawFrame", "if (!drawFrame)",
                       "Cold materialization must consume a matching preflight payload before considering fallback");
        requireOrdered(coldBuild, "if (!drawFrame)", "drawFrame = prepareUiDrawFrame",
                       "Cold materialization may prepare draw data only when no preflight payload exists");
        requireOrdered(coldBuild, "drawFrame = prepareUiDrawFrame", "static_cast<bool>(drawFrame)",
                       "Every cold path must converge on an explicitly validated immutable payload");

        requirePresent(callbackTypes, "RasterPassPrepareCallback prepare{}",
                       "Ui callbacks must preserve the renderer prepare-stage contract");
        requirePresent(callbackTypes,
                       "PipelinePassBindingSnapshotCallback<nr::rhi::GraphicsPipeline> dynamicBindingSnapshot{}",
                       "Ui callbacks must preserve the renderer dynamic binding-snapshot contract");
        requirePresent(callbackTypes, "RasterPassRecordCallback record{}",
                       "Ui callbacks must preserve the renderer record-stage contract");
        requirePresent(callbackFactory,
                       "std::reference_wrapper<nr::renderer::BindlessImageTableCache> cache",
                       "Delayed Ui callbacks must store the required cache as an explicit non-owning handle");
        requirePresent(prepareCallback, "[runtime, drawFrame, cache]",
                       "Ui prepare must capture only runtime, immutable payload, and cache handle");
        requirePresent(snapshotCallback, "[runtime, cache]",
                       "Ui binding snapshot must capture only runtime and cache handle");
        requirePresent(recordCallback, "[runtime, drawFrame]",
                       "Ui recording must capture only runtime and immutable payload");
        requireAbsent(callbackFactory, "[&]", "Returned Ui callbacks must not borrow stack state implicitly");
        requireAbsent(callbackFactory, "ImDrawData",
                      "Returned Ui callbacks must not borrow finalized ImGui frame storage");
        requireAbsent(callbackFactory, "ImDrawList",
                      "Returned Ui callbacks must not borrow ImGui command-list storage");
        requireAbsent(callbackFactory, "NodeBuildContext",
                      "Returned Ui callbacks must not borrow a build context past materialization");

        requirePresent(prepareCallback, "prepareBindlessTextureTableForFrame",
                       "Descriptor-backed Ui resources must be updated in prepare");
        requirePresent(prepareCallback, "uploadUiDrawFrameBuffers(device, *runtime, frameSlot, *drawFrame)",
                       "Mapped Ui geometry uploads must remain in prepare");
        requirePresent(snapshotCallback, "makeBindlessTextureBindingSnapshotForFrame",
                       "The dynamic descriptor snapshot must remain a prepare-stage callback");
        requirePresent(recordCallback, "bindVertexBuffers(",
                       "Ui recording must bind the prepared vertex buffer");
        requirePresent(recordCallback, "bindIndexBuffer(",
                       "Ui recording must bind the prepared index buffer");
        requirePresent(recordCallback, "std::ranges::for_each(drawFrame->commands",
                       "Ui recording must traverse only immutable copied commands");
        requirePresent(callbackTypes, "context.pushConstants(\"gUiPush\", pushConstants)",
                       "Ui command recording must push reflected draw constants");
        requirePresent(callbackTypes, "setScissor(", "Ui command recording must set per-draw scissors");
        requirePresent(callbackTypes, "drawIndexed(", "Ui command recording must issue indexed draws");
        requireAbsent(recordCallback, "prepareBindlessTextureTableForFrame",
                      "Ui recording must not update descriptor-backed resources");
        requireAbsent(recordCallback, "makeBindlessTextureBindingSnapshotForFrame",
                      "Ui recording must not create binding snapshots");
        requireAbsent(recordCallback, "uploadUiDrawFrameBuffers",
                      "Ui recording must not upload mapped geometry");
        requireAbsent(callbackTypes, "writeMappedAndFlush",
                      "Ui callback orchestration must keep byte-checked writes in the shared upload helper");

        requirePresent(skeleton, "makeUiOverlayCallbacks(runtime, drawFrame, std::ref(bindlessCache))",
                       "Skeleton materialization must construct the shared callback bundle once");
        requirePresent(coldBuild,
                       "makeUiOverlayCallbacks(runtime, drawFrame, std::ref(bindlessImageTableCache))",
                       "Cold materialization must construct the same shared callback bundle once");
        requirePresent(skeleton, "RasterPassPatchBuilder",
                       "Skeleton materialization must retain its patch-specific builder");
        requirePresent(coldBuild, "RasterPassBuilder",
                       "Cold materialization must retain its graph-build-specific builder");
        auto const callbackMembers = std::array{
            std::string_view{".prepare(std::move(callbacks.prepare))"},
            std::string_view{".dynamicBindingSnapshot(std::move(callbacks.dynamicBindingSnapshot))"},
            std::string_view{".record(std::move(callbacks.record))"},
        };
        std::ranges::for_each(callbackMembers, [&](std::string_view member) {
            requirePresent(skeleton, member, "Skeleton materialization must install every shared callback");
            requirePresent(coldBuild, member, "Cold materialization must install every shared callback");
        });
        requireAbsent(skeleton, ".prepare([",
                      "Skeleton materialization must not retain a private prepare implementation");
        requireAbsent(coldBuild, ".prepare([",
                      "Cold materialization must not retain a private prepare implementation");
        requireAbsent(skeleton, ".record([",
                      "Skeleton materialization must not retain a private record implementation");
        requireAbsent(coldBuild, ".record([",
                      "Cold materialization must not retain a private record implementation");
    }};

const nr::test::CaseRegistrar uiSkeletonTopologyIdentityCase{
    "UI Skeleton identity tracks only the active texture resource count", [] {
        auto ui = readProjectFile("src/renderPasses/Ui/nrUiNode.cpp");
        auto rendererInterface = readProjectFile("src/renderer/nrRenderer.ixx");
        auto renderer = readProjectFile("src/renderer/nrRenderer.cpp");
        auto textureEntry = sourceSection(ui, "struct UiTextureEntry", "struct UiRetiredTexture");
        auto runtime = sourceSection(ui, "struct UiRuntimeCache",
                                     "[[nodiscard]] vk::Format validatedFrozenUiBufferFormat");
        auto acquireTextureSlot = sourceSection(ui, "[[nodiscard]] std::uint32_t acquireUiTextureSlot(",
                                                "struct UiTexturePayloadLayout");
        auto coldTextureResources = sourceSection(
            ui, "[[nodiscard]] std::vector<nr::renderer::GraphResourceHandle> "
                "registerUiTextureImageResources(",
            "[[nodiscard]] std::string uiSkeletonBranchKey(");
        auto branchKey = sourceSection(ui, "[[nodiscard]] std::string uiSkeletonBranchKey(",
                                       "[[nodiscard]] vk::DeviceSize checkedUiUploadByteSize(");
        auto structuralSnapshot = sourceSection(
            ui, "std::optional<nr::renderer::NodeRuntime::StructuralSnapshot> UiNode::structuralSnapshot(",
            "bool UiNode::materializeRenderGraphSkeleton(");
        auto skeleton = sourceSection(ui, "bool UiNode::materializeRenderGraphSkeleton(",
                                      "void UiNode::materializeCurrentFrame(");
        auto coldBuild = sourceSection(ui, "void UiNode::materializeCurrentFrame(", "void UiNode::shutdown(");
        auto snapshotDefaults = sourceSection(rendererInterface, "struct StructuralSnapshot",
                                              "/// Opt-in for the generic Skeleton contract");
        auto rendererKeyAssembly = sourceSection(renderer, "auto skeletonKey = RenderGraphSkeletonKey{};",
                                                  "auto timings = RendererGraphBuildTimings{}");

        requirePresent(branchKey, "std::ranges::count_if(runtime.texturesBySlot",
                       "Ui topology identity must count texture entries with a range algorithm");
        requirePresent(branchKey, "return entry.image.valid()",
                       "Ui topology identity must use the same validity predicate as resource materialization");
        requirePresent(branchKey, "return std::format(\"overlay;textures={}\", activeTextureCount)",
                       "Ui topology identity must encode only the overlay and active texture count");
        requireAbsent(branchKey, "vk::Format", "The frozen attachment format must not be duplicated in the Ui key");
        requireAbsent(branchKey, "textureTableRevision",
                      "Descriptor-content revision must not invalidate unchanged graph topology");
        requireAbsent(branchKey, "texturesBySlot.size()",
                      "Inactive capacity and holes must not invalidate unchanged graph topology");
        requireAbsent(branchKey, "textureKey", "External texture identity must not enter graph topology");
        requireAbsent(branchKey, ".extent", "Texture extent must remain patchable under the topology key");
        requireAbsent(branchKey, ".handle", "Vulkan handles must remain patchable under the topology key");
        requireAbsent(branchKey, ".state", "Retained resource state must remain patchable under the topology key");

        requirePresent(snapshotDefaults, "std::uint64_t configurationRevision = 1",
                       "Ui may rely on the renderer's stable default runtime revision");
        requirePresent(structuralSnapshot, ".branchKey = detail::uiSkeletonBranchKey(*runtime_)",
                       "Ui structural snapshots must publish their exact topology identity directly");
        requireAbsent(structuralSnapshot, ".configurationRevision =",
                      "Ui structural snapshots must not duplicate the branch in a manual revision");
        requireAbsent(structuralSnapshot, "std::hash",
                      "Ui structural snapshots must not hash a key already compared as a string");
        requireAbsent(structuralSnapshot, "std::max",
                      "Ui structural snapshots must use the type's default runtime revision");
        requirePresent(rendererKeyAssembly,
                       ".runtimeConfigurationRevision = structuralSnapshots[nodeIndex].configurationRevision",
                       "Renderer cache keys must retain the StructuralSnapshot default revision");
        requirePresent(rendererKeyAssembly,
                       ".structuralBranchKey = structuralSnapshots[nodeIndex].branchKey",
                       "Renderer cache keys must compare the exact Ui topology branch");
        requirePresent(rendererKeyAssembly, "skeletonKey.installedGraphGeneration = installedGraphGeneration_",
                       "Installed graph generation must remain a renderer-owned key component");
        requirePresent(rendererKeyAssembly, "skeletonKey.swapchainFormat = frameParameters.swapchainFormat",
                       "Swapchain format must remain a renderer-owned key component");

        requireAbsent(textureEntry, "textureKey",
                      "Ui texture entries must not duplicate the external key-to-slot map");
        requireAbsent(ui, ".textureKey", "No dead UiTextureEntry texture-key assignment may remain");
        requirePresent(runtime, "std::map<std::uint64_t, std::uint32_t> textureSlotByKey{}",
                       "Ui runtime must retain the true external texture key-to-slot identity map");
        requirePresent(acquireTextureSlot, "runtime.textureSlotByKey.insert_or_assign(textureKey, slot)",
                       "Slot acquisition must retain external texture identity in the map");
        requirePresent(ui, ".tableVersion = runtime.textureTableRevision",
                       "Descriptor table revision behavior is intentionally unchanged in this phase");
        requirePresent(ui, ".refreshActiveDescriptorsOnCacheHit = true",
                       "Descriptor cache-hit refresh behavior is intentionally unchanged in this phase");
        requirePresent(ui, "markBindlessTextureTableDirty(runtime)",
                       "Descriptor dirty marking is intentionally unchanged in this phase");

        requirePresent(skeleton, "context.patchResource(0u",
                       "Ui Skeleton patching must retain one fixed overlay resource");
        requirePresent(skeleton, "auto resourceSlot = std::size_t{1u}",
                       "Active texture resources must start immediately after the overlay resource");
        requireOrdered(skeleton, "if (!entry.image.valid())", "context.patchResource(resourceSlot++",
                       "Ui Skeleton patching must fill resource slots only for valid textures");
        requirePresent(coldTextureResources, "if (!textureEntry.image.valid())",
                       "Cold graph construction must import only valid texture images");
        requirePresent(coldTextureResources, "graphResources.push_back(resource)",
                       "Cold graph construction must retain only the ordered valid resource handles");
        requirePresent(coldBuild, "std::ranges::for_each(textureResources",
                       "Cold graph resource uses must remain one-for-one with valid imported textures");
        requirePresent(coldBuild, "overlayPass.resourceUse(",
                       "Every valid cold texture import must remain shader-visible to the overlay pass");

        requireExactlyOne(skeleton, "RasterPassPatchBuilder{",
                          "Ui Skeleton topology must contain exactly one overlay pass");
        requireExactlyOne(coldBuild, "RasterPassBuilder{",
                          "Ui cold topology must contain exactly one overlay pass");

        auto topologyKey = [](const auto &activeSlots) {
            auto const activeTextureCount = std::ranges::count(activeSlots, true);
            return nr::renderer::RenderGraphSkeletonKey{
                .nodes = {
                    nr::renderer::RenderGraphSkeletonNodeKey{
                        .configurationRevision = 1u,
                        .runtimeConfigurationRevision = 1u,
                        .structuralBranchKey = std::format("overlay;textures={}", activeTextureCount),
                    },
                },
            };
        };
        auto const firstSlots = std::array{true, false, true, false};
        auto const relocatedSlots = std::array{false, true, false, true};
        auto const expandedSlots = std::array{true, true, true, false};
        nr::test::require(topologyKey(firstSlots) == topologyKey(relocatedSlots),
                          "Different slot identities with the same active count must hit the same topology key");
        nr::test::require(topologyKey(firstSlots) != topologyKey(expandedSlots),
                          "Changing the active texture count must miss the cached topology key");
    }};

const nr::test::CaseRegistrar uiDescriptorRevisionAndOrderedResourcesCase{
    "UI descriptor revision changes once per visible handle transition", [] {
        auto ui = readProjectFile("src/renderPasses/Ui/nrUiNode.cpp");
        auto runtime = sourceSection(ui, "struct UiRuntimeCache",
                                     "[[nodiscard]] vk::Format validatedFrozenUiBufferFormat");
        auto dirtyMark = sourceSection(ui, "void markBindlessTextureTableDirty(",
                                       "[[nodiscard]] void *uiTextureBackendMarker(");
        auto acquireTextureSlot = sourceSection(ui, "[[nodiscard]] std::uint32_t acquireUiTextureSlot(",
                                                "struct UiTexturePayloadLayout");
        auto createTexture = sourceSection(ui, "void createOrUpdateUiTexture(", "void destroyUiTexture(");
        auto destroyTexture = sourceSection(ui, "void destroyUiTexture(",
                                            "[[nodiscard]] bool runtimeHasValidTextureFor(");
        auto registerResources = sourceSection(
            ui, "[[nodiscard]] std::vector<nr::renderer::GraphResourceHandle> "
                "registerUiTextureImageResources(",
            "[[nodiscard]] std::string uiSkeletonBranchKey(");
        auto descriptorMap = sourceSection(
            ui, "[[nodiscard]] std::map<std::uint32_t, nr::renderer::BindlessImageDescriptor> "
                "makeUiTextureDescriptors(",
            "[[nodiscard]] nr::renderer::BindlessImageTableRequest makeBindlessTextureTableRequest(");
        auto tableRequest = sourceSection(
            ui, "[[nodiscard]] nr::renderer::BindlessImageTableRequest makeBindlessTextureTableRequest(",
            "void prepareBindlessTextureTableForFrame(");
        auto coldBuild = sourceSection(ui, "void UiNode::materializeCurrentFrame(", "void UiNode::shutdown(");

        requirePresent(runtime, "std::uint64_t textureTableRevision = 1u",
                       "Ui runtime must retain a monotonic descriptor-content revision");
        requireOrdered(dirtyMark, "++runtime.textureTableRevision", "runtime.textureTableRevision == 0u",
                       "Descriptor revision wrap detection must follow the increment");
        requirePresent(dirtyMark, "runtime.textureTableRevision = 1u",
                       "Descriptor revision wrap must skip the reserved zero value");
        requirePresent(tableRequest, ".tableVersion = runtime.textureTableRevision",
                       "The renderer bindless cache must consume the Ui descriptor revision");
        requirePresent(tableRequest, ".refreshActiveDescriptorsOnCacheHit = true",
                       "Ui must retain active descriptor refresh on renderer cache hits");

        requirePresent(acquireTextureSlot, "runtime.textureSlotByKey.insert_or_assign(textureKey, slot)",
                       "Slot acquisition must publish the external identity mapping");
        requireAbsent(acquireTextureSlot, "markBindlessTextureTableDirty",
                      "Slot allocation alone must not advance descriptor-content revision");
        requirePresent(acquireTextureSlot, "runtime.freeTextureSlots.back()",
                       "Reused free slots must follow the same descriptor-neutral acquisition path");
        requirePresent(acquireTextureSlot, "runtime.texturesBySlot.emplace_back()",
                       "New slots must follow the same descriptor-neutral acquisition path");

        requirePresent(createTexture,
                       "auto needsReplacement = !textureEntry.image.valid() || "
                       "textureData.Status == ImTextureStatus_WantCreate ||",
                       "New, reused, and explicitly replaced textures must converge on handle replacement");
        requirePresent(createTexture, "textureData.Status == ImTextureStatus_WantUpdates",
                       "WantUpdates must continue to replace the descriptor-visible image handle");
        requireOrdered(createTexture, "textureEntry = UiTextureEntry{", "markBindlessTextureTableDirty(runtime)",
                       "A newly installed image handle must advance descriptor revision after publication");
        requireOrdered(createTexture, "markBindlessTextureTableDirty(runtime)", "uploadUiTextureThroughRing",
                       "Each replacement must advance revision exactly once before the common content upload");
        requireExactlyOne(createTexture, "markBindlessTextureTableDirty(runtime)",
                          "Create/update must mark exactly once, and only on handle replacement");
        requireAbsent(createTexture, "wasOwnedByRuntime",
                      "Reattaching BackendUserData must not carry descriptor revision state");
        requireAbsent(createTexture, "hadRuntimeSlot",
                      "An existing identity mapping must not independently advance descriptor revision");
        requireOrdered(createTexture, "uploadUiTextureThroughRing", "textureData.BackendUserData =",
                       "Same-handle uploads and marker reattachment must converge without a trailing mark");

        requireOrdered(destroyTexture, "runtime.textureSlotByKey.erase(slotIt)",
                       "markBindlessTextureTableDirty(runtime)",
                       "Removing a published mapping must advance revision even for an anomalously invalid entry");
        requireExactlyOne(destroyTexture, "markBindlessTextureTableDirty(runtime)",
                          "Destroy must advance descriptor revision exactly once per removed mapping");

        requirePresent(registerResources, "std::vector<nr::renderer::GraphResourceHandle>",
                       "Cold graph registration must return only its ordered resource handles");
        requirePresent(registerResources, "graphResources.push_back(resource)",
                       "Valid texture handles must retain ascending-slot registration order");
        requireAbsent(registerResources, "std::map",
                      "Cold graph registration must not retain an unconsumed slot identity map");
        requireAbsent(registerResources, "insert_or_assign",
                      "Cold graph registration must not manufacture unused map keys");
        requirePresent(coldBuild,
                       "std::ranges::for_each(textureResources, "
                       "[&](nr::renderer::GraphResourceHandle resource)",
                       "Cold pass resource uses must consume handles directly");
        requireAbsent(coldBuild, "pair.second",
                      "Cold pass resource use must not unwrap a discarded map entry");

        requirePresent(descriptorMap,
                       "std::map<std::uint32_t, nr::renderer::BindlessImageDescriptor>",
                       "The true shader descriptor map must remain keyed by texture slot");
        requirePresent(descriptorMap,
                       "descriptorsById.insert_or_assign(static_cast<std::uint32_t>(slot)",
                       "Bindless descriptors must retain exact shader-visible slot addressing");
        requirePresent(runtime, "std::map<std::uint64_t, std::uint32_t> textureSlotByKey{}",
                       "External texture identity must retain its true key-to-slot map");

        auto advanceRevision = [](std::uint64_t revision) {
            ++revision;
            return revision == 0u ? std::uint64_t{1u} : revision;
        };
        auto revisionAfterUpdate = [&](bool imageValid, bool replacementRequested, bool extentChanged) {
            auto constexpr initialRevision = std::uint64_t{41u};
            return !imageValid || replacementRequested || extentChanged ? advanceRevision(initialRevision)
                                                                         : initialRevision;
        };
        struct UpdateTransition
        {
            std::string_view label{};
            bool imageValid = false;
            bool replacementRequested = false;
            bool extentChanged = false;
            std::uint64_t expectedRevision = 41u;
        };
        auto const transitions = std::array{
            UpdateTransition{"new slot", false, false, false, 42u},
            UpdateTransition{"reused free slot", false, false, false, 42u},
            UpdateTransition{"WantCreate", true, true, false, 42u},
            UpdateTransition{"WantUpdates", true, true, false, 42u},
            UpdateTransition{"extent replacement", true, false, true, 42u},
            UpdateTransition{"lost backend marker", true, false, false, 41u},
            UpdateTransition{"same-handle content upload", true, false, false, 41u},
        };
        std::ranges::for_each(transitions, [&](const UpdateTransition &transition) {
            nr::test::requireEqual(
                revisionAfterUpdate(transition.imageValid, transition.replacementRequested, transition.extentChanged),
                transition.expectedRevision,
                std::format("{} must have exactly the modeled descriptor revision effect", transition.label));
        });
        nr::test::requireEqual(advanceRevision(std::numeric_limits<std::uint64_t>::max()), std::uint64_t{1u},
                               "Descriptor revision wrap must preserve the non-zero table version invariant");
        auto revisionAfterDestroy = [&](bool mappingExists) {
            auto constexpr initialRevision = std::uint64_t{41u};
            return mappingExists ? advanceRevision(initialRevision) : initialRevision;
        };
        nr::test::requireEqual(revisionAfterDestroy(true), std::uint64_t{42u},
                               "Removing an existing mapping must mark exactly once");
        nr::test::requireEqual(revisionAfterDestroy(false), std::uint64_t{41u},
                               "A destroy request without an identity mapping must not mark");
    }};

const nr::test::CaseRegistrar materialFilterPacketAdvanceCase{
    "path tracing material-filter reservation advances exactly three rand4 packets", [] {
        auto const seeds = std::array{
            0u, 1u, 0x12345678u, 0x80000000u, 0xffffffffu,
        };
        std::ranges::for_each(seeds, [](std::uint32_t seed) {
            auto expanded = seed;
            std::ranges::for_each(std::views::iota(0u, 15u),
                                  [&](auto) { expanded = expanded * 0x915f77f5u + 0x93d765ddu; });

            auto const collapsed = seed * 0x98a5741du + 0xacfbeaa7u;
            nr::test::requireEqual(expanded, collapsed,
                                   "fifteen LCG steps must equal the affine skip for three rand4 packets");
        });
    }};

static_assert(
    std::same_as<nr::renderer::RendererTlasTextureRevisionProjection, nr::scene::SceneRtStructuralRevisionProjection>);

const nr::test::CaseRegistrar normalBufferTransformAndFormatContractCase{
    "normal buffer preserves transformed tangent frames, raster parity, and pipeline attachment identity", [] {
        auto normalBuffer = readProjectFile("src/renderPasses/NormalBuffer/nrNormalBufferNode.cpp");
        auto normalBufferVertex = readProjectFile("shader/renderer/normalBuffer/vertex.slang");
        auto sceneExtraction = readProjectFile("src/scene/nrSceneExtraction.cpp");
        auto runtimeCache = sourceSection(normalBuffer, "struct NormalBufferRuntimeCache",
                                          "inline constexpr float kModelLinearSingularityTolerance");
        auto runtimeCreation =
            sourceSection(normalBuffer, "ensureNormalBufferRuntime(", "void ensureNormalBufferImages(");
        auto imageCreation =
            sourceSection(normalBuffer, "void ensureNormalBufferImages(", "} // namespace nr::renderPasses::detail");
        auto initialization =
            sourceSection(normalBuffer, "void NormalBufferNode::initialize(", "void NormalBufferNode::finalizeInitialization(");
        auto build = sourceSection(normalBuffer, "void NormalBufferNode::build(", "void NormalBufferNode::shutdown(");

        requirePresent(normalBufferVertex, "float3x3 modelLinear =",
                       "NormalBuffer vertex shading should reconstruct the model linear transform from packed rows");
        requirePresent(normalBufferVertex, "inverseTranspose(modelLinear, modelLinearDeterminant)",
                       "NormalBuffer normals should use the inverse-transpose model linear transform");
        requirePresent(normalBufferVertex, "cross(value[1], value[2])",
                       "NormalBuffer should provide its file-local 3x3 inverse-transpose through cofactor rows");
        requirePresent(normalBufferVertex, "1.0f / determinant",
                       "NormalBuffer's file-local inverse-transpose should reuse the validated model determinant");
        requirePresent(normalBufferVertex, "mul(modelLinear, input.tangent.xyz)",
                       "NormalBuffer tangents should use the model linear transform");
        requirePresent(normalBufferVertex, "modelLinearDeterminant < 0.0f ? -1.0f : 1.0f",
                       "NormalBuffer should derive tangent-frame handedness from model parity");
        requirePresent(normalBufferVertex, "input.tangent.w * modelHandedness",
                       "NormalBuffer should flip authored tangent handedness for mirrored models");
        requirePresent(normalBuffer, "static_assert(sizeof(NormalBufferPushConstants) == 96u",
                       "NormalBuffer transform correction must preserve its reflected push ABI");

        requirePresent(normalBuffer, "validatedModelLinearDeterminant(draw.world)",
                       "NormalBuffer should validate each complete instance/model transform at record time");
        requirePresent(normalBuffer, "std::isfinite(determinant)",
                       "NormalBuffer should reject non-finite model parity");
        requirePresent(normalBuffer,
                       "std::abs(determinant) > kModelLinearSingularityTolerance * determinantScale",
                       "NormalBuffer should reject scale-relative singular model transforms before shader inversion");
        requirePresent(normalBuffer, "frontFaceForModelParity(draw.geometry.frontFace, modelLinearDeterminant)",
                       "NormalBuffer should combine object-space mesh winding with full model parity");
        requirePresent(build, "commandBuffer.setFrontFace(drawFrontFace)",
                       "NormalBuffer should apply mirrored winding through per-draw dynamic raster state");
        requireAbsent(build, "commandBuffer.setFrontFace(draw.geometry.frontFace)",
                      "NormalBuffer must not ignore instance/model parity when selecting dynamic front face");
        requirePresent(
            sceneExtraction,
            ".frontFace = meshRecord->cpu.clockwiseFrontFace ? vk::FrontFace::eClockwise : vk::FrontFace::eCounterClockwise",
            "Scene extraction should keep mesh winding as an object-space contract");

        requirePresent(runtimeCache, "vk::Format colorFormat = vk::Format::eUndefined",
                       "NormalBuffer runtime should retain the color format used to create its PSO");
        requirePresent(runtimeCache, "vk::Format depthFormat = vk::Format::eUndefined",
                       "NormalBuffer runtime should retain the depth format used to create its PSO");
        requirePresent(runtimeCreation, "pipelineDesc.colorAttachmentFormats = {runtime->colorFormat}",
                       "NormalBuffer PSO creation should consume the frozen runtime color format");
        requirePresent(runtimeCreation, "pipelineDesc.depthAttachmentFormat = runtime->depthFormat",
                       "NormalBuffer PSO creation should consume the frozen runtime depth format");
        requirePresent(imageCreation, "makeImageCreateInfo(runtime.colorFormat",
                       "NormalBuffer color images should use the PSO's frozen format");
        requirePresent(imageCreation, "makeImageCreateInfo(runtime.depthFormat",
                       "NormalBuffer depth images should use the PSO's frozen format");
        requirePresent(initialization, "presentationContext.swapchainFormat()",
                       "NormalBuffer should resolve an undefined color format exactly at initialization");
        requirePresent(initialization, "ensureNormalBufferImages(context.device.get(), *runtime_, initialExtent)",
                       "NormalBuffer initialization should allocate from frozen runtime formats");
        requirePresent(build, "requestedColorFormat == runtime_->colorFormat",
                       "NormalBuffer should fail fast when its effective color format changes after initialization");
        requirePresent(build, "input.depthFormat == runtime_->depthFormat",
                       "NormalBuffer should fail fast when its depth format changes after initialization");
        requirePresent(build, "ensureNormalBufferImages(device_->get(), *runtime_, viewportExtent)",
                       "NormalBuffer build should resize attachments without accepting mutable formats");
        requirePresent(build, "viewportExtent, runtime_->colorFormat",
                       "NormalBuffer graph import should use the frozen color format");
        requirePresent(build, "viewportExtent, runtime_->depthFormat",
                       "NormalBuffer graph import should use the frozen depth format");
        requireAbsent(build, "viewportExtent, requestedColorFormat",
                      "NormalBuffer build must not drive attachment creation or import from mutable color input");
        requireAbsent(build, "viewportExtent, input.depthFormat",
                      "NormalBuffer build must not drive attachment creation or import from mutable depth input");
    }};

const nr::test::CaseRegistrar renderPassesRendererCacheOwnershipCase{
    "renderpasses no longer own renderer/RDG descriptor table cache state", [] {
        auto normalBuffer = readProjectFile("src/renderPasses/NormalBuffer/nrNormalBufferNode.cpp");
        auto embeddedTriangle = readProjectFile("src/renderPasses/EmbeddedTriangle/nrEmbeddedTriangleNode.cpp");
        auto pathTracing = readProjectFile("src/renderPasses/PathTracing/nrPathTracingNode.cpp");
        auto normalBufferShader = readProjectFile("shader/renderer/normalBuffer/fragment.slang");
        auto pathTracingInterface = readProjectFile("src/renderPasses/PathTracing/nrPathTracingNode.ixx");
        auto accumulate = readProjectFile("src/renderPasses/Accumulate/nrAccumulateNode.cpp");
        auto accumulateInterface = readProjectFile("src/renderPasses/Accumulate/nrAccumulateNode.ixx");
        auto accumulateTemporalIdentity = sourceSection(accumulate, "struct AccumulateTemporalIdentity", "};");
        auto ui = readProjectFile("src/renderPasses/Ui/nrUiNode.cpp");
        auto sceneTextureBinding = readProjectFile("src/renderPasses/nrSceneTextureTableBinding.ixx");
        auto rendererInterface = readProjectFile("src/renderer/nrRenderer.ixx");
        auto rendererCacheInterface = readProjectFile("src/renderer/nrRendererCache.ixx");
        auto rendererImplementation =
            readProjectFile("src/renderer/nrRenderer.cpp") + readProjectFile("src/renderer/nrRendererPassBuilders.cpp");
        auto globalUniform = readProjectFile("shader/include/globalUniform.slang");
        auto pathTracingGuides = readProjectFile("shader/renderer/pathTracing/guides.slang");
        auto pipelineImplementation = readProjectFile("src/pipeline/nrPipeline.cpp");
        auto accelerationStructureBuild =
            readProjectFile("src/renderPasses/AccelerationStructureBuild/nrAccelerationStructureBuildNode.cpp");
        auto rtHitSbtPlan = readProjectFile("src/renderPasses/nrRtHitSbtPlan.ixx");

        requireAbsent(normalBuffer, "SceneTextureTableBindingCache",
                      "NormalBuffer must use renderer-owned bindless table cache instead of a node-local scene cache");
        requireAbsent(pathTracing, "SceneTextureTableBindingCache",
                      "PathTracing must use renderer-owned bindless table cache instead of a node-local scene cache");
        requireAbsent(ui, "appliedTextureTableRevisionByFrame",
                      "Ui must not keep per-frame applied texture table revisions");
        requireAbsent(ui, "ensureBindlessTextureBindingSetsForFrame",
                      "Ui texture table binding-set allocation should be owned by renderer bindless cache");
        requireAbsent(ui, "if (tablePrepare.requiresDescriptorCacheInvalidation)",
                      "Ui must not clear descriptor write cache based on bindless table prepare state");
        requirePresent(ui, ".refreshActiveDescriptorsOnCacheHit = true",
                       "Ui GPU-AV descriptor refresh should request active descriptor writes on bindless cache hits");
        requireAbsent(ui, ".forceDescriptorWritesOnCacheHit = true",
                      "Ui GPU-AV descriptor refresh should use one cache-hit refresh option");
        requireAbsent(sceneTextureBinding, "resetSceneTextureTableFrameCache",
                      "scene texture table helper should not own frame-slot cache reset state");
        requirePresent(
            normalBuffer, "sceneTextureTableImmutableSamplerBinding()",
            "NormalBuffer should install the scene texture table immutable sampler before graphics PSO creation");
        requirePresent(pathTracing, "sceneTextureTableImmutableSamplerBinding()",
                       "PathTracing should install the scene texture table immutable sampler before RT PSO creation");
        requireAbsent(pathTracingInterface, "PathTracingVariantKey variant{}",
                      "PathTracing input must not retain a second writable option value");
        requirePresent(pathTracingInterface, "enableRussianRoulette",
                       "PathTracing variant key should expose the Russian roulette toggle");
        requirePresent(pathTracingInterface, "enableFilterAfterShading",
                       "PathTracing variant key should expose the compile-time FAS toggle");
        requirePresent(pathTracing,
                       "std::map<PathTracingPipelineKey, "
                       "std::shared_ptr<nr::renderer::PipelineRuntime<nr::rhi::RayTracingPipeline>>>",
                       "PathTracing should cache RT pipelines by root variant and CHS permutation set");
        requirePresent(pathTracing, "std::map<PathTracingSbtKey, nr::rhi::ShaderBindingTable>",
                       "PathTracing should cache SBTs separately from pipeline runtimes");
        requirePresent(pathTracing, "std::uint64_t chsPermutationSetHash",
                       "PathTracing pipeline keys should include the CHS permutation set");
        requirePresent(pathTracing, "std::uint64_t hitRecordPlanHash",
                       "PathTracing SBT keys should include the hit record plan");
        requirePresent(pathTracing, "std::uint64_t shaderSessionGeneration",
                       "PathTracing pipeline keys should invalidate on shader service session reload");
        requirePresent(pathTracing, "ShaderService::instance().sessionGeneration()",
                       "PathTracing should capture the current shader session generation in the pipeline key");
        requirePresent(pathTracing, "ensurePathTracingFrameRuntime",
                       "PathTracing should compose the current frame pipeline and SBT from separate node-owned caches");
        requireAbsent(
            pathTracing, "PathTracingRuntimeKey",
            "PathTracing should not merge root variants, CHS permutations, and SBT records into one runtime key");
        requireAbsent(pathTracing, "PathTracingVariantRuntime",
                      "PathTracing should keep pipeline and SBT runtime state in separate caches");
        requireAbsent(pathTracingInterface, "variantUiDraft_", "PathTracing must not retain a node-local UI draft");
        requireAbsent(pathTracingInterface, "pendingVariant_",
                      "PathTracing must not retain a node-local pending variant");
        requirePresent(pathTracingInterface, "void declareOptions",
                       "PathTracing must declare its controls through the option catalog");
        requirePresent(pathTracing, "pathTracingVariant(frameParameters.optionSnapshot.get())",
                       "PathTracing must derive its active variant from the immutable frame option snapshot");
        requireAbsent(pathTracing, "context.variants", "PathTracing should not access renderer-owned variant state");
        requirePresent(pathTracing, "createPathTracingPipelineRuntime(device, pipelineKey, hitSbtPlan)",
                       "PathTracing pipeline cache misses should enqueue the PSO before constructing the SBT");
        requirePresent(pathTracing, "createPathTracingShaderBindingTable(device, pipelineRuntime, sbtKey, hitSbtPlan)",
                       "PathTracing SBT cache misses should rebuild only the SBT for the active record plan");
        requireAbsent(pathTracingInterface, "collectUi", "PathTracing must not expose a node-local mutation UI");
        requireAbsent(rendererInterface, "VariantStateRegistry",
                      "Renderer interfaces should not expose a shared variant registry");
        requireAbsent(rendererCacheInterface, "variantRegistry",
                      "Renderer cache suite should not own node variant state");
        requireAbsent(rendererImplementation, "commitFramePatches",
                      "Renderer should not commit shader variant patches");
        requireAbsent(rendererImplementation, "collectVariantUiSections",
                      "Renderer should not append registry-generated variant UI sections");
        requirePresent(rendererInterface, "std::span<const nr::rhi::SlangProgram> shaderPrograms{}",
                       "Node initialization should receive only its ordered slice of precompiled shaders");
        requirePresent(rendererInterface, "shaderRequests() const",
                       "Node runtimes should declare static shader requirements before initialization");
        requirePresent(rendererImplementation, "createInfo.runtime->shaderRequests()",
                       "Renderer graph installation should collect every node's static shader requirements");
        requirePresent(rendererImplementation, "compileProgramsByFile(shaderRequests)",
                       "Renderer graph installation should submit one flattened static shader batch");
        requirePresent(rendererImplementation, "device_->pipeline().waitForBuilds()",
                       "Renderer graph installation should join the concurrently submitted PSO batch");
        requirePresent(rendererImplementation, "node.runtime->finalizeInitialization()",
                       "Renderer graph installation should resolve PSO futures before publishing the graph");
        requirePresent(rendererInterface, "nr::rhi::PipelineBuild<TPipeline>",
                       "Pipeline runtimes should retain asynchronous RHI PSO builds");
        requirePresent(rendererImplementation,
                       ".shaderPrograms = std::span<const nr::rhi::SlangProgram>{shaderPrograms}.subspan(",
                       "Renderer should return each node's ordered shader-program slice only after batch completion");
        requireAbsent(normalBuffer, "ShaderService::instance()",
                      "NormalBuffer initialization must consume the renderer-provided batch result");
        requireAbsent(embeddedTriangle, "ShaderService::instance()",
                      "EmbeddedTriangle initialization must consume the renderer-provided batch result");
        requireAbsent(accumulate, "ShaderService::instance()",
                      "Accumulate initialization must consume the renderer-provided batch result");
        requireAbsent(ui, "ShaderService::instance()",
                      "Ui initialization must consume the renderer-provided batch result");
        requirePresent(rendererImplementation, "createInfo.config.instanceName.empty()",
                       "Renderer graph preflight should require NodeConfig as the node-name source");
        requireAbsent(pathTracing, "RendererCacheSuite",
                      "PathTracing variant PSOs must not be stored in RendererCacheSuite");
        requirePresent(rendererImplementation, "makeTlasTextureCollectionKey",
                       "Renderer should cache TLAS-only scene texture collection by an exact structural key");
        requirePresent(rendererInterface, "std::optional<RendererTlasTextureCollectionKey> tlasTextureCollectionKey_",
                       "Renderer should own the TLAS texture collection cache");
        requirePresent(rendererInterface,
                       "using RendererTlasTextureRevisionProjection = nr::scene::SceneRtStructuralRevisionProjection;",
                       "Renderer should reuse the canonical scene RT structural revision projection");
        requirePresent(accelerationStructureBuild, "RtStructuralPlanCache structuralPlan",
                       "AS runtime should own the immutable structural metadata/SBT plan cache");
        requirePresent(accelerationStructureBuild,
                       "using AsStructuralRevisionProjection = nr::scene::SceneRtStructuralRevisionProjection;",
                       "AS should reuse the canonical scene RT structural revision projection");
        requirePresent(accelerationStructureBuild, "appliedStructuralPlanGeneration",
                       "AS frame slots should track the static plan generation already uploaded");
        requirePresent(accelerationStructureBuild, "runtime.activeSceneIdentity != revisions.sceneIdentity",
                       "AS cache reuse should have an explicit scene identity boundary");
        requirePresent(accelerationStructureBuild, "std::vector<AsStructuralMeshSemanticEntry> meshSemantics",
                       "AS structural keys should store unique mesh semantics separately from ordered packet identity");
        requirePresent(accelerationStructureBuild, ".semanticKey = entry.second.cachedBuild.get().semanticKey",
                       "AS structural keys should reuse semantic keys from the current BLAS scan");
        requirePresent(accelerationStructureBuild, "std::shared_ptr<const SceneRtHitSbtPlan> hitSbtPlan",
                       "AS structural plans should share immutable SBT plans across graph frames");
        requirePresent(pathTracing, "resolveBuildFrameData<std::shared_ptr<const SceneRtHitSbtPlan>>",
                       "PathTracing should resolve shared immutable SBT plan ownership");
        requirePresent(accelerationStructureBuild, "RtMaterialRevisionProjection revisions",
                       "RT material cache keys should include authoritative material and texture revisions");
        requirePresent(accelerationStructureBuild, "std::optional<BlasRevisionProjection> blasRevisions",
                       "AS runtime should retain the mesh revision projection used by cached BLAS descriptors");
        requirePresent(accelerationStructureBuild,
                       "runtime.blasRevisions.has_value() && *runtime.blasRevisions != blasRevisions",
                       "mesh revision changes alone should invalidate the BLAS subcache");
        requirePresent(accelerationStructureBuild, "entry.second.cachedBuild = {};",
                       "mesh content or layout revision changes should invalidate cached BLAS descriptors");
        requirePresent(
            accelerationStructureBuild, "replacementBlasAtlasCapacity",
            "revision-only BLAS atlas replacement should preserve capacity without applying the growth policy");
        requirePresent(accelerationStructureBuild, "createBlasAtlas(runtime, device, requiredBytes, capacityOverflow)",
                       "only actual BLAS atlas overflow should select capacity growth");
        requirePresent(accelerationStructureBuild, "recordBuildTlas",
                       "AS optimization should preserve unconditional per-frame TLAS rebuild recording");
        requirePresent(accelerationStructureBuild,
                       "rtPhysicalHitRecordIndex(logicalHitRecordBase, RtRayType::material)",
                       "TLAS emission should convert the logical instance base to the physical material slot");
        requirePresent(accelerationStructureBuild, "physicalHitRecordBase <= 0x00FF'FFFFu",
                       "TLAS emission should enforce Vulkan's 24-bit physical record offset boundary");
        requirePresent(rtHitSbtPlan, "SceneRtHitSbtPlan.recordPlan.v4",
                       "the hit-record plan hash schema should identify the two-ray-type layout");
        requirePresent(rtHitSbtPlan, "static_cast<std::uint32_t>(RtRayType::material)",
                       "the hit-record plan hash should include the material ray-type ABI value");
        requirePresent(rtHitSbtPlan, "static_cast<std::uint32_t>(RtRayType::shadow)",
                       "the hit-record plan hash should include the shadow ray-type ABI value");
        requirePresent(rtHitSbtPlan, "nr::hash::hashAppend(recordState, kRtRayTypeCount);",
                       "the hit-record plan hash should include the ray-type count");
        requirePresent(sceneTextureBinding, ".usesImmutableSampler = true",
                       "scene texture table descriptor writes should rely on the immutable sampler in the PSO layout");
        requirePresent(sceneTextureBinding, "sceneTextureTableNearestSamplerDesc",
                       "scene texture table should expose its nearest immutable sampler");
        requirePresent(sceneTextureBinding, ".magFilter = vk::Filter::eNearest",
                       "scene texture table immutable sampler should use nearest magnification");
        requirePresent(sceneTextureBinding, ".minFilter = vk::Filter::eNearest",
                       "scene texture table immutable sampler should use nearest minification");
        requirePresent(sceneTextureBinding, ".mipmapMode = vk::SamplerMipmapMode::eNearest",
                       "scene texture table immutable sampler should disable mip interpolation");
        requirePresent(sceneTextureBinding, ".minLod = 0.0f",
                       "scene texture table immutable sampler should clamp its minimum LOD to zero");
        requirePresent(sceneTextureBinding, ".maxLod = 0.0f",
                       "scene texture table immutable sampler should clamp its maximum LOD to zero");
        requirePresent(normalBufferShader, ".Sample(normalUv)",
                       "NormalBuffer should retain its raster implicit-LOD texture-coordinate policy over the nearest "
                       "scene sampler");
        requirePresent(pathTracing, ".magFilter = vk::Filter::eLinear",
                       "PathTracing environment sampling should retain linear magnification");
        requirePresent(pathTracing, ".minFilter = vk::Filter::eLinear",
                       "PathTracing environment sampling should retain linear minification");
        requirePresent(
            pathTracing, ".mipmapMode = vk::SamplerMipmapMode::eLinear",
            "PathTracing environment sampling should remain independent from the nearest scene texture table");
        requirePresent(rendererImplementation, "void collectTlasMaterialTextureHandles(",
                       "the remaining renderer material texture walk should be explicitly TLAS-only");
        requireAbsent(rendererImplementation, "MissingMaterialTexturePolicy",
                      "a TLAS-only helper should not retain a general raster-versus-RT texture policy switch");
        requireAbsent(rendererImplementation, "collectSceneMaterialTextures",
                      "renderer should not walk raster materials after Scene packet extraction");
        requirePresent(rendererImplementation, "sceneBridgeFrame->rasterTextureHandlesById",
                       "renderer should consume the Scene-authored raster texture handle table directly");
        requirePresent(rendererImplementation, "slotIndex == anisotropySlotIndex",
                       "the unavailable-texture exception must be limited to the anisotropy slot");
        requireAbsent(rendererInterface, "sceneTextureSampler",
                      "Renderer global resources should not expose a per-frame scene texture sampler");
        requireAbsent(rendererImplementation, "SceneTextureSampler",
                      "Renderer should not create a separate scene texture sampler for gSceneTextures");
        requirePresent(rendererImplementation, "vk::PipelineStageFlagBits2::eAllGraphics,",
                       "RasterPassBuilder should stamp raster passes with a graphics shader scope");
        requirePresent(rendererImplementation, "vk::PipelineStageFlagBits2::eComputeShader,",
                       "ComputePassBuilder should stamp compute passes with compute shader scope");
        requirePresent(rendererImplementation, "vk::PipelineStageFlagBits2::eRayTracingShaderKHR,",
                       "RayTracingPassBuilder should stamp RT passes with ray tracing shader scope");
        requirePresent(rendererInterface, "withOptionalShaderStages",
                       "Shader-visible pass builders should support per-resource shader stage overrides");
        requireAbsent(rendererInterface, "descriptorCacheOwnerId()",
                      "PipelineRuntime should not expose a cache owner id for bindless tables");
        requireAbsent(rendererInterface, "bindingSetGenerationForFrame",
                      "PipelineRuntime should not expose per-frame binding-set generations for bindless table cache");
        requireAbsent(rendererCacheInterface, "bindingSetGenerations",
                      "BindlessImageTableCache should not track binding-set generations for this UI GPU-AV workaround");
        requirePresent(rendererInterface, "class PassBindingHandle",
                       "PipelineRuntime should expose a strongly typed pass-binding handle");
        requirePresent(rendererInterface, "passBinding(std::string_view runtimeName",
                       "PipelineRuntime pass owners should use stable runtime-name and node-local ordinal identity");
        requirePresent(rendererCacheInterface, "PipelinePassBindingCacheKey ownerKey",
                       "BindlessImageTableCache should key applied versions by the validated typed pass owner");
        requireAbsent(rendererCacheInterface, "reinterpret_cast<std::uintptr_t>",
                      "BindlessImageTableCache must not infer descriptor ownership from a pipeline object address");
        requirePresent(rendererCacheInterface, "invalidateTablesForFrame(ownerKey)",
                       "descriptor-set reallocation should invalidate every table for the pass owner and frame");
        requirePresent(rendererImplementation, "[runtime, passBinding, colorAttachments, depthAttachment, bindingSnapshot",
                       "parallel raster chunks should capture the same typed pass owner as pass preparation");
        requirePresent(embeddedTriangle, "ShaderStageIntent::Vertex",
                       "EmbeddedTriangle frame uniform should be scoped to vertex shader access");
        requirePresent(normalBuffer, "ShaderStageIntent::Vertex",
                       "NormalBuffer frame uniform should be scoped to vertex shader access");
        requirePresent(ui, "ShaderStageIntent::Fragment",
                       "Ui texture samples should be scoped to fragment shader access");
        requirePresent(accumulate, "ComputePassBuilder",
                       "Accumulate must use renderer-side compute pass builder descriptor handling");
        requirePresent(accumulate, "struct AccumulateTemporalIdentity",
                       "Accumulate must name its complete sampled-radiance identity");
        requirePresent(accumulate, "std::optional<AccumulateTemporalIdentity> previousTemporalIdentity{}",
                       "Accumulate must use one optional identity as its history-validity authority");
        requireAbsent(accumulate, "historyValid",
                      "Accumulate must not retain a second history-validity flag beside its optional identity");
        requirePresent(accumulate, "frameParameters.renderCameraConstants.view",
                       "Accumulate must compare the unjittered render-camera view matrix");
        requirePresent(accumulate, "frameParameters.renderCameraConstants.projection",
                       "Accumulate must compare the unjittered render-camera projection matrix");
        requirePresent(accumulate, "frameParameters.renderCameraConstants.cameraWorld",
                       "Accumulate must compare the camera origin used to generate primary rays");
        requirePresent(accumulate, "frameParameters.sceneRevisions",
                       "Accumulate must snapshot the complete scene identity supplied to graph nodes");
        requirePresent(accumulate, "left.sceneRevisions == right.sceneRevisions",
                       "Accumulate scene history must use full snapshot equality rather than a static projection");
        requirePresent(accumulate, "accumulateTemporalIdentitiesEquivalent",
                       "Accumulate must derive its reset from one complete temporal identity");
        auto const nonIdentityState = std::array{
            std::string_view{"jitter"},
            std::string_view{"sampleFrameOrdinal"},
            std::string_view{"frameIndex"},
            std::string_view{"frameSlot"},
            std::string_view{"maxHistorySampleCount"},
            std::string_view{"viewProjection"},
        };
        std::ranges::for_each(nonIdentityState, [&](std::string_view token) {
            requireAbsent(accumulateTemporalIdentity, token,
                          std::format("Accumulate temporal identity must not contain '{}'", token));
        });
        requirePresent(accumulate, "runtime.previousTemporalIdentity.reset();",
                       "Accumulate image recreation must invalidate the sole temporal identity");
        requirePresent(accumulate, "frameParameters.resolutionPlan.resetHistory",
                       "Accumulate must consume renderer-wide temporal resets such as environment replacement");
        requirePresent(accumulate, "frameParameters.resolutionPlan.displayExtent == extent",
                       "Accumulate must validate its display-sized storage contract");
        requirePresent(accumulate, "frameParameters.resolutionPlan.renderExtent == extent",
                       "Accumulate must fail fast instead of pretending to upscale a smaller render extent");
        requirePresent(accumulate, "upscaler.",
                       "Accumulate extent mismatch diagnostics must identify the unsupported upscaling boundary");
        requirePresent(accumulate, "std::uint32_t historySampleCount = 0u",
                       "Accumulate must own its history sample count");
        requirePresent(accumulate,
                       "std::min(runtime.historySampleCount + 1u, framePlan.maximumHistorySampleCount)",
                       "Accumulate must clamp its stored sample count when the history cap is lowered");
        requirePresent(accumulateInterface, "kAccumulateMaxHistorySampleCount = 4096u",
                       "Accumulate should own its 4096-sample implementation cap");
        requireAbsent(accumulate, "cameraFrameState",
                      "Accumulate history reset and weighting must not depend on another node's camera state");
        requirePresent(rendererImplementation, "temporalHistoryResetPending_ = true;",
                       "Environment replacement must queue a renderer-wide temporal history reset");
        requirePresent(rendererImplementation, "resolutionPlan.resetHistory || temporalHistoryResetPending_",
                       "Renderer must merge an environment replacement reset into the next frame plan");
        requireAbsent(globalUniform, "float4x4 previousView;",
                      "GlobalFrameUniforms must not retain the shader-dead previousView matrix");
        requireAbsent(rendererImplementation, "glm::mat4 previousView{",
                      "RendererGlobalFrameUniforms must not retain the shader-dead previousView matrix");
        requirePresent(globalUniform, "float4x4 unjitteredViewProjection;",
                       "GlobalFrameUniforms should reuse the retired matrix slot for current unjittered VP");
        requirePresent(rendererImplementation, "glm::mat4 unjitteredViewProjection{1.0f};",
                       "RendererGlobalFrameUniforms should mirror the current unjittered VP field");
        requirePresent(rendererImplementation, "sizeof(RendererGlobalFrameUniforms) == 416u",
                       "the temporal camera correction must preserve the 416-byte frame-uniform ABI");
        requirePresent(rendererImplementation,
                       ".unjitteredViewProjection = unjitteredFrameConstants.viewProjection",
                       "the current unjittered VP must come from the selected pre-jitter render camera");
        requirePresent(rendererImplementation,
                       ".previousViewProjection = previousUnjitteredFrameConstants.viewProjection",
                       "the previous VP must come from the last accepted pre-jitter render camera");
        requirePresent(rendererImplementation, ".viewProjection = renderingFrameConstants.viewProjection",
                       "raster and ray-generation consumers must retain the current jittered VP");

        auto const surfaceMotion = sourceSection(pathTracingGuides, "pathTracingGuideSurfaceMotionVector(",
                                                 "public float2 pathTracingGuideSkyMotionVector(");
        auto const skyMotion = sourceSection(pathTracingGuides, "pathTracingGuideSkyMotionVector(",
                                             "public float3 pathTracingGuideEnvBrdfApprox2(");
        auto const primaryGuides = sourceSection(pathTracingGuides, "capturePathTracingPrimarySurfaceGuides(",
                                                 "public bool pathTracingGuideIsSpecularReflection(");
        requirePresent(surfaceMotion, "mul(gFrame.unjitteredViewProjection, float4(position, 1.0f))",
                       "surface motion must project the current position without camera jitter");
        requirePresent(skyMotion, "mul(gFrame.unjitteredViewProjection, direction)",
                       "sky motion must project the current direction without camera jitter");
        requirePresent(surfaceMotion, "mul(gFrame.previousViewProjection, float4(position, 1.0f))",
                       "surface motion must use the last accepted unjittered VP");
        requirePresent(skyMotion, "mul(gFrame.previousViewProjection, direction)",
                       "sky motion must use the last accepted unjittered VP");
        requireAbsent(surfaceMotion, "mul(gFrame.viewProjection",
                      "surface motion must not encode the current jitter delta");
        requireAbsent(skyMotion, "mul(gFrame.viewProjection", "sky motion must not encode the current jitter delta");
        requirePresent(primaryGuides, "mul(gFrame.viewProjection, float4(material.position, 1.0f))",
                       "RR depth must retain the current jittered projection used by ray generation");
        requirePresent(pathTracingGuides,
                       "pathTracingGuideClipToPixel(previousClip, dimensions) - "
                       "pathTracingGuideClipToPixel(currentClip, dimensions)",
                       "RR motion must remain previousPixel-currentPixel in pixel units");
        requirePresent(pathTracingGuides, "return uv * float2(dimensions);",
                       "RR motion conversion must retain top-left pixel-space units");

        requirePresent(rendererInterface, "struct AcceptedTemporalFrameState",
                       "Renderer should name the state committed by an accepted temporal frame");
        requirePresent(rendererInterface, "std::optional<nr::scene::SceneRevisionSnapshot> sceneRevisions{}",
                       "scene presence and complete revisions should share one optional identity");
        requirePresent(rendererInterface,
                       "std::optional<AcceptedTemporalFrameState> acceptedTemporalFrameState_{}",
                       "Renderer should own one optional authority for previous camera and scene history");
        requireAbsent(rendererInterface, "previousGlobalFrameConstants_",
                      "Renderer must not retain a second camera-only temporal optional");
        requirePresent(rendererImplementation,
                       "acceptedTemporalFrameState_->sceneRevisions != currentTemporalFrameState.sceneRevisions",
                       "Renderer temporal reset must compare optional scene presence and the complete snapshot");
        auto const renderFrameBody = sourceSection(rendererImplementation, "Renderer::renderFrame(",
                                                   "[[nodiscard]] nr::rhi::Device &Renderer::device()");
        auto const buildGraphBody = sourceSection(rendererImplementation, "Renderer::buildInstalledGraph(",
                                                  "void Renderer::teardownInstalledGraph()");
        requirePresent(renderFrameBody, "!acceptedTemporalFrameState_.has_value()",
                       "the first accepted frame after any lifecycle reset must reset temporal history");
        requirePresent(renderFrameBody, "acceptedTemporalFrameState_ = std::move(currentTemporalFrameState);",
                       "Renderer must commit the complete current temporal state once");
        requireAbsent(buildGraphBody, "acceptedTemporalFrameState_ =",
                      "graph construction must not publish temporal history before execution");
        auto const executePosition = renderFrameBody.find("auto executeReport = executor_.executePrepared(");
        auto const commitPosition = renderFrameBody.find("acceptedTemporalFrameState_ = std::move(");
        nr::test::require(executePosition != std::string_view::npos && commitPosition != std::string_view::npos &&
                              executePosition < commitPosition,
                          "Renderer must commit temporal state only after graph execution succeeds");

        auto requireTemporalReset = [&](std::string_view begin, std::string_view end, std::string_view reason) {
            requirePresent(sourceSection(rendererImplementation, begin, end), "acceptedTemporalFrameState_.reset();",
                           reason);
        };
        requireTemporalReset("bool Renderer::installGraph(", "void Renderer::uninstallGraph()",
                             "graph installation must invalidate accepted temporal state");
        requireTemporalReset("void Renderer::uninstallGraph()", "void Renderer::shutdown()",
                             "graph uninstall must invalidate accepted temporal state");
        requireTemporalReset("void Renderer::resize()", "void Renderer::resetSceneBinding()",
                             "resize must invalidate accepted temporal state");
        requireTemporalReset("void Renderer::resetSceneBinding()", "void Renderer::collectOptionAvailability(",
                             "scene detachment must invalidate accepted temporal state");
        requireTemporalReset("Renderer::ensureSceneExtractProfile(", "Renderer::ensureSceneTlasExtractProfile(",
                             "raster scene replacement must invalidate accepted temporal state");
        requireTemporalReset("Renderer::ensureSceneTlasExtractProfile(", "} // namespace nr::renderer",
                             "TLAS scene replacement must invalidate accepted temporal state");
        requirePresent(rendererInterface, "void requestTemporalHistoryReset() noexcept;",
                       "Renderer should expose the narrow temporal-history reset request used by committed options");
        auto const resetRequestBody =
            sourceSection(rendererImplementation, "void Renderer::requestTemporalHistoryReset() noexcept",
                          "[[nodiscard]] RendererGraphPreflightResult Renderer::preflightGraph");
        requirePresent(resetRequestBody, "temporalHistoryResetPending_ = true;",
                       "An explicit temporal-history reset request should arm only the pending renderer reset");
        requireAbsent(resetRequestBody, "sampleFrameOrdinal_",
                      "A temporal-history reset request must not restart the monotonic sampling sequence");
        requirePresent(
            pipelineImplementation, "auto const resetsTemporalHistory = definition->resetsTemporalHistory;",
            "Option execution should snapshot the selected definition's temporal-reset policy before commit");
        requirePresent(pipelineImplementation, "app.renderer().requestTemporalHistoryReset();",
                       "A successfully committed temporal-resetting option should request one renderer-wide reset");
        requireAbsent(accumulate, "VariantItemEffect::RuntimeOnly",
                      "Accumulate max history samples should not be registered as a runtime-only variant item");
        requirePresent(
            accumulate, "maxHistorySampleCount(frameParameters.optionSnapshot.get())",
            "Accumulate must read its maximum history sample count from the immutable frame option snapshot");
        requireAbsent(accumulate, "AccumulateNode::collectUi", "Accumulate must not expose a node-local mutation UI");
        requireAbsent(rendererImplementation, "snapshot.desc.effect != VariantItemEffect::RuntimeOnly",
                      "Renderer should not contain generated runtime-only variant UI branching");
    }};

const nr::test::CaseRegistrar rendererTlasTextureKeyCase{
    "renderer TLAS texture key ignores transform and mask while retaining exact topology", [] {
        using Domain = nr::scene::SceneRtRevisionDomain;
        auto revisions = nr::revision::RevisionSet<Domain>{};
        auto snapshot = nr::scene::SceneRevisionSnapshot{
            .sceneIdentity = 7u,
            .rt = revisions.snapshot(),
        };
        auto packet = nr::renderer::RendererTlasTexturePacketIdentity{
            .renderableId = 11u,
            .mesh = nr::resource::MeshHandle{3u, 2u},
            .tlasBucket = 4u,
        };
        auto baseline = nr::renderer::RendererTlasTextureCollectionKey{
            .sceneIdentity = snapshot.sceneIdentity,
            .revisions = nr::renderer::RendererTlasTextureRevisionProjection::capture(snapshot.rt),
            .packets = {packet},
        };

        revisions.advance<Domain::transform, Domain::traceMask>();
        auto dynamicOnly = baseline;
        dynamicOnly.revisions = nr::renderer::RendererTlasTextureRevisionProjection::capture(revisions.snapshot());
        nr::test::requireEqual(dynamicOnly, baseline);

        revisions.advance<Domain::topology>();
        auto topologyChanged = dynamicOnly;
        topologyChanged.revisions = nr::renderer::RendererTlasTextureRevisionProjection::capture(revisions.snapshot());
        nr::test::require(topologyChanged != baseline, "topology revision must invalidate TLAS texture collection");

        auto orderedPacketChanged = baseline;
        orderedPacketChanged.packets.front().renderableId += 1u;
        nr::test::require(orderedPacketChanged != baseline,
                          "ordered packet structural identity must be an exact discriminator");

        auto sceneChanged = baseline;
        sceneChanged.sceneIdentity += 1u;
        nr::test::require(sceneChanged != baseline, "scene identity must be an exact cache boundary");
    }};

const nr::test::CaseRegistrar completeSceneRevisionTemporalIdentityCase{
    "renderer and accumulate temporal identities observe scene presence and every RT revision domain", [] {
        using Domain = nr::scene::SceneRtRevisionDomain;
        auto const baselineRevisions = nr::revision::RevisionSet<Domain>{}.snapshot();
        auto const baseline = nr::scene::SceneRevisionSnapshot{
            .sceneIdentity = 7u,
            .rt = baselineRevisions,
        };
        auto const noScene = std::optional<nr::scene::SceneRevisionSnapshot>{};
        auto const presentScene = std::optional<nr::scene::SceneRevisionSnapshot>{baseline};
        nr::test::require(noScene != presentScene, "scene presence must change renderer temporal identity");

        auto domainIndices = std::views::iota(std::size_t{0u}, nr::revision::revisionDomainCount<Domain>);
        std::ranges::for_each(domainIndices, [&](std::size_t domainIndex) {
            auto revisions = nr::revision::RevisionSet<Domain>{};
            auto changedDomain = nr::revision::RevisionMask<Domain>{};
            changedDomain.values[domainIndex] = true;
            revisions.advance(changedDomain);
            auto const changed = nr::scene::SceneRevisionSnapshot{
                .sceneIdentity = baseline.sceneIdentity,
                .rt = revisions.snapshot(),
            };
            nr::test::require(changed != baseline,
                              std::format("scene RT revision domain {} must change temporal identity", domainIndex));
        });

        auto differentScene = baseline;
        differentScene.sceneIdentity += 1u;
        nr::test::require(differentScene != baseline, "scene replacement must change temporal identity");
    }};

const nr::test::CaseRegistrar sceneRtMutationPolicyCase{
    "scene RT mutation policy separates dynamic and static cache invalidation", [] {
        using Domain = nr::scene::SceneRtRevisionDomain;
        using Mutation = nr::scene::SceneRevisionMutation;
        using Mask = nr::revision::RevisionMask<Domain>;

        auto const asSource =
            readProjectFile("src/renderPasses/AccelerationStructureBuild/nrAccelerationStructureBuildNode.cpp");
        requirePresent(asSource, ".materialCpuVersion = materialRecord.cpuVersion",
                       "AS material cache identity must retain the CPU material semantic version");

        nr::test::requireEqual(nr::scene::SceneRevisionMutationPolicy::mask(Mutation::templateRegistered),
                               Mask::of<Domain::topology, Domain::meshBinding, Domain::meshContent, Domain::meshLayout,
                                        Domain::materialBinding, Domain::materialPayload, Domain::textureBinding,
                                        Domain::textureContent>());
        nr::test::requireEqual(nr::scene::SceneRevisionMutationPolicy::mask(Mutation::templateDestroyed),
                               Mask::of<Domain::topology, Domain::meshBinding, Domain::meshContent, Domain::meshLayout,
                                        Domain::materialBinding, Domain::materialPayload, Domain::textureBinding,
                                        Domain::textureContent>());
        nr::test::requireEqual(nr::scene::SceneRevisionMutationPolicy::mask(Mutation::instanceAdded),
                               Mask::of<Domain::topology, Domain::transform, Domain::visibility, Domain::traceMask,
                                        Domain::meshBinding>());
        nr::test::requireEqual(nr::scene::SceneRevisionMutationPolicy::mask(Mutation::instanceRemoved),
                               Mask::of<Domain::topology, Domain::transform, Domain::visibility, Domain::traceMask,
                                        Domain::meshBinding>());
        nr::test::requireEqual(nr::scene::SceneRevisionMutationPolicy::mask(Mutation::simulationUpdated),
                               Mask::of<Domain::transform>());
        nr::test::requireEqual(nr::scene::SceneRevisionMutationPolicy::mask(Mutation::meshResident),
                               Mask::of<Domain::topology, Domain::meshContent>());
        nr::test::requireEqual(nr::scene::SceneRevisionMutationPolicy::mask(Mutation::textureResident),
                               Mask::of<Domain::topology, Domain::textureResidency>());

        auto const assertProjectionBehavior = [](Mutation mutation, bool staticInvalidation) {
            auto revisions = nr::revision::RevisionSet<Domain>{};
            auto const baseline = revisions.snapshot();
            auto batch =
                nr::revision::RevisionBatch<Domain, Mutation, nr::scene::SceneRevisionMutationPolicy>{revisions};
            batch.apply(mutation);
            batch.commit();
            auto const changed = revisions.snapshot();

            auto const rendererBefore = nr::renderer::RendererTlasTextureRevisionProjection::capture(baseline);
            auto const rendererAfter = nr::renderer::RendererTlasTextureRevisionProjection::capture(changed);
            auto const asBefore = nr::scene::SceneRtStructuralRevisionProjection::capture(baseline);
            auto const asAfter = nr::scene::SceneRtStructuralRevisionProjection::capture(changed);
            nr::test::require((rendererAfter != rendererBefore) == staticInvalidation,
                              "renderer static projection mutation classification mismatch");
            nr::test::require((asAfter != asBefore) == staticInvalidation,
                              "AS static projection mutation classification mismatch");
        };

        assertProjectionBehavior(Mutation::templateRegistered, true);
        assertProjectionBehavior(Mutation::templateDestroyed, true);
        assertProjectionBehavior(Mutation::instanceAdded, true);
        assertProjectionBehavior(Mutation::instanceRemoved, true);
        assertProjectionBehavior(Mutation::simulationUpdated, false);
        assertProjectionBehavior(Mutation::meshResident, true);
        assertProjectionBehavior(Mutation::textureResident, true);
    }};

const nr::test::CaseRegistrar presentLinearExrScreenshotCase{
    "present screenshots read back linear source images and write EXR", [] {
        auto manifest = readProjectFile("vcpkg.json");
        auto externCMake = readProjectFile("src/extern/CMakeLists.txt");
        auto dependencyAssets = readProjectFile("src/extern/dependencyAssets.ixx");
        auto rendererInterface = readProjectFile("src/renderer/nrRenderer.ixx");
        auto rendererImplementation = readProjectFile("src/renderer/nrRendererPassBuilders.cpp");
        auto presentInterface = readProjectFile("src/renderPasses/Present/nrPresentNode.ixx");
        auto present = readProjectFile("src/renderPasses/Present/nrPresentNode.cpp");

        auto parsedManifest = dependency::json::parseJson(manifest);
        nr::test::require(parsedManifest.valid(), "vcpkg manifest should be valid JSON");
        auto const *manifestObject = std::get_if<dependency::json::JsonValue::Object>(&parsedManifest.value->storage);
        nr::test::require(manifestObject != nullptr, "vcpkg manifest should be a JSON object");
        auto const dependencies = manifestObject->find("dependencies");
        nr::test::require(dependencies != manifestObject->end(), "vcpkg manifest should declare dependencies");
        auto const *dependencyArray = std::get_if<dependency::json::JsonValue::Array>(&dependencies->second.storage);
        nr::test::require(dependencyArray != nullptr, "vcpkg dependencies should be a JSON array");
        auto const hasOpenExr = std::ranges::any_of(*dependencyArray, [](const dependency::json::JsonValue &entry) {
            auto const *name = std::get_if<std::string>(&entry.storage);
            return name != nullptr && *name == "openexr";
        });
        nr::test::require(hasOpenExr, "vcpkg manifest should install OpenEXR");
        requirePresent(externCMake, "find_package(OpenEXR CONFIG REQUIRED)", "dependency boundary should find OpenEXR");
        requirePresent(externCMake, "OpenEXR::OpenEXR", "dependency target should link OpenEXR");
        requirePresent(dependencyAssets, "namespace nr::dependency::openexr",
                       "OpenEXR declarations should be exposed only through dependency.assets");
        requirePresent(rendererInterface, "describeImageResource(GraphResourceHandle resource)",
                       "Present should be able to query source image metadata without owning graph internals");
        requirePresent(rendererImplementation, "describeGraphImageResource",
                       "renderer should implement image resource metadata lookup");

        requirePresent(presentInterface, "struct PresentRuntimeState;",
                       "Present interface must expose only an incomplete implementation-state declaration");
        requirePresent(presentInterface, "std::unique_ptr<detail::PresentRuntimeState> runtime_{};",
                       "Present node must exclusively own its implementation state");
        requireAbsent(presentInterface, "PresentRuntimeCache",
                      "Present interface must not retain the former shared cache concept");
        requireAbsent(presentInterface, "std::shared_ptr<detail::PresentRuntime",
                      "Present implementation state must not use shared ownership");
        requireAbsent(presentInterface, "std::optional<std::reference_wrapper<nr::rhi::Device>>",
                      "Present interface must not expose a nullable device observation");
        requireAbsent(presentInterface, "screenshotReadbackBuffer_",
                      "Present screenshot buffer must not remain a dispersed node member");
        requirePresent(present, "struct PresentRuntimeState",
                       "Present implementation must define one domain runtime state");
        requirePresent(present, "nr::rhi::Device &device;",
                       "Present runtime must hold its required device observation as a reference");
        requirePresent(present, "std::shared_ptr<nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>> pipeline",
                       "Present pass callbacks may retain the pipeline's established shared lifetime");
        requirePresent(present, "nr::rhi::Buffer screenshotReadbackBuffer{};",
                       "Present runtime must own its screenshot buffer");
        requirePresent(present, "std::optional<PresentScreenshotPrepared> screenshotPrepared{};",
                       "Present runtime must own provisional capture state");
        requirePresent(present, "std::optional<PresentScreenshotPendingSave> screenshotPendingSave{};",
                       "Present runtime must own pending continuation state");
        requirePresent(present, "std::unique_ptr<PresentRuntimeState> makePresentRuntime(",
                       "Present runtime construction must be an explicit one-time factory");
        requireAbsent(present, "ensurePresentRuntime(",
                      "Present runtime construction must not retain ensure-style shared-cache semantics");
        requirePresent(present, "Present initialization must create runtime state exactly once",
                       "Present initialization must reject replacement of live runtime state");
        requireOrdered(present, "runtime_->pipeline->clearBindingSets();", "runtime_.reset();",
                       "Present shutdown must clear bindings before releasing all runtime resources");

        requireAbsent(present, "stbi_write_png", "Present screenshots should no longer write PNG files");
        requireAbsent(present, "Present.ConvertScreenshot",
                      "Present screenshots should not run a shader conversion pass");
        requireAbsent(present, "kScreenshotFormat", "Present screenshots should not force a fixed RGBA8 format");
        requirePresent(present, "context.describeImageResource(sourceColor)",
                       "Present screenshots should inspect the published source image format");
        requirePresent(present,
                       "detail::addPresentReadbackCopyPass(\n                context,\n                sourceColor",
                       "Present screenshots should copy frameResource::presentSourceColor directly");
        requirePresent(present, ".format = sourceDesc->format",
                       "Pending screenshot save should remember the source format");
        requireAbsent(presentInterface, "PresentScreenshotPrepared",
                      "Present prepared screenshot state must stay out of the module interface");
        requireAbsent(presentInterface, "PresentScreenshotPendingSave",
                      "Present pending screenshot state must stay out of the module interface");
        requirePresent(present, "struct PresentScreenshotPrepared",
                       "Present implementation must retain prepared screenshot state");
        requirePresent(present, "struct PresentScreenshotPendingSave",
                       "Present implementation must carry pending screenshot state across the frame fence");
        requirePresent(present, "vk::Format format = vk::Format::eUndefined",
                       "Present implementation state must retain the screenshot source format");
        requirePresent(present, "writeLinearScreenshotExr",
                       "Present should save the readback payload through the EXR writer");
        requirePresent(present, "!runtime_->screenshotPrepared.has_value() &&",
                       "capture availability must remain busy while dispatch state exists");
        requirePresent(present, "!runtime_->screenshotPendingSave.has_value()",
                       "capture availability must remain busy while continuation state exists");
        requirePresent(present, "frameParameters.frameEffectSink->get().claim(*this, readbackPass)",
                       "capture must claim its exact image-to-readback copy pass");
        requirePresent(present, "if (!targetBatchSubmitted || runtime.screenshotPendingSave.has_value())",
                       "capture must not arm its continuation unless the target batch submitted");
        requireOrdered(present, "if (!targetBatchSubmitted || runtime.screenshotPendingSave.has_value())",
                       "runtime.screenshotPendingSave = detail::PresentScreenshotPendingSave",
                       "capture continuation state must be created only after target submission validation");
        requirePresent(present, "runtime_->screenshotPendingSave->frameSlot != frameSlot",
                       "capture harvest must wait for the owning RHI frame slot");
        requirePresent(present, "void PresentNode::flushContinuations()",
                       "graph replacement and shutdown must expose a synchronous capture flush hook");
        requirePresent(present, ".phase = nr::options::OptionLogPhase::terminal",
                       "capture harvest must emit its terminal machine record");
        requireAbsent(present, "screenshotRequestCount_", "Present must not retain a multi-request screenshot counter");
    }};

const nr::test::CaseRegistrar pathTracingNodeAssemblyCase{
    "path tracing node resolves typed inputs and uses named RT assembly groups", [] {
        auto pathTracingNode = readProjectFile("src/renderPasses/PathTracing/nrPathTracingNode.cpp");
        auto rhiPipelineHeader = readProjectFile("src/rhi/nrPipeline.ixx");
        auto rhiPipelineSource = readProjectFile("src/rhi/nrPipeline.cpp");

        requirePresent(pathTracingNode, "PathTracingFrameInputs",
                       "PathTracing should resolve its scene inputs through one typed bundle");
        requirePresent(pathTracingNode, "clearUnavailableGuides",
                       "PathTracing unavailable inputs should clear the complete guide set");
        requirePresent(pathTracingNode, "kPathTracingGuideResourceCount",
                       "PathTracing should define one fixed guide resource count");
        requirePresent(pathTracingNode, "guideFrameSlots",
                       "PathTracing guides should be isolated by resource frame slot");
        requirePresent(pathTracingNode, "nr::maxFrameInFlight",
                       "PathTracing should retain one guide set per frame in flight");
        requirePresent(pathTracingNode, "publishPathTracingGuides",
                       "PathTracing should publish its guide set through frame resources");
        requirePresent(pathTracingNode, "PathTracing.ClearUnavailable.",
                       "PathTracing fallback clears should preserve their reason in the debug name");
        requirePresent(pathTracingNode, "makePathTracingProgramAssembly",
                       "PathTracing should assemble RT stages and shader groups from one description");
        requirePresent(pathTracingNode, "shaderGroupIndex",
                       "PathTracing SBT records should resolve named RT shader groups");
        requireAbsent(pathTracingNode, "2u + record.permutationIndex",
                      "PathTracing SBT records must not depend on hard-coded RT shader group indices");
        requirePresent(rhiPipelineHeader, "struct RayTracingPipelineStageSelection",
                       "RHI should expose explicit RT stage selection records");
        requirePresent(rhiPipelineHeader, "struct RayTracingProgramAssemblyDesc",
                       "RHI should expose one RT program assembly description");
        requirePresent(rhiPipelineHeader, "shaderGroupIndex",
                       "RHI RT pipelines should expose named shader group lookup");
        requirePresent(rhiPipelineHeader, "logicalEntryPointName",
                       "RHI RT stage selections should carry logical names for shader group lookup");
        requirePresent(rhiPipelineSource,
                       "program.logicalEntryPointNames_.push_back(std::move(logicalEntryPointName));",
                       "RHI RT shader program should store logical names for group lookup");
        requirePresent(rhiPipelineSource, "stageInfo.pName = program.shaderEntryPointNames_.back().c_str();",
                       "RHI Vulkan shader stages should use the actual name discovered from each single-entry program");
    }};

const nr::test::CaseRegistrar rendererSubmissionTimelinesCase{
    "renderer submission batches use producer-owned per-queue timelines", [] {
        auto executor = readProjectFile("src/renderer/nrRenderGraphExecutor.cpp") +
                        readProjectFile("src/renderer/nrRenderGraphExecutorResources.cpp");
        auto timeline = readProjectFile("src/renderer/nrRendererSubmission.ixx");
        auto waitStageBegin = executor.find("RenderGraphExecutor::submissionWaitStage");
        auto waitStageEnd = executor.find("RenderGraphExecutor::shaderWaitStageForQueue", waitStageBegin);
        nr::test::require(waitStageBegin != std::string::npos && waitStageEnd != std::string::npos,
                          "executor should define a bounded submission wait-stage helper");
        auto waitStageFunction = executor.substr(waitStageBegin, waitStageEnd - waitStageBegin);

        requirePresent(waitStageFunction, "queue == QueueDomain::Graphics",
                       "executor should select the graphics submission wait scope explicitly");
        requirePresent(waitStageFunction, "vk::PipelineStageFlagBits2::eAllCommands",
                       "graphics submission waits should cover batch-head acquire and RT shader work");
        requireAbsent(waitStageFunction, "vk::PipelineStageFlagBits2::eColorAttachmentOutput",
                      "graphics submission waits must not be limited to color attachment output");
        requirePresent(executor, "timelines->get().semaphore(previousSignalToken.queue)",
                       "adjacent RDG batches should wait on the producer queue timeline semaphore");
        requirePresent(executor, "timelines->get().acquireSignalToken(planBatch.queue)",
                       "each inter-batch signal should acquire a token from its queue timeline");
        requirePresent(executor, "eQueueFamilyOwnershipTransferUseAllStagesKHR",
                       "explicit QFOT barriers should opt into maintenance8 stage semantics");
        requirePresent(executor, "dstStageMask = srcStageMask",
                       "maintenance8 release barriers should keep both scopes at the producer stage");
        requirePresent(executor, "srcStageMask = dstStageMask",
                       "maintenance8 acquire barriers should keep both scopes at the consumer stage");
        requirePresent(executor, "applyQueueFamilyTransferPolicy(compiled, context.device);",
                       "executor preparation should specialize ownership transitions from the runtime device policy");
        requirePresent(executor, "policy.canOmitBufferQueueFamilyTransfer",
                       "renderer buffers and acceleration structures should consult maintenance9 before explicit QFOT");
        requirePresent(executor, "policy.canOmitImageQueueFamilyTransfer",
                       "renderer images should consult maintenance9 before explicit QFOT");
        requirePresent(executor, "transition.strength = needsLayoutTransition",
                       "omitted image QFOT should retain any required consumer-side layout transition");
        requirePresent(executor, "remainingOwnershipTransitions",
                       "prepared ownership diagnostics should exclude omitted QFOTs");
        requirePresent(executor, "signalTokenByBatch.insert_or_assign",
                       "normal graph submits should retain their queue timeline token for cross-frame resources");
        requirePresent(
            executor, "initialReleaseBatches",
            "non-omittable retained initial ownership changes should preserve the source-queue release fallback");
        requirePresent(timeline, "std::array<QueueTimeline, timelineCount> timelines_",
                       "renderer should retain one timeline state per queue domain");
        requirePresent(timeline, ".queue = queue", "renderer submission tokens should identify their producer queue");
        requirePresent(timeline, "++timeline.nextSignalValue",
                       "each queue timeline value should remain strictly increasing across frames");
    }};

const nr::test::CaseRegistrar pathTracingShaderOrganizationCase{
    "path tracing shader keeps raygen core separate from material hit shaders", [] {
        auto raygenEntry = readProjectFile("shader/renderer/pathTracing/raygen.slang");
        auto missEntry = readProjectFile("shader/renderer/pathTracing/miss.slang");
        auto anyHitEntry = readProjectFile("shader/renderer/pathTracing/anyHit.slang");
        auto shadowMissEntry = readProjectFile("shader/renderer/pathTracing/shadowMiss.slang");
        auto shadowAnyHitEntry = readProjectFile("shader/renderer/pathTracing/shadowAnyHit.slang");
        auto closestHitEntry = readProjectFile("shader/renderer/pathTracing/closestHit.slang");
        auto entryPoints =
            raygenEntry + missEntry + anyHitEntry + shadowMissEntry + shadowAnyHitEntry + closestHitEntry;
        auto common = readProjectFile("shader/common.slang");
        auto core = readProjectFile("shader/renderer/pathTracing/core.slang");
        auto directLighting = sourceSection(core, "static const uint kDirectLightSampleCount",
                                            "public bool reachedBounceLimit(");
        auto guides = readProjectFile("shader/renderer/pathTracing/guides.slang");
        auto params = readProjectFile("shader/renderer/pathTracing/params.slang");
        auto pathState = readProjectFile("shader/renderer/pathTracing/pathState.slang");
        auto resources = readProjectFile("shader/renderer/pathTracing/resources.slang");
        auto environment = readProjectFile("shader/renderer/pathTracing/environment.slang");
        auto random = readProjectFile("shader/include/pathTracing/random.slang");
        auto scheduler = readProjectFile("shader/renderer/pathTracing/scheduler.slang");
        auto visibility = readProjectFile("shader/renderer/pathTracing/visibility.slang");
        auto anyHitPolicy = readProjectFile("shader/renderer/pathTracing/anyHitPolicy.slang");
        auto shadowPayload = readProjectFile("shader/renderer/pathTracing/shadowPayload.slang");
        auto rtRayType = readProjectFile("shader/include/share/rtRayType.slang");
        auto hitShaders = readProjectFile("shader/renderer/pathTracing/hitShaders.slang");
        auto materialBsdf = readProjectFile("shader/include/material/bsdf.slang");
        auto materialTypes = readProjectFile("shader/include/material/types.slang");
        auto materialPayload = readProjectFile("shader/include/material/payload.slang");
        auto materialSampling = readProjectFile("shader/include/material/sampling.slang");
        auto stochasticTextureFiltering = readProjectFile("shader/include/material/stochasticTextureFiltering.slang");
        auto rtMaterial = readProjectFile("shader/include/rtMaterial.slang");
        auto hitSurface = readProjectFile("shader/include/rt/hitSurface.slang");
        auto roulette = readProjectFile("shader/include/pathTracing/roulette.slang");
        auto chs = readProjectFile("shader/include/pathTracing/chs.slang");
        auto pathTracingNode = readProjectFile("src/renderPasses/PathTracing/nrPathTracingNode.cpp");
        auto rtHitSbtPlan = readProjectFile("src/renderPasses/nrRtHitSbtPlan.ixx");
        auto rhiPipelineHeader = readProjectFile("src/rhi/nrPipeline.ixx");
        auto rhiPipelineSource = readProjectFile("src/rhi/nrPipeline.cpp");

        nr::test::require(
            !std::filesystem::exists(std::filesystem::path{std::string{nr::projectRoot}} /
                                     "shader/include/pathTracing/mis.slang"),
            "The unused PathTracing MIS implementation must remain deleted");
        requireAbsent(common, "__include include.pathTracing.mis;",
                      "The common shader aggregation module must not restore the unused MIS include");
        requirePresent(raygenEntry, "Scheduler scheduler;", "PathTracing raygen entry should construct the scheduler");
        requirePresent(raygenEntry, "scheduler.traceSample(pixel, dimensions);",
                       "PathTracing raygen entry should delegate work to the scheduler");
        requirePresent(closestHitEntry, "CHS chs = CHS();",
                       "PathTracing closest-hit entry should construct its entry-local CHS type");
        requirePresent(closestHitEntry, "chs.handleClosestHit", "PathTracing closest-hit entry should delegate to CHS");
        requireAbsent(chs, "kKnownRtMaterialVariantMask",
                      "CHS must not retain the unused Shader-side known layer-mask constant");
        requireAbsent(chs, "kKnownRtMaterialFeatureMask",
                      "CHS must not retain the unused Shader-side known feature-mask constant");
        requirePresent(chs, "public interface ICHS", "CHS variants must retain their shared typed interface");
        requirePresent(chs, "public struct MaterialCHS<let LayerFlags : RtMaterialLayerFlag> : ICHS",
                       "CHS must retain its layer-specialized production implementation");
        requirePresent(chs, "public extern struct CHS : ICHS;",
                       "Closest-hit entry points must retain their link-specialized CHS contract");
        requirePresent(rtHitSbtPlan, "constexpr bool rtMaterialLayerFlagsValid(",
                       "CPU hit-SBT planning must retain its canonical material layer-mask validation");
        requirePresent(rtHitSbtPlan, "nr::scene::kRtMaterialVariantMask",
                       "CPU hit-SBT validation must use the scene-owned material variant mask");
        requirePresent(rtHitSbtPlan, "(bits & ~knownMask) == 0u",
                       "CPU hit-SBT validation must continue rejecting unknown material layer bits");
        requireAbsent(rtHitSbtPlan, "kKnownRtMaterialVariantMask",
                      "CPU mask validation must not depend on the deleted Shader-only mask symbol");
        requirePresent(anyHitEntry, "ahMaterialPolicy",
                       "PathTracing any-hit entry should expose the shared material-policy ABI");
        requirePresent(shadowMissEntry, "msShadow(inout ShadowRayPayload payload)",
                       "PathTracing shadow miss should use the compact shadow payload ABI");
        requirePresent(shadowAnyHitEntry, "ahShadow(inout ShadowRayPayload payload",
                       "PathTracing shadow any-hit should use the compact shadow payload ABI");
        requireAbsent(anyHitEntry, "ahMain", "PathTracing should not keep the old universal any-hit entry name");
        requireAbsent(entryPoints, "evaluateSceneLightAt", "PathTracing entries should not own lighting logic");
        requireAbsent(entryPoints, "resolveRtMaterialPayload",
                      "PathTracing entries should not own material payload decoding");
        requirePresent(missEntry, "payload.missRadiance = sampleEnvironment",
                       "material-ray miss should reuse the resolved position slot for environment radiance");
        requireAbsent(missEntry, "RayKind", "material miss should no longer branch on a shared ray kind");
        requireAbsent(params, "RayKind", "path tracing params should not retain the shared ray-kind enum");
        requirePresent(rtRayType, "material = 0u", "material ray type must remain ABI slot zero");
        requirePresent(rtRayType, "shadow = 1u", "shadow ray type must remain ABI slot one");
        requirePresent(rtRayType, "count = 2u", "ray-type geometry multiplier must remain two");
        auto const shadowPayloadRecord = sourceSection(shadowPayload, "public struct ShadowRayPayload", "}");
        requirePresent(shadowPayloadRecord, "public uint occluded = 1u;",
                       "shadow payload should default to occluded until the shadow miss clears it");
        requireAbsent(shadowPayloadRecord, "float", "shadow payload must remain one uint32 lane");

        requirePresent(scheduler, "public struct Scheduler", "PathTracing scheduler should be a shader-side struct");
        requirePresent(scheduler, "Pt pt = makePt(pixel, dimensions);",
                       "PathTracing scheduler should construct the PT path object");
        requirePresent(scheduler, "while (pt.isActive())", "PathTracing scheduler should own the raygen path loop");
        requirePresent(scheduler, "pt.traceMaterialRay(payload);",
                       "PathTracing scheduler should ask Pt to issue material rays");
        requirePresent(scheduler, "pt.handleTraceResult(payload);",
                       "PathTracing scheduler should ask Pt to handle hit or miss results");
        requirePresent(scheduler, "pt.writeOutput();",
                       "PathTracing scheduler should run exactly one camera sample per pixel");
        requireAbsent(scheduler, "kSamplesPerPixel", "PathTracing scheduler must not expose a samples-per-pixel loop");
        requireAbsent(scheduler, "for (uint sampleIndex",
                      "PathTracing scheduler must stay fixed at one camera sample per pixel");
        requirePresent(core, "public struct Pt", "PathTracing core should define the PT path object");
        requirePresent(core, "public PathState path", "Pt should hold the per-path state");
        requirePresent(core, "public bool isActive()", "Pt should expose active-state testing");
        requirePresent(core, "public void traceMaterialRay", "Pt should own material ray scheduling");
        requirePresent(core, "HitObject hitObject = HitObject::TraceRay(",
                       "material rays should separate traversal from hit/miss shader invocation");
        requireAbsent(core, "RayPayload traversalPayload",
                      "material rays should reuse the output payload instead of carrying a second payload across SER");
        requirePresent(core, "materialRayCoherenceHint(path.vertexIndex)",
                       "material rays should compute a path-specific SER coherence hint");
        requirePresent(core, "kMaterialRayCoherenceHintBitCount = 3u",
                       "material-ray SER should use the three architecturally meaningful hint bits");
        requirePresent(core, "HitObject::Invoke(scene, hitObject, payload);",
                       "material rays should invoke their reordered hit or miss shader");
        requireAbsent(core, "\n        TraceRay(", "material rays must not retain the synchronous TraceRay path");
        requirePresent(core,
                       "uint(RtRayType.material),\n                                uint(RtRayType.count), "
                       "uint(RtRayType.material)",
                       "material rays should route through the shared material/count ABI");
        requirePresent(materialPayload, "payload.layers.transmissionIor == 0.0f",
                       "IOR zero compatibility mode should force the final interface Fresnel to one");
        requirePresent(anyHitPolicy, "shouldIgnoreSingleSidedBackFace",
                       "PathTracing any-hit should restore per-material back-face policy for mixed BLAS instances");
        requirePresent(
            hitSurface, "rtObjectToWorldHandedness()",
            "RT tangent frames should account for mirrored instance transforms independently from object-space facing");
        requirePresent(core, "public void handleTraceResult", "Pt should own hit or miss dispatch");
        requirePresent(core, "public void handleHit", "Pt should expose hit handling");
        requirePresent(core, "public void handleMiss", "Pt should expose miss handling");
        requireAbsent(pathState, "public uint diffuseBounces",
                      "PathState must not retain the unread diffuse-bounce counter");
        requireAbsent(pathState, "public uint specularBounces",
                      "PathState must not retain the unread specular-bounce counter");
        requireAbsent(core, "path.diffuseBounces",
                      "Path integration must not write the removed diffuse-bounce state");
        requireAbsent(core, "path.specularBounces",
                      "Path integration must not write the removed specular-bounce state");
        requirePresent(core, "ResolvedMaterialScatterKind scatterKind = scatter.kind",
                       "Path integration must retain the scatter kind used by guide tracking");
        requirePresent(core, "beginPathTracingSpecularHitTracking(path.guides, scatterKind)",
                       "The retained scatter kind must continue to drive primary specular-hit guide tracking");
        requireOrdered(core, "ResolvedMaterialScatterKind scatterKind = scatter.kind",
                       "beginPathTracingSpecularHitTracking(path.guides, scatterKind)",
                       "Scatter kind must remain live from material resolution through guide tracking");
        requirePresent(guides,
                       "beginPathTracingSpecularHitTracking(inout PathTracingGuideState guides, "
                       "ResolvedMaterialScatterKind kind)",
                       "Guide ABI must retain its strongly typed scatter-kind input");
        requireAbsent(guides, "resolvePathTracingSpecularMiss",
                      "Specular guide tracking must not retain an unobservable miss-state reset helper");
        requireAbsent(core, "resolvePathTracingSpecularMiss",
                      "Path misses must not write guide state that is never consumed or published");
        requirePresent(guides, "public uint awaitingSpecularHit = 0u",
                       "Specular hit tracking must retain its pending-hit state");
        requirePresent(guides, "public void resolvePathTracingSpecularHit(",
                       "Specular guide tracking must retain its observable hit resolver");
        requirePresent(guides, "if (guides.awaitingSpecularHit == 0u)",
                       "The observable hit resolver must remain gated by pending specular tracking");
        requirePresent(guides,
                       "guides.specularHitDistance = "
                       "min(length(hitPosition - guides.primaryPosition), kPathTracingGuideHalfMax)",
                       "A tracked secondary hit must retain its observable world-space distance");
        requirePresent(core, "resolvePathTracingSpecularHit(path.guides, material.position)",
                       "The second surface hit must continue to resolve specular guide distance");
        requirePresent(guides, "specularHitDistanceImage[location] = guides.specularHitDistance",
                       "Guide output must continue publishing the observable specular hit distance");
        requirePresent(pathState, "public uint vertexIndex = 0u",
                       "PathState must retain the canonical aggregate bounce index");
        requirePresent(core, "if (reachedBounceLimit(path))",
                       "Path continuation must retain its existing aggregate bounce limit");
        requirePresent(core, "path.vertexIndex += 1u",
                       "Material-ray scheduling must retain aggregate vertex advancement");
        requirePresent(core, "handleMiss(inout PathState path, float3 missRadiance)",
                       "PathTracing miss handling should consume the miss shader's environment radiance");
        requirePresent(core, "sampleEnvironment(path.direction) * (1.0f - hitAlpha)",
                       "primary alpha blend should use the same directional environment background");
        requirePresent(core, "public void writeOutput", "Pt should expose output writing");
        requirePresent(core, "makePt", "PathTracing core should provide Pt construction");
        requirePresent(core, "handleHit", "PathTracing core should own hit shading");
        requirePresent(core, "sampleDirectLighting", "PathTracing core should own direct lighting");
        requirePresent(core, "writeOutput", "PathTracing core should own output writes");
        requirePresent(
            core, "makeErrorDiffusionSequence(pixel, gFrame.frameState.xy)",
            "PathTracing core should seed error-diffusion sampling from the complete 64-bit sample-frame ordinal");
        requireAbsent(core, "makeErrorDiffusionSequence(pixel, gFrame.frameState.x)",
                      "PathTracing core must not truncate the sample-frame ordinal to its low lane");
        requireAbsent(params, "kSamplesPerPixel",
                      "PathTracing params must not expose configurable camera samples per pixel");
        requirePresent(params, "public extern static const uint kMaxSurfaceBounces;",
                       "PathTracing max bounce variant must be provided by C++ VariantDesc");
        requireAbsent(params,
                      "kMaxSurfaceBounces =", "PathTracing max bounce variant must not have a shader-side default");
        requirePresent(
            stochasticTextureFiltering, "public extern static const bool kEnableFilterAfterShading;",
            "The common material filtering include must expose the FAS root link-time constant to linked CHS programs");
        requireAbsent(stochasticTextureFiltering,
                      "kEnableFilterAfterShading =", "The closest-hit FAS variant must not have a shader-side default");
        requireAbsent(
            params, "kEnableFilterAfterShading",
            "The FAS constant should have one common-visible declaration rather than a PathTracing-local duplicate");
        requireAbsent(params, "kMissRadiance",
                      "PathTracing should not retain a constant miss radiance after environment integration");
        requirePresent(environment, "Sampler2D<float4> gEnvironmentMap",
                       "environment should use a dedicated combined sampler");
        requirePresent(environment, "ConstantBuffer<EnvironmentMapParameters> gEnvironment",
                       "environment decode controls should use push constants");
        requirePresent(environment, "SampleLevel(uv, 0.0f)",
                       "mipless environment sampling should explicitly use level zero");
        requirePresent(environment, "gEnvironment.radianceDecodeScale",
                       "environment sampling should restore scaled source radiance");
        requirePresent(environment, "gEnvironment.intensity",
                       "environment sampling should apply independent user intensity");
        requireAbsent(visibility, "sampleEnvironment", "visibility rays must not evaluate environment radiance");
        requirePresent(visibility,
                       "public bool tracePathVisibilityRay(float3 origin, float3 direction, float maxDistance)",
                       "Visibility traversal must retain its explicit real-distance contract");
        requireExactlyOne(visibility, "public bool tracePathVisibilityRay(",
                          "Visibility traversal must expose one canonical ray helper");
        requireAbsent(visibility, "tracePathVisibilityRayToLight",
                      "PathTracing must not retain the unused infinite-distance visibility wrapper");
        requirePresent(visibility, "ray.TMax = max(kRayEpsilon, maxDistance - kRayEpsilon)",
                       "Visibility traversal must preserve the caller-provided finite distance boundary");
        requirePresent(core,
                       "tracePathVisibilityRay(visibilityOrigin, contribution.direction, "
                       "lightVisibilityDistance(lightSample.lightIndex, material.position))",
                       "Direct lighting must continue to pass the sampled light's real visibility distance");
        requirePresent(visibility, "TraceRay(", "visibility/shadow rays should retain direct TraceRay traversal");
        requireAbsent(visibility, "HitObject::TraceRay", "visibility/shadow rays should remain outside the SER path");
        requireAbsent(visibility, "ReorderThread", "visibility/shadow rays should not pay an SER reorder");
        requirePresent(roulette, "public extern struct RussianRoulettePolicy : IRussianRoulettePolicy;",
                       "PathTracing roulette policy variant must be provided by C++ VariantDesc");
        requireAbsent(roulette, "RussianRoulettePolicy : IRussianRoulettePolicy =",
                      "PathTracing roulette policy variant must not rely on a shader-side default");
        requirePresent(random, "uint2 sampleFrameOrdinal",
                       "PathTracing random sequence should receive both lanes of the 64-bit sample-frame ordinal");
        requirePresent(
            random, "int tileBits = 11",
            "PathTracing random sequence should default to Hilbert11 with 22 spatial bits and 10 in-era frame bits");
        requireAbsent(random, "int tileBits = 8",
                      "PathTracing random sequence must not retain the 256x256 Hilbert8 default");
        requirePresent(random, "sampleFrameOrdinal.x & frameInEraMask",
                       "PathTracing random sequence should preserve the low ordinal bits in its Sobol index");
        requirePresent(random, "sampleFrameOrdinal.y << spatialBitCount",
                       "PathTracing random sequence should fold the high ordinal lane into the frame era");
        requirePresent(random, "sampleFrameOrdinal.y >> frameBitCount",
                       "PathTracing random sequence should consume all remaining high-lane era bits");
        requirePresent(random, "seq.sampleSeed = strongIntegerHash(frameEra.x ^ strongIntegerHash(frameEra.y));",
                       "PathTracing random sequence should derive a frame-shared scramble seed from the complete era "
                       "while preserving hash zero");
        requirePresent(random, "public float4 rand4()",
                       "PathTracing random sequence should expose one complete four-lane packet draw");
        requireAbsent(random, "IRandomSequence",
                      "PathTracing random sequence should not retain a generic variable-width sampling interface");
        requireAbsent(random, "get1D", "PathTracing random sequence should not expose scalar packet draws");
        requireAbsent(random, "get2D", "PathTracing random sequence should not expose two-lane packet draws");
        requireAbsent(random, "get3D", "PathTracing random sequence should not expose three-lane packet draws");
        requireAbsent(random, "get4D", "PathTracing random sequence should use the single rand4 packet API");
        requireAbsent(random, "getBits",
                      "PathTracing random sequence should not retain a hidden variable-width bit draw");
        requireAbsent(random, "split(", "PathTracing random sequence should not retain a secondary packet draw API");
        requireAbsent(random, "random01FromHash",
                      "PathTracing random sequence should not retain the legacy white-noise light-sample hash");
        requireAbsent(params, "kDirectLightSampleCount",
                      "The direct-light sample count must not remain exported by shared PathTracing params");
        requirePresent(directLighting, "static const uint kDirectLightSampleCount = 4u;",
                       "PathTracing core must locally own its exact four-sample packet contract");
        requireAbsent(directLighting, "public static const uint kDirectLightSampleCount",
                      "The core-local direct-light sample count must not become a module export");
        requirePresent(directLighting, "float sampleWeight = 1.0f / float(kDirectLightSampleCount);",
                       "Direct-light normalization must continue deriving its denominator from the local count");
        requirePresent(directLighting, "float4 lightRandomValues = path.rng.rand4();",
                       "PathTracing direct lighting should draw its first pair of low-discrepancy sample pairs from "
                       "one four-lane packet");
        requirePresent(directLighting, "\n    lightRandomValues = path.rng.rand4();",
                       "PathTracing direct lighting should draw its second pair of low-discrepancy sample pairs from "
                       "one four-lane packet");
        requirePresent(directLighting, "lightRandomValues.xy",
                       "PathTracing direct lighting should consume the first pair from each four-lane packet");
        requirePresent(directLighting, "lightRandomValues.zw",
                       "PathTracing direct lighting should consume the second pair from each four-lane packet");
        auto occurrenceCount = [](std::string_view source, std::string_view token) {
            return static_cast<std::size_t>(std::ranges::count_if(
                std::views::iota(std::size_t{0u}, source.size()),
                [&](std::size_t offset) { return source.substr(offset).starts_with(token); }));
        };
        nr::test::requireEqual(occurrenceCount(directLighting, "path.rng.rand4()"), std::size_t{2u},
                               "Direct lighting must retain exactly two four-lane random packet draws");
        nr::test::requireEqual(occurrenceCount(directLighting, "accumulateDirectLightSample("), std::size_t{4u},
                               "Direct lighting must retain exactly four sample accumulations");
        nr::test::requireEqual(occurrenceCount(directLighting, "lightRandomValues.xy"), std::size_t{2u},
                               "Each random packet must contribute its xy sample pair exactly once");
        nr::test::requireEqual(occurrenceCount(directLighting, "lightRandomValues.zw"), std::size_t{2u},
                               "Each random packet must contribute its zw sample pair exactly once");
        requireAbsent(
            core, "makeAliasLightSample",
            "PathTracing direct lighting should not hash native low-discrepancy packet lanes into white noise");
        requirePresent(core, "path.rng.rand4().x",
                       "PathTracing roulette should consume one lane from the only public random packet API");
        requirePresent(core, "float4 scatterRandomValues = path.rng.rand4();",
                       "PathTracing scatter should consume one complete four-lane random packet");
        requirePresent(random, "public void advanceThreeRand4Packets()",
                       "RandomSequence should expose the fixed three-packet material-filter skip");
        requirePresent(random, "sampleSeed = sampleSeed * 0x98a5741du + 0xacfbeaa7u;",
                       "The three-packet reservation should use the validated closed-form fifteen-step LCG jump");
        requirePresent(core, "RandomSequence materialFilterSequence = path.rng;",
                       "Each material segment should snapshot its FAS sequence immediately after the scatter packet");
        requirePresent(core, "path.rng.advanceThreeRand4Packets();",
                       "PathTracing must reserve exactly three rand4 packets for every material segment");
        requireOrdered(core, "RandomSequence materialFilterSequence = path.rng;",
                       "path.rng.advanceThreeRand4Packets();",
                       "The material CHS must receive the pre-advance sequence while the path reserves the same "
                       "dimensions unconditionally");
        requirePresent(materialPayload, "RandomSequence localFilterSequence = filterSequence;",
                       "Material resolution must draw from a by-value sequence copy");
        requirePresent(materialPayload, "float4 filterPacket0 = localFilterSequence.rand4();",
                       "Packet 0 should provide base color, metallic-roughness, emissive, and base-normal lanes");
        requirePresent(materialPayload, "filterPacket0.xyz",
                       "Packet 0 XYZ should feed the three core material textures");
        requirePresent(materialPayload, "filterPacket0.w", "Packet 0 W should feed the base normal");
        requirePresent(materialPayload, "float4 filterPacket1 = localFilterSequence.rand4();",
                       "Packet 1 should provide anisotropy and the three clearcoat lanes");
        requirePresent(materialPayload, "filterPacket1.x", "Packet 1 X should feed anisotropy");
        requirePresent(materialPayload, "filterPacket1.yzw",
                       "Packet 1 YZW should feed clearcoat factor, roughness, and normal");
        requirePresent(materialSampling, "float4 layerFilterRandomValues = filterSequence.rand4();",
                       "Packet 2 should be generated after packet 1 is dead");
        requirePresent(materialSampling, "MaterialTextureSlot.sheenColor",
                       "Packet 2 X should have a sheen-color consumer");
        requirePresent(materialSampling, "MaterialTextureSlot.sheenRoughness",
                       "Packet 2 Y should have a sheen-roughness consumer");
        requirePresent(materialSampling, "MaterialTextureSlot.transmission",
                       "Packet 2 Z should have a transmission consumer");
        requirePresent(materialSampling, "layerFilterRandomValues.x", "Packet 2 X should feed sheen color");
        requirePresent(materialSampling, "layerFilterRandomValues.y", "Packet 2 Y should feed sheen roughness");
        requirePresent(materialSampling, "layerFilterRandomValues.z", "Packet 2 Z should feed transmission");
        requireAbsent(materialSampling, "layerFilterRandomValues.w",
                      "Packet 2 W must remain the explicit twelfth padding lane");
        requireAbsent(materialSampling, "MaterialTextureSlot.occlusion",
                      "The unsampled occlusion slot must not consume a FAS lane");
        requirePresent(pathState, "public RandomSequence rng = {};",
                       "PathTracing path state should keep a per-pixel/per-frame random sequence");
        requireAbsent(sourceSection(pathState, "public struct PathState", "public void terminatePathState"),
                      "sampleIndex", "PathTracing path state must not carry camera sample state in fixed 1spp mode");
        requirePresent(pathState, "public float3 specularThroughput",
                       "PathTracing path state should track primary-specular throughput independently");
        requirePresent(pathState, "public float3 diffuseThroughput",
                       "PathTracing path state should track primary-diffuse throughput independently");
        requirePresent(pathState, "public float3 specularRadiance",
                       "PathTracing path state should accumulate primary-specular radiance independently");
        requirePresent(pathState, "public float3 diffuseRadiance",
                       "PathTracing path state should accumulate primary-diffuse radiance independently");
        requirePresent(core, "pathCombinedThroughput(path) * path.etaScale",
                       "PathTracing roulette should use eta-compensated combined split throughput");
        requirePresent(core, "initializePrimaryPathThroughput",
                       "PathTracing should split primary continuation throughput by BSDF component");
        requirePresent(core, "pathCombinedRadiance(path)", "PathTracing should merge split radiance only at output");
        requirePresent(core, "writePathTracingGuides(path.pixel, path.guides)",
                       "PathTracing should write all RR guides with the noisy color");
        requirePresent(pathState, "public PathTracingGuideState guides = {};",
                       "PathTracing should carry guide state with its camera path");
        requirePresent(resources, "RWTexture2D<float4> outputImage", "RR noisy color should use RGBA float storage");
        requirePresent(resources, "RWTexture2D<float> depthImage", "RR depth should use one float channel");
        requirePresent(resources, "RWTexture2D<float4> diffuseAlbedoImage",
                       "RR diffuse albedo should use linear float storage");
        requirePresent(resources, "RWTexture2D<float4> specularAlbedoImage",
                       "RR specular albedo should use linear float storage");
        requirePresent(resources, "RWTexture2D<float4> normalRoughnessImage",
                       "RR normal and roughness should share one packed texture");
        requirePresent(resources, "RWTexture2D<float2> motionVectorsImage",
                       "RR dense motion vectors should use two float channels");
        requirePresent(resources, "RWTexture2D<float> specularHitDistanceImage",
                       "RR specular hit distance should use one float channel");
        requireAbsent(resources, "disocclusion", "PathTracing must not generate a disocclusion guide");
        requireAbsent(resources, "motionVectors3D", "PathTracing must not generate experimental 3D motion vectors");
        requireAbsent(resources, "rayDirection", "PathTracing must not generate experimental ray-direction guides");
        requirePresent(guides, "gFrame.previousViewProjection",
                       "RR motion vectors should use the jitter-decoupled previous transform");
        requirePresent(hitSurface, "float3x4 worldToObject = WorldToObject3x4()",
                       "RT normal reconstruction should start from the inverse object transform");
        requirePresent(hitSurface, "transformRtObjectNormalToWorld(objectNormal)",
                       "RT normals should use inverse-transpose transformation under non-uniform instance scaling");
        requireAbsent(hitSurface, "transformRtObjectVectorToWorld(objectNormal)",
                      "RT normals must not use the direct object-to-world vector transform");
        requirePresent(guides, "guides.depth = saturate(clip.z",
                       "RR hardware depth should remain in Vulkan clip depth range");
        requirePresent(guides, "guides.normalRoughness = float4(normal, saturate(material.roughness))",
                       "RR should pack linear roughness in normal alpha");
        requirePresent(guides, "pathTracingGuideEnvBrdfApprox2",
                       "RR specular albedo should use NVIDIA's view-dependent EnvBRDF approximation");
        requirePresent(guides, "length(hitPosition - guides.primaryPosition)",
                       "RR specular hit distance should be measured in world space from the primary surface");
        requirePresent(materialPayload,
                       "public ResolvedMaterialBsdfComponents evaluateResolvedMaterialBsdfComponentsVariant<",
                       "Material payload should expose variant-aware BSDF component evaluation");
        requirePresent(materialPayload,
                       "public ResolvedMaterialDirectComponents evaluateResolvedMaterialDirectComponentsVariant<",
                       "Material payload should expose variant-aware direct-light component evaluation");
        requireAbsent(materialPayload, "evaluateResolvedMaterialDirectVariant",
                      "Material payload must not retain the dead aggregate direct-light wrapper");
        requireAbsent(materialTypes, "MaterialShadingResult",
                      "Material types must not retain the zero-consumer aggregate shading result");
        requireAbsent(materialPayload, "evaluateResolvedMaterialBsdfVariant",
                      "Material payload must not retain the dead variant aggregate BSDF wrapper");
        requireAbsent(materialPayload, "public float3 evaluateResolvedMaterialBsdf(",
                      "Material payload must not retain the dead default aggregate BSDF wrapper");
        requirePresent(core, "evaluateResolvedMaterialDirectComponentsVariant<RtMaterialLayerFlag(",
                       "PathTracing core must continue dispatching to direct-light component variants");
        requirePresent(materialPayload,
                       "evaluateResolvedMaterialBsdfComponentsVariant<LayerFlags>(payload, viewDirection, "
                       "scatter.direction)",
                       "Scatter sampling must continue evaluating live BSDF component variants");
        requireAbsent(core, "path.throughput", "PathTracing core must not keep the old monolithic throughput path");
        requireAbsent(core, "path.radiance", "PathTracing core must not keep the old monolithic radiance path");
        requirePresent(visibility, "RAY_FLAG_CULL_BACK_FACING_TRIANGLES",
                       "visibility rays should share material-ray single-sided culling");
        requirePresent(visibility, "uint(RtRayType.shadow), uint(RtRayType.count), uint(RtRayType.shadow)",
                       "visibility rays should route through the independent shadow ray type");

        requireAbsent(rtMaterial, "hasRtMaterialLayer", "The Shader material helper must not retain its dead layer query");
        requireAbsent(rtMaterial, "rtMaterialTextureId",
                      "The Shader material helper must not retain its unused texture-id forwarding wrapper");
        requireAbsent(rtMaterial, "rtMaterialFeaturePreviewColor",
                      "The Shader material helper must not retain its unused preview-color implementation");
        requirePresent(rtMaterial, "public bool hasRtMaterialFeature(",
                       "The live Shader feature query must remain part of the common material surface");
        requirePresent(anyHitPolicy, "hasRtMaterialFeature(material, RtMaterialFeatureFlag.doubleSided)",
                       "PathTracing any-hit policy must continue using the live Shader feature query");
        requirePresent(rtMaterial, "public RtMaterialTextureRef rtMaterialTextureRef(",
                       "The live dense texture-ref accessor must remain part of the common material surface");
        requirePresent(materialSampling, "rtMaterialTextureRef(textureRefs, material, slot)",
                       "Material sampling must continue using the live dense texture-ref accessor");
        requireExactlyOne(anyHitPolicy,
                          "hasRtMaterialFeature(material, RtMaterialFeatureFlag.alphaMask)",
                          "PathTracing any-hit must gate alpha reconstruction exactly once");
        requirePresent(anyHitPolicy,
                       "resolveAlphaCoverage(rtMaterialTextureRefs, material, hit.surface) < "
                       "material.alphaCutoff",
                       "PathTracing any-hit must apply alpha coverage directly after surface validation");
        requireAbsent(anyHitPolicy, "shouldIgnoreAlphaMaskedHit",
                      "PathTracing any-hit must not retain a one-call alpha-mask wrapper");
        requireAbsent(anyHitPolicy, "orientRtSurfaceForMaterial",
                      "Alpha coverage must not mutate the reconstructed any-hit shading frame");
        requirePresent(anyHitPolicy, "float3 viewDirection = normalize(-WorldRayDirection())",
                       "Any-hit must retain the view direction required for surface reconstruction");
        requirePresent(anyHitPolicy, "currentRtHitSurface(attributes, viewDirection)",
                       "Any-hit must retain canonical interpolated surface reconstruction");
        requirePresent(anyHitPolicy, "shouldIgnoreSingleSidedBackFace(material, frontFace)",
                       "Any-hit must retain material-aware single-sided back-face rejection");
        requirePresent(anyHitPolicy, "RtMaterialFeatureFlag.doubleSided",
                       "The back-face policy must continue honoring double-sided materials");
        requirePresent(anyHitPolicy, "RtMaterialFeatureFlag.volumeBoundary",
                       "The back-face policy must continue preserving volume boundaries");
        requirePresent(hitShaders, "orientRtSurfaceForMaterial(hit.surface, material, viewDirection)",
                       "Closest-hit reconstruction must retain material-facing surface orientation");
        requirePresent(hitShaders, "makeClosestHitInput",
                       "PathTracing hit shaders should prepare CHS closest-hit inputs");
        requireAbsent(hitShaders, "handleClosestHitWithPolicy",
                      "PathTracing should not keep wrapper-policy closest-hit contract");
        requireAbsent(chs, "RtHitAlphaPolicy", "PathTracing CHS variants should not specialize alpha policy");
        requireAbsent(hitShaders, "sampleDirectLighting", "PathTracing hit shaders must not own direct lighting");
        requireAbsent(hitShaders, "evaluateResolvedMaterialDirect",
                      "PathTracing hit shaders must not shade direct light");
        requireAbsent(hitShaders, "outputImage", "PathTracing hit shaders must not write the output image");
        requirePresent(chs, "public interface ICHS",
                       "PathTracing CHS contract should define the closest-hit interface");
        requireAbsent(anyHitEntry, "materialFilter", "PathTracing any-hit must not receive or consume FAS state");
        requireAbsent(hitShaders, "materialFilter",
                      "Alpha coverage and hit reconstruction must remain independent from FAS");
        requirePresent(closestHitEntry, "input.materialFilterSequence = payload.materialFilterSequence;",
                       "Closest hit should decode only its pre-reserved material-filter sequence before overwriting "
                       "shared payload slots");
        requirePresent(pathState, "public property RandomSequence materialFilterSequence",
                       "MaterialRayPayload should decode the transient material-filter sequence from shared output "
                       "slots without adding it to PathState");
        requirePresent(
            pathState, "public struct ResolvedMaterialRayPayload",
            "PathTracing should use a dedicated packed ray-transport record instead of the BSDF working record");
        requirePresent(pathState, "public void initializeMaterialRayPayload(",
                       "Material-ray invoke input should be encoded into shared result slots");
        requirePresent(pathState, "public void writeResolvedMaterialRayPayload(",
                       "Closest hit should overwrite the shared invoke slots with the resolved hit result");
        requirePresent(pathState, "packSnorm2x16(encoded)",
                       "Closest hit should encode ray-boundary directions with standard oct32 storage");
        requirePresent(pathState, "unpackSnorm2x16ToFloat(value)",
                       "Raygen should decode oct32 directions back to full-precision working vectors");
        requirePresent(pathState, "packUnorm2x16(saturate(value))",
                       "Closest hit should saturate and pair-pack bounded material scalars");
        requirePresent(pathState, "unpackUnorm2x16ToFloat(",
                       "Raygen should decode bounded material scalar pairs into full-precision working values");
        requirePresent(pathState, "anisotropyTangent - shadingNormal * dot(shadingNormal, anisotropyTangent)",
                       "Decoded anisotropy tangents should be reprojected onto the decoded shading-normal plane");
        requirePresent(core, "ResolvedMaterialPayload material = resolvedMaterialFromRayPayload(",
                       "Raygen should decode packed material transport before integration updates PathState");
        requirePresent(core, "resolvedMaterialScatterFromRayPayload(payload)",
                       "Raygen should decode packed scatter transport before integration updates PathState");
        requirePresent(core, "pathCurrentMediumIor(path)",
                       "Raygen should restore the current medium IOR while decoding the compact hit payload");
        requirePresent(core, "pathExteriorMediumIor(path)",
                       "Raygen should restore the exterior medium IOR while decoding the compact hit payload");
        auto const rayPayloadRecord = sourceSection(pathState, "public struct MaterialRayPayload",
                                                    "// Invoke input and hit output have disjoint lifetimes");
        requirePresent(rayPayloadRecord, "public ResolvedMaterialRayPayload resolved = {};",
                       "MaterialRayPayload should contain only the packed shared-lifetime transport record");
        requireAbsent(rayPayloadRecord, "public RandomSequence materialFilterSequence",
                      "MaterialRayPayload must not retain a dedicated RNG storage field after lifetime reuse");
        requireAbsent(rayPayloadRecord, "ResolvedMaterialPayload material",
                      "MaterialRayPayload must not embed the full BSDF working material");
        requireAbsent(rayPayloadRecord, "ResolvedMaterialScatter scatter",
                      "MaterialRayPayload must not embed the full working scatter record");
        requireAbsent(pathState, "public struct RayPayload",
                      "PathTracing should not retain the old shared material/visibility payload wrapper");
        requireAbsent(materialPayload, "visibilityRay",
                      "PathTracing material payload flags should not retain visibility-ray state");
        requireAbsent(materialPayload, "rayStateMask",
                      "PathTracing material payload should not preserve a shared ray-state mask");
        auto const resolvedMaterialRecord = sourceSection(materialPayload, "public struct ResolvedMaterialPayload",
                                                          "public struct ResolvedMaterialScatter");
        requirePresent(materialPayload, "[Flags]\npublic enum ResolvedMaterialFlag : uint",
                       "Resolved material bool, enum, and flag state should use a strong uint-backed flag enum");
        requirePresent(resolvedMaterialRecord, "public ResolvedMaterialFlag flags",
                       "Resolved material metadata should use the shared strong flag enum");
        requirePresent(resolvedMaterialRecord, "public property RtMaterialLayerFlag layerFlags",
                       "Resolved material layer flags should use a typed property over packed metadata");
        requirePresent(resolvedMaterialRecord, "public property AlphaMode alphaMode",
                       "Resolved material alpha mode should use a typed property over packed metadata");
        requirePresent(resolvedMaterialRecord, "public property bool frontFace",
                       "Resolved material front-face state should use a property over packed metadata");
        requireAbsent(resolvedMaterialRecord, "featureFlags",
                      "Resolved material should not copy header feature flags that have no downstream consumer");
        requireAbsent(resolvedMaterialRecord, "alphaCutoff",
                      "Resolved material should not copy the any-hit-only alpha cutoff");
        requireAbsent(resolvedMaterialRecord, "hitT",
                      "Resolved material should not retain unused hit distance beside full-precision position");
        requireAbsent(resolvedMaterialRecord, "public float alpha",
                      "Resolved material alpha should derive from baseColor.a");
        auto const persistentPathState =
            sourceSection(pathState, "public struct PathState", "public void terminatePathState");
        requireAbsent(persistentPathState, "materialFilterSequence",
                      "FAS must not add persistent RNG state to PathState");
        requirePresent(materialPayload, "RandomSequence filterSequence",
                       "Material payload resolution should receive the pre-reserved sequence by value rather than draw "
                       "from the live path RNG");
        requirePresent(materialSampling, "public float4 sampleMaterialTextureVariant(",
                       "RT material sampling should centralize the root-controlled FAS A/B policy");
        requirePresent(
            materialSampling, "if (kEnableFilterAfterShading)",
            "Only the enabled closest-hit variant should stochastically select a bilinear reconstruction tap");
        requirePresent(materialSampling, "gSceneTextures[textureRef.textureId].GetDimensions(width, height);",
                       "LOD0 FAS should derive the texel grid from the sampled texture");
        requirePresent(materialSampling, "stochasticBilinearTexelCenterUv(",
                       "Enabled FAS should select one bilinear tap and convert it to a texel-center UV");
        requirePresent(materialSampling, "gSceneTextures[textureRef.textureId].SampleLevel(uv, 0.0f);",
                       "Both FAS states should fetch exactly one nearest texel at mip zero");
        requirePresent(stochasticTextureFiltering, "selectStochasticFilterUpperTap(",
                       "FAS should use scalar remapping for bilinear tap selection");
        requirePresent(stochasticTextureFiltering, "uniformValue = selectUpper",
                       "The X decision should remap the scalar before it is reused for Y");
        requireAbsent(materialSampling, "ddx(", "RT material sampling should not use derivative footprints");
        requireAbsent(materialSampling, "ddy(", "RT material sampling should not use derivative footprints");
        requireAbsent(materialSampling, "RayCone", "First-stage FAS should not introduce ray cones");
        requireAbsent(materialSampling, "rayCone", "First-stage FAS should not introduce ray cones");
        requirePresent(chs, "let LayerFlags : RtMaterialLayerFlag",
                       "PathTracing CHS contract should expose one combined material-flag specialization target");
        requireAbsent(chs, "let EnableFilterAfterShading",
                      "PathTracing CHS must retain only the material-layer generic dimension");
        requirePresent(materialSampling, "kEnableFilterAfterShading",
                       "Material sampling should consume the common root link-time FAS constant");
        requireAbsent(chs, "RtBaseLobeVariant",
                      "PathTracing CHS contract should not retain a separate base-lobe specialization type");
        requirePresent(chs, "public extern struct CHS : ICHS;",
                       "PathTracing CHS contract should require C++ link-time type binding");
        requirePresent(chs, "resolveLitMaterialPayloadVariant",
                       "MaterialCHS should resolve lit material payloads through the layer-flag variant");
        requirePresent(pathTracingNode, "makePathTracingRaygenVariantDesc",
                       "PathTracing should isolate bounce and roulette assignments to raygen requests");
        requirePresent(pathTracingNode, "makePathTracingClosestHitVariantDesc",
                       "PathTracing should combine FAS and material CHS assignments only for closest-hit requests");
        requirePresent(pathTracingNode, "\"MaterialCHS<RtMaterialLayerFlag({}u)>\"",
                       "CHS variants should remain keyed only by the material layer flags");
        requireAbsent(pathTracingNode, "makePathTracingSyntheticRootSource",
                      "PathTracing node should no longer generate synthetic closest-hit wrappers");
        requireAbsent(pathTracingNode, "RtHitPolicy_",
                      "PathTracing node should no longer generate shader-side policy structs");
        requireAbsent(pathTracingNode, ".linkVariants",
                      "Single-entry compile requests must not retain the old secondary link-variant list");
        requirePresent(pathTracingNode, "compileProgramsByFile",
                       "PathTracing should submit all required single-entry shaders through one batch compiler call");
        requirePresent(pathTracingNode, "renderer/pathTracing/raygen",
                       "PathTracing should compile its raygen entry from its own file");
        requirePresent(pathTracingNode, "renderer/pathTracing/miss",
                       "PathTracing should compile its non-variant miss entry from its own file");
        requirePresent(pathTracingNode, "renderer/pathTracing/shadowMiss",
                       "PathTracing should compile its fixed shadow miss entry from its own file");
        requirePresent(pathTracingNode, "renderer/pathTracing/shadowAnyHit",
                       "PathTracing should compile its fixed shadow any-hit entry from its own file");
        requirePresent(pathTracingNode, "renderer/pathTracing/anyHit",
                       "PathTracing should compile its non-variant any-hit entry from its own file when required");
        requirePresent(pathTracingNode, "renderer/pathTracing/closestHit",
                       "PathTracing should compile each closest-hit variant from its own file");
        requirePresent(pathTracingNode, "permutation.key.bsdf",
                       "PathTracing node should derive CHS variants from BSDF keys, not full hit-group keys");
        requirePresent(pathTracingNode, "createRayTracingPipeline(\n        programs.raygen,",
                       "PathTracing should use raygen as the explicit canonical reflection program");
        requirePresent(pathTracingNode, "assembly.groups.reserve(4u + hitSbtPlan.permutations.size());",
                       "PathTracing pipeline should contain four fixed groups plus material permutations");
        requirePresent(pathTracingNode, ".name = std::string{kPathTracingMaterialMissGroupName}",
                       "PathTracing pipeline should expose a stable material miss group");
        requirePresent(pathTracingNode, ".name = std::string{kPathTracingShadowMissGroupName}",
                       "PathTracing pipeline should expose a stable shadow miss group");
        requirePresent(pathTracingNode, ".name = std::string{kPathTracingShadowHitGroupName}",
                       "PathTracing pipeline should expose one fixed shadow hit group");
        requirePresent(pathTracingNode, ".anyHitEntryPoint = \"ahShadow\"",
                       "the fixed shadow hit group should contain only the shadow any-hit stage");
        requirePresent(pathTracingNode, ".logicalEntryPointName = \"msMaterial\"",
                       "material miss should use its dedicated entry name");
        requirePresent(pathTracingNode, ".logicalEntryPointName = \"msShadow\"",
                       "shadow miss should use its dedicated entry name");
        requirePresent(pathTracingNode, ".logicalEntryPointName = \"ahMaterialPolicy\"",
                       "material any-hit permutations should use the material-policy entry name");
        requirePresent(pathTracingNode, ".logicalEntryPointName = \"ahShadow\"",
                       "shadow records should use the compact fixed any-hit entry");
        requireAbsent(pathTracingNode, "\"msMain\"",
                      "PathTracing pipeline should not retain the old shared miss entry name");
        requireAbsent(pathTracingNode, "\"ahMain\"",
                      "PathTracing pipeline should not retain the old shared any-hit entry name");

        auto const pathTracingSbt = sourceSection(
            pathTracingNode, "[[nodiscard]] nr::rhi::ShaderBindingTable createPathTracingShaderBindingTable(",
            "[[nodiscard]] std::shared_ptr<nr::renderer::PipelineRuntime<nr::rhi::RayTracingPipeline>> &");
        requirePresent(pathTracingSbt, "auto const missRecords = std::array{",
                       "PathTracing SBT should build an explicit miss-record array");
        requireOrdered(pathTracingSbt, "pipeline.shaderGroupIndex(kPathTracingMaterialMissGroupName)",
                       "pipeline.shaderGroupIndex(kPathTracingShadowMissGroupName)",
                       "miss records must be ordered [material, shadow]");
        requirePresent(pathTracingSbt, "hitRecords.push_back(nr::rhi::ShaderBindingTableRecordDesc{",
                       "each logical geometry should expand into explicit physical hit records");
        requirePresent(pathTracingSbt, ".groupIndex = shadowHitGroupIndex",
                       "every logical geometry should append the shared shadow hit group");
        requirePresent(pathTracingSbt, "hitRecords.size() == physicalHitRecordCount",
                       "physical SBT expansion should be checked against the ray-type count");
        requireAbsent(pathTracingSbt,
                      ".stride =", "SBT byte stride must remain RHI-computed rather than encode the ray-type count");
        requirePresent(pathTracingNode, ".sampledImage(\"gEnvironmentMap\"",
                       "PathTracing should bind the renderer-global environment through reflection");
        requirePresent(pathTracingNode, ".pushConstants(\"gEnvironment\"",
                       "PathTracing should bind environment parameters through reflection");
        requirePresent(pathTracingNode, "addressModeU = vk::SamplerAddressMode::eRepeat",
                       "lat-long environment longitude should repeat");
        requirePresent(pathTracingNode, "addressModeV = vk::SamplerAddressMode::eClampToEdge",
                       "lat-long environment latitude should clamp at poles");
        auto const missingInputs = pathTracingNode.find("if (!frameInputs.has_value())");
        auto const environmentBinding = pathTracingNode.find("auto const environmentMap", missingInputs);
        nr::test::require(missingInputs != std::string::npos && environmentBinding != std::string::npos &&
                              missingInputs < environmentBinding,
                          "missing TLAS/sideband should retain the clear path before environment binding");
        requirePresent(materialPayload, "ResolvedMaterialPayload",
                       "Common material payload helper should define resolved hit material data");
        requirePresent(materialPayload, "public struct BaseSurfaceBsdfLobe<",
                       "Common material payload helper should expose the shared base/transmission lobe");
        requirePresent(
            materialPayload, "public struct BaseGgxDistribution<let LayerFlags",
            "Common material payload helper should derive its GGX distribution from combined material flags");
        requirePresent(materialPayload, "alphaT = lerp(isotropicAlpha, 1.0f, strength * strength)",
                       "Anisotropic GGX should use the approved alphaT mapping");
        requirePresent(materialPayload, "visibleHalfVectorPdf",
                       "Anisotropic evaluate, PDF and VNDF sampling should share the base GGX helper");
        requireAbsent(materialBsdf, "evaluateMetallicRoughnessDiffuse",
                      "The common BSDF surface must not retain the dead metallic-roughness diffuse evaluator");
        requireAbsent(materialBsdf, "evaluateMetallicRoughnessSpecular",
                      "The common BSDF surface must not retain the dead metallic-roughness specular evaluator");
        requireAbsent(materialBsdf, "evaluateMetallicRoughnessBsdf",
                      "The common BSDF surface must not retain the dead combined metallic-roughness evaluator");
        requireAbsent(materialBsdf, "evaluateClearcoatDirect",
                      "The common BSDF surface must not retain the dead clearcoat direct-light leaf");
        requirePresent(materialBsdf, "public float3 metallicRoughnessF0(",
                       "The live metallic-roughness F0 helper must remain available to guide generation");
        requirePresent(guides, "return metallicRoughnessF0(core);",
                       "PathTracing guides must continue deriving specular albedo from metallic-roughness F0");
        requirePresent(materialBsdf, "public float3 fresnelSchlick(",
                       "The live vector Fresnel helper must remain in the shared BSDF implementation");
        requirePresent(materialBsdf, "public float ggxDistribution(",
                       "The live GGX distribution helper must remain in the shared BSDF implementation");
        requirePresent(materialBsdf, "public float evaluateClearcoatSpecular(",
                       "The live clearcoat specular kernel must remain in the shared BSDF implementation");
        requirePresent(materialPayload, "evaluateClearcoatSpecular(payload.layers.clearcoatRoughness",
                       "The material payload must continue using the clearcoat specular kernel");
        requirePresent(materialPayload, "clearcoatGgxEnergyTerms(payload.layers, clearcoatSpecularNormal",
                       "The material payload must continue using clearcoat energy compensation");
        requirePresent(materialPayload, "clearcoatBaseAttenuation(payload.layers, clearcoatSpecularNormal",
                       "The material payload must continue attenuating lower layers with clearcoat energy");
        requirePresent(materialBsdf, "GgxSpecularEnergyTerms",
                       "GGX shading should expose Spec.W compensation and Spec.E directional albedo");
        requirePresent(materialBsdf, "ggxDirectionalAlbedoAnalytic",
                       "GGX energy compensation should use the resource-free UE analytic lookup");
        requirePresent(materialBsdf, "noL * lenV + noV * lenL",
                       "Isotropic GGX should use joint correlated Smith masking-shadowing");
        requirePresent(materialPayload, "energyWeight * fresnel * distribution * geometry",
                       "Base GGX reflection should apply Spec.W to the single-scattering lobe");
        requirePresent(materialPayload, "reflectionImportance = energy.E",
                       "Base lobe selection should use Spec.E directional albedo");
        requirePresent(materialPayload, "if (!hasActiveTransmission(payload))",
                       "Opaque Spec.W/Spec.E should not be reused for active glass energy compensation");
        requirePresent(materialPayload, "clearcoatBaseAttenuation", "Clearcoat Spec.E should attenuate lower layers");
        requirePresent(materialPayload, "adjustMaterialPayloadSpecularNormal",
                       "Specular lobes should derive a view-dependent normal without replacing the raw shading normal");
        requirePresent(materialPayload, "MaterialPayloadReflectionEvaluation",
                       "Folded reflection should carry both unprojected and projected evaluation kernels");
        requirePresent(materialPayload, "materialPayloadMirrorReflectionDirection",
                       "Reflection folding should expose its geometry-plane mirror isometry");
        requirePresent(materialPayload, "materialPayloadFoldReflectionDirection",
                       "Base, sheen, and clearcoat samples should fold into the exterior geometry hemisphere");
        requirePresent(materialPayload, "evaluateFoldedReflection",
                       "Reflection evaluation should sum the exterior direction and mirrored preimage");
        requirePresent(materialPayload, "foldedReflectionPdf",
                       "Reflection PDFs should use the same two-preimage push-forward as evaluation");
        requirePresent(materialPayload, "materialPayloadGeometrySupportsReflection",
                       "Folded reflection queries should retain exterior-only geometry support");
        requirePresent(materialPayload, "materialPayloadGeometrySupportsTransmission",
                       "transmission should use the complementary view-facing geometric hemisphere contract");
        requirePresent(materialPayload, "bsdf.diffuseProjected / pdf",
                       "continuation throughput should use the diffuse lobe's own projected contribution");
        requirePresent(materialPayload, "bsdf.specularProjected / pdf",
                       "continuation throughput should use the specular lobe's own projected contribution");
        requirePresent(hitSurface, "surface.tangent = -surface.tangent;",
                       "double-sided orientation should reverse tangent with normal and tangent sign");
        requireAbsent(materialPayload, "TransmissionBsdfLobe",
                      "Transmission must not remain an independent top-level lobe");
        requirePresent(materialPayload, "scatterDelta",
                       "Common material scatter should carry the delta-lobe flag in packed metadata");
        requirePresent(materialPayload, "if (scatter.delta)",
                       "Common material scatter sampling should keep delta lobes out of continuous PDF mixing");
        requirePresent(materialPayload, "public ResolvedMaterialScatter sampleResolvedMaterialScatterVariant<",
                       "Common material payload helper should expose variant-aware scatter sampling");
        requirePresent(chs, "sampleResolvedMaterialScatterVariant<LayerFlags>(result.material, input.viewDirection",
                       "PathTracing closest-hit material evaluation must continue using variant scatter sampling");
        requirePresent(materialPayload, "resolvedMaterialCombinedPdfVariant",
                       "Common material payload helper should expose variant-aware combined PDFs");
        requirePresent(materialPayload, "public static const RtMaterialLayerFlag kResolvedMaterialVariantMask =",
                       "The deferred v1 material entry points must retain their exact default variant mask");
        requireExactlyOne(materialPayload, "public ResolvedMaterialScatter sampleResolvedMaterialScatter(",
                          "Common material payload helper must retain exactly one default v1 scatter entry point");
        requirePresent(materialPayload,
                       "return sampleResolvedMaterialScatterVariant<kResolvedMaterialVariantMask>(payload, "
                       "viewDirection, randomValues);",
                       "The default v1 scatter entry point must continue forwarding through the exact variant mask");
    }};

const nr::test::CaseRegistrar accumulateShaderCase{
    "accumulate shader owns capped current-frame weight", [] {
        auto shader = readProjectFile("shader/renderer/accumulate.slang");
        auto node = readProjectFile("src/renderPasses/Accumulate/nrAccumulateNode.cpp");

        requirePresent(shader, "Texture2D<float4> gCurrentColor", "Accumulate shader should read current frame color");
        requirePresent(shader, "Texture2D<float4> gHistoryColor",
                       "Accumulate shader should read previous history color");
        requirePresent(shader, "RWTexture2D<float4> gAccumulatedColor",
                       "Accumulate shader should write history output");
        requirePresent(shader, "max(exactWeight, cappedWeight)", "Accumulate shader should clamp current-frame weight");
        requirePresent(node, "std::array<nr::renderer::RetainedImageState, 2u> historyStates{}",
                       "Accumulate history slots should use retained image state tracking");
        requirePresent(node, ".retainedState = std::ref(state)",
                       "Accumulate history imports should attach retained state to the graph");
        requireAbsent(node, "ImageLayoutIntent::ShaderReadOnly",
                      "Accumulate should not hard-code previous-slot layout outside retained state");
    }};

const nr::test::CaseRegistrar retainedImportedImageStateCase{
    "renderpasses use retained state for renderer-persistent imported images", [] {
        auto present = readProjectFile("src/renderPasses/Present/nrPresentNode.cpp");
        auto accumulate = readProjectFile("src/renderPasses/Accumulate/nrAccumulateNode.cpp");
        auto pathTracing = readProjectFile("src/renderPasses/PathTracing/nrPathTracingNode.cpp");
        auto ui = readProjectFile("src/renderPasses/Ui/nrUiNode.cpp");
        auto rendererInterface = readProjectFile("src/renderer/nrRenderer.ixx");

        requirePresent(rendererInterface, "importRetainedStorageColor",
                       "NodeBuildContext should expose a retained imported storage-color helper");
        requirePresent(present, "RetainedImageState convertedColorState",
                       "Present should retain converted-color image state across frames");
        requirePresent(present, "convertedColorState.reset()",
                       "Present should reset converted-color state when the image is recreated");
        requirePresent(present, "context.importRetainedStorageColor",
                       "Present.ConvertedColor should use retained import tracking");
        requirePresent(accumulate, "std::array<nr::renderer::RetainedImageState, 2u> historyStates{}",
                       "Accumulate history ping-pong images should each have retained state");
        requirePresent(accumulate, "runtime.historyStates[slot].reset()",
                       "Accumulate should reset retained history state on image recreation");
        requirePresent(pathTracing, "std::array<PathTracingGuideFrameSlot, nr::maxFrameInFlight> guideFrameSlots{}",
                       "PathTracing should own one complete RR guide set per frame in flight");
        requirePresent(pathTracing, ".retainedState = std::ref(state)",
                       "PathTracing persistent guides should attach retained layout and ownership state");
        requirePresent(pathTracing, "frameSlot.states[guideResourceIndex].reset()",
                       "PathTracing should reset retained guide state when images are recreated");
        requirePresent(ui, "nr::renderer::RetainedImageState state{}",
                       "Ui texture entries should use the shared retained image state object");
        requirePresent(ui, "textureEntry.state.layout = nr::renderer::ImageLayoutIntent::ShaderReadOnly",
                       "Ui upload completion should seed texture retained layout");
        requireAbsent(ui, "currentLayout", "Ui should not keep an ad-hoc currentLayout field");
    }};

const nr::test::CaseRegistrar skeletonPatchCapabilityCase{
    "all rtobject nodes expose exact patch-only Skeleton materialization", [] {
        auto light = readProjectFile("src/renderPasses/LightPrepare/nrLightPrepareNode.cpp");
        auto accumulate = readProjectFile("src/renderPasses/Accumulate/nrAccumulateNode.cpp");
        auto present = readProjectFile("src/renderPasses/Present/nrPresentNode.cpp");
        auto presentShader = readProjectFile("shader/renderer/presentConvert.slang");
        auto path = readProjectFile("src/renderPasses/PathTracing/nrPathTracingNode.cpp");
        auto dlss = readProjectFile("src/renderPasses/DlssRayReconstruction/nrDlssRayReconstructionNode.cpp");
        auto ui = readProjectFile("src/renderPasses/Ui/nrUiNode.cpp");
        auto asInterface =
            readProjectFile("src/renderPasses/AccelerationStructureBuild/nrAccelerationStructureBuildNode.ixx");
        auto asSource =
            readProjectFile("src/renderPasses/AccelerationStructureBuild/nrAccelerationStructureBuildNode.cpp");
        auto renderer = readProjectFile("src/renderer/nrRenderer.cpp");
        auto pathInterface = readProjectFile("src/renderPasses/PathTracing/nrPathTracingNode.ixx");
        auto dlssInterface = readProjectFile("src/renderPasses/DlssRayReconstruction/nrDlssRayReconstructionNode.ixx");
        auto uiInterface = readProjectFile("src/renderPasses/Ui/nrUiNode.ixx");
        auto lightHit = sourceSection(light, "bool LightPrepareNode::materializeRenderGraphSkeleton(",
                                      "void LightPrepareNode::materializeCurrentFrame(");
        auto accumulateHit = sourceSection(accumulate, "bool AccumulateNode::materializeRenderGraphSkeleton(",
                                           "void AccumulateNode::materializeCurrentFrame(");
        auto accumulateCold = sourceSection(accumulate, "void AccumulateNode::materializeCurrentFrame(",
                                            "void AccumulateNode::shutdown(");
        auto presentHit = sourceSection(present, "bool PresentNode::materializeRenderGraphSkeleton(",
                                        "void PresentNode::materializeCurrentFrame(");
        auto presentPushConstantContract = sourceSection(
            present, "struct PresentConvertPushConstants", "inline constexpr std::uint32_t kOutputEncodingLinear");
        auto presentFormatConversion = sourceSection(
            present, "[[nodiscard]] std::optional<PresentFormatConversion> resolvePresentFormatConversion(",
            "[[nodiscard]] std::unique_ptr<PresentRuntimeState> makePresentRuntime(");
        auto presentSdrFormatConversion = sourceSection(
            presentFormatConversion, "case vk::Format::eB8G8R8A8Srgb:",
            "case vk::Format::eA2B10G10R10UnormPack32:");
        auto presentHdr10FormatConversion = sourceSection(
            presentFormatConversion, "case vk::Format::eA2B10G10R10UnormPack32:",
            "case vk::Format::eR16G16B16A16Sfloat:");
        auto presentScRgbFormatConversion = sourceSection(
            presentFormatConversion, "case vk::Format::eR16G16B16A16Sfloat:", "default:");
        auto presentDivideRoundUp = sourceSection(
            present, "[[nodiscard]] std::uint32_t divideRoundUp(",
            "[[nodiscard]] vk::DeviceSize presentReadbackTexelBlockByteSize(");
        auto presentReadbackContract = sourceSection(
            present, "[[nodiscard]] vk::DeviceSize presentReadbackTexelBlockByteSize(",
            "[[nodiscard]] bool supportsLinearExrScreenshotFormat(");
        auto presentColdReadback = sourceSection(
            present, "[[nodiscard]] nr::renderer::GraphPassHandle addPresentReadbackCopyPass(",
            "struct ExrHalfRgba");
        auto presentStructuralSnapshot = sourceSection(
            present, "std::optional<nr::renderer::NodeRuntime::StructuralSnapshot> PresentNode::structuralSnapshot(",
            "bool PresentNode::materializeRenderGraphSkeleton(");
        auto presentShaderBeforeUi = sourceSection(presentShader, "void presentConvertMain(",
                                                   "if (effectiveUiOpacity > 0.0f)");
        auto presentShaderUiBranch = sourceSection(presentShader, "if (effectiveUiOpacity > 0.0f)",
                                                   "float3 convertedRgb =");
        auto presentShaderAfterUi = sourceSection(presentShader, "float3 convertedRgb =", "}");
        auto presentShaderPqEncode = sourceSection(presentShader, "float pqSignalFromNormalized(",
                                                  "// Inverse ST 2084 / PQ");
        auto presentShaderPqDecode = sourceSection(presentShader, "float normalizedFromPqSignal(",
                                                  "float3 nitsToSt2084Pq(");
        auto pathHit = sourceSection(path, "bool PathTracingNode::materializeRenderGraphSkeleton(",
                                     "void PathTracingNode::materializeCurrentFrame(");
        auto dlssHit = sourceSection(dlss, "bool DlssRayReconstructionNode::materializeRenderGraphSkeleton(",
                                     "void DlssRayReconstructionNode::materializeCurrentFrame(");
        auto dlssCold = sourceSection(dlss, "void DlssRayReconstructionNode::materializeCurrentFrame(",
                                      "void DlssRayReconstructionNode::shutdown(");
        auto dlssPrepareCallback = sourceSection(
            dlss, "[[nodiscard]] nr::renderer::PassPrepareCallback makeDlssPrepareCallback(",
            "[[nodiscard]] nr::renderer::PassRecordCallback makeDlssRecordCallback(");
        auto dlssRecordCallback = sourceSection(
            dlss, "[[nodiscard]] nr::renderer::PassRecordCallback makeDlssRecordCallback(",
            "} // namespace nr::renderPasses::detail");
        auto dlssStructuralSnapshot = sourceSection(
            dlss, "structuralSnapshot(const NodeFrameParameters &frameParameters) const",
            "bool DlssRayReconstructionNode::materializeRenderGraphSkeleton(");
        auto dlssPublication = sourceSection(dlss, "auto publishedColor = outputColor;",
                                             "void DlssRayReconstructionNode::shutdown(");
        auto uiHit =
            sourceSection(ui, "bool UiNode::materializeRenderGraphSkeleton(", "void UiNode::materializeCurrentFrame(");
        auto uiDrawPreparation = sourceSection(ui, "[[nodiscard]] UiDrawFramePayload prepareUiDrawFrame(",
                                               "} // namespace nr::renderPasses::detail");
        auto asHit = sourceSection(asSource, "bool AccelerationStructureBuildNode::materializeRenderGraphSkeleton(",
                                   "void AccelerationStructureBuildNode::materializeCurrentFrame(");
        auto presentCold = sourceSection(present, "void PresentNode::materializeCurrentFrame(",
                                         "void PresentNode::advanceContinuations(");
        auto presentAdvance =
            sourceSection(present, "void PresentNode::advanceContinuations(", "void PresentNode::flushContinuations(");
        auto asCold = sourceSection(asSource, "void AccelerationStructureBuildNode::materializeCurrentFrame(",
                                    "} // namespace nr::renderPasses");

        requirePresent(light, "scene.tryGetLightAsset(packet.light)",
                       "LightPrepare must resolve each packet through the CPU Scene light registry");
        requirePresent(light, "lightRecord->get().cpuReady",
                       "LightPrepare must require ready CPU light authoring data");
        requirePresent(light, "packLightRecord(lightRecord->get().cpu, packet)",
                       "LightPrepare must pack its per-frame GPU payload from CPU light data and instance packets");
        requireAbsent(light, "lightRecord->get().gpu",
                      "LightPrepare must not depend on a Scene-owned light GPU lifecycle");
        requirePresent(light, "nr::renderer::RenderGraphSkeletonPatchContext& context",
                       "LightPrepare hit path must use patch-only context");
        requirePresent(light, "context.patchFrameData", "LightPrepare hit path must patch current upload payload");
        requirePresent(accumulate, "nr::renderer::ComputePassPatchBuilder",
                       "Accumulate hit path must patch compute callbacks and bindings");
        requirePresent(accumulate, "context.patchResource", "Accumulate hit path must patch ping-pong imports");
        requirePresent(accumulateHit, "detail::prepareAccumulateFramePlan(",
                       "Accumulate Skeleton hits must use the canonical temporal frame plan");
        requirePresent(accumulateCold, "detail::prepareAccumulateFramePlan(",
                       "Accumulate cold builds must use the canonical temporal frame plan");
        requirePresent(accumulateHit, "detail::commitAccumulateFramePlan(*runtime_, framePlan);",
                       "Accumulate Skeleton hits must commit canonical sample-count state");
        requirePresent(accumulateCold, "detail::commitAccumulateFramePlan(*runtime_, framePlan);",
                       "Accumulate cold builds must commit canonical sample-count state");
        requirePresent(present, "nr::renderer::ComputePassPatchBuilder",
                       "Present hit path must patch compute callbacks and bindings");
        requirePresent(present, "patchCopyImageToBuffer", "Present hit path must patch readback copies");
        requirePresent(present, "patchCopyImageToImage", "Present hit path must patch the swapchain copy");
        requirePresent(presentPushConstantContract, "std::is_standard_layout_v<PresentConvertPushConstants>",
                       "Present CPU push constants must retain a standard-layout ABI");
        requirePresent(presentPushConstantContract, "sizeof(PresentConvertPushConstants) == 24u",
                       "Present CPU push constants must remain exactly 24 bytes");
        requirePresent(presentPushConstantContract, "offsetof(PresentConvertPushConstants, width) == 0u",
                       "Present CPU width must remain at byte zero");
        requirePresent(presentPushConstantContract, "offsetof(PresentConvertPushConstants, height) == 4u",
                       "Present CPU height must remain at byte four");
        requirePresent(presentPushConstantContract, "offsetof(PresentConvertPushConstants, swizzleBgr) == 8u",
                       "Present CPU swizzle flag must remain at byte eight");
        requirePresent(presentPushConstantContract, "offsetof(PresentConvertPushConstants, outputEncoding) == 12u",
                       "Present CPU output encoding must remain at byte twelve");
        requirePresent(presentPushConstantContract, "offsetof(PresentConvertPushConstants, toneMapping) == 16u",
                       "Present CPU tone mapping must remain at byte sixteen");
        requirePresent(presentPushConstantContract, "offsetof(PresentConvertPushConstants, uiOpacity) == 20u",
                       "Present CPU UI opacity must remain at byte twenty");
        requireAbsent(presentPushConstantContract, "<= nr::rhi::kMaxPushConstantBytes",
                      "Present must not substitute a fuzzy maximum-size check for its exact CPU/shader ABI");
        requirePresent(presentSdrFormatConversion, "case vk::Format::eB8G8R8A8Srgb:",
                       "Present SDR conversion must accept the BGRA8 sRGB format");
        requirePresent(presentSdrFormatConversion, "case vk::Format::eB8G8R8A8Unorm:",
                       "Present SDR conversion must accept the BGRA8 UNORM format");
        requirePresent(presentSdrFormatConversion, "case vk::Format::eR8G8B8A8Srgb:",
                       "Present SDR conversion must accept the RGBA8 sRGB format");
        requirePresent(presentSdrFormatConversion, "case vk::Format::eR8G8B8A8Unorm:",
                       "Present SDR conversion must accept the RGBA8 UNORM format");
        requireOrdered(presentSdrFormatConversion, "case vk::Format::eR8G8B8A8Unorm:",
                       "colorSpace != vk::ColorSpaceKHR::eSrgbNonlinear",
                       "All four SDR formats must share the exact sRGB-nonlinear color-space boundary");
        requireOrdered(presentSdrFormatConversion, "colorSpace != vk::ColorSpaceKHR::eSrgbNonlinear",
                       "return std::nullopt",
                       "Present must reject RGBA8 and BGRA8 paired with a non-SDR color space");
        requirePresent(
            presentSdrFormatConversion,
            ".swizzleBgr = format == vk::Format::eB8G8R8A8Srgb || format == vk::Format::eB8G8R8A8Unorm",
            "Present must keep BGRA swizzle as the only semantic difference inside the shared SDR format group");
        requireOrdered(presentSdrFormatConversion, "return std::nullopt", "return PresentFormatConversion",
                       "Present must return an SDR conversion only after the exact color-space check succeeds");
        requireOrdered(presentHdr10FormatConversion, "isHdr10SwapchainColorSpace(colorSpace)",
                       "return PresentFormatConversion",
                       "Present must accept packed 10-bit output only for the exact HDR10 color-space helper");
        requireOrdered(presentHdr10FormatConversion, "return PresentFormatConversion", "return std::nullopt",
                       "Present must reject packed 10-bit output paired with a non-HDR10 color space");
        requireOrdered(presentScRgbFormatConversion, "isScRgbSwapchainColorSpace(colorSpace)",
                       "return PresentFormatConversion",
                       "Present must accept float16 output only for the exact scRGB color-space helper");
        requireOrdered(presentScRgbFormatConversion, "return PresentFormatConversion", "return std::nullopt",
                       "Present must reject float16 output paired with a non-scRGB color space");
        requirePresent(present, "inline constexpr std::uint32_t kPresentThreadGroupSize = 16u",
                       "Present CPU dispatches must share one file-local thread-group dimension");
        requirePresent(presentHit, "detail::divideRoundUp(conversionExtent.width, detail::kPresentThreadGroupSize)",
                       "Present Skeleton dispatch must use the shared thread-group dimension");
        requirePresent(presentCold, "detail::divideRoundUp(conversionExtent.width, detail::kPresentThreadGroupSize)",
                       "Present cold dispatch must use the shared thread-group dimension");
        requireAbsent(present, "constexpr auto kThreadGroupSize = 16u",
                      "Present dispatch callbacks must not duplicate their thread-group constant");
        requirePresent(presentShader, "[numthreads(16, 16, 1)]",
                       "Present shader local size must remain synchronized with the CPU dispatch constant");
        requirePresent(presentShader, "static const float kPqM1 = 2610.0f / 16384.0f",
                       "Present shader must own one file-local PQ m1 constant");
        requirePresent(presentShader, "static const float kPqM2 = 2523.0f / 32.0f",
                       "Present shader must own one file-local PQ m2 constant");
        requirePresent(presentShader, "static const float kPqC1 = 3424.0f / 4096.0f",
                       "Present shader must own one file-local PQ c1 constant");
        requirePresent(presentShader, "static const float kPqC2 = 2413.0f / 128.0f",
                       "Present shader must own one file-local PQ c2 constant");
        requirePresent(presentShader, "static const float kPqC3 = 2392.0f / 128.0f",
                       "Present shader must own one file-local PQ c3 constant");
        requireAbsent(presentShaderPqEncode, "static const float m1",
                      "Present PQ encoding must not duplicate function-local domain constants");
        requireAbsent(presentShaderPqDecode, "static const float m1",
                      "Present PQ decoding must not duplicate function-local domain constants");
        requirePresent(presentShaderPqEncode, "pow(saturate(y), kPqM1)",
                       "Present PQ encoding must use the shared m1 domain constant");
        requirePresent(presentShaderPqDecode, "1.0f / kPqM2",
                       "Present PQ decoding must use the shared m2 domain constant");
        requirePresent(presentDivideRoundUp,
                       "value / divisor + static_cast<std::uint32_t>(value % divisor != 0u)",
                       "Present dispatch division must avoid overflowing value + divisor - 1");
        requireAbsent(presentDivideRoundUp, "value + divisor - 1u",
                      "Present dispatch division must not retain overflow-prone addition");
        requirePresent(presentReadbackContract, "case vk::Format::eR8G8B8A8Unorm:",
                       "Present readback must define the supported 32-bit texel block formats");
        requirePresent(presentReadbackContract, "case vk::Format::eA2B10G10R10UnormPack32:",
                       "Present readback must define the supported packed 32-bit texel block formats");
        requireOrdered(presentReadbackContract, "case vk::Format::eA2R10G10B10UnormPack32:", "return 4u;",
                       "Present readback must size every supported 32-bit texel block as four bytes");
        requirePresent(presentReadbackContract, "case vk::Format::eR16G16B16A16Sfloat:",
                       "Present readback must define the supported 64-bit texel block format");
        requireOrdered(presentReadbackContract, "case vk::Format::eR16G16B16A16Sfloat:", "return 8u;",
                       "Present readback must size the supported 64-bit texel block as eight bytes");
        requirePresent(presentReadbackContract, "case vk::Format::eR32G32B32A32Sfloat:",
                       "Present readback must define the supported 128-bit texel block format");
        requireOrdered(presentReadbackContract, "case vk::Format::eR32G32B32A32Sfloat:", "return 16u;",
                       "Present readback must size the supported 128-bit texel block as sixteen bytes");
        requirePresent(presentReadbackContract, "Present readback unsupported format for size estimation",
                       "Present readback must reject formats outside its explicit non-compressed contract");
        requireOrdered(presentReadbackContract, "height <= maximum / width",
                       "auto const texelCount = width * height",
                       "Present readback must check extent multiplication before multiplying");
        requireOrdered(presentReadbackContract, "texelBlockByteSize <= maximum / texelCount",
                       "return texelCount * texelBlockByteSize",
                       "Present readback must check byte-size multiplication before multiplying");
        requirePresent(presentReadbackContract, "target.offset % texelBlockByteSize == 0u",
                       "Present readback must reject offsets not aligned to the format texel block");
        requireOrdered(presentReadbackContract, "requiredBytes <= maximum - target.offset",
                       "auto const rangeEnd = target.offset + requiredBytes",
                       "Present readback must check range-end addition before adding");
        requirePresent(presentReadbackContract, "target.offset <= buffer.size() && rangeEnd <= buffer.size()",
                       "Present readback must reject overflow while allowing a range ending exactly at buffer tail");
        requirePresent(presentReadbackContract, "buffer.valid()",
                       "Present readback must validate the destination buffer at its trusted boundary");
        requirePresent(presentReadbackContract, "vk::BufferUsageFlagBits::eTransferDst",
                       "Present readback must validate transfer-destination usage at its trusted boundary");
        requirePresent(presentReadbackContract, "buffer.mapped() != nullptr",
                       "Present readback must validate persistent host mapping at its trusted boundary");
        requirePresent(presentColdReadback, "validatePresentReadbackTarget(readbackTarget, extent, format)",
                       "Present cold readback must use the shared trusted target boundary");
        requirePresent(presentHit, "detail::validatePresentReadbackTarget(target, extent, format)",
                       "Present Skeleton readback must use the same trusted target boundary");
        requirePresent(presentColdReadback, ".destinationBufferRangeSize = requiredBytes",
                       "Present cold readback must declare the exact copied byte range");
        requirePresent(presentHit, ".destinationBufferRangeSize = requiredBytes",
                       "Present Skeleton readback must patch the exact copied byte range");
        requirePresent(presentColdReadback, "copyRegion.bufferOffset = readbackTarget.offset",
                       "Present cold readback must copy from the validated destination offset");
        requirePresent(presentHit, "region.bufferOffset = target.offset",
                       "Present Skeleton readback must copy from the same validated destination offset");
        requireAbsent(presentColdReadback, "readbackBuffer.valid()",
                      "Present cold readback must not duplicate trusted-boundary validation");
        requireAbsent(presentHit, "Present Skeleton readback target requires a valid buffer",
                      "Present Skeleton readback must not retain a divergent validation policy");
        requireAbsent(present, "makeTransparentUiFallback",
                      "Present must not retain a node-local transparent UI fallback factory");
        requireAbsent(present, "Present.TransparentUiFallback",
                      "Present must not allocate a transparent UI fallback image");
        requireAbsent(present, "Present.ClearTransparentUiFallback",
                      "Present must not schedule a fallback clear pass");
        requireAbsent(presentHit, "GraphTransientImageDesc",
                      "Present Skeleton must not patch a fallback UI resource");
        requireAbsent(presentHit, "patchClearColorImage(",
                      "Present Skeleton must not patch a fallback clear pass");
        requireAbsent(presentCold, "makeTransparentUiFallback(",
                      "Present cold build must not declare a fallback UI resource or clear");
        requirePresent(
            presentHit,
            "hasUiBuffer ? context.namedResource(nr::renderer::frameResource::uiColor) : sourceColor",
            "Present Skeleton must alias missing UI to source color");
        requirePresent(presentCold, "auto const resolvedUiBuffer = context.resolveFrameResource(",
                       "Present cold build must resolve optional real UI");
        requirePresent(presentCold, "hasUiBuffer ? resolvedUiBuffer : sourceColor",
                       "Present cold build must alias missing UI to source color");
        requirePresent(presentHit, ".uiOpacity = hasUiBuffer ? opacity : 0.0f",
                       "Present Skeleton must disable UI sampling when UI is absent");
        requirePresent(presentCold, ".uiOpacity = hasUiBuffer ? opacity : 0.0f",
                       "Present cold build must preserve configured opacity only for real UI");
        requirePresent(presentHit, ".sampledImage(\"gUiColor\", uiBuffer",
                       "Present Skeleton must bind the selected UI or source alias");
        requirePresent(presentCold, ".sampledImage(\"gUiColor\", uiBuffer",
                       "Present cold build must bind the selected UI or source alias");
        requirePresent(presentShaderBeforeUi, "float effectiveUiOpacity = saturate(gPresentConvert.uiOpacity)",
                       "Present shader must derive effective UI opacity before any UI read");
        requireAbsent(presentShaderBeforeUi, "gUiColor.Load(",
                      "Present shader must not read UI before testing effective opacity");
        requirePresent(presentShaderUiBranch, "gUiColor.Load(",
                       "Present shader may read UI only inside the positive-opacity branch");
        requireAbsent(presentShaderAfterUi, "gUiColor.Load(",
                      "Present shader must not read UI after the guarded branch");
        requireOrdered(presentShaderUiBranch, "if (effectiveUiOpacity > 0.0f)", "gUiColor.Load(",
                       "Present shader UI read must be guarded by positive effective opacity");
        requirePresent(presentStructuralSnapshot,
                       ".branchKey = input.readback.has_value() ? \"readback=1\" : \"readback=0\"",
                       "Present structural identity must encode only readback topology");
        requireAbsent(presentStructuralSnapshot, "configurationRevision =",
                      "Present structural identity must not duplicate readback topology in a hashed revision");
        requireAbsent(presentStructuralSnapshot, "swapchainFormat",
                      "Present format must be patched under the renderer-owned swapchain key");
        requireAbsent(presentStructuralSnapshot, "swapchainColorSpace",
                      "Present color space must be patched under the renderer-owned swapchain key");
        requireAbsent(presentStructuralSnapshot, "uiOpacity(",
                      "Present UI opacity must be patched through push constants");
        requireAbsent(presentStructuralSnapshot, "toneMappingSelection(",
                      "Present tone mapping must be patched through push constants");
        requireAbsent(presentStructuralSnapshot, "readback->offset",
                      "Present readback offset must be patched through resource and copy ranges");
        requireAbsent(presentStructuralSnapshot, "hasUiBuffer",
                      "Present UI presence must not enter structural identity");
        requirePresent(presentHit, "auto resourceSlot = std::size_t{2u};",
                       "Present Skeleton resources must start after converted and swapchain imports");
        requirePresent(presentHit, "auto passSlot = std::size_t{0u};",
                       "Present Skeleton passes must start at the conversion slot");
        requirePresent(presentHit, "if (input.readback.has_value())",
                       "Present Skeleton must patch the optional readback topology selected by its key");
        requirePresent(presentCold, "if (input.readback.has_value())",
                       "Present cold build must declare the same optional readback topology");
        requirePresent(presentHit, "patchReadback(context.resource(0u), *input.readback",
                       "Present Skeleton readback must consume the next resource and pass slots");
        requirePresent(presentCold, "detail::addPresentReadbackCopyPass(context, convertedColor, *input.readback",
                       "Present cold readback must declare the corresponding copy pass");
        requirePresent(presentHit, ".record([conversionExtent]",
                       "Present Skeleton record callback must capture only dispatch extent");
        requirePresent(presentCold, ".record([conversionExtent]",
                       "Present cold record callback must capture only dispatch extent");
        requireAbsent(present, ".record([runtime",
                      "Present deferred record callbacks must not share the node runtime");
        requireAbsent(present, ".record([&",
                      "Present deferred record callbacks must not capture node-local resources by reference");
        requirePresent(pathHit, "RayTracingPassPatchBuilder",
                       "PathTracing hit path must patch ray tracing bindings and callbacks");
        requirePresent(dlssHit, "context.patchPass", "DLSS hit path must patch current evaluation callbacks");
        requirePresent(dlssHit, "detail::dlssInputResourceActive(input, slot)",
                       "DLSS Skeleton hits must use the complete required-or-included active-slot contract");
        requirePresent(dlssCold, "detail::dlssInputResourceActive(input, slot)",
                       "DLSS cold builds must use the same active-slot contract");
        requirePresent(dlssHit, "if (!activeResourcesAvailable)",
                       "DLSS Skeleton hits must fall back when an active optional resource is absent");
        requirePresent(dlssHit, "detail::validateCoordinatedResolutionOverrides(",
                       "DLSS Skeleton hits must enforce coordinated size ownership");
        requirePresent(dlssCold, "detail::validateCoordinatedResolutionOverrides(",
                       "DLSS cold builds must enforce the same coordinated size ownership");
        requirePresent(dlssHit, "detail::validateDlssResolvedConfiguration(",
                       "DLSS Skeleton hits must share resolved size and DoF validation");
        requirePresent(dlssCold, "detail::validateDlssResolvedConfiguration(",
                       "DLSS cold builds must share resolved size and DoF validation");
        requirePresent(dlssHit, "detail::validateDlssEvaluationConfiguration(",
                       "DLSS Skeleton hits must validate manual/finite evaluation values");
        requirePresent(dlssCold, "detail::validateDlssEvaluationConfiguration(",
                       "DLSS cold builds must validate manual/finite evaluation values");
        requireAbsent(dlssCold, "descriptions[colorIndex]->extent.width == createDesc.renderSize.width",
                      "DLSS cold builds must accept oversized Color backings through active-rect bounds");
        requireAbsent(dlssInterface, "effectiveResolutionRequest(",
                      "DLSS must not expose the unused node request forwarding member");
        requirePresent(dlssInterface, "dlssResolutionRequestFromSnapshot(",
                       "DLSS must retain the request helper used by graph assembly");
        requireAbsent(dlssInterface, "device_", "DLSS runtime must be the only initialization marker");
        requireAbsent(dlss, "optimalSettingsQueried", "DLSS must remove the write-only query marker");
        requireAbsent(dlss, "runtime->optimalSettings", "DLSS optimal settings must remain prepare-local");
        requireAbsent(dlss, "runtime->status", "DLSS must remove write-only runtime status updates");
        requirePresent(dlssHit, "detail::makeDlssPrepareCallback(",
                       "DLSS Skeleton patching must reuse shared feature preparation");
        requirePresent(dlssCold, "detail::makeDlssPrepareCallback(",
                       "DLSS cold builds must reuse shared feature preparation");
        requirePresent(dlssHit, "detail::makeDlssRecordCallback(",
                       "DLSS Skeleton patching must reuse shared evaluation recording");
        requirePresent(dlssCold, "detail::makeDlssRecordCallback(",
                       "DLSS cold builds must reuse shared evaluation recording");
        requirePresent(dlssPrepareCallback, "createDlssRayReconstructionFeature(",
                       "DLSS feature recreation must remain in prepare");
        requireAbsent(dlssPrepareCallback, "feature->evaluate(", "DLSS prepare must not evaluate the feature");
        requirePresent(dlssRecordCallback, "feature->evaluate(", "DLSS evaluation must remain in record");
        requireAbsent(dlssRecordCallback, "createDlssRayReconstructionFeature(",
                      "DLSS record must not recreate the feature");
        requirePresent(dlssHit, "context.patchPass(", "DLSS Skeleton must retain patch-only graph ownership");
        requirePresent(dlssCold, "context.addPass(", "DLSS cold build must retain pass-declaration ownership");
        requirePresent(dlssStructuralSnapshot, "{};bypass={};alpha={};hdr={};debug={};quality={}",
                       "DLSS Skeleton identity must separate every publication topology");
        requirePresent(dlssPublication, "if (input.bypass && !input.evaluate.visualizeMotionVectors)",
                       "DLSS bypass must yield to the post-evaluation debug color");
        requirePresent(dlssPublication, "publishedColor = handles[colorIndex]",
                       "DLSS ordinary bypass must publish its input color");
        requirePresent(dlssPublication, "auto publishedAlpha = outputAlpha",
                       "DLSS non-bypass must publish reconstructed alpha");
        requireOrdered(dlssPublication, "nrAssert(handles[alphaIndex].valid()",
                       "publishedAlpha = handles[alphaIndex]",
                       "DLSS bypass must validate and publish its paired input alpha");
        requireOrdered(dlssCold, "auto pass = context.addPass(", "auto publishedColor = outputColor;",
                       "DLSS publication must not bypass NGX evaluation");
        requireOrdered(dlssCold, "DLSS.RayReconstruction.VisualizeMotionVectors",
                       "auto publishedColor = outputColor;",
                       "DLSS debug color must be produced before publication");
        requirePresent(uiHit, "RasterPassPatchBuilder", "UI hit path must patch raster state and draw callbacks");
        requirePresent(ui, "auto drawFrame = detail::prepareUiDrawFrame",
                       "UI structural snapshot must prepare draw data and synchronize textures before keying");
        requireOrdered(uiDrawPreparation, "uiSystem->get().drawData()", "synchronizeUiTextures(",
                       "UiNode must consume finalized app draw data before synchronizing its renderer textures");
        requireAbsent(uiDrawPreparation, "renderSections(",
                      "UiNode draw preparation must not render app-owned sections");
        requireAbsent(uiDrawPreparation, "finalizeFrame(",
                      "UiNode draw preparation must not finalize the app-owned UI frame");
        requirePresent(uiHit, "runtime_->preparedDrawFrame",
                       "UI hit path must consume the structural preflight draw frame");
        requireAbsent(uiHit, "finalizeFrame()",
                      "UI hit path must not finalize a second frame after structural preflight");
        requireAbsent(uiHit, "synchronizeUiTextures(",
                      "UI hit path must not mutate texture topology after structural key lookup");
        requirePresent(asHit, "context.patchFrameData", "AS hit path must patch BLAS/TLAS frame data");
        requirePresent(asHit, "context.patchPass", "AS hit path must patch current build callbacks");
        requirePresent(asHit, "entry.retainedState", "AS hit path must patch retained BLAS backing state");
        requirePresent(asHit, "prepared.instances", "AS hit path must patch every BLAS referenced by the current TLAS");
        requirePresent(asSource, "RetainedAccelerationStructureState retainedState",
                       "AS cache entries must retain cross-frame BLAS build/read state");
        requirePresent(asSource, "blasResourceByMesh.at(instance.mesh)",
                       "TLAS declarations must read every current BLAS, including stable entries");
        requirePresent(asSource, "\"no-instances\"", "AS preflight must key the unavailable branch");
        requirePresent(asSource, "\"tlas-only\"", "AS preflight must key the TLAS-only branch");
        requirePresent(asSource, "\"dirty-blas\"", "AS preflight must key the dirty-BLAS branch");
        requirePresent(asSource, "prepared.dirtyMeshes", "AS exact key must include the current dirty mesh set");
        requirePresent(asSource, "cached.geometries.size()", "AS dirty variants must include geometry topology");
        requirePresent(asSource, "frameSlot.instanceBufferSize",
                       "AS exact key must include current per-slot resource capacity");
        requireAbsent(
            asHit, "detail::prepareAsFrame",
            "AS hit patch must not rebuild or advance preflight state when its prepared packet is unavailable");
        requireOrdered(asHit, "snapshot.branchKey != expectedBranch",
                       "auto prepared = std::move(*runtime_->preparedFrame)",
                       "AS hit patch must validate the branch before consuming its prepared packet");
        requireOrdered(asHit, "snapshot.branchKey != expectedBranch", "runtime_->preparedFrame.reset()",
                       "AS hit patch must preserve its prepared packet on a branch mismatch");
        requireOrdered(asCold, "runtime_->preparedFrame.has_value()",
                       "auto prepared = std::move(*runtime_->preparedFrame)",
                       "AS build must require preflight preparation before consuming its packet");
        requireOrdered(asCold, "auto prepared = std::move(*runtime_->preparedFrame)",
                       "detail::declarePreparedAsFrame(context, *runtime_, std::move(prepared))",
                       "AS build must declare only the preflight-prepared packet");
        requireAbsent(asCold, "detail::prepareAsFrame(",
                      "AS build must not retain a no-packet fallback preparation path");
        requirePresent(presentAdvance, "processCompletedScreenshot(frameSlot)",
                       "Present must process completed screenshots through the renderer continuation hook");
        requireOrdered(renderer, "installedNode.runtime->advanceContinuations(begin.frameIndex)",
                       "installedNode.runtime->structuralSnapshot(nodeFrameParameters)",
                       "renderer must harvest Present screenshot continuations before capturing structural snapshots");
        requireAbsent(presentHit, "processCompletedScreenshot(",
                      "Present hit patch must not change screenshot topology after key selection");
        requireAbsent(presentCold, "processCompletedScreenshot(",
                      "Present cold materialization must not harvest a continuation during graph construction");
        requireOrdered(presentHit, "expectedSnapshot->branchKey != snapshot.branchKey", "context.patchResource(",
                       "Present hit patch must validate the selected screenshot branch before patching slots");
        requireOrdered(dlssHit, "!snapshot.branchKey.starts_with(\"disabled;\")", "previousBuildTime_ = {}",
                       "disabled DLSS hit patch must validate its branch before changing reset state");
        auto hitSections = std::array{lightHit, asHit, pathHit, accumulateHit, dlssHit, uiHit, presentHit};
        std::ranges::for_each(hitSections, [](std::string_view hit) {
            requireAbsent(hit, "NodeBuildContext", "migrated hit path must not receive structural context");
            requireAbsent(hit, "materializeCurrentFrame", "migrated hit path must not call cold materialization");
            requireAbsent(hit, ".addNode(", "migrated hit path must not declare nodes");
            requireAbsent(hit, ".addResource(", "migrated hit path must not declare resources");
            requireAbsent(hit, ".addFrameData(", "migrated hit path must not declare frame data");
            requireAbsent(hit, ".addPass(", "migrated hit path must not declare passes");
            requireAbsent(hit, ".addSubmitNode(", "migrated hit path must not declare submits");
        });
        requirePresent(asInterface, "supportsRenderGraphSkeleton() const noexcept override { return true; }",
                       "AS supports Skeleton patching");
        requirePresent(pathInterface, "supportsRenderGraphSkeleton() const noexcept override { return true; }",
                       "PathTracing supports Skeleton patching");
        requirePresent(dlssInterface, "supportsRenderGraphSkeleton() const noexcept override { return true; }",
                       "DLSS supports Skeleton patching");
        requirePresent(uiInterface, "supportsRenderGraphSkeleton() const noexcept override { return true; }",
                       "UI supports Skeleton patching");
        auto const screenshotValidation = presentCold.find("sourceDesc = context.describeImageResource(sourceColor)");
        auto const screenshotConsume =
            presentCold.find("runtime.screenshotPrepared = detail::PresentScreenshotPrepared");
        nr::test::require(
            screenshotValidation != std::string_view::npos && screenshotConsume != std::string_view::npos &&
                screenshotValidation < screenshotConsume,
            "Present capture must validate screenshot source metadata before preparing the one-shot effect");
    }};
} // namespace
