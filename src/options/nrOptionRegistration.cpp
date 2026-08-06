module nr.options;

import :model;
import :registration;
import nr.utils;
import std;

namespace nr::options
{
namespace
{
[[nodiscard]] bool hasForbiddenPathForm(std::string_view value)
{
    if (value.empty())
    {
        return true;
    }
    auto const path = std::filesystem::path{value};
    return path.is_absolute() || path.has_root_name() || path.has_root_directory() || value.starts_with("//") ||
           value.starts_with(R"(\\)") || value.starts_with(R"(\\?\)") || value.starts_with(R"(\\.\)") ||
           value.contains("://") || value.find_first_of("|&;<>\n\r\t\"'`$*?") != std::string_view::npos;
}

[[nodiscard]] bool isRegularFileBelow(const std::filesystem::path &resolved, const std::filesystem::path &root)
{
    auto error = std::error_code{};
    if (!std::filesystem::is_regular_file(resolved, error) || error)
    {
        return false;
    }

    auto current = resolved.parent_path();
    while (!current.empty())
    {
        if (std::filesystem::equivalent(current, root, error))
        {
            return !error;
        }
        if (error)
        {
            return false;
        }
        auto const parent = current.parent_path();
        if (parent == current)
        {
            break;
        }
        current = parent;
    }
    return false;
}

[[nodiscard]] OptionAdmissionValidator modelSourceValidator()
{
    return [](const OptionWireValue &value, const OptionValueMap &) -> std::optional<std::string> {
        auto const &source = std::get<std::string>(value.storage);
        if (hasForbiddenPathForm(source))
        {
            return "model_source_must_be_assets_root_relative";
        }

        auto error = std::error_code{};
        auto const root =
            std::filesystem::canonical(std::filesystem::path{std::string{nr::projectRoot}} / "assets", error);
        if (error)
        {
            return "model_asset_root_unavailable";
        }
        auto const resolved = std::filesystem::canonical(root / std::filesystem::path{source}, error);
        if (error || !isRegularFileBelow(resolved, root))
        {
            return "model_source_outside_assets_or_missing";
        }
        return {};
    };
}

[[nodiscard]] constexpr std::size_t saturatedAdd(std::size_t left, std::size_t right) noexcept
{
    return right > std::numeric_limits<std::size_t>::max() - left ? std::numeric_limits<std::size_t>::max()
                                                                  : left + right;
}

[[nodiscard]] constexpr std::size_t saturatedMultiply(std::size_t left, std::size_t right) noexcept
{
    if (left == 0u || right == 0u)
    {
        return 0u;
    }
    return right > std::numeric_limits<std::size_t>::max() / left ? std::numeric_limits<std::size_t>::max()
                                                                  : left * right;
}

[[nodiscard]] std::size_t estimateWireBytes(const OptionWireValue &value)
{
    return std::visit(
        [](auto const &stored) -> std::size_t {
            using Stored = std::remove_cvref_t<decltype(stored)>;
            if constexpr (std::same_as<Stored, bool>)
            {
                return 5u;
            }
            else if constexpr (std::integral<Stored> || std::floating_point<Stored>)
            {
                return 32u;
            }
            else if constexpr (std::same_as<Stored, std::string>)
            {
                return stored.size() * 6u + 2u;
            }
            else if constexpr (std::same_as<Stored, OptionWireValue::Array>)
            {
                return 2u + std::transform_reduce(stored.begin(), stored.end(), std::size_t{0u}, std::plus{},
                                                  [](auto const &element) { return estimateWireBytes(element) + 1u; });
            }
            else
            {
                return 2u + std::transform_reduce(
                                stored.begin(), stored.end(), std::size_t{0u}, std::plus{}, [](auto const &entry) {
                                    return entry.first.size() * 6u + estimateWireBytes(entry.second) + 4u;
                                });
            }
        },
        value.storage);
}

[[nodiscard]] std::size_t estimateMaximumWireBytes(const OptionSchema &schema)
{
    switch (schema.type)
    {
    case OptionValueType::boolean:
        return 5u;
    case OptionValueType::signedInteger:
    case OptionValueType::unsignedInteger:
    case OptionValueType::number:
        return 32u;
    case OptionValueType::string: {
        auto maximumBytes = schema.maximumSize;
        if (!schema.allowedStrings.empty())
        {
            maximumBytes = std::ranges::max(
                schema.allowedStrings | std::views::transform([](const std::string &value) { return value.size(); }));
        }
        return saturatedAdd(saturatedMultiply(maximumBytes, 6u), 2u);
    }
    case OptionValueType::array: {
        if (!schema.elementSchema)
        {
            return std::numeric_limits<std::size_t>::max();
        }
        auto const elementBytes = saturatedAdd(estimateMaximumWireBytes(*schema.elementSchema), 1u);
        return saturatedAdd(2u, saturatedMultiply(schema.maximumSize, elementBytes));
    }
    case OptionValueType::object: {
        auto result = std::size_t{2u};
        std::ranges::for_each(schema.objectFields, [&](auto const &entry) {
            auto const fieldBytes = entry.second.schema ? estimateMaximumWireBytes(*entry.second.schema)
                                                        : std::numeric_limits<std::size_t>::max();
            result = saturatedAdd(
                result, saturatedAdd(saturatedMultiply(entry.first.size(), 6u), saturatedAdd(fieldBytes, 4u)));
        });
        return result;
    }
    }
    std::unreachable();
}

[[nodiscard]] std::size_t estimateSchemaBytes(const OptionSchema &schema)
{
    auto estimate = std::size_t{256u};
    std::ranges::for_each(schema.allowedStrings, [&](const std::string &value) {
        estimate = saturatedAdd(estimate, saturatedAdd(saturatedMultiply(value.size(), 6u), 3u));
    });
    if (schema.elementSchema)
    {
        estimate = saturatedAdd(estimate, estimateSchemaBytes(*schema.elementSchema));
    }
    std::ranges::for_each(schema.objectFields, [&](const auto &entry) {
        auto fieldEstimate = saturatedAdd(saturatedMultiply(entry.first.size(), 12u), 64u);
        if (entry.second.schema)
        {
            fieldEstimate = saturatedAdd(fieldEstimate, estimateSchemaBytes(*entry.second.schema));
        }
        estimate = saturatedAdd(estimate, fieldEstimate);
    });
    return estimate;
}

[[nodiscard]] std::size_t estimateDefinitionBytes(const OptionDefinition &definition)
{
    auto estimate = saturatedAdd(512u, saturatedMultiply(definition.id.value().size(), 6u));
    estimate = saturatedAdd(estimate, saturatedMultiply(definition.presentation.group.size(), 6u));
    estimate = saturatedAdd(estimate, saturatedMultiply(definition.presentation.label.size(), 6u));
    estimate = saturatedAdd(estimate, estimateSchemaBytes(definition.schema));
    estimate = saturatedAdd(estimate, saturatedAdd(saturatedMultiply(maximumAvailabilityReasonBytes, 6u), 64u));
    estimate = saturatedAdd(
        estimate, std::max(estimateWireBytes(definition.defaultValue), estimateMaximumWireBytes(definition.schema)));
    return estimate;
}

[[nodiscard]] std::optional<std::string> validateSchemaDefinition(const OptionSchema &schema, std::size_t depth = 0u)
{
    if (depth > 16u)
    {
        return "schema nesting depth exceeds 16";
    }

    switch (schema.type)
    {
    case OptionValueType::boolean:
        return {};
    case OptionValueType::signedInteger:
        if (!schema.signedMinimum || !schema.signedMaximum || *schema.signedMinimum > *schema.signedMaximum)
        {
            return "signed integer schema requires an ordered closed range";
        }
        return {};
    case OptionValueType::unsignedInteger:
        if (!schema.unsignedMinimum || !schema.unsignedMaximum || *schema.unsignedMinimum > *schema.unsignedMaximum)
        {
            return "unsigned integer schema requires an ordered closed range";
        }
        return {};
    case OptionValueType::number:
        if (!schema.numberMinimum || !schema.numberMaximum || !std::isfinite(*schema.numberMinimum) ||
            !std::isfinite(*schema.numberMaximum) || *schema.numberMinimum > *schema.numberMaximum)
        {
            return "number schema requires an ordered finite closed range";
        }
        return {};
    case OptionValueType::string: {
        if (schema.minimumSize > schema.maximumSize)
        {
            return "string schema has an inverted byte range";
        }
        auto unique = std::set<std::string>{};
        auto issue = std::optional<std::string>{};
        std::ranges::for_each(schema.allowedStrings, [&](const std::string &value) {
            if (issue)
            {
                return;
            }
            if (!unique.emplace(value).second)
            {
                issue = "string schema contains a duplicate enum value";
                return;
            }
            if (!schema.validate(OptionWireValue{value}).valid)
            {
                issue = "string schema contains an invalid enum value";
            }
        });
        return issue;
    }
    case OptionValueType::array:
        if (schema.minimumSize > schema.maximumSize)
        {
            return "array schema has an inverted item range";
        }
        if (!schema.elementSchema)
        {
            return "array schema requires an element schema";
        }
        return validateSchemaDefinition(*schema.elementSchema, depth + 1u);
    case OptionValueType::object: {
        if (!schema.closedObject)
        {
            return "option objects must be closed";
        }
        auto const requiredCount = static_cast<std::size_t>(
            std::ranges::count_if(schema.objectFields, [](const auto &entry) { return entry.second.required; }));
        if (schema.minimumSize > schema.maximumSize || requiredCount > schema.maximumSize ||
            schema.maximumSize > schema.objectFields.size())
        {
            return "object schema field bounds do not match its closed fields";
        }
        auto issue = std::optional<std::string>{};
        std::ranges::for_each(schema.objectFields, [&](const auto &entry) {
            if (issue)
            {
                return;
            }
            if (entry.first.empty())
            {
                issue = "object schema field name must not be empty";
                return;
            }
            if (!entry.second.schema)
            {
                issue = std::format("object schema field '{}' has no schema", entry.first);
                return;
            }
            if (auto nested = validateSchemaDefinition(*entry.second.schema, depth + 1u))
            {
                issue = std::format("object schema field '{}': {}", entry.first, *nested);
            }
        });
        return issue;
    }
    }
    std::unreachable();
}

[[nodiscard]] std::shared_ptr<const OptionSchema> schema(OptionSchema value)
{
    return std::make_shared<const OptionSchema>(std::move(value));
}

[[nodiscard]] const std::string *stringValue(const OptionValueMap &values, OptionKey<std::string> key)
{
    auto const found = values.find(optionId(key));
    if (found == values.end())
    {
        return nullptr;
    }
    return std::get_if<std::string>(&found->second.storage);
}

[[nodiscard]] OptionPresentation ui(std::string group, std::string label, OptionUiControl control, std::int32_t order)
{
    return OptionPresentation{
        .group = std::move(group),
        .label = std::move(label),
        .control = control,
        .order = order,
    };
}
} // namespace

bool OptionCatalogBuilder::add(OptionDefinition definition)
{
    if (!definition.id.valid())
    {
        issues_.push_back(CatalogIssue{
            .code = CatalogIssueCode::invalidId,
            .detail = "option definition has an invalid ID",
        });
        return false;
    }

    auto const id = definition.id;
    if (definitions_.contains(id))
    {
        issues_.push_back(CatalogIssue{
            .code = CatalogIssueCode::duplicateId,
            .id = id,
            .detail = "option ID is declared more than once",
        });
        return false;
    }

    if (auto issue = validateSchemaDefinition(definition.schema))
    {
        issues_.push_back(CatalogIssue{
            .code = CatalogIssueCode::invalidDefinition,
            .id = id,
            .detail = std::move(*issue),
        });
        return false;
    }

    auto validation = definition.schema.validate(definition.defaultValue);
    if (!validation.valid)
    {
        issues_.push_back(CatalogIssue{
            .code = CatalogIssueCode::invalidDefault,
            .id = id,
            .detail = std::format("{}: {}", validation.path, validation.detail),
        });
        return false;
    }

    definitions_.emplace(id, std::move(definition));
    return true;
}

CatalogBuildResult OptionCatalogBuilder::build(std::size_t maximumSnapshotBytes) const
{
    auto result = CatalogBuildResult{.issues = issues_};
    if (!result.issues.empty())
    {
        return result;
    }

    auto estimate = std::size_t{256u};
    std::ranges::for_each(definitions_, [&](auto const &entry) {
        estimate = saturatedAdd(estimate, estimateDefinitionBytes(entry.second));
    });
    if (estimate > maximumSnapshotBytes)
    {
        result.issues.push_back(CatalogIssue{
            .code = CatalogIssueCode::snapshotTooLarge,
            .detail = std::format("estimated snapshot size {} exceeds limit {}", estimate, maximumSnapshotBytes),
        });
        return result;
    }

    auto catalog = std::make_shared<OptionCatalog>();
    catalog->definitions_ = definitions_;
    catalog->estimatedSnapshotBytes_ = estimate;
    result.catalog = std::move(catalog);
    return result;
}

OptionDefinition makeBooleanDefinition(OptionKey<bool> key, bool defaultValue, OptionScope scope,
                                       OptionPresentation presentation)
{
    return makeDefinition(key, OptionSchema::boolean(), defaultValue, scope, std::move(presentation));
}

OptionDefinition makeUnsignedDefinition(OptionKey<std::uint64_t> key, std::uint64_t defaultValue, std::uint64_t minimum,
                                        std::uint64_t maximum, OptionScope scope, OptionPresentation presentation)
{
    return makeDefinition(key, OptionSchema::unsignedInteger(minimum, maximum), defaultValue, scope,
                          std::move(presentation));
}

OptionDefinition makeNumberDefinition(OptionKey<double> key, double defaultValue, double minimum, double maximum,
                                      OptionScope scope, OptionPresentation presentation)
{
    return makeDefinition(key, OptionSchema::number(minimum, maximum), defaultValue, scope, std::move(presentation));
}

OptionDefinition makeEnumDefinition(OptionKey<std::string> key, std::string defaultValue,
                                    std::vector<std::string> allowed, OptionScope scope,
                                    OptionPresentation presentation, OptionAdmissionValidator validator)
{
    return makeDefinition(key, OptionSchema::string(4u * 1024u, std::move(allowed)), std::move(defaultValue), scope,
                          std::move(presentation), std::move(validator));
}

OptionDefinition makeEmptyEffectDefinition(OptionKey<OptionWireValue::Object> key, OptionScope scope,
                                           OptionPresentation presentation)
{
    auto definition =
        makeDefinition(key, OptionSchema::emptyObject(), OptionWireValue::Object{}, scope, std::move(presentation));
    definition.lifetime = OptionValueLifetime::frameEffect;
    return definition;
}

std::vector<OptionDefinition> makeSessionDefinitions(const SessionDefinitionSeed &seed)
{
    nrAssert(!seed.environmentNames.empty() && std::ranges::contains(seed.environmentNames, seed.environmentName),
             "Session environment name must be one of the discovered environment names.");

    auto const floatMaximum = static_cast<double>(std::numeric_limits<float>::max());
    auto positionSchema = OptionSchema::array(OptionSchema::number(-floatMaximum, floatMaximum), 3u, 3u);
    auto cameraSchema = OptionSchema::object({
        {"position", OptionObjectField{.schema = schema(std::move(positionSchema))}},
        {"yaw_degrees",
         OptionObjectField{.schema = schema(OptionSchema::number(-180.0, std::nextafter(180.0, -180.0)))}},
        {"pitch_degrees", OptionObjectField{.schema = schema(OptionSchema::number(-89.0, 89.0))}},
    });
    auto clipSchema = OptionSchema::object(
        {
            {"near", OptionObjectField{.schema = schema(OptionSchema::number(0.001, floatMaximum))}},
            {"far", OptionObjectField{.schema = schema(OptionSchema::number(0.001, floatMaximum))}},
        },
        [](const OptionWireValue::Object &object) -> std::optional<std::string> {
            auto const nearIt = object.find("near");
            auto const farIt = object.find("far");
            auto const *nearPlane = nearIt != object.end() ? std::get_if<double>(&nearIt->second.storage) : nullptr;
            auto const *farPlane = farIt != object.end() ? std::get_if<double>(&farIt->second.storage) : nullptr;
            return nearPlane != nullptr && farPlane != nullptr && *farPlane > *nearPlane
                       ? std::nullopt
                       : std::optional{std::string{"far must be greater than near"}};
        });

    return {
        makeEnumDefinition(keys::viewerPipelineSelected, seed.selectedPipeline, seed.pipelineIds, OptionScope::session,
                           ui("Viewer", "Pipeline", OptionUiControl::combo, 10)),
        makeDefinition(keys::viewerModelSource, OptionSchema::string(4u * 1024u), seed.modelSource,
                       OptionScope::session, ui("Viewer", "Model", OptionUiControl::input, 20), modelSourceValidator()),
        makeEnumDefinition(keys::viewerEnvironmentSource, seed.environmentName, seed.environmentNames,
                           OptionScope::session, ui("Viewer", "Environment", OptionUiControl::combo, 30)),
        makeEnumDefinition(keys::viewerRtPostProcessingMode, seed.postProcessingMode,
                           {"accumulate", "dlss_ray_reconstruction"}, OptionScope::session,
                           ui("Viewer", "RT post processing", OptionUiControl::combo, 40)),
        makeBooleanDefinition(keys::viewerWindowFullscreen, seed.fullscreen, OptionScope::session,
                              ui("Viewer", "Fullscreen", OptionUiControl::checkbox, 50)),
        makeEmptyEffectDefinition(keys::viewerExit, OptionScope::session,
                                  ui("Viewer", "Exit", OptionUiControl::button, 60)),
        makeDefinition(keys::viewerCameraPose, std::move(cameraSchema), seed.cameraPose, OptionScope::session,
                       ui("Camera", "Pose", OptionUiControl::hidden, 10)),
        makeUnsignedDefinition(keys::viewerCameraVerticalFovDegrees, seed.verticalFovDegrees, 1u, 179u,
                               OptionScope::session, ui("Camera", "Vertical FOV", OptionUiControl::slider, 20)),
        makeDefinition(keys::viewerCameraClipPlanes, std::move(clipSchema), seed.clipPlanes, OptionScope::session,
                       ui("Camera", "Clip planes", OptionUiControl::input, 30)),
        makeNumberDefinition(keys::viewerCameraMovementSpeed, seed.cameraMovementSpeed, 0.01, 1000.0,
                             OptionScope::session, ui("Camera", "Movement speed", OptionUiControl::input, 40)),
    };
}

std::vector<OptionDefinition> makePathTracingDefinitions()
{
    auto definitions = std::vector<OptionDefinition>{
        makeUnsignedDefinition(keys::pathTracingMaxSurfaceBounces, 16u, 1u, 64u, OptionScope::graph,
                               ui("Path tracing", "Max surface bounces", OptionUiControl::slider, 10)),
        makeBooleanDefinition(keys::pathTracingRussianRouletteEnabled, true, OptionScope::graph,
                              ui("Path tracing", "Russian roulette", OptionUiControl::checkbox, 20)),
        makeBooleanDefinition(keys::pathTracingFilterAfterShadingEnabled, false, OptionScope::graph,
                              ui("Path tracing", "Filter after shading", OptionUiControl::checkbox, 30)),
    };
    std::ranges::for_each(definitions,
                          [](OptionDefinition &definition) { definition.resetsTemporalHistory = true; });
    return definitions;
}

std::vector<OptionDefinition> makeAccumulateDefinitions()
{
    return {
        makeUnsignedDefinition(keys::accumulateMaxHistorySamples, 1024u, 1u, 4096u, OptionScope::graph,
                               ui("Accumulation", "Max history samples", OptionUiControl::slider, 10)),
    };
}

std::vector<OptionDefinition> makeDlssDefinitions(std::string initialQuality)
{
    auto qualityValidator = [](const OptionWireValue &candidate,
                               const OptionValueMap &values) -> std::optional<std::string> {
        auto const *quality = std::get_if<std::string>(&candidate.storage);
        auto const bypassIt = values.find(optionId(keys::dlssBypass));
        auto const *bypass = bypassIt != values.end() ? std::get_if<bool>(&bypassIt->second.storage) : nullptr;
        return quality != nullptr && bypass != nullptr && *bypass && *quality != "dlaa"
                   ? std::optional{std::string{"DLSS bypass requires DLAA quality"}}
                   : std::nullopt;
    };
    auto bypassValidator = [](const OptionWireValue &candidate,
                              const OptionValueMap &values) -> std::optional<std::string> {
        auto const *bypass = std::get_if<bool>(&candidate.storage);
        auto const *quality = stringValue(values, keys::dlssQuality);
        return bypass != nullptr && *bypass && (quality == nullptr || *quality != "dlaa")
                   ? std::optional{std::string{"DLSS bypass can only be enabled in DLAA quality"}}
                   : std::nullopt;
    };
    auto const qualityValues =
        std::vector<std::string>{"performance", "balanced", "quality", "ultra_performance", "dlaa"};

    return {
        makeBooleanDefinition(keys::dlssEnabled, true, OptionScope::graph,
                              ui("DLSS", "Enabled", OptionUiControl::checkbox, 10)),
        makeEnumDefinition(keys::dlssQuality, std::move(initialQuality), qualityValues, OptionScope::graph,
                           ui("DLSS", "Quality", OptionUiControl::combo, 20), std::move(qualityValidator)),
        OptionDefinition{
            .id = optionId(keys::dlssBypass),
            .schema = OptionSchema::boolean(),
            .defaultValue = false,
            .scope = OptionScope::graph,
            .presentation = ui("DLSS", "Bypass", OptionUiControl::checkbox, 30),
            .admissionValidator = std::move(bypassValidator),
        },
        makeBooleanDefinition(keys::dlssVisualizeMotionVectors, false, OptionScope::graph,
                              ui("DLSS", "Visualize motion vectors", OptionUiControl::checkbox, 40)),
        makeEmptyEffectDefinition(keys::dlssResetHistory, OptionScope::graph,
                                  ui("DLSS", "Reset history", OptionUiControl::button, 50)),
    };
}

std::vector<OptionDefinition> makePresentDefinitions()
{
    return {
        makeEnumDefinition(keys::presentToneMapping, "auto", {"auto", "none", "reinhard", "aces_filmic", "bt2390_eetf"},
                           OptionScope::graph, ui("Present", "Tone mapping", OptionUiControl::combo, 10)),
        makeNumberDefinition(keys::presentUiOpacity, 1.0, 0.0, 1.0, OptionScope::graph,
                             ui("Present", "UI opacity", OptionUiControl::slider, 20)),
        makeEmptyEffectDefinition(keys::presentCaptureExr, OptionScope::graph,
                                  ui("Present", "Capture EXR", OptionUiControl::button, 30)),
    };
}
} // namespace nr::options
