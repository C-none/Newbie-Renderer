export module nr.automation;

import nr.options;
import std;

export namespace nr::automation
{
[[nodiscard]] std::filesystem::path automationRootPath();
[[nodiscard]] std::expected<std::filesystem::path, std::string> resolveAutomationScriptPath(
    const std::filesystem::path &rootRelativePath);

enum class OfflineLuaStartError : std::uint8_t
{
    none,
    alreadyStarted,
    wrongAuthority,
    invalidPath,
    sourceReadFailed,
    sandboxStartFailed,
};

struct OfflineLuaStartResult
{
    bool started = false;
    OfflineLuaStartError error = OfflineLuaStartError::none;
    std::string detail{};
};

enum class OfflineLuaFrameStatus : std::uint8_t
{
    running,
    completed,
    failed,
    notStarted,
};

struct OfflineLuaFrameResult
{
    OfflineLuaFrameStatus status = OfflineLuaFrameStatus::notStarted;
    std::string detail{};
};

class OfflineLuaHost
{
  public:
    OfflineLuaHost();
    ~OfflineLuaHost();

    OfflineLuaHost(const OfflineLuaHost &) = delete;
    OfflineLuaHost &operator=(const OfflineLuaHost &) = delete;
    OfflineLuaHost(OfflineLuaHost &&) = delete;
    OfflineLuaHost &operator=(OfflineLuaHost &&) = delete;

    [[nodiscard]] OfflineLuaStartResult start(nr::options::OptionSystem &optionSystem,
                                              const std::filesystem::path &rootRelativeScript);
    [[nodiscard]] OfflineLuaFrameResult resume(std::shared_ptr<const nr::options::OptionFrameSnapshot> snapshot);
    void stop() noexcept;
    [[nodiscard]] bool started() const noexcept;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace nr::automation
