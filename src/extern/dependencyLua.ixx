export module dependency.lua;

import std;

export namespace dependency::lua
{
struct Value
{
    using Array = std::vector<Value>;
    using Object = std::map<std::string, Value, std::less<>>;
    using Storage = std::variant<std::monostate, bool, std::int64_t, double, std::string, Array, Object>;

    Value() = default;
    Value(bool value) : storage(value)
    {
    }
    Value(std::int64_t value) : storage(value)
    {
    }
    Value(double value) : storage(value)
    {
    }
    Value(std::string value) : storage(std::move(value))
    {
    }
    Value(std::string_view value) : storage(std::string{value})
    {
    }
    Value(const char *value) : storage(std::string{value})
    {
    }
    Value(Array value) : storage(std::move(value))
    {
    }
    Value(Object value) : storage(std::move(value))
    {
    }

    Storage storage{};

    [[nodiscard]] friend bool operator==(const Value &, const Value &) = default;
};

struct SandboxLimits
{
    std::size_t maximumAllocatedBytes = 32u * 1024u * 1024u;
    std::size_t maximumSourceBytes = 256u * 1024u;
    std::uint32_t maximumInstructionsPerResume = 100'000u;
    std::uint32_t hookInstructionInterval = 1'000u;
    std::chrono::milliseconds softWallBudget{5};
    std::size_t maximumConversionDepth = 16u;
    std::size_t maximumTableEntries = 4'096u;
    std::size_t maximumConvertedNodes = 16'384u;
    std::size_t maximumStringBytes = 64u * 1024u;
};

struct HostCallResult
{
    std::vector<Value> values{};
    std::string error{};
    bool yield = false;

    [[nodiscard]] static HostCallResult success(std::vector<Value> values = {})
    {
        return HostCallResult{.values = std::move(values)};
    }

    [[nodiscard]] static HostCallResult failure(std::string error)
    {
        return HostCallResult{.error = std::move(error)};
    }

    [[nodiscard]] static HostCallResult suspend() noexcept
    {
        return HostCallResult{.yield = true};
    }
};

using HostFunction = std::function<HostCallResult(std::span<const Value>)>;

struct HostFunctionBinding
{
    std::string dottedName{};
    HostFunction function{};
};

struct SandboxConfig
{
    SandboxLimits limits{};
    std::vector<HostFunctionBinding> hostFunctions{};
};

enum class SandboxStartError : std::uint8_t
{
    none,
    alreadyStarted,
    invalidConfiguration,
    sourceTooLarge,
    allocationFailed,
    loadFailed,
};

struct SandboxStartResult
{
    bool started = false;
    SandboxStartError error = SandboxStartError::none;
    std::string detail{};
};

enum class ResumeStatus : std::uint8_t
{
    yielded,
    completed,
    failed,
    notStarted,
};

struct ResumeResult
{
    ResumeStatus status = ResumeStatus::notStarted;
    std::string detail{};
};

class Sandbox
{
  public:
    Sandbox();
    ~Sandbox();

    Sandbox(const Sandbox &) = delete;
    Sandbox &operator=(const Sandbox &) = delete;
    Sandbox(Sandbox &&) = delete;
    Sandbox &operator=(Sandbox &&) = delete;

    [[nodiscard]] SandboxStartResult start(std::string source, std::string chunkName, SandboxConfig config);
    [[nodiscard]] ResumeResult resume();
    void close() noexcept;
    [[nodiscard]] bool started() const noexcept;
    [[nodiscard]] std::size_t allocatedBytes() const noexcept;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dependency::lua
