import dependency.json;
import nr.options;
import nr.test;
import std;

namespace
{
using namespace nr::options;
namespace json = dependency::json;

[[nodiscard]] json::JsonValue parseJsonValue(std::string_view text)
{
    auto parsed = json::parseJson(text);
    nr::test::require(parsed.valid(), "JSON payload should parse through dependency.json");
    return std::move(*parsed.value);
}

[[nodiscard]] std::string_view jsonStringField(const json::JsonValue &value, std::string_view name)
{
    auto const *object = std::get_if<json::JsonValue::Object>(&value.storage);
    nr::test::require(object != nullptr, "JSON payload should be an object");
    auto const field = object->find(name);
    nr::test::require(field != object->end(), "JSON payload is missing an expected field");
    auto const *text = std::get_if<std::string>(&field->second.storage);
    nr::test::require(text != nullptr, "JSON field should be a string");
    return *text;
}

[[nodiscard]] std::shared_ptr<const OptionCatalog> catalog(std::span<const OptionDefinition> definitions)
{
    auto builder = OptionCatalogBuilder{};
    std::ranges::for_each(definitions, [&](auto const &definition) {
        nr::test::require(builder.add(definition), "definition should be accepted");
    });
    auto result = builder.build();
    nr::test::require(result.valid(), "catalog should build");
    return result.catalog;
}

[[nodiscard]] SessionDefinitionSeed sessionDefinitionSeed()
{
    return SessionDefinitionSeed{
        .environmentName = "kloofendal_48d_partly_cloudy_puresky_8k",
        .environmentNames =
            {
                "brown_photostudio_02_8k",
                "kloofendal_48d_partly_cloudy_puresky_8k",
            },
    };
}

[[nodiscard]] std::shared_ptr<const OptionCatalog> sessionCatalog()
{
    auto definitions = makeSessionDefinitions(sessionDefinitionSeed());
    return catalog(definitions);
}

[[nodiscard]] OptionAvailabilityMap allAvailable(const OptionCatalog &value)
{
    auto result = OptionAvailabilityMap{};
    std::ranges::for_each(value.definitions(), [&](auto const &entry) {
        result.emplace(entry.first, OptionAvailability{.available = true, .reason = {}});
    });
    return result;
}

[[nodiscard]] std::unique_ptr<OptionSystem> initializedSystem(AuthorityMode mode = AuthorityMode::human)
{
    auto system = std::make_unique<OptionSystem>(mode);
    auto session = sessionCatalog();
    nr::test::require(system->initializeSession(session, allAvailable(*session)).committed,
                      "session catalog should initialize");
    return system;
}

template <WireValueAlternative T>
[[nodiscard]] OptionMutationRequest request(OptionSystem &system, OptionKey<T> key, T value,
                                            MutationOrigin origin = MutationOrigin::imgui)
{
    return OptionMutationRequest{
        .id = optionId(key),
        .value = OptionWireValue{std::move(value)},
        .binding = BindingProof{.bindingEpoch = system.liveBinding().bindingEpoch},
        .origin = origin,
    };
}

[[nodiscard]] std::shared_ptr<const OptionFrameSnapshot> finishFrame(OptionSystem &system,
                                                                     std::optional<FrameEffect> effect = {})
{
    auto currentCatalog = system.activeCatalog();
    return system.publishRenderableFrame(allAvailable(*currentCatalog), std::move(effect));
}

const nr::test::CaseRegistrar schemaAndCatalogCase{
    "option catalog validates IDs defaults schemas and snapshot bounds", [] {
        nr::test::require(OptionId::parse("render.present.ui_opacity").has_value());
        nr::test::require(!OptionId::parse("Render.present.ui_opacity").has_value());
        nr::test::require(!OptionId::parse("render..opacity").has_value());
        nr::test::require(!OptionId::parse("render.present.trailing.").has_value());

        auto sessionDefinitions = makeSessionDefinitions(sessionDefinitionSeed());
        auto clipDefinition = std::ranges::find_if(sessionDefinitions, [](auto const &definition) {
            return definition.id == optionId(keys::viewerCameraClipPlanes);
        });
        nr::test::require(clipDefinition != sessionDefinitions.end(), "clip definition must be registered");

        auto invalidClip = OptionWireValue{OptionWireValue::Object{
            {"near", OptionWireValue{10.0}},
            {"far", OptionWireValue{1.0}},
        }};
        nr::test::require(!clipDefinition->schema.validate(invalidClip).valid,
                          "clip cross-field validation should reject far <= near");
        auto tooLargeClip = OptionWireValue{OptionWireValue::Object{
            {"near", OptionWireValue{0.1}},
            {"far", OptionWireValue{static_cast<double>(std::numeric_limits<float>::max()) * 2.0}},
        }};
        nr::test::require(!clipDefinition->schema.validate(tooLargeClip).valid,
                          "camera clip values must remain representable by the float renderer camera");

        auto poseDefinition = std::ranges::find_if(sessionDefinitions, [](auto const &definition) {
            return definition.id == optionId(keys::viewerCameraPose);
        });
        nr::test::require(poseDefinition != sessionDefinitions.end(), "camera pose definition must be registered");
        auto tooLargePose = OptionWireValue{OptionWireValue::Object{
            {"position",
             OptionWireValue::Array{
                 OptionWireValue{static_cast<double>(std::numeric_limits<float>::max()) * 2.0},
                 OptionWireValue{0.0},
                 OptionWireValue{0.0},
             }},
            {"yaw_degrees", OptionWireValue{0.0}},
            {"pitch_degrees", OptionWireValue{0.0}},
        }};
        nr::test::require(!poseDefinition->schema.validate(tooLargePose).valid,
                          "camera position values must remain representable by the float renderer camera");

        auto movementSpeedDefinition = std::ranges::find_if(sessionDefinitions, [](auto const &definition) {
            return definition.id == optionId(keys::viewerCameraMovementSpeed);
        });
        nr::test::require(movementSpeedDefinition != sessionDefinitions.end(),
                          "camera movement speed definition must be registered");
        std::ranges::for_each(std::array{0.01, 1000.0}, [&](double movementSpeed) {
            nr::test::require(movementSpeedDefinition->schema.validate(OptionWireValue{movementSpeed}).valid,
                              "camera movement speed schema must accept both closed boundaries");
        });
        std::ranges::for_each(
            std::array{
                0.0,
                -0.01,
                std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::infinity(),
                1000.01,
            },
            [&](double movementSpeed) {
                nr::test::require(!movementSpeedDefinition->schema.validate(OptionWireValue{movementSpeed}).valid,
                                  "camera movement speed schema must reject non-finite and out-of-range values");
            });

        auto duplicateBuilder = OptionCatalogBuilder{};
        auto definition = makeBooleanDefinition(keys::viewerWindowFullscreen, false, OptionScope::session);
        nr::test::require(duplicateBuilder.add(definition));
        nr::test::require(!duplicateBuilder.add(definition));
        nr::test::require(!duplicateBuilder.build().valid(), "duplicate catalog must fail");

        auto boundedBuilder = OptionCatalogBuilder{};
        nr::test::require(boundedBuilder.add(std::move(definition)));
        auto tooSmall = boundedBuilder.build(1u);
        nr::test::require(!tooSmall.valid(), "snapshot estimate must be enforced");
        nr::test::requireEqual(tooSmall.issues.front().code, CatalogIssueCode::snapshotTooLarge);

        auto malformedBuilder = OptionCatalogBuilder{};
        auto malformedSchema = OptionSchema{
            .type = OptionValueType::number,
            .numberMinimum = 2.0,
            .numberMaximum = 1.0,
        };
        nr::test::require(!malformedBuilder.add(OptionDefinition{
            .id = optionId(keys::viewerCameraVerticalFovDegrees),
            .schema = std::move(malformedSchema),
            .defaultValue = 1.5,
            .scope = OptionScope::session,
        }));
        nr::test::requireEqual(malformedBuilder.build().issues.front().code, CatalogIssueCode::invalidDefinition);

        auto openObjectBuilder = OptionCatalogBuilder{};
        auto openObjectSchema = OptionSchema::emptyObject();
        openObjectSchema.closedObject = false;
        nr::test::require(!openObjectBuilder.add(OptionDefinition{
            .id = optionId(keys::viewerCameraPose),
            .schema = std::move(openObjectSchema),
            .defaultValue = OptionWireValue::Object{},
            .scope = OptionScope::session,
        }));

        auto maximumValueBuilder = OptionCatalogBuilder{};
        nr::test::require(maximumValueBuilder.add(makeDefinition(
            keys::viewerModelSource, OptionSchema::string(64u * 1024u), std::string{}, OptionScope::session)));
        nr::test::require(!maximumValueBuilder.build(256u * 1024u).valid(),
                          "snapshot preflight must bound the schema's largest value, not only its default");
    }};

const nr::test::CaseRegistrar fixedCatalogCase{
    "fixed option catalog has one canonical DLSS quality and explicit frame effects", [] {
        auto session = sessionCatalog();
        nr::test::require(session->find(optionId(keys::viewerWindowFullscreen)) != nullptr);
        nr::test::require(session->find(optionId(keys::viewerCameraPose)) != nullptr);
        auto const *environment = session->find(optionId(keys::viewerEnvironmentSource));
        nr::test::require(environment != nullptr && environment->presentation.control == OptionUiControl::combo &&
                              environment->schema.allowedStrings == sessionDefinitionSeed().environmentNames,
                          "environment selection must be a closed combo of discovered extension-free names");
        auto const *exit = session->find(optionId(keys::viewerExit));
        nr::test::require(exit != nullptr && exit->scope == OptionScope::session &&
                              exit->lifetime == OptionValueLifetime::frameEffect &&
                              exit->schema.type == OptionValueType::object && exit->presentation.group == "Viewer" &&
                              exit->presentation.label == "Exit" &&
                              exit->presentation.control == OptionUiControl::button && exit->presentation.order == 60,
                          "viewer exit must be a shared session frame-effect button");
        auto const *verticalFov = session->find(optionId(keys::viewerCameraVerticalFovDegrees));
        nr::test::require(verticalFov != nullptr && verticalFov->schema.type == OptionValueType::unsignedInteger &&
                              verticalFov->schema.unsignedMinimum == 1u &&
                              verticalFov->schema.unsignedMaximum == 179u &&
                              verticalFov->presentation.control == OptionUiControl::slider,
                          "vertical FOV must be an integer-degree slider");
        nr::test::require(session->find(optionId(keys::viewerCameraClipPlanes)) != nullptr);
        auto const *movementSpeed = session->find(optionId(keys::viewerCameraMovementSpeed));
        auto const *defaultMovementSpeed =
            movementSpeed != nullptr ? std::get_if<double>(&movementSpeed->defaultValue.storage) : nullptr;
        nr::test::require(movementSpeed != nullptr && movementSpeed->scope == OptionScope::session &&
                              movementSpeed->schema.type == OptionValueType::number &&
                              movementSpeed->schema.numberMinimum == 0.01 &&
                              movementSpeed->schema.numberMaximum == 1000.0 && defaultMovementSpeed != nullptr &&
                              *defaultMovementSpeed == 3.5 && movementSpeed->presentation.group == "Camera" &&
                              movementSpeed->presentation.label == "Movement speed" &&
                              movementSpeed->presentation.control == OptionUiControl::input &&
                              movementSpeed->presentation.order == 40,
                          "camera movement speed must be the canonical Camera session number input");
        auto legacyQuality = OptionId::parse("viewer.rt.dlss_quality");
        nr::test::require(legacyQuality.has_value());
        nr::test::require(session->find(*legacyQuality) == nullptr,
                          "the removed viewer-scoped DLSS quality must not be registered");

        auto dlss = catalog(makeDlssDefinitions());
        nr::test::require(dlss->find(optionId(keys::dlssQuality)) != nullptr);
        auto const *reset = dlss->find(optionId(keys::dlssResetHistory));
        nr::test::require(reset != nullptr && reset->lifetime == OptionValueLifetime::frameEffect,
                          "DLSS reset must be a one-frame effect");
        auto const removedPresetIds = std::array<std::string_view, 5u>{
            "render.dlss.preset.performance",       "render.dlss.preset.balanced", "render.dlss.preset.quality",
            "render.dlss.preset.ultra_performance", "render.dlss.preset.dlaa",
        };
        std::ranges::for_each(removedPresetIds, [&](std::string_view id) {
            auto parsed = OptionId::parse(id);
            nr::test::require(parsed.has_value());
            nr::test::require(dlss->find(*parsed) == nullptr,
                              std::format("removed DLSS preset option '{}' must not be registered", id));
        });

        auto present = catalog(makePresentDefinitions());
        auto const *capture = present->find(optionId(keys::presentCaptureExr));
        nr::test::require(capture != nullptr && capture->lifetime == OptionValueLifetime::frameEffect,
                          "EXR capture must be a one-frame effect");

        auto pathTracing = catalog(makePathTracingDefinitions());
        auto const *filterAfterShading = pathTracing->find(optionId(keys::pathTracingFilterAfterShadingEnabled));
        auto const *defaultFilterAfterShading =
            filterAfterShading != nullptr ? std::get_if<bool>(&filterAfterShading->defaultValue.storage) : nullptr;
        nr::test::require(
            filterAfterShading != nullptr && filterAfterShading->scope == OptionScope::graph &&
                filterAfterShading->schema.type == OptionValueType::boolean && defaultFilterAfterShading != nullptr &&
                !*defaultFilterAfterShading && filterAfterShading->resetsTemporalHistory,
            "PathTracing FAS must default off and reset temporal consumers after a committed A/B transition");

        auto completeRtBuilder = OptionCatalogBuilder{};
        auto addDefinitions = [&](std::vector<OptionDefinition> definitions) {
            std::ranges::for_each(definitions, [&](OptionDefinition &definition) {
                nr::test::require(completeRtBuilder.add(std::move(definition)),
                                  "complete RT catalog definition should be accepted");
            });
        };
        addDefinitions(makeSessionDefinitions(sessionDefinitionSeed()));
        addDefinitions(makePathTracingDefinitions());
        addDefinitions(makeDlssDefinitions());
        addDefinitions(makePresentDefinitions());
        nr::test::require(completeRtBuilder.build().valid(),
                          "the complete fixed RT snapshot must fit in one 256 KiB response");
    }};

const nr::test::CaseRegistrar verticalFovAdmissionCase{
    "vertical FOV rejects fractional degrees", [] {
        auto system = initializedSystem();
        auto fractional = system->trySchedule(OptionMutationRequest{
            .id = optionId(keys::viewerCameraVerticalFovDegrees),
            .value = OptionWireValue{75.5},
            .binding = BindingProof{.bindingEpoch = system->liveBinding().bindingEpoch},
            .origin = MutationOrigin::imgui,
        });
        nr::test::require(!fractional.started);
        nr::test::requireEqual(fractional.reason, ScheduleRejectReason::invalidValue);
        nr::test::require(!system->hasPendingMutation());
    }};

const nr::test::CaseRegistrar pathAdmissionCase{
    "filesystem paths and unknown environment names are rejected before reserving the mutation slot", [] {
        auto system = initializedSystem();
        auto reject = [&](OptionKey<std::string> key, std::string value) {
            auto result = system->trySchedule(request(*system, key, std::move(value)));
            nr::test::require(!result.started);
            nr::test::requireEqual(result.reason, ScheduleRejectReason::invalidValue);
            nr::test::require(!system->hasPendingMutation());
        };

        reject(keys::viewerModelSource, "../CMakeLists.txt");
        reject(keys::viewerModelSource, "missing/model.gltf");
        reject(keys::viewerModelSource, "https://example.invalid/model.gltf");
        reject(keys::viewerEnvironmentSource, "brown_photostudio_02_8k.exr");
        reject(keys::viewerEnvironmentSource, "assets/envMap/missing.exr");
        reject(keys::viewerEnvironmentSource, "assets/envMap/../glTF-Sample-Assets/README.md");
        reject(keys::viewerEnvironmentSource, "missing");

        auto validModel =
            system->trySchedule(request(*system, keys::viewerModelSource,
                                        std::string{"glTF-Sample-Assets/Models/AnimatedCube/glTF/AnimatedCube.gltf"}));
        nr::test::require(validModel.started);
        auto frame = system->beginRenderableFrame();
        nr::test::require(frame && frame->mutation);
        nr::test::require(system->discardMutation(std::move(*frame->mutation)));
        static_cast<void>(finishFrame(*system));

        auto validEnvironment = system->trySchedule(
            request(*system, keys::viewerEnvironmentSource, std::string{"brown_photostudio_02_8k"}));
        nr::test::require(validEnvironment.started);
        frame = system->beginRenderableFrame();
        nr::test::require(frame && frame->mutation);
        nr::test::require(system->discardMutation(std::move(*frame->mutation)));
    }};

const nr::test::CaseRegistrar singleSlotCase{
    "option admission has one slot and one irreversible attempt per frame", [] {
        auto system = initializedSystem();
        auto first = system->trySchedule(request(*system, keys::viewerWindowFullscreen, true));
        nr::test::require(first.started);
        nr::test::requireEqual(first.sequence, std::uint64_t{1u});

        auto second = system->trySchedule(request(*system, keys::viewerCameraVerticalFovDegrees, std::uint64_t{75u}));
        nr::test::require(!second.started);
        nr::test::requireEqual(second.reason, ScheduleRejectReason::busy);

        auto frame = system->beginRenderableFrame();
        nr::test::require(frame.has_value() && frame->mutation.has_value());
        nr::test::require(!system->admissionOpen(), "gate must close before execution");
        nr::test::requireEqual(
            system->trySchedule(request(*system, keys::viewerCameraVerticalFovDegrees, std::uint64_t{80u})).reason,
            ScheduleRejectReason::admissionClosed, "closed frame gate must reject every new mutation source");
        auto commit = system->commitCanonical(std::move(*frame->mutation));
        nr::test::require(commit.committed);

        auto duplicateCommit = system->commitCanonical(std::move(*frame->mutation));
        nr::test::require(!duplicateCommit.committed, "consumed attempt cannot commit twice");
        nr::test::requireEqual(duplicateCommit.reason, ScheduleRejectReason::invalidParams);

        auto published = finishFrame(*system);
        nr::test::require(system->admissionOpen());
        auto const *fullscreen = published->find(keys::viewerWindowFullscreen);
        nr::test::require(fullscreen != nullptr && *fullscreen);
        nr::test::requireEqual(published->frameIndex, std::uint64_t{1u});
    }};

const nr::test::CaseRegistrar proofCase{
    "option admission rejects missing stale and mismatched binding proofs", [] {
        auto system = initializedSystem();
        auto binding = system->liveBinding();

        auto noProof = request(*system, keys::viewerWindowFullscreen, true);
        noProof.binding = {};
        nr::test::requireEqual(system->trySchedule(std::move(noProof)).reason, ScheduleRejectReason::invalidParams);

        auto staleEpoch = request(*system, keys::viewerWindowFullscreen, true);
        staleEpoch.binding.bindingEpoch = binding.bindingEpoch + 1u;
        nr::test::requireEqual(system->trySchedule(std::move(staleEpoch)).reason, ScheduleRejectReason::staleBinding);

        auto staleToken = request(*system, keys::viewerWindowFullscreen, true);
        staleToken.binding = BindingProof{.snapshotToken = "stale"};
        nr::test::requireEqual(system->trySchedule(std::move(staleToken)).reason, ScheduleRejectReason::staleSnapshot);

        auto bothMismatch = request(*system, keys::viewerWindowFullscreen, true);
        bothMismatch.binding.snapshotToken = "stale";
        nr::test::requireEqual(system->trySchedule(std::move(bothMismatch)).reason,
                               ScheduleRejectReason::bindingProofMismatch);

        auto bothMatch = request(*system, keys::viewerWindowFullscreen, true);
        bothMatch.binding.snapshotToken = binding.snapshotToken;
        nr::test::require(system->trySchedule(std::move(bothMatch)).started,
                          "matching epoch and token proofs must be accepted together");
        auto frame = system->beginRenderableFrame();
        nr::test::require(frame && frame->mutation);
        nr::test::requireEqual(system->validateForExecution(*frame->mutation), ScheduleRejectReason::none);
        nr::test::require(system->discardMutation(std::move(*frame->mutation)));
        (void)finishFrame(*system);
    }};

const nr::test::CaseRegistrar snapshotTokenIdentityCase{
    "snapshot tokens are session-bound stable binding proofs", [] {
        auto first = initializedSystem();
        auto second = initializedSystem();
        auto const firstToken = first->liveBinding().snapshotToken;
        auto const secondToken = second->liveBinding().snapshotToken;
        nr::test::require(!firstToken.empty());
        nr::test::require(firstToken != secondToken,
                          "two OptionSystem sessions in one process must never share a snapshot token");
        auto foreignToken = request(*first, keys::viewerWindowFullscreen, true);
        foreignToken.binding = BindingProof{.snapshotToken = secondToken};
        nr::test::requireEqual(first->trySchedule(std::move(foreignToken)).reason, ScheduleRejectReason::staleSnapshot);
        auto mixedProof = request(*first, keys::viewerWindowFullscreen, true);
        mixedProof.binding.snapshotToken = secondToken;
        nr::test::requireEqual(first->trySchedule(std::move(mixedProof)).reason,
                               ScheduleRejectReason::bindingProofMismatch);

        auto frame = first->beginRenderableFrame();
        nr::test::require(frame && !frame->mutation);
        auto nextSnapshot = finishFrame(*first);
        nr::test::requireEqual(nextSnapshot->snapshotToken, firstToken,
                               "ordinary frame publication must not invalidate a binding proof");

        frame = first->beginRenderableFrame();
        nr::test::require(first->replaceGraphCatalog(catalog(makePathTracingDefinitions())).committed);
        auto replacementSnapshot = finishFrame(*first);
        nr::test::require(replacementSnapshot->snapshotToken != firstToken,
                          "graph replacement must invalidate the previous session binding token");
    }};

const nr::test::CaseRegistrar concurrentSingleSlotCase{
    "concurrent admission linearizes to one started mutation", [] {
        auto system = initializedSystem();
        auto readyCount = std::atomic_uint32_t{0u};
        auto start = std::atomic_bool{false};
        auto results = std::array<ScheduleResult, 2u>{};
        auto run = [&](std::size_t index, OptionMutationRequest mutation) {
            readyCount.fetch_add(1u, std::memory_order_release);
            while (!start.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            results[index] = system->trySchedule(std::move(mutation));
        };
        auto first = std::jthread{run, 0u, request(*system, keys::viewerWindowFullscreen, true)};
        auto second = std::jthread{run, 1u, request(*system, keys::viewerCameraVerticalFovDegrees, std::uint64_t{75u})};
        while (readyCount.load(std::memory_order_acquire) != results.size())
        {
            std::this_thread::yield();
        }
        start.store(true, std::memory_order_release);
        first.join();
        second.join();

        nr::test::requireEqual(std::ranges::count(results, true, &ScheduleResult::started), std::ptrdiff_t{1},
                               "exactly one racing request may reserve the pending slot");
        auto const rejected = std::ranges::find(results, false, &ScheduleResult::started);
        nr::test::require(rejected != results.end());
        nr::test::requireEqual(rejected->reason, ScheduleRejectReason::busy);

        auto frame = system->beginRenderableFrame();
        nr::test::require(frame && frame->mutation);
        nr::test::require(system->discardMutation(std::move(*frame->mutation)));
        (void)finishFrame(*system);
    }};

const nr::test::CaseRegistrar executionRevalidationCase{
    "moved frame mutation is rejected after a binding generation changes", [] {
        auto system = initializedSystem();
        nr::test::require(system->trySchedule(request(*system, keys::viewerWindowFullscreen, true)).started);
        auto frame = system->beginRenderableFrame();
        nr::test::require(frame && frame->mutation);

        auto graph = catalog(makePathTracingDefinitions());
        nr::test::require(system->replaceGraphCatalog(graph).committed);
        nr::test::requireEqual(system->validateForExecution(*frame->mutation), ScheduleRejectReason::staleBinding,
                               "execution must revalidate the binding under the same state mutex");
        auto commit = system->commitCanonical(std::move(*frame->mutation));
        nr::test::require(!commit.committed);
        nr::test::requireEqual(commit.reason, ScheduleRejectReason::staleBinding);
        auto published = finishFrame(*system);
        nr::test::require(!*published->find(keys::viewerWindowFullscreen));
    }};

const nr::test::CaseRegistrar catalogAdmissionRaceCase{
    "catalog replacement and admission race has no lost or misbound mutation", [] {
        auto const graph = catalog(makePathTracingDefinitions());
        std::ranges::for_each(std::views::iota(0u, 32u), [&](auto) {
            auto system = initializedSystem();
            auto mutation = request(*system, keys::viewerWindowFullscreen, true);
            auto readyCount = std::atomic_uint32_t{0u};
            auto start = std::atomic_bool{false};
            auto schedule = ScheduleResult{};
            auto frame = std::optional<RenderableFrameStart>{};
            auto replacement = CatalogCommitResult{};

            auto waitForStart = [&] {
                readyCount.fetch_add(1u, std::memory_order_release);
                while (!start.load(std::memory_order_acquire))
                {
                    std::this_thread::yield();
                }
            };
            auto scheduler = std::jthread{[&] {
                waitForStart();
                schedule = system->trySchedule(std::move(mutation));
            }};
            auto frameOwner = std::jthread{[&] {
                waitForStart();
                frame = system->beginRenderableFrame();
                if (frame.has_value())
                {
                    replacement = system->replaceGraphCatalog(graph);
                }
            }};
            while (readyCount.load(std::memory_order_acquire) != 2u)
            {
                std::this_thread::yield();
            }
            start.store(true, std::memory_order_release);
            scheduler.join();
            frameOwner.join();

            nr::test::require(frame.has_value());
            nr::test::require(replacement.committed);
            if (schedule.started)
            {
                nr::test::require(frame->mutation.has_value(),
                                  "a request linearized before gate close must be moved into that frame");
                nr::test::requireEqual(frame->mutation->sequence(), schedule.sequence);
                nr::test::requireEqual(system->validateForExecution(*frame->mutation),
                                       ScheduleRejectReason::staleBinding,
                                       "catalog replacement must invalidate the already-moved old binding");
                nr::test::require(system->discardMutation(std::move(*frame->mutation)));
            }
            else
            {
                nr::test::requireEqual(schedule.reason, ScheduleRejectReason::admissionClosed);
                nr::test::require(!frame->mutation.has_value());
            }
            (void)finishFrame(*system);
        });
    }};

const nr::test::CaseRegistrar crossOptionCase{
    "cross-option validation rejects DLSS bypass combinations without implicit correction", [] {
        auto system = initializedSystem();
        auto dlssDefinitions = makeDlssDefinitions();
        auto graph = catalog(dlssDefinitions);
        auto frame = system->beginRenderableFrame();
        nr::test::require(system->replaceGraphCatalog(graph).committed);
        (void)finishFrame(*system);

        auto invalidBypass = system->trySchedule(request(*system, keys::dlssBypass, true));
        nr::test::require(!invalidBypass.started);
        nr::test::requireEqual(invalidBypass.reason, ScheduleRejectReason::invalidValue);
        nr::test::requireEqual(*system->snapshot()->find(keys::dlssQuality), std::string{"quality"},
                               "invalid bypass must not rewrite quality");

        nr::test::require(system->trySchedule(request(*system, keys::dlssQuality, std::string{"dlaa"})).started);
        frame = system->beginRenderableFrame();
        nr::test::require(system->commitCanonical(std::move(*frame->mutation)).committed);
        (void)finishFrame(*system);

        nr::test::require(system->trySchedule(request(*system, keys::dlssBypass, true)).started);
        frame = system->beginRenderableFrame();
        nr::test::require(system->commitCanonical(std::move(*frame->mutation)).committed);
        (void)finishFrame(*system);

        auto invalidQuality = system->trySchedule(request(*system, keys::dlssQuality, std::string{"performance"}));
        nr::test::require(!invalidQuality.started);
        nr::test::requireEqual(invalidQuality.reason, ScheduleRejectReason::invalidValue);
    }};

const nr::test::CaseRegistrar minimizedCase{
    "skipping option frame preserves pending mutation and immutable snapshot", [] {
        auto system = initializedSystem();
        auto before = system->snapshot();
        nr::test::require(system->trySchedule(request(*system, keys::viewerWindowFullscreen, true)).started);

        auto stillBefore = system->snapshot();
        nr::test::requireEqual(stillBefore, before, "read-only/minimized iteration must not publish");
        nr::test::require(system->hasPendingMutation(), "pending must survive without a renderable frame");
        nr::test::requireEqual(stillBefore->frameIndex, std::uint64_t{0u});

        auto frame = system->beginRenderableFrame();
        nr::test::require(frame && frame->mutation);
        nr::test::require(system->discardMutation(std::move(*frame->mutation)));
        auto after = finishFrame(*system);
        nr::test::requireEqual(after->frameIndex, std::uint64_t{1u});
        nr::test::require(!*after->find(keys::viewerWindowFullscreen),
                          "discarded attempt must not change canonical value");

        auto nextFrame = system->beginRenderableFrame();
        nr::test::require(nextFrame && !nextFrame->mutation,
                          "failed/discarded mutation must never be restored or retried");
        (void)finishFrame(*system);
    }};

const nr::test::CaseRegistrar availabilityCase{
    "published conservative availability gates only mutation admission", [] {
        auto system = initializedSystem();
        auto frame = system->beginRenderableFrame();
        nr::test::require(frame && !frame->mutation);
        auto availability = allAvailable(*system->activeCatalog());
        availability.insert_or_assign(optionId(keys::viewerWindowFullscreen), OptionAvailability{
                                                                                  .available = false,
                                                                                  .reason = "window_transition_busy",
                                                                              });
        auto unavailableSnapshot = system->publishRenderableFrame(availability);
        nr::test::requireEqual(unavailableSnapshot->findAvailability(optionId(keys::viewerWindowFullscreen))->reason,
                               std::string{"window_transition_busy"});
        nr::test::requireEqual(system->trySchedule(request(*system, keys::viewerWindowFullscreen, true)).reason,
                               ScheduleRejectReason::unavailable);
        nr::test::require(unavailableSnapshot->find(keys::viewerWindowFullscreen) != nullptr,
                          "unavailability must not hide the option from read-only snapshots");

        frame = system->beginRenderableFrame();
        nr::test::require(frame && !frame->mutation);
        availability.insert_or_assign(optionId(keys::viewerWindowFullscreen),
                                      OptionAvailability{
                                          .available = false,
                                          .reason = std::string(maximumAvailabilityReasonBytes + 1u, 'x'),
                                      });
        auto boundedSnapshot = system->publishRenderableFrame(availability);
        nr::test::requireEqual(boundedSnapshot->findAvailability(optionId(keys::viewerWindowFullscreen))->reason,
                               std::string{"invalid_availability_reason"},
                               "snapshot availability reasons must remain inside the preflighted response bound");
    }};

const nr::test::CaseRegistrar graphReplacementCase{
    "graph replacement preserves session values resets graph defaults and invalidates old proof", [] {
        auto system = initializedSystem();
        auto oldSnapshot = system->snapshot();

        auto graphDefinitions = makePathTracingDefinitions();
        auto graph = catalog(graphDefinitions);
        auto frame = system->beginRenderableFrame();
        nr::test::require(frame && !frame->mutation);
        nr::test::require(system->replaceGraphCatalog(graph).committed);
        auto installed = finishFrame(*system);
        nr::test::require(installed->find(keys::pathTracingMaxSurfaceBounces) != nullptr);
        nr::test::require(oldSnapshot->find(keys::pathTracingMaxSurfaceBounces) == nullptr,
                          "held old snapshot must remain immutable");
        nr::test::requireEqual(installed->graphGeneration, std::uint64_t{1u});

        auto stale = request(*system, keys::pathTracingMaxSurfaceBounces, std::uint64_t{24u});
        stale.binding = BindingProof{.snapshotToken = oldSnapshot->snapshotToken};
        nr::test::requireEqual(system->trySchedule(std::move(stale)).reason, ScheduleRejectReason::staleSnapshot);

        nr::test::require(
            system->trySchedule(request(*system, keys::pathTracingMaxSurfaceBounces, std::uint64_t{24u})).started);
        frame = system->beginRenderableFrame();
        nr::test::require(system->commitCanonical(std::move(*frame->mutation)).committed);
        auto changed = finishFrame(*system);
        nr::test::requireEqual(*changed->find(keys::pathTracingMaxSurfaceBounces), std::uint64_t{24u});

        frame = system->beginRenderableFrame();
        nr::test::require(system->replaceGraphCatalog(graph).committed);
        auto reset = finishFrame(*system);
        nr::test::requireEqual(*reset->find(keys::pathTracingMaxSurfaceBounces), std::uint64_t{16u});
        nr::test::requireEqual(*reset->find(keys::viewerWindowFullscreen), false,
                               "session value must remain present across graph replacement");
    }};

const nr::test::CaseRegistrar graphMutationReplacementCase{
    "graph-switch mutation commits its session value and catalog under the old binding", [] {
        auto system = initializedSystem();
        auto graphDefinitions = makePathTracingDefinitions();
        auto graph = catalog(graphDefinitions);
        auto frame = system->beginRenderableFrame();
        nr::test::require(system->replaceGraphCatalog(graph).committed);
        (void)finishFrame(*system);

        auto before = system->liveBinding();
        nr::test::require(
            system->trySchedule(request(*system, keys::viewerPipelineSelected, std::string{"rtobject"})).started);
        frame = system->beginRenderableFrame();
        auto replacementDefinitions = makeAccumulateDefinitions();
        auto replacement = catalog(replacementDefinitions);
        nr::test::require(system->commitGraphReplacement(std::move(*frame->mutation), replacement).committed);
        auto after = finishFrame(*system);

        nr::test::requireEqual(*after->find(keys::viewerPipelineSelected), std::string{"rtobject"});
        nr::test::require(after->find(keys::pathTracingMaxSurfaceBounces) == nullptr);
        nr::test::requireEqual(*after->find(keys::accumulateMaxHistorySamples), std::uint64_t{1024u});
        nr::test::requireEqual(after->bindingEpoch, before.bindingEpoch + 1u);
        nr::test::requireEqual(after->graphGeneration, before.graphGeneration + 1u);
    }};

const nr::test::CaseRegistrar effectCase{
    "frame effects are one-shot and never become a retained operation result", [] {
        auto system = initializedSystem();
        auto graphDefinitions = makePresentDefinitions();
        auto graph = catalog(graphDefinitions);
        auto frame = system->beginRenderableFrame();
        nr::test::require(system->replaceGraphCatalog(graph).committed);
        (void)finishFrame(*system);

        nr::test::require(
            system->trySchedule(request(*system, keys::presentCaptureExr, OptionWireValue::Object{})).started);
        frame = system->beginRenderableFrame();
        auto materialized = system->materializeFrameEffect(std::move(*frame->mutation));
        nr::test::require(materialized.effect.has_value());
        auto withEffect = finishFrame(*system, std::move(materialized.effect));
        nr::test::require(withEffect->effect.has_value());
        nr::test::requireEqual(withEffect->effect->id, optionId(keys::presentCaptureExr));
        nr::test::requireEqual(withEffect->findAvailability(optionId(keys::presentCaptureExr))->available, false,
                               "effect must publish provisional busy availability");

        frame = system->beginRenderableFrame();
        nr::test::require(frame && !frame->mutation);
        auto next = finishFrame(*system);
        nr::test::require(!next->effect.has_value(), "effect must disappear from the next snapshot");
        nr::test::require(next->findAvailability(optionId(keys::presentCaptureExr))->available,
                          "capture availability may recover only in a later published snapshot");
    }};

const nr::test::CaseRegistrar derivedModelCase{
    "model commit atomically applies the explicit camera reset exception", [] {
        auto system = initializedSystem();
        auto const modelSource = std::string{"glTF-Sample-Assets/Models/AnimatedCube/glTF/AnimatedCube.gltf"};
        nr::test::require(system->trySchedule(request(*system, keys::viewerModelSource, modelSource)).started);
        auto frame = system->beginRenderableFrame();
        auto camera = CameraResetValues{
            .pose =
                {
                    {"position",
                     OptionWireValue::Array{
                         OptionWireValue{1.0},
                         OptionWireValue{2.0},
                         OptionWireValue{3.0},
                     }},
                    {"yaw_degrees", OptionWireValue{45.0}},
                    {"pitch_degrees", OptionWireValue{-10.0}},
                },
            .verticalFovDegrees = 72u,
            .clipPlanes =
                {
                    {"near", OptionWireValue{0.05}},
                    {"far", OptionWireValue{500.0}},
                },
        };
        nr::test::require(system->commitModelAndCameraReset(std::move(*frame->mutation), std::move(camera)).committed);
        auto snapshot = finishFrame(*system);
        nr::test::requireEqual(*snapshot->find(keys::viewerModelSource), modelSource);
        nr::test::requireEqual(*snapshot->find(keys::viewerCameraVerticalFovDegrees), std::uint64_t{72u});
        auto const *pose = snapshot->find(keys::viewerCameraPose);
        nr::test::require(pose != nullptr && pose->contains("position"));
    }};

const nr::test::CaseRegistrar shutdownCase{
    "shutdown abandons one pending mutation and keeps the last snapshot readable", [] {
        auto system = initializedSystem();
        auto readable = system->snapshot();
        auto started = system->trySchedule(request(*system, keys::viewerWindowFullscreen, true));
        nr::test::require(started.started);

        auto abandoned = system->shutdown();
        nr::test::require(abandoned.has_value());
        nr::test::requireEqual(abandoned->sequence, started.sequence);
        nr::test::require(!system->hasPendingMutation());
        nr::test::requireEqual(system->snapshot(), readable);
        nr::test::requireEqual(system->trySchedule(request(*system, keys::viewerWindowFullscreen, true)).reason,
                               ScheduleRejectReason::shutdown);
        nr::test::require(!system->beginRenderableFrame().has_value());
    }};

const nr::test::CaseRegistrar machineRecordCase{
    "option machine records are compact UTF-8 JSON with stable field names", [] {
        auto dispatch = OptionMachineRecord{
            .sequence = 183u,
            .id = optionId(keys::presentCaptureExr),
            .phase = OptionLogPhase::dispatchStarted,
            .status = OptionLogStatus::started,
            .frameIndex = 41u,
            .origin = MutationOrigin::websocket,
        };
        auto dispatchSerialized = serializeMachineRecord(dispatch);
        auto dispatchJson = parseJsonValue(dispatchSerialized);
        nr::test::requireEqual(jsonStringField(dispatchJson, "phase"), std::string_view{"dispatch"});
        nr::test::requireEqual(jsonStringField(dispatchJson, "status"), std::string_view{"started"});

        auto record = OptionMachineRecord{
            .sequence = 184u,
            .id = optionId(keys::presentCaptureExr),
            .phase = OptionLogPhase::terminal,
            .status = OptionLogStatus::failed,
            .frameIndex = 42u,
            .origin = MutationOrigin::websocket,
            .requestId = std::string{"capture-\"one\""},
            .reason = std::string{"write\nfailed"},
        };
        auto serialized = serializeMachineRecord(record);
        auto recordJson = parseJsonValue(serialized);
        nr::test::requireEqual(jsonStringField(recordJson, "option_id"),
                               std::string_view{"render.present.capture_exr"});
        nr::test::requireEqual(jsonStringField(recordJson, "request_id"), std::string_view{"capture-\"one\""});
        nr::test::requireEqual(jsonStringField(recordJson, "reason"), std::string_view{"write\nfailed"});
        nr::test::require(!serialized.contains('\n'), "machine payload must be one line");

        record.requestId = std::string{"request-"};
        record.requestId->push_back(static_cast<char>(0xff));
        record.reason = std::string{"reason-"};
        record.reason->push_back(static_cast<char>(0xc3));
        serialized = serializeMachineRecord(record);
        recordJson = parseJsonValue(serialized);
        nr::test::requireEqual(jsonStringField(recordJson, "request_id"), std::string_view{"request_id_invalid_utf8"});
        nr::test::requireEqual(jsonStringField(recordJson, "reason"), std::string_view{"reason_invalid_utf8"});

        record.requestId = std::string{"capture-\"one\""};
        record.reason = std::string(4u * 1024u, '\x01');
        serialized = serializeMachineRecord(record);
        recordJson = parseJsonValue(serialized);
        nr::test::requireEqual(jsonStringField(recordJson, "reason").size(), std::size_t{4u * 1024u});

        record.requestId = std::string(129u, 'r');
        record.reason = std::string(4u * 1024u + 1u, 'x');
        serialized = serializeMachineRecord(record);
        recordJson = parseJsonValue(serialized);
        nr::test::requireEqual(jsonStringField(recordJson, "request_id"),
                               std::string_view{"request_id_exceeded_limit"});
        nr::test::requireEqual(jsonStringField(recordJson, "reason"), std::string_view{"reason_exceeded_limit"});
    }};

const nr::test::CaseRegistrar authorityCase{
    "authority mode keeps all entry points on one admission API", [] {
        auto agent = initializedSystem(AuthorityMode::agent);
        nr::test::requireEqual(agent->trySchedule(request(*agent, keys::viewerWindowFullscreen, true)).reason,
                               ScheduleRejectReason::unauthorizedOrigin);
        auto websocket = request(*agent, keys::viewerWindowFullscreen, true, MutationOrigin::websocket);
        nr::test::require(agent->trySchedule(std::move(websocket)).started);

        auto lua = initializedSystem(AuthorityMode::offlineLua);
        auto luaRequest = request(*lua, keys::viewerWindowFullscreen, true, MutationOrigin::lua);
        nr::test::require(lua->trySchedule(std::move(luaRequest)).started);
    }};

const nr::test::CaseRegistrar atomicReadCase{
    "published option snapshots support concurrent immutable readers", [] {
        auto system = initializedSystem();
        auto stop = std::atomic_bool{false};
        auto failed = std::atomic_bool{false};
        auto reader = std::jthread{[&] {
            auto lastRevision = std::uint64_t{0u};
            while (!stop.load(std::memory_order_acquire))
            {
                auto current = system->snapshot();
                if (!current || !current->catalog || current->revision < lastRevision)
                {
                    failed.store(true, std::memory_order_release);
                    return;
                }
                lastRevision = current->revision;
            }
        }};

        auto frames = std::views::iota(0u, 64u);
        std::ranges::for_each(frames, [&](auto) {
            auto frame = system->beginRenderableFrame();
            nr::test::require(frame.has_value());
            nr::test::require(finishFrame(*system) != nullptr);
        });
        stop.store(true, std::memory_order_release);
        reader.join();
        nr::test::require(!failed.load(std::memory_order_acquire));
        nr::test::requireEqual(system->snapshot()->frameIndex, std::uint64_t{64u});
    }};
} // namespace
