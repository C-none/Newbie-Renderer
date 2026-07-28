export module nr.app:optionUi;

import nr.options;
import :ui;
import std;

export namespace nr::app
{
enum class OptionUiInteractionPolicy : std::uint8_t
{
    readOnly,
    interactive,
};

struct OptionUiPresentResult
{
    bool mutationAttempted = false;
    bool mutationStarted = false;
};

class OptionUiPresenter
{
  public:
    [[nodiscard]] OptionUiPresentResult present(UiSystem &ui, nr::options::OptionSystem &system, std::shared_ptr<const nr::options::OptionFrameSnapshot> snapshot, OptionUiInteractionPolicy interactionPolicy);

  private:
    struct DraftState
    {
        nr::options::OptionWireValue canonical{};
        nr::options::OptionWireValue value{};
    };

    [[nodiscard]] DraftState &draftFor(const nr::options::OptionDefinition &definition, const nr::options::OptionWireValue &canonical);
    void drawReadOnlyOption(UiSystem &ui, const nr::options::OptionFrameSnapshot &snapshot, const nr::options::OptionDefinition &definition) const;
    [[nodiscard]] bool drawInteractiveOption(UiSystem &ui, nr::options::OptionSystem &system, const nr::options::OptionFrameSnapshot &snapshot, const nr::options::OptionDefinition &definition, OptionUiPresentResult &result);
    [[nodiscard]] bool schedule(nr::options::OptionSystem &system, const nr::options::OptionFrameSnapshot &snapshot, const nr::options::OptionDefinition &definition, nr::options::OptionWireValue value, OptionUiPresentResult &result);

    std::map<nr::options::OptionId, DraftState> drafts_{};
    std::optional<std::uint64_t> blockedRevision_{};
};
} // namespace nr::app
