import dependency.lua;
import nr.automation;
import nr.options;
import nr.test;
import std;

namespace
{
namespace lua = dependency::lua;
namespace automation = nr::automation;
namespace options = nr::options;

struct HostTrace
{
    std::vector<std::string> logMessages{};
    std::size_t applyCalls = 0u;
};

[[nodiscard]] lua::SandboxConfig sandboxConfig(HostTrace &trace)
{
    auto config = lua::SandboxConfig{};
    config.limits.softWallBudget = std::chrono::seconds{5};
    config.hostFunctions = {
        lua::HostFunctionBinding{
            .dottedName = "nr.options.snapshot",
            .function =
                [](std::span<const lua::Value> arguments) {
                    if (!arguments.empty())
                    {
                        return lua::HostCallResult::failure("snapshot expects no arguments");
                    }
                    auto result = lua::Value::Object{
                        {"snapshot_token", lua::Value{"test-token"}},
                    };
                    result.emplace(std::string{"a\0b", 3u}, lua::Value{std::int64_t{7}});
                    return lua::HostCallResult::success({lua::Value{std::move(result)}});
                },
        },
        lua::HostFunctionBinding{
            .dottedName = "nr.options.get",
            .function = [](std::span<const lua::Value>) { return lua::HostCallResult::success({lua::Value{}}); },
        },
        lua::HostFunctionBinding{
            .dottedName = "nr.options.apply",
            .function =
                [&trace](std::span<const lua::Value>) {
                    ++trace.applyCalls;
                    return lua::HostCallResult::success({lua::Value{true}});
                },
        },
        lua::HostFunctionBinding{
            .dottedName = "nr.frame.next",
            .function = [](std::span<const lua::Value> arguments) { return arguments.empty() ? lua::HostCallResult::suspend() : lua::HostCallResult::failure("next expects no arguments"); },
        },
        lua::HostFunctionBinding{
            .dottedName = "nr.log.info",
            .function =
                [&trace](std::span<const lua::Value> arguments) {
                    if (arguments.size() != 1u)
                    {
                        return lua::HostCallResult::failure("log expects one argument");
                    }
                    auto const *message = std::get_if<std::string>(&arguments.front().storage);
                    if (message == nullptr)
                    {
                        return lua::HostCallResult::failure("log expects a string");
                    }
                    trace.logMessages.push_back(*message);
                    return lua::HostCallResult::success();
                },
        },
    };
    return config;
}

[[nodiscard]] std::shared_ptr<const options::OptionCatalog> sessionCatalog()
{
    auto builder = options::OptionCatalogBuilder{};
    auto definitions = options::makeSessionDefinitions(options::SessionDefinitionSeed{});
    std::ranges::for_each(definitions, [&](auto definition) { nr::test::require(builder.add(std::move(definition))); });
    auto result = builder.build();
    nr::test::require(result.valid());
    return result.catalog;
}

[[nodiscard]] options::OptionAvailabilityMap allAvailable(const options::OptionCatalog &catalog)
{
    auto result = options::OptionAvailabilityMap{};
    std::ranges::for_each(catalog.definitions(), [&](auto const &entry) { result.emplace(entry.first, options::OptionAvailability{.available = true}); });
    return result;
}

[[nodiscard]] std::unique_ptr<options::OptionSystem> offlineSystem()
{
    auto system = std::make_unique<options::OptionSystem>(options::AuthorityMode::offlineLua);
    auto catalog = sessionCatalog();
    nr::test::require(system->initializeSession(catalog, allAvailable(*catalog)).committed);
    return system;
}

const nr::test::CaseRegistrar exactAllowlistCase{"Lua sandbox exposes only the reviewed per-function allowlist", [] {
                                                     auto trace = HostTrace{};
                                                     auto sandbox = lua::Sandbox{};
                                                     auto const source = R"lua(
local function assert_exact_members(namespace, names)
    local expected = {}
    for _, name in ipairs(names) do
        expected[name] = true
    end
    local count = 0
    for name in pairs(namespace) do
        assert(expected[name] == true)
        count = count + 1
    end
    assert(count == #names)
end

local base_names = {
    "assert", "error", "ipairs", "next", "pairs", "select",
    "tonumber", "tostring", "type"
}
for _, name in ipairs(base_names) do
    assert(type(_ENV[name]) == "function")
end

local string_names = {
    "byte", "char", "find", "format", "gmatch", "gsub", "len",
    "lower", "match", "rep", "reverse", "sub", "upper"
}
for _, name in ipairs(string_names) do
    assert(type(string[name]) == "function")
end
assert_exact_members(string, string_names)

local table_names = {
    "concat", "insert", "move", "pack", "remove", "sort", "unpack"
}
for _, name in ipairs(table_names) do
    assert(type(table[name]) == "function")
end
assert_exact_members(table, table_names)

local math_function_names = {
    "abs", "acos", "asin", "atan", "ceil", "cos", "deg", "exp",
    "floor", "fmod", "log", "max", "min", "modf", "rad", "sin",
    "sqrt", "tan", "tointeger", "type", "ult"
}
for _, name in ipairs(math_function_names) do
    assert(type(math[name]) == "function")
end
assert(type(math.maxinteger) == "number")
assert(type(math.mininteger) == "number")
assert(type(math.pi) == "number")
local math_names = {}
for _, name in ipairs(math_function_names) do
    table.insert(math_names, name)
end
table.insert(math_names, "maxinteger")
table.insert(math_names, "mininteger")
table.insert(math_names, "pi")
assert_exact_members(math, math_names)

local utf8_names = {
    "char", "charpattern", "codes", "codepoint", "len", "offset"
}
for _, name in ipairs(utf8_names) do
    local member_type = type(utf8[name])
    assert(member_type == "function" or
           (name == "charpattern" and member_type == "string"))
end
assert_exact_members(utf8, utf8_names)

local forbidden_globals = {
    "_G", "package", "require", "io", "os", "debug", "coroutine",
    "dofile", "loadfile", "load", "collectgarbage", "rawget", "rawset",
    "rawlen", "rawequal", "getmetatable", "setmetatable", "pcall", "xpcall",
    "print", "warn", "_VERSION"
}
for _, name in ipairs(forbidden_globals) do
    assert(_ENV[name] == nil)
end

assert(string.dump == nil)
assert(string.pack == nil)
assert(string.packsize == nil)
assert(string.unpack == nil)
assert(("abc").dump == nil)
assert(math.random == nil)
assert(math.randomseed == nil)
assert(next(nr) == nil)
assert(next(nr.options) == nil)
assert(("abc"):upper() == "ABC")
assert(table.concat({"a", "b"}) == "ab")
assert(math.abs(-4) == 4)
assert(utf8.len("abc") == 3)
assert(type(nr.options.snapshot) == "function")
assert(type(nr.options.get) == "function")
assert(type(nr.options.apply) == "function")
assert(type(nr.frame.next) == "function")
assert(type(nr.log.info) == "function")
assert_exact_members(nr, {"options", "frame", "log"})
assert_exact_members(nr.options, {"snapshot", "get", "apply"})
assert_exact_members(nr.frame, {"next"})
assert_exact_members(nr.log, {"info"})
local copied = nr.options.snapshot()
assert(copied[string.char(97, 0, 98)] == 7)
nr.log.info("allowlist-ok")
)lua";

                                                     auto started = sandbox.start(std::string{source}, "@allowlist.lua", sandboxConfig(trace));
                                                     nr::test::require(started.started, started.detail);
                                                     auto resumed = sandbox.resume();
                                                     nr::test::requireEqual(resumed.status, lua::ResumeStatus::completed);
                                                     nr::test::requireEqual(trace.logMessages, std::vector<std::string>{"allowlist-ok"});
                                                 }};

const nr::test::CaseRegistrar readOnlyProxyCase{"Lua namespace proxies reject direct and library-assisted mutation", [] {
                                                    constexpr auto attempts = std::array{
                                                        "nr.options.snapshot = 7", "nr.options.extra = 7", "string.byte = nil", "table.insert(nr.options, \"escape\")", "table.move({7}, 1, 1, 1, nr.options)", "_ENV.escape = true",
                                                    };
                                                    std::ranges::for_each(attempts, [](std::string_view source) {
                                                        auto trace = HostTrace{};
                                                        auto sandbox = lua::Sandbox{};
                                                        auto started = sandbox.start(std::string{source}, "@readonly.lua", sandboxConfig(trace));
                                                        nr::test::require(started.started, started.detail);
                                                        auto resumed = sandbox.resume();
                                                        nr::test::requireEqual(resumed.status, lua::ResumeStatus::failed, "proxy mutation must terminate the script");
                                                        nr::test::require(resumed.detail.contains("read-only"), resumed.detail);
                                                    });
                                                }};

const nr::test::CaseRegistrar coroutineCase{"Lua frame yields resume one host-owned coroutine in order", [] {
                                                auto trace = HostTrace{};
                                                auto sandbox = lua::Sandbox{};
                                                auto started = sandbox.start(
                                                    R"lua(
nr.log.info("frame-0")
nr.frame.next()
nr.log.info("frame-1")
nr.frame.next()
nr.log.info("frame-2")
)lua",
                                                    "@frames.lua", sandboxConfig(trace));
                                                nr::test::require(started.started, started.detail);

                                                nr::test::requireEqual(sandbox.resume().status, lua::ResumeStatus::yielded);
                                                nr::test::requireEqual(trace.logMessages, std::vector<std::string>{"frame-0"});

                                                nr::test::requireEqual(sandbox.resume().status, lua::ResumeStatus::yielded);
                                                nr::test::requireEqual(trace.logMessages, (std::vector<std::string>{"frame-0", "frame-1"}));

                                                nr::test::requireEqual(sandbox.resume().status, lua::ResumeStatus::completed);
                                                nr::test::requireEqual(trace.logMessages, (std::vector<std::string>{"frame-0", "frame-1", "frame-2"}));
                                                nr::test::requireEqual(sandbox.resume().status, lua::ResumeStatus::completed, "terminal resume must be stable");
                                            }};

