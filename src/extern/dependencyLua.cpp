module;

#include <lua.hpp>

module dependency.lua;

import std;

namespace dependency::lua
{
namespace
{
static_assert(LUA_VERSION_NUM == 505);
static_assert(LUA_VERSION_RELEASE_NUM == 50500);
static_assert(sizeof(lua_Integer) == sizeof(std::int64_t));
static_assert(std::is_signed_v<lua_Integer>);
static_assert(sizeof(lua_Number) == sizeof(double));

inline constexpr std::array kBaseFunctions{
    "assert", "error", "ipairs", "next", "pairs", "select", "tonumber", "tostring", "type",
};

inline constexpr std::array kStringMembers{
    "byte", "char", "find", "format", "gmatch", "gsub", "len", "lower", "match", "rep", "reverse", "sub", "upper",
};

inline constexpr std::array kTableMembers{
    "concat", "insert", "move", "pack", "remove", "sort", "unpack",
};

inline constexpr std::array kMathMembers{
    "abs", "acos", "asin", "atan", "ceil", "cos", "deg", "exp", "floor", "fmod", "log", "max", "min", "modf", "rad", "sin", "sqrt", "tan", "tointeger", "type", "ult", "maxinteger", "mininteger", "pi",
};

inline constexpr std::array kUtf8Members{
    "char", "charpattern", "codes", "codepoint", "len", "offset",
};

struct AllocationState
{
    std::size_t usedBytes = 0u;
    std::size_t limitBytes = 0u;
};

void *cappedAllocator(void *userData, void *pointer, std::size_t oldSize, std::size_t newSize) noexcept
{
    auto &state = *static_cast<AllocationState *>(userData);
    auto const accountedOldSize = pointer != nullptr ? std::min(oldSize, state.usedBytes) : 0u;
    if (newSize == 0u)
    {
        std::free(pointer);
        state.usedBytes -= accountedOldSize;
        return nullptr;
    }

    auto const retainedBytes = state.usedBytes - accountedOldSize;
    if (newSize > state.limitBytes || retainedBytes > state.limitBytes - newSize)
    {
        return nullptr;
    }

    auto *replacement = std::realloc(pointer, newSize);
    if (replacement != nullptr)
    {
        state.usedBytes = retainedBytes + newSize;
    }
    return replacement;
}

[[nodiscard]] bool validNamespaceSegment(std::string_view segment) noexcept
{
    if (segment.empty() || !((segment.front() >= 'a' && segment.front() <= 'z') || segment.front() == '_'))
    {
        return false;
    }
    return std::ranges::all_of(segment.substr(1u), [](char value) { return (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') || value == '_'; });
}

[[nodiscard]] std::vector<std::string> splitDottedName(std::string_view name)
{
    auto result = std::vector<std::string>{};
    auto remaining = name;
    while (!remaining.empty())
    {
        auto const separator = remaining.find('.');
        auto const segment = remaining.substr(0u, separator);
        if (!validNamespaceSegment(segment))
        {
            return {};
        }
        result.emplace_back(segment);
        if (separator == std::string_view::npos)
        {
            break;
        }
        remaining.remove_prefix(separator + 1u);
    }
    return result;
}

[[nodiscard]] std::string luaErrorText(lua_State *state)
{
    auto size = std::size_t{};
    auto const *text = lua_tolstring(state, -1, std::addressof(size));
    if (text != nullptr)
    {
        return std::string{text, std::min(size, std::size_t{4096u})};
    }
    return std::format("Lua error object has type '{}'.", luaL_typename(state, -1));
}
} // namespace

class Sandbox::Impl
{
  public:
    struct NamespaceNode
    {
        std::map<std::string, NamespaceNode, std::less<>> children{};
        std::optional<std::size_t> functionIndex{};
    };

    struct ConversionState
    {
        std::size_t nodes = 0u;
        std::set<const void *> activeTables{};
    };

    ~Impl()
    {
        close();
    }

    [[nodiscard]] SandboxStartResult start(std::string source, std::string chunkName, SandboxConfig config)
    {
        if (state_ != nullptr)
        {
            return SandboxStartResult{.error = SandboxStartError::alreadyStarted};
        }
        if (!validateConfig(config))
        {
            return SandboxStartResult{.error = SandboxStartError::invalidConfiguration};
        }
        if (source.size() > config.limits.maximumSourceBytes)
        {
            return SandboxStartResult{.error = SandboxStartError::sourceTooLarge};
        }

        auto namespaceRoot = NamespaceNode{};
        if (!buildNamespaceTree(config.hostFunctions, namespaceRoot))
        {
            return SandboxStartResult{.error = SandboxStartError::invalidConfiguration};
        }

        allocation_ = AllocationState{
            .limitBytes = config.limits.maximumAllocatedBytes,
        };
        state_ = lua_newstate(cappedAllocator, std::addressof(allocation_), 0x4e525631u);
        if (state_ == nullptr)
        {
            allocation_ = {};
            return SandboxStartResult{.error = SandboxStartError::allocationFailed};
        }

        config_ = std::move(config);
        hostFunctions_ = std::move(config_.hostFunctions);
        lua_pushlightuserdata(state_, this);
        lua_rawsetp(state_, LUA_REGISTRYINDEX, std::addressof(kImplRegistryKey_));

        if (!buildEnvironment(namespaceRoot))
        {
            close();
            return SandboxStartResult{
                .error = SandboxStartError::invalidConfiguration,
                .detail = "Failed to construct the Lua allowlist environment.",
            };
        }

        coroutine_ = lua_newthread(state_);
        if (coroutine_ == nullptr)
        {
            close();
            return SandboxStartResult{.error = SandboxStartError::allocationFailed};
        }
        coroutineReference_ = luaL_ref(state_, LUA_REGISTRYINDEX);

        auto const loadStatus = luaL_loadbufferx(coroutine_, source.data(), source.size(), chunkName.c_str(), "t");
        if (loadStatus != LUA_OK)
        {
            auto detail = luaErrorText(coroutine_);
            close();
            return SandboxStartResult{
                .error = SandboxStartError::loadFailed,
                .detail = std::move(detail),
            };
        }

        lua_rawgeti(coroutine_, LUA_REGISTRYINDEX, environmentReference_);
        auto const *upvalueName = lua_setupvalue(coroutine_, -2, 1);
        if (upvalueName == nullptr || std::string_view{upvalueName} != "_ENV")
        {
            close();
            return SandboxStartResult{
                .error = SandboxStartError::loadFailed,
                .detail = "Lua text chunk does not expose the expected _ENV upvalue.",
            };
        }

        terminalStatus_.reset();
        return SandboxStartResult{.started = true};
    }

    [[nodiscard]] ResumeResult resume()
    {
        if (state_ == nullptr || coroutine_ == nullptr)
        {
            return ResumeResult{.status = ResumeStatus::notStarted};
        }
        if (terminalStatus_)
        {
            return ResumeResult{.status = *terminalStatus_, .detail = terminalDetail_};
        }

        executedInstructions_ = 0u;
        resumeStartedAt_ = std::chrono::steady_clock::now();
        lua_sethook(coroutine_, hook, LUA_MASKCOUNT, static_cast<int>(config_.limits.hookInstructionInterval));

        auto resultCount = 0;
        auto const status = lua_resume(coroutine_, nullptr, 0, std::addressof(resultCount));
        lua_sethook(coroutine_, nullptr, 0, 0);
        auto const elapsed = std::chrono::steady_clock::now() - resumeStartedAt_;
        if (elapsed > config_.limits.softWallBudget && (status == LUA_OK || status == LUA_YIELD))
        {
            if (resultCount > 0)
            {
                lua_pop(coroutine_, resultCount);
            }
            terminalStatus_ = ResumeStatus::failed;
            terminalDetail_ = "Lua resume exceeded the soft wall-time budget.";
            return ResumeResult{.status = *terminalStatus_, .detail = terminalDetail_};
        }

        if (status == LUA_YIELD)
        {
            if (resultCount > 0)
            {
                lua_pop(coroutine_, resultCount);
            }
            return ResumeResult{.status = ResumeStatus::yielded};
        }
        if (status == LUA_OK)
        {
            if (resultCount > 0)
            {
                lua_pop(coroutine_, resultCount);
            }
            terminalStatus_ = ResumeStatus::completed;
            return ResumeResult{.status = *terminalStatus_};
        }

        terminalDetail_ = luaErrorText(coroutine_);
        lua_pop(coroutine_, 1);
        terminalStatus_ = ResumeStatus::failed;
        return ResumeResult{.status = *terminalStatus_, .detail = terminalDetail_};
    }

    void close() noexcept
    {
        if (state_ != nullptr)
        {
            lua_close(state_);
        }
        state_ = nullptr;
        coroutine_ = nullptr;
        coroutineReference_ = LUA_NOREF;
        environmentReference_ = LUA_NOREF;
        hostFunctions_.clear();
        config_ = {};
        allocation_ = {};
        terminalStatus_.reset();
        terminalDetail_.clear();
        pendingError_.clear();
        pendingYield_ = false;
        executedInstructions_ = 0u;
    }

    [[nodiscard]] bool started() const noexcept
    {
        return state_ != nullptr;
    }

    [[nodiscard]] std::size_t allocatedBytes() const noexcept
    {
        return allocation_.usedBytes;
    }

  private:
    [[nodiscard]] static bool validateConfig(const SandboxConfig &config) noexcept
    {
        auto const &limits = config.limits;
        return limits.maximumAllocatedBytes > 0u && limits.maximumSourceBytes > 0u && limits.maximumInstructionsPerResume > 0u && limits.hookInstructionInterval > 0u && limits.hookInstructionInterval <= static_cast<std::uint32_t>(std::numeric_limits<int>::max()) &&
               limits.hookInstructionInterval <= limits.maximumInstructionsPerResume && limits.softWallBudget > std::chrono::milliseconds::zero() && limits.maximumConversionDepth > 0u && limits.maximumTableEntries > 0u &&
               limits.maximumTableEntries <= static_cast<std::size_t>(std::numeric_limits<int>::max()) && limits.maximumConvertedNodes > 0u && limits.maximumStringBytes > 0u &&
               std::ranges::all_of(config.hostFunctions, [](auto const &binding) { return !binding.dottedName.empty() && static_cast<bool>(binding.function); });
    }

    [[nodiscard]] static bool buildNamespaceTree(std::span<const HostFunctionBinding> bindings, NamespaceNode &root)
    {
        auto valid = true;
        auto indices = std::views::iota(std::size_t{0u}, bindings.size());
        std::ranges::for_each(indices, [&](std::size_t index) {
            if (!valid)
            {
                return;
            }
            auto segments = splitDottedName(bindings[index].dottedName);
            if (segments.size() < 2u || segments.front() != "nr")
            {
                valid = false;
                return;
            }

            auto *node = std::addressof(root);
            std::ranges::for_each(segments, [&](const std::string &segment) { node = std::addressof(node->children[segment]); });
            if (node->functionIndex || !node->children.empty())
            {
                valid = false;
                return;
            }
            node->functionIndex = index;
        });
        if (!valid)
        {
            return false;
        }

        auto validateLeaves = [&](this auto &&self, const NamespaceNode &node) -> bool {
            if (node.functionIndex && !node.children.empty())
            {
                return false;
            }
            return std::ranges::all_of(node.children, [&](auto const &entry) { return self(entry.second); });
        };
        return validateLeaves(root);
    }

    static int readOnlyWrite(lua_State *state)
    {
        lua_pushliteral(state, "attempt to modify a read-only Lua namespace");
        return lua_error(state);
    }

    static int proxyIterator(lua_State *state)
    {
        lua_pushvalue(state, lua_upvalueindex(1));
        auto const backingIndex = lua_gettop(state);
        if (lua_gettop(state) >= 3 && !lua_isnil(state, 2))
        {
            lua_pushvalue(state, 2);
        }
        else
        {
            lua_pushnil(state);
        }
        if (lua_next(state, backingIndex) != 0)
        {
            lua_remove(state, backingIndex);
            return 2;
        }
        return 0;
    }

    static int proxyPairs(lua_State *state)
    {
        lua_pushvalue(state, lua_upvalueindex(1));
        lua_pushcclosure(state, proxyIterator, 1);
        lua_pushnil(state);
        lua_pushnil(state);
        return 3;
    }

    static void pushReadOnlyProxy(lua_State *state, int backingIndex)
    {
        backingIndex = lua_absindex(state, backingIndex);
        lua_newtable(state);
        auto const proxyIndex = lua_gettop(state);
        lua_newtable(state);
        auto const metatableIndex = lua_gettop(state);

        lua_pushvalue(state, backingIndex);
        lua_setfield(state, metatableIndex, "__index");
        lua_pushvalue(state, backingIndex);
        lua_pushcclosure(state, proxyPairs, 1);
        lua_setfield(state, metatableIndex, "__pairs");
        lua_pushcfunction(state, readOnlyWrite);
        lua_setfield(state, metatableIndex, "__newindex");
        lua_pushliteral(state, "locked");
        lua_setfield(state, metatableIndex, "__metatable");
        lua_setmetatable(state, proxyIndex);
        lua_remove(state, backingIndex);
    }

    static void pushEnvironmentProxy(lua_State *state, int allowlistProxyIndex)
    {
        allowlistProxyIndex = lua_absindex(state, allowlistProxyIndex);
        lua_newtable(state);
        auto const proxyIndex = lua_gettop(state);
        lua_newtable(state);
        auto const metatableIndex = lua_gettop(state);
        lua_pushvalue(state, allowlistProxyIndex);
        lua_setfield(state, metatableIndex, "__index");
        lua_pushcfunction(state, readOnlyWrite);
        lua_setfield(state, metatableIndex, "__newindex");
        lua_pushliteral(state, "locked");
        lua_setfield(state, metatableIndex, "__metatable");
        lua_setmetatable(state, proxyIndex);
    }

    template <std::size_t Size> static bool copyMembers(lua_State *state, int sourceIndex, int targetIndex, const std::array<const char *, Size> &names)
    {
        sourceIndex = lua_absindex(state, sourceIndex);
        targetIndex = lua_absindex(state, targetIndex);
        auto valid = true;
        std::ranges::for_each(names, [&](const char *name) {
            if (!valid)
            {
                return;
            }
            lua_getfield(state, sourceIndex, name);
            if (lua_isnil(state, -1))
            {
                lua_pop(state, 1);
                valid = false;
                return;
            }
            lua_setfield(state, targetIndex, name);
        });
        return valid;
    }

    template <std::size_t Size> [[nodiscard]] bool installLibrary(int allowlistBackingIndex, const char *name, lua_CFunction openFunction, const std::array<const char *, Size> &members)
    {
        allowlistBackingIndex = lua_absindex(state_, allowlistBackingIndex);
        luaL_requiref(state_, name, openFunction, 0);
        auto const sourceIndex = lua_gettop(state_);
        lua_newtable(state_);
        auto const approvedBackingIndex = lua_gettop(state_);
        if (!copyMembers(state_, sourceIndex, approvedBackingIndex, members))
        {
            lua_pop(state_, 2);
            return false;
        }
        pushReadOnlyProxy(state_, approvedBackingIndex);
        lua_setfield(state_, allowlistBackingIndex, name);
        lua_pop(state_, 1);
        return true;
    }

    static int hostThunk(lua_State *state)
    {
        auto *implementation = static_cast<Impl *>(lua_touserdata(state, lua_upvalueindex(1)));
        auto const index = static_cast<std::size_t>(lua_tointeger(state, lua_upvalueindex(2)));
        auto const resultCount = implementation->invokeHost(state, index);
        if (resultCount >= 0)
        {
            return resultCount;
        }
        if (implementation->pendingYield_)
        {
            implementation->pendingYield_ = false;
            return lua_yield(state, 0);
        }

        auto const *errorData = implementation->pendingError_.data();
        auto const errorSize = implementation->pendingError_.size();
        lua_pushlstring(state, errorData, errorSize);
        return lua_error(state);
    }

    void pushNamespace(const NamespaceNode &node)
    {
        lua_newtable(state_);
        auto const backingIndex = lua_gettop(state_);
        std::ranges::for_each(node.children, [&](auto const &entry) {
            auto const &[name, child] = entry;
            if (child.functionIndex)
            {
                lua_pushlightuserdata(state_, this);
                lua_pushinteger(state_, static_cast<lua_Integer>(*child.functionIndex));
                lua_pushcclosure(state_, hostThunk, 2);
            }
            else
            {
                pushNamespace(child);
            }
            lua_setfield(state_, backingIndex, name.c_str());
        });
        pushReadOnlyProxy(state_, backingIndex);
    }

    [[nodiscard]] bool buildEnvironment(const NamespaceNode &namespaceRoot)
    {
        luaL_requiref(state_, "_G", luaopen_base, 1);
        auto const baseSourceIndex = lua_gettop(state_);
        lua_newtable(state_);
        auto const allowlistBackingIndex = lua_gettop(state_);
        if (!copyMembers(state_, baseSourceIndex, allowlistBackingIndex, kBaseFunctions))
        {
            return false;
        }

        if (!installLibrary(allowlistBackingIndex, LUA_STRLIBNAME, luaopen_string, kStringMembers) || !installLibrary(allowlistBackingIndex, LUA_TABLIBNAME, luaopen_table, kTableMembers) || !installLibrary(allowlistBackingIndex, LUA_MATHLIBNAME, luaopen_math, kMathMembers) ||
            !installLibrary(allowlistBackingIndex, LUA_UTF8LIBNAME, luaopen_utf8, kUtf8Members))
        {
            return false;
        }

        auto const nrNode = namespaceRoot.children.find("nr");
        if (nrNode == namespaceRoot.children.end())
        {
            return false;
        }
        pushNamespace(nrNode->second);
        lua_setfield(state_, allowlistBackingIndex, "nr");

        pushReadOnlyProxy(state_, allowlistBackingIndex);
        auto const allowlistProxyIndex = lua_gettop(state_);

        lua_pushliteral(state_, "");
        if (!lua_getmetatable(state_, -1))
        {
            lua_pop(state_, 1);
            return false;
        }
        auto const stringMetatableIndex = lua_gettop(state_);
        lua_getfield(state_, allowlistProxyIndex, LUA_STRLIBNAME);
        lua_setfield(state_, stringMetatableIndex, "__index");
        lua_pop(state_, 2);

        pushEnvironmentProxy(state_, allowlistProxyIndex);
        environmentReference_ = luaL_ref(state_, LUA_REGISTRYINDEX);
        lua_pop(state_, 2);
        return environmentReference_ != LUA_NOREF && environmentReference_ != LUA_REFNIL;
    }

    [[nodiscard]] std::expected<Value, std::string> readValue(lua_State *state, int index, std::size_t depth, ConversionState &conversion) const
    {
        if (depth > config_.limits.maximumConversionDepth)
        {
            return std::unexpected("Lua value exceeds the conversion depth limit.");
        }
        if (++conversion.nodes > config_.limits.maximumConvertedNodes)
        {
            return std::unexpected("Lua value exceeds the converted-node limit.");
        }

        index = lua_absindex(state, index);
        switch (lua_type(state, index))
        {
        case LUA_TNIL:
            return Value{};
        case LUA_TBOOLEAN:
            return Value{lua_toboolean(state, index) != 0};
        case LUA_TNUMBER:
            if (lua_isinteger(state, index))
            {
                return Value{static_cast<std::int64_t>(lua_tointeger(state, index))};
            }
            else
            {
                auto const number = static_cast<double>(lua_tonumber(state, index));
                if (!std::isfinite(number))
                {
                    return std::unexpected("Lua value contains a non-finite number.");
                }
                return Value{number};
            }
        case LUA_TSTRING: {
            auto size = std::size_t{};
            auto const *text = lua_tolstring(state, index, std::addressof(size));
            if (size > config_.limits.maximumStringBytes)
            {
                return std::unexpected("Lua string exceeds the conversion byte limit.");
            }
            return Value{std::string{text, size}};
        }
        case LUA_TTABLE:
            return readTable(state, index, depth, conversion);
        default:
            return std::unexpected(std::format("Lua value type '{}' cannot cross the host boundary.", luaL_typename(state, index)));
        }
    }

    [[nodiscard]] std::expected<Value, std::string> readTable(lua_State *state, int index, std::size_t depth, ConversionState &conversion) const
    {
        index = lua_absindex(state, index);
        auto const identity = lua_topointer(state, index);
        if (!conversion.activeTables.insert(identity).second)
        {
            return std::unexpected("Lua value contains a cyclic table.");
        }

        auto object = Value::Object{};
        auto indexed = std::map<std::int64_t, Value>{};
        auto keyKind = 0;
        auto entryCount = std::size_t{};
        lua_pushnil(state);
        while (lua_next(state, index) != 0)
        {
            ++entryCount;
            if (entryCount > config_.limits.maximumTableEntries)
            {
                return std::unexpected("Lua table exceeds the entry limit.");
            }

            auto value = readValue(state, -1, depth + 1u, conversion);
            if (!value)
            {
                return value;
            }

            if (lua_type(state, -2) == LUA_TSTRING)
            {
                if (keyKind == 2)
                {
                    return std::unexpected("Lua table cannot mix array and object keys.");
                }
                keyKind = 1;
                auto size = std::size_t{};
                auto const *key = lua_tolstring(state, -2, std::addressof(size));
                if (size > config_.limits.maximumStringBytes)
                {
                    return std::unexpected("Lua object key exceeds the string byte limit.");
                }
                object.emplace(std::string{key, size}, std::move(*value));
            }
            else if (lua_isinteger(state, -2))
            {
                if (keyKind == 1)
                {
                    return std::unexpected("Lua table cannot mix array and object keys.");
                }
                keyKind = 2;
                auto const key = static_cast<std::int64_t>(lua_tointeger(state, -2));
                if (key <= 0 || static_cast<std::uint64_t>(key) > config_.limits.maximumTableEntries)
                {
                    return std::unexpected("Lua array keys must be positive contiguous integers.");
                }
                indexed.emplace(key, std::move(*value));
            }
            else
            {
                return std::unexpected("Lua tables crossing the host boundary require string or array keys.");
            }
            lua_pop(state, 1);
        }

        conversion.activeTables.erase(identity);
        if (keyKind != 2)
        {
            return Value{std::move(object)};
        }
        if (indexed.size() != entryCount || (!indexed.empty() && indexed.rbegin()->first != static_cast<std::int64_t>(indexed.size())))
        {
            return std::unexpected("Lua array keys must be contiguous.");
        }
        auto array = Value::Array{};
        array.reserve(indexed.size());
        std::ranges::transform(indexed, std::back_inserter(array), [](auto &entry) { return std::move(entry.second); });
        return Value{std::move(array)};
    }

    [[nodiscard]] bool pushValue(lua_State *state, const Value &value, std::size_t depth, std::size_t &nodes)
    {
        if (depth > config_.limits.maximumConversionDepth || ++nodes > config_.limits.maximumConvertedNodes)
        {
            pendingError_ = "Host value exceeds the Lua conversion limits.";
            return false;
        }

        return std::visit(
            [&](auto const &stored) {
                using Stored = std::remove_cvref_t<decltype(stored)>;
                if constexpr (std::same_as<Stored, std::monostate>)
                {
                    lua_pushnil(state);
                    return true;
                }
                else if constexpr (std::same_as<Stored, bool>)
                {
                    lua_pushboolean(state, stored);
                    return true;
                }
                else if constexpr (std::same_as<Stored, std::int64_t>)
                {
                    lua_pushinteger(state, static_cast<lua_Integer>(stored));
                    return true;
                }
                else if constexpr (std::same_as<Stored, double>)
                {
                    if (!std::isfinite(stored))
                    {
                        pendingError_ = "Host value contains a non-finite number.";
                        return false;
                    }
                    lua_pushnumber(state, static_cast<lua_Number>(stored));
                    return true;
                }
                else if constexpr (std::same_as<Stored, std::string>)
                {
                    if (stored.size() > config_.limits.maximumStringBytes)
                    {
                        pendingError_ = "Host string exceeds the Lua conversion byte limit.";
                        return false;
                    }
                    lua_pushlstring(state, stored.data(), stored.size());
                    return true;
                }
                else if constexpr (std::same_as<Stored, Value::Array>)
                {
                    if (stored.size() > config_.limits.maximumTableEntries)
                    {
                        pendingError_ = "Host array exceeds the Lua table entry limit.";
                        return false;
                    }
                    lua_createtable(state, static_cast<int>(stored.size()), 0);
                    auto const tableIndex = lua_gettop(state);
                    auto indices = std::views::iota(std::size_t{0u}, stored.size());
                    auto valid = true;
                    std::ranges::for_each(indices, [&](std::size_t index) {
                        if (!valid)
                        {
                            return;
                        }
                        valid = pushValue(state, stored[index], depth + 1u, nodes);
                        if (valid)
                        {
                            lua_seti(state, tableIndex, static_cast<lua_Integer>(index + 1u));
                        }
                    });
                    return valid;
                }
                else
                {
                    if (stored.size() > config_.limits.maximumTableEntries)
                    {
                        pendingError_ = "Host object exceeds the Lua table entry limit.";
                        return false;
                    }
                    lua_createtable(state, 0, static_cast<int>(stored.size()));
                    auto const tableIndex = lua_gettop(state);
                    auto valid = true;
                    std::ranges::for_each(stored, [&](auto const &entry) {
                        if (!valid)
                        {
                            return;
                        }
                        if (entry.first.size() > config_.limits.maximumStringBytes)
                        {
                            pendingError_ = "Host object key exceeds the Lua string byte limit.";
                            valid = false;
                            return;
                        }
                        lua_pushlstring(state, entry.first.data(), entry.first.size());
                        valid = pushValue(state, entry.second, depth + 1u, nodes);
                        if (valid)
                        {
                            lua_settable(state, tableIndex);
                        }
                    });
                    return valid;
                }
            },
            value.storage);
    }

    [[nodiscard]] int invokeHost(lua_State *state, std::size_t index)
    {
        pendingError_.clear();
        pendingYield_ = false;
        if (index >= hostFunctions_.size())
        {
            pendingError_ = "Invalid Lua host function binding.";
            return -1;
        }

        auto conversion = ConversionState{};
        auto arguments = std::vector<Value>{};
        arguments.reserve(static_cast<std::size_t>(lua_gettop(state)));
        auto indices = std::views::iota(1, lua_gettop(state) + 1);
        auto valid = true;
        std::ranges::for_each(indices, [&](int argumentIndex) {
            if (!valid)
            {
                return;
            }
            auto converted = readValue(state, argumentIndex, 0u, conversion);
            if (!converted)
            {
                pendingError_ = std::move(converted.error());
                valid = false;
                return;
            }
            arguments.push_back(std::move(*converted));
        });
        if (!valid)
        {
            return -1;
        }

        auto result = hostFunctions_[index].function(arguments);
        if (!result.error.empty())
        {
            pendingError_ = std::move(result.error);
            return -1;
        }
        if (result.yield)
        {
            if (!result.values.empty())
            {
                pendingError_ = "A yielding Lua host function cannot also return values.";
                return -1;
            }
            pendingYield_ = true;
            return -1;
        }

        auto nodes = std::size_t{};
        auto pushed = 0;
        std::ranges::for_each(result.values, [&](const Value &value) {
            if (pushed < 0)
            {
                return;
            }
            if (!pushValue(state, value, 0u, nodes))
            {
                pushed = -1;
                return;
            }
            ++pushed;
        });
        return pushed;
    }

    static void hook(lua_State *state, lua_Debug *)
    {
        lua_rawgetp(state, LUA_REGISTRYINDEX, std::addressof(kImplRegistryKey_));
        auto *implementation = static_cast<Impl *>(lua_touserdata(state, -1));
        lua_pop(state, 1);
        if (implementation == nullptr)
        {
            lua_pushliteral(state, "Lua sandbox hook lost its host state.");
            lua_error(state);
            return;
        }

        implementation->executedInstructions_ += implementation->config_.limits.hookInstructionInterval;
        if (implementation->executedInstructions_ >= implementation->config_.limits.maximumInstructionsPerResume)
        {
            lua_pushliteral(state, "Lua resume exceeded the instruction budget.");
            lua_error(state);
            return;
        }
        if (std::chrono::steady_clock::now() - implementation->resumeStartedAt_ > implementation->config_.limits.softWallBudget)
        {
            lua_pushliteral(state, "Lua resume exceeded the soft wall-time budget.");
            lua_error(state);
        }
    }

    static inline char kImplRegistryKey_ = 0;

    lua_State *state_ = nullptr;
    lua_State *coroutine_ = nullptr;
    int coroutineReference_ = LUA_NOREF;
    int environmentReference_ = LUA_NOREF;
    SandboxConfig config_{};
    std::vector<HostFunctionBinding> hostFunctions_{};
    AllocationState allocation_{};
    std::optional<ResumeStatus> terminalStatus_{};
    std::string terminalDetail_{};
    std::string pendingError_{};
    bool pendingYield_ = false;
    std::uint64_t executedInstructions_ = 0u;
    std::chrono::steady_clock::time_point resumeStartedAt_{};
};

Sandbox::Sandbox() : impl_(std::make_unique<Impl>())
{
}

Sandbox::~Sandbox() = default;

SandboxStartResult Sandbox::start(std::string source, std::string chunkName, SandboxConfig config)
{
    return impl_->start(std::move(source), std::move(chunkName), std::move(config));
}

ResumeResult Sandbox::resume()
{
    return impl_->resume();
}

void Sandbox::close() noexcept
{
    impl_->close();
}

bool Sandbox::started() const noexcept
{
    return impl_->started();
}

std::size_t Sandbox::allocatedBytes() const noexcept
{
    return impl_->allocatedBytes();
}
} // namespace dependency::lua
