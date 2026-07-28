module nr.app;

import :optionUi;
import nr.options;
import nr.utils;
import std;

namespace nr::app
{
namespace
{
[[nodiscard]] std::string optionLabel(const nr::options::OptionDefinition &definition)
{
    return definition.presentation.label.empty() ? std::string{definition.id.value()} : definition.presentation.label;
}

[[nodiscard]] bool optionAvailable(const nr::options::OptionFrameSnapshot &snapshot, const nr::options::OptionDefinition &definition) noexcept
{
    auto const *availability = snapshot.findAvailability(definition.id);
    return availability != nullptr && availability->available;
}
} // namespace

OptionUiPresenter::DraftState &OptionUiPresenter::draftFor(const nr::options::OptionDefinition &definition, const nr::options::OptionWireValue &canonical)
{
    auto [iterator, inserted] = drafts_.try_emplace(definition.id, DraftState{
                                                                       .canonical = canonical,
                                                                       .value = canonical,
                                                                   });
    if (!inserted && iterator->second.canonical != canonical)
    {
        iterator->second = DraftState{
            .canonical = canonical,
            .value = canonical,
        };
    }
    return iterator->second;
}

bool OptionUiPresenter::schedule(nr::options::OptionSystem &system, const nr::options::OptionFrameSnapshot &snapshot, const nr::options::OptionDefinition &definition, nr::options::OptionWireValue value, OptionUiPresentResult &result)
{
    result.mutationAttempted = true;
    auto scheduled = system.trySchedule(nr::options::OptionMutationRequest{
        .id = definition.id,
        .value = std::move(value),
        .binding = nr::options::BindingProof{.bindingEpoch = snapshot.bindingEpoch},
        .origin = nr::options::MutationOrigin::imgui,
    });
    if (!scheduled.started)
    {
        drafts_.erase(definition.id);
        return false;
    }

    blockedRevision_ = snapshot.revision;
    result.mutationStarted = true;
    return true;
}

void OptionUiPresenter::drawReadOnlyOption(UiSystem &ui, const nr::options::OptionFrameSnapshot &snapshot, const nr::options::OptionDefinition &definition) const
{
    auto const *canonical = snapshot.findValue(definition.id);
    if (canonical == nullptr || definition.presentation.control == nr::options::OptionUiControl::hidden)
    {
        return;
    }

    ui.beginDisabled(true);
    auto const closeDisabled = [&ui] { ui.endDisabled(); };
    auto const label = optionLabel(definition);

    switch (definition.schema.type)
    {
    case nr::options::OptionValueType::boolean: {
        auto value = std::get<bool>(canonical->storage);
        static_cast<void>(ui.checkbox(label, value));
        closeDisabled();
        return;
    }
    case nr::options::OptionValueType::string: {
        auto value = std::get<std::string>(canonical->storage);
        if (!definition.schema.allowedStrings.empty())
        {
            if (ui.beginCombo(label, value))
            {
                std::ranges::for_each(definition.schema.allowedStrings, [&](const std::string &candidate) {
                    auto const selected = candidate == value;
                    static_cast<void>(ui.selectable(candidate, selected));
                    if (selected)
                    {
                        ui.setItemDefaultFocus();
                    }
                });
                ui.endCombo();
            }
        }
        else
        {
            static_cast<void>(ui.inputText(label, value));
        }
        closeDisabled();
        return;
    }
    case nr::options::OptionValueType::unsignedInteger: {
        auto value = static_cast<std::uint32_t>(std::get<std::uint64_t>(canonical->storage));
        auto const minimum = static_cast<std::uint32_t>(definition.schema.unsignedMinimum.value_or(0u));
        auto const maximum = static_cast<std::uint32_t>(definition.schema.unsignedMaximum.value_or(std::numeric_limits<std::uint32_t>::max()));
        if (definition.presentation.control == nr::options::OptionUiControl::slider)
        {
            static_cast<void>(ui.sliderUInt(label, value, minimum, maximum));
        }
        else
        {
            static_cast<void>(ui.inputUInt(label, value, minimum, maximum));
        }
        closeDisabled();
        return;
    }
    case nr::options::OptionValueType::number: {
        auto value = static_cast<float>(std::get<double>(canonical->storage));
        auto const minimum = static_cast<float>(definition.schema.numberMinimum.value_or(-1.0e6));
        auto const maximum = static_cast<float>(definition.schema.numberMaximum.value_or(1.0e6));
        if (definition.presentation.control == nr::options::OptionUiControl::slider)
        {
            static_cast<void>(ui.sliderFloat(label, value, minimum, maximum));
        }
        else
        {
            static_cast<void>(ui.inputFloat(label, value, minimum, maximum));
        }
        closeDisabled();
        return;
    }
    case nr::options::OptionValueType::object: {
        if (definition.lifetime == nr::options::OptionValueLifetime::frameEffect)
        {
            static_cast<void>(ui.button(label));
            closeDisabled();
            return;
        }

        auto const &object = std::get<nr::options::OptionWireValue::Object>(canonical->storage);
        auto nearValue = static_cast<float>(std::get<double>(object.at("near").storage));
        auto farValue = static_cast<float>(std::get<double>(object.at("far").storage));
        static_cast<void>(ui.inputFloat(std::format("{} Near", label), nearValue, 0.001f, std::max(farValue - 0.001f, 0.001f)));
        static_cast<void>(ui.inputFloat(std::format("{} Far", label), farValue, std::max(nearValue + 0.001f, 0.002f), std::numeric_limits<float>::max()));
        closeDisabled();
        return;
    }
    case nr::options::OptionValueType::signedInteger:
    case nr::options::OptionValueType::array:
        ui.text(std::format("{} (unsupported presentation)", label));
        closeDisabled();
        return;
    }
    std::unreachable();
}

bool OptionUiPresenter::drawInteractiveOption(UiSystem &ui, nr::options::OptionSystem &system, const nr::options::OptionFrameSnapshot &snapshot, const nr::options::OptionDefinition &definition, OptionUiPresentResult &result)
{
    auto const *canonical = snapshot.findValue(definition.id);
    if (canonical == nullptr || definition.presentation.control == nr::options::OptionUiControl::hidden)
    {
        return false;
    }

    auto const globallyBlocked = blockedRevision_.has_value() && *blockedRevision_ == snapshot.revision;
    ui.beginDisabled(globallyBlocked || !optionAvailable(snapshot, definition));
    auto const closeDisabled = [&ui] { ui.endDisabled(); };
    auto const label = optionLabel(definition);
    auto changed = false;

    switch (definition.schema.type)
    {
    case nr::options::OptionValueType::boolean: {
        auto value = std::get<bool>(canonical->storage);
        changed = ui.checkbox(label, value);
        closeDisabled();
        return changed && schedule(system, snapshot, definition, value, result);
    }
    case nr::options::OptionValueType::string: {
        if (!definition.schema.allowedStrings.empty())
        {
            auto const &value = std::get<std::string>(canonical->storage);
            if (ui.beginCombo(label, value))
            {
                std::ranges::for_each(definition.schema.allowedStrings, [&](const std::string &candidate) {
                    auto const selected = candidate == value;
                    if (!changed && ui.selectable(candidate, selected))
                    {
                        changed = true;
                        static_cast<void>(schedule(system, snapshot, definition, candidate, result));
                    }
                    if (selected)
                    {
                        ui.setItemDefaultFocus();
                    }
                });
                ui.endCombo();
            }
            closeDisabled();
            return changed && result.mutationStarted;
        }

        auto &draft = draftFor(definition, *canonical);
        auto &value = std::get<std::string>(draft.value.storage);
        static_cast<void>(ui.inputText(label, value));
        auto const commit = ui.itemEditCommitted();
        closeDisabled();
        return commit && schedule(system, snapshot, definition, value, result);
    }
    case nr::options::OptionValueType::unsignedInteger: {
        auto &draft = draftFor(definition, *canonical);
        auto value = static_cast<std::uint32_t>(std::get<std::uint64_t>(draft.value.storage));
        auto const minimum = static_cast<std::uint32_t>(definition.schema.unsignedMinimum.value_or(0u));
        auto const maximum = static_cast<std::uint32_t>(definition.schema.unsignedMaximum.value_or(std::numeric_limits<std::uint32_t>::max()));
        if (definition.presentation.control == nr::options::OptionUiControl::slider)
        {
            static_cast<void>(ui.sliderUInt(label, value, minimum, maximum));
        }
        else
        {
            static_cast<void>(ui.inputUInt(label, value, minimum, maximum));
        }
        draft.value = static_cast<std::uint64_t>(value);
        auto const commit = ui.itemEditCommitted();
        closeDisabled();
        return commit && schedule(system, snapshot, definition, static_cast<std::uint64_t>(value), result);
    }
    case nr::options::OptionValueType::number: {
        auto &draft = draftFor(definition, *canonical);
        auto value = static_cast<float>(std::get<double>(draft.value.storage));
        auto const minimum = static_cast<float>(definition.schema.numberMinimum.value_or(-1.0e6));
        auto const maximum = static_cast<float>(definition.schema.numberMaximum.value_or(1.0e6));
        if (definition.presentation.control == nr::options::OptionUiControl::slider)
        {
            static_cast<void>(ui.sliderFloat(label, value, minimum, maximum));
        }
        else
        {
            static_cast<void>(ui.inputFloat(label, value, minimum, maximum));
        }
        draft.value = static_cast<double>(value);
        auto const commit = ui.itemEditCommitted();
        closeDisabled();
        return commit && schedule(system, snapshot, definition, static_cast<double>(value), result);
    }
    case nr::options::OptionValueType::object: {
        if (definition.lifetime == nr::options::OptionValueLifetime::frameEffect)
        {
            changed = ui.button(label);
            closeDisabled();
            return changed && schedule(system, snapshot, definition, nr::options::OptionWireValue::Object{}, result);
        }

        auto &draft = draftFor(definition, *canonical);
        auto &object = std::get<nr::options::OptionWireValue::Object>(draft.value.storage);
        auto nearValue = static_cast<float>(std::get<double>(object.at("near").storage));
        auto farValue = static_cast<float>(std::get<double>(object.at("far").storage));
        static_cast<void>(ui.inputFloat(std::format("{} Near", label), nearValue, 0.001f, std::max(farValue - 0.001f, 0.001f)));
        auto commit = ui.itemEditCommitted();
        static_cast<void>(ui.inputFloat(std::format("{} Far", label), farValue, std::max(nearValue + 0.001f, 0.002f), std::numeric_limits<float>::max()));
        commit = ui.itemEditCommitted() || commit;
        object.insert_or_assign("near", static_cast<double>(nearValue));
        object.insert_or_assign("far", static_cast<double>(farValue));
        closeDisabled();
        return commit && schedule(system, snapshot, definition, object, result);
    }
    case nr::options::OptionValueType::signedInteger:
    case nr::options::OptionValueType::array:
        ui.text(std::format("{} (unsupported presentation)", label));
        closeDisabled();
        return false;
    }
    std::unreachable();
}

OptionUiPresentResult OptionUiPresenter::present(UiSystem &ui, nr::options::OptionSystem &system, std::shared_ptr<const nr::options::OptionFrameSnapshot> snapshot, OptionUiInteractionPolicy interactionPolicy)
{
    auto result = OptionUiPresentResult{};
    if (!snapshot || !snapshot->catalog)
    {
        return result;
    }
    if (interactionPolicy == OptionUiInteractionPolicy::readOnly)
    {
        drafts_.clear();
        blockedRevision_.reset();
    }
    else if (blockedRevision_.has_value() && *blockedRevision_ != snapshot->revision)
    {
        blockedRevision_.reset();
        drafts_.clear();
    }

    auto definitions = snapshot->catalog->definitions() | std::views::values | std::views::filter([](const nr::options::OptionDefinition &definition) { return definition.presentation.control != nr::options::OptionUiControl::hidden; }) |
                       std::views::transform([](const nr::options::OptionDefinition &definition) { return std::cref(definition); }) | std::ranges::to<std::vector>();
    std::ranges::sort(definitions, [](auto lhs, auto rhs) {
        auto const &left = lhs.get();
        auto const &right = rhs.get();
        return std::tuple{left.presentation.group, left.presentation.order, left.id.value()} < std::tuple{right.presentation.group, right.presentation.order, right.id.value()};
    });

    auto sections = std::vector<UiSection>{};
    auto groups = definitions | std::views::chunk_by([](auto lhs, auto rhs) { return lhs.get().presentation.group == rhs.get().presentation.group; });
    std::ranges::for_each(groups, [&](auto group) {
        auto groupDefinitions = group | std::ranges::to<std::vector>();
        auto const &title = groupDefinitions.front().get().presentation.group;
        sections.push_back(UiSection{
            .id = title,
            .title = title,
            .draw =
                [this, &system, snapshot, interactionPolicy, groupDefinitions = std::move(groupDefinitions), &result](UiSystem &sectionUi) {
                    std::ranges::for_each(groupDefinitions, [&](auto definition) {
                        if (interactionPolicy == OptionUiInteractionPolicy::readOnly)
                        {
                            drawReadOnlyOption(sectionUi, *snapshot, definition.get());
                        }
                        else
                        {
                            static_cast<void>(drawInteractiveOption(sectionUi, system, *snapshot, definition.get(), result));
                        }
                    });
                },
        });
    });
    ui.renderSections(sections);
    return result;
}
} // namespace nr::app