const nr::test::CaseRegistrar loaderAndQuotaCase{"Lua sandbox enforces text source memory instruction wall and conversion bounds", [] {
                                                     {
                                                         auto trace = HostTrace{};
                                                         auto sandbox = lua::Sandbox{};
                                                         auto config = sandboxConfig(trace);
                                                         config.limits.maximumSourceBytes = 8u;
                                                         auto result = sandbox.start("123456789", "@too-large.lua", std::move(config));
                                                         nr::test::requireEqual(result.error, lua::SandboxStartError::sourceTooLarge);
                                                     }
                                                     {
                                                         auto trace = HostTrace{};
                                                         auto sandbox = lua::Sandbox{};
                                                         auto binary = std::string{"\x1bLua"};
                                                         auto result = sandbox.start(std::move(binary), "@binary.lua", sandboxConfig(trace));
                                                         nr::test::requireEqual(result.error, lua::SandboxStartError::loadFailed);
                                                         nr::test::require(!sandbox.started());
                                                     }
                                                     {
                                                         auto trace = HostTrace{};
                                                         auto sandbox = lua::Sandbox{};
                                                         auto config = sandboxConfig(trace);
                                                         config.limits.maximumInstructionsPerResume = 2'000u;
                                                         config.limits.hookInstructionInterval = 100u;
                                                         auto started = sandbox.start("while true do end", "@instruction.lua", std::move(config));
                                                         nr::test::require(started.started, started.detail);
                                                         auto resumed = sandbox.resume();
                                                         nr::test::requireEqual(resumed.status, lua::ResumeStatus::failed);
                                                         nr::test::require(resumed.detail.contains("instruction budget"), resumed.detail);
                                                     }
                                                     {
                                                         auto trace = HostTrace{};
                                                         auto sandbox = lua::Sandbox{};
                                                         auto config = sandboxConfig(trace);
                                                         config.limits.maximumInstructionsPerResume = 4'000'000'000u;
                                                         config.limits.hookInstructionInterval = 1'000u;
                                                         config.limits.softWallBudget = std::chrono::milliseconds{1};
                                                         auto started = sandbox.start("while true do end", "@wall.lua", std::move(config));
                                                         nr::test::require(started.started, started.detail);
                                                         auto resumed = sandbox.resume();
                                                         nr::test::requireEqual(resumed.status, lua::ResumeStatus::failed);
                                                         nr::test::require(resumed.detail.contains("wall-time budget"), resumed.detail);
                                                     }
                                                     {
                                                         auto trace = HostTrace{};
                                                         auto sandbox = lua::Sandbox{};
                                                         auto config = sandboxConfig(trace);
                                                         config.limits.maximumAllocatedBytes = 2u * 1024u * 1024u;
                                                         auto started = sandbox.start("local value = string.rep(\"x\", 8 * 1024 * 1024)", "@memory.lua", std::move(config));
                                                         nr::test::require(started.started, started.detail);
                                                         nr::test::requireEqual(sandbox.resume().status, lua::ResumeStatus::failed);
                                                         nr::test::require(sandbox.allocatedBytes() <= 2u * 1024u * 1024u);
                                                     }
                                                     {
                                                         auto trace = HostTrace{};
                                                         auto sandbox = lua::Sandbox{};
                                                         auto started = sandbox.start(R"lua(nr.options.apply(string.rep("x", 65537), true, {}))lua", "@string-boundary.lua", sandboxConfig(trace));
                                                         nr::test::require(started.started, started.detail);
                                                         auto resumed = sandbox.resume();
                                                         nr::test::requireEqual(resumed.status, lua::ResumeStatus::failed);
                                                         nr::test::require(resumed.detail.contains("string"), resumed.detail);
                                                         nr::test::requireEqual(trace.applyCalls, std::size_t{0u});
                                                     }
                                                     {
                                                         auto trace = HostTrace{};
                                                         auto sandbox = lua::Sandbox{};
                                                         auto config = sandboxConfig(trace);
                                                         config.limits.maximumTableEntries = 4u;
                                                         auto started = sandbox.start("nr.options.apply({1, 2, 3, 4, 5})", "@table-boundary.lua", std::move(config));
                                                         nr::test::require(started.started, started.detail);
                                                         auto resumed = sandbox.resume();
                                                         nr::test::requireEqual(resumed.status, lua::ResumeStatus::failed);
                                                         nr::test::require(resumed.detail.contains("entry limit"), resumed.detail);
                                                         nr::test::requireEqual(trace.applyCalls, std::size_t{0u});
                                                     }
                                                     {
                                                         auto trace = HostTrace{};
                                                         auto sandbox = lua::Sandbox{};
                                                         auto config = sandboxConfig(trace);
                                                         config.limits.maximumConversionDepth = 2u;
                                                         auto started = sandbox.start("nr.options.apply({{{{1}}}})", "@depth-boundary.lua", std::move(config));
                                                         nr::test::require(started.started, started.detail);
                                                         auto resumed = sandbox.resume();
                                                         nr::test::requireEqual(resumed.status, lua::ResumeStatus::failed);
                                                         nr::test::require(resumed.detail.contains("depth limit"), resumed.detail);
                                                         nr::test::requireEqual(trace.applyCalls, std::size_t{0u});
                                                     }
                                                     {
                                                         auto trace = HostTrace{};
                                                         auto sandbox = lua::Sandbox{};
                                                         auto config = sandboxConfig(trace);
                                                         config.limits.maximumConvertedNodes = 4u;
                                                         auto started = sandbox.start("nr.options.apply({1, 2, 3, 4})", "@node-boundary.lua", std::move(config));
                                                         nr::test::require(started.started, started.detail);
                                                         auto resumed = sandbox.resume();
                                                         nr::test::requireEqual(resumed.status, lua::ResumeStatus::failed);
                                                         nr::test::require(resumed.detail.contains("converted-node limit"), resumed.detail);
                                                         nr::test::requireEqual(trace.applyCalls, std::size_t{0u});
                                                     }
                                                     {
                                                         auto trace = HostTrace{};
                                                         auto sandbox = lua::Sandbox{};
                                                         auto started = sandbox.start("local value = {}; value.self = value; "
                                                                                      "nr.options.apply(value)",
                                                                                      "@cycle-boundary.lua", sandboxConfig(trace));
                                                         nr::test::require(started.started, started.detail);
                                                         auto resumed = sandbox.resume();
                                                         nr::test::requireEqual(resumed.status, lua::ResumeStatus::failed);
                                                         nr::test::require(resumed.detail.contains("cyclic table"), resumed.detail);
                                                         nr::test::requireEqual(trace.applyCalls, std::size_t{0u});
                                                     }
                                                 }};

const nr::test::CaseRegistrar offlineHostCase{"Offline Lua uses automation-root paths and the shared option admission slot", [] {
                                                  auto valid = automation::resolveAutomationScriptPath("test/offline_contract.lua");
                                                  nr::test::require(valid.has_value(), valid ? std::string_view{} : std::string_view{valid.error()});
                                                  nr::test::require(!automation::resolveAutomationScriptPath("../test/unit/automation/outside_automation.lua").has_value());
                                                  nr::test::require(!automation::resolveAutomationScriptPath(automation::automationRootPath() / "test/offline_contract.lua").has_value());
                                                  nr::test::require(!automation::resolveAutomationScriptPath(R"(\\?\C:\automation\escape.lua)").has_value());
                                                  nr::test::require(!automation::resolveAutomationScriptPath("test/%TEMP%.lua").has_value());

                                                  {
                                                      auto wrongAuthority = options::OptionSystem{options::AuthorityMode::human};
                                                      auto catalog = sessionCatalog();
                                                      nr::test::require(wrongAuthority.initializeSession(catalog, allAvailable(*catalog)).committed);
                                                      auto host = automation::OfflineLuaHost{};
                                                      auto result = host.start(wrongAuthority, "test/offline_contract.lua");
                                                      nr::test::requireEqual(result.error, automation::OfflineLuaStartError::wrongAuthority);
                                                  }

                                                  auto system = offlineSystem();
                                                  auto host = automation::OfflineLuaHost{};
                                                  auto started = host.start(*system, "test/offline_contract.lua");
                                                  nr::test::require(started.started, started.detail);
                                                  auto firstResume = host.resume(system->snapshot());
                                                  nr::test::requireEqual(firstResume.status, automation::OfflineLuaFrameStatus::running, firstResume.detail);
                                                  nr::test::require(system->hasPendingMutation());

                                                  auto frame = system->beginRenderableFrame();
                                                  nr::test::require(frame.has_value() && frame->mutation.has_value());
                                                  nr::test::require(system->commitCanonical(std::move(*frame->mutation)).committed);
                                                  auto catalog = system->activeCatalog();
                                                  auto published = system->publishRenderableFrame(allAvailable(*catalog));
                                                  auto const *fullscreen = published->find(options::keys::viewerWindowFullscreen);
                                                  nr::test::require(fullscreen != nullptr && *fullscreen);

                                                  auto secondResume = host.resume(published);
                                                  nr::test::requireEqual(secondResume.status, automation::OfflineLuaFrameStatus::completed, secondResume.detail);
                                                  nr::test::require(!system->hasPendingMutation());
                                                  host.stop();
                                              }};

const nr::test::CaseRegistrar finalApplyAndLogQuotaCase{"Offline Lua completion does not drain a final apply and log quotas fail the script", [] {
                                                            {
                                                                auto system = offlineSystem();
                                                                auto host = automation::OfflineLuaHost{};
                                                                auto started = host.start(*system, "test/final_apply.lua");
                                                                nr::test::require(started.started, started.detail);
                                                                auto resumed = host.resume(system->snapshot());
                                                                nr::test::requireEqual(resumed.status, automation::OfflineLuaFrameStatus::completed, resumed.detail);
                                                                nr::test::require(system->hasPendingMutation(), "normal return must not add a hidden drain frame");
                                                                host.stop();
                                                                nr::test::require(system->hasPendingMutation(), "stopping the Lua host must not cancel admitted work");
                                                                nr::test::require(system->shutdown().has_value());
                                                            }
                                                            {
                                                                auto system = offlineSystem();
                                                                auto host = automation::OfflineLuaHost{};
                                                                auto started = host.start(*system, "test/log_oversize.lua");
                                                                nr::test::require(started.started, started.detail);
                                                                auto resumed = host.resume(system->snapshot());
                                                                nr::test::requireEqual(resumed.status, automation::OfflineLuaFrameStatus::failed);
                                                                nr::test::require(resumed.detail.contains("1 KiB"), resumed.detail);
                                                                host.stop();
                                                            }
                                                            constexpr auto quotaScripts = std::array{
                                                                "test/log_call_quota.lua",
                                                                "test/log_total_quota.lua",
                                                            };
                                                            std::ranges::for_each(quotaScripts, [](const char *script) {
                                                                auto system = offlineSystem();
                                                                auto host = automation::OfflineLuaHost{};
                                                                auto started = host.start(*system, script);
                                                                nr::test::require(started.started, started.detail);
                                                                auto resumed = host.resume(system->snapshot());
                                                                nr::test::requireEqual(resumed.status, automation::OfflineLuaFrameStatus::failed);
                                                                nr::test::require(resumed.detail.contains("per-resume log quota"), resumed.detail);
                                                                host.stop();
                                                            });
                                                        }};
} // namespace
