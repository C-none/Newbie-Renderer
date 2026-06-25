
export module nr.rhi:slang;
import dependency.slang;
import dependency.vulkan;

import nr.utils;
import std;

namespace nr::rhi::detail
{
[[nodiscard]] constexpr bool slangSucceeded(SlangResult result) noexcept
{
    return result >= 0;
}

[[nodiscard]] std::string moduleNameToPath(std::string_view moduleName);

[[nodiscard]] std::string modulePathToName(std::string_view modulePath);

[[nodiscard]] std::string readTextFile(const std::filesystem::path &path);

[[nodiscard]] std::vector<std::byte> readBinaryFile(const std::filesystem::path &path);
[[nodiscard]] std::filesystem::path normalizePath(const std::filesystem::path &path);

[[nodiscard]] std::string normalizeModuleNameFromSourceToken(std::string token);

[[nodiscard]] std::optional<std::string> extractDeclaredModuleNameFromSource(std::string_view sourceText);

[[nodiscard]] std::optional<std::string> deriveModuleNameFromSourcePath(const std::filesystem::path &sourcePath, std::span<const std::string> searchPaths);

[[nodiscard]] std::optional<std::string> normalizeRequestModulePath(const std::filesystem::path &requestPath);

[[nodiscard]] std::string moduleLeafName(std::string_view moduleName);

[[nodiscard]] std::filesystem::path resolveShaderRootPath();

[[nodiscard]] std::filesystem::path makeModuleBinaryPath(const std::filesystem::path &cacheRoot, std::string_view moduleName);

[[nodiscard]] std::vector<std::filesystem::path> makeModuleSourceSuffixes(
    std::string_view moduleName);

[[nodiscard]] std::vector<std::filesystem::path> makeModulePathCandidates(
    std::string_view moduleName,
    std::optional<std::filesystem::path> explicitSourcePath,
    std::span<const std::string> searchPaths);

} // namespace nr::rhi::detail

export namespace nr::rhi
{

/**
 * @brief Convert a Slang shader stage to the corresponding Vulkan stage flag.
 *
 * @note Unknown stages currently map to `vk::ShaderStageFlagBits::eAll`.
 */
[[nodiscard]] constexpr vk::ShaderStageFlagBits toVkShaderStage(SlangStage stage) noexcept
{
    switch (stage)
    {
    case SLANG_STAGE_VERTEX:
        return vk::ShaderStageFlagBits::eVertex;
    case SLANG_STAGE_HULL:
        return vk::ShaderStageFlagBits::eTessellationControl;
    case SLANG_STAGE_DOMAIN:
        return vk::ShaderStageFlagBits::eTessellationEvaluation;
    case SLANG_STAGE_GEOMETRY:
        return vk::ShaderStageFlagBits::eGeometry;
    case SLANG_STAGE_FRAGMENT:
        return vk::ShaderStageFlagBits::eFragment;
    case SLANG_STAGE_COMPUTE:
        return vk::ShaderStageFlagBits::eCompute;
    case SLANG_STAGE_AMPLIFICATION:
        return vk::ShaderStageFlagBits::eTaskEXT;
    case SLANG_STAGE_MESH:
        return vk::ShaderStageFlagBits::eMeshEXT;
    case SLANG_STAGE_RAY_GENERATION:
        return vk::ShaderStageFlagBits::eRaygenKHR;
    case SLANG_STAGE_INTERSECTION:
        return vk::ShaderStageFlagBits::eIntersectionKHR;
    case SLANG_STAGE_ANY_HIT:
        return vk::ShaderStageFlagBits::eAnyHitKHR;
    case SLANG_STAGE_CLOSEST_HIT:
        return vk::ShaderStageFlagBits::eClosestHitKHR;
    case SLANG_STAGE_MISS:
        return vk::ShaderStageFlagBits::eMissKHR;
    case SLANG_STAGE_CALLABLE:
        return vk::ShaderStageFlagBits::eCallableKHR;
    default:
        return vk::ShaderStageFlagBits::eAll;
    }
}

/**
 * @brief A single preprocessor macro passed to Slang session creation.
 */
struct SlangMacro
{
    std::string_view name;
    std::string_view value;
};

/**
 * @brief Compile-time friendly compiler option payload.
 */
struct SlangCompilerOption
{
    slang::CompilerOptionName name = slang::CompilerOptionName::VulkanBindShiftAll;
    slang::CompilerOptionValueKind kind = slang::CompilerOptionValueKind::Int;
    std::int32_t intValue0 = 0;
    std::int32_t intValue1 = 0;
    std::string_view stringValue0{};
    std::string_view stringValue1{};
};

/**
 * @brief Global compile configuration shared by all shader programs.
 *
 * This project uses one unified Slang session configuration for all modules.
 */
template <std::size_t SearchPathCount, std::size_t MacroCount, std::size_t CompilerOptionCount>
struct SlangCompileOptions
{
    std::array<std::string_view, SearchPathCount> searchPaths;
    std::array<SlangMacro, MacroCount> macros;
    SlangCompileTarget target = SLANG_SPIRV;
    std::string_view profile = "SPIRV_1_6";
    std::array<SlangCompilerOption, CompilerOptionCount> compilerOptions;
    std::uint64_t hashValue = hash::fnv1a64OffsetBasis;
    std::array<char, 16> hashHex = hash::toHexChars(hash::fnv1a64OffsetBasis);
};

template <std::size_t SearchPathCount, std::size_t MacroCount, std::size_t CompilerOptionCount>
[[nodiscard]] consteval std::uint64_t computeCompileOptionsHashValue(
    const SlangCompileOptions<SearchPathCount, MacroCount, CompilerOptionCount> &options) noexcept
{
    std::uint64_t state = hash::fnv1a64OffsetBasis;
    hash::hashAppend(state, static_cast<std::uint32_t>(options.target));
    hash::hashAppendString(state, options.profile);

    std::ranges::for_each(options.searchPaths, [&](std::string_view path) constexpr noexcept {
        hash::hashAppendString(state, path);
    });
    std::ranges::for_each(options.macros, [&](const SlangMacro &macro) constexpr noexcept {
        hash::hashAppendString(state, macro.name);
        hash::hashAppendString(state, macro.value);
    });
    std::ranges::for_each(options.compilerOptions, [&](const SlangCompilerOption &option) constexpr noexcept {
        hash::hashAppend(state, static_cast<std::uint32_t>(option.name));
        hash::hashAppend(state, static_cast<std::uint32_t>(option.kind));
        hash::hashAppend(state, option.intValue0);
        hash::hashAppend(state, option.intValue1);
        hash::hashAppendString(state, option.stringValue0);
        hash::hashAppendString(state, option.stringValue1);
    });
    return state;
}

template <std::size_t SearchPathCount, std::size_t MacroCount, std::size_t CompilerOptionCount>
[[nodiscard]] consteval SlangCompileOptions<SearchPathCount, MacroCount, CompilerOptionCount> finalizeCompileOptions(
    SlangCompileOptions<SearchPathCount, MacroCount, CompilerOptionCount> options) noexcept
{
    options.hashValue = computeCompileOptionsHashValue(options);
    options.hashHex = hash::toHexChars(options.hashValue);
    return options;
}

[[nodiscard]] consteval std::array<SlangCompilerOption, 6> makeBaseCompilerOptions(
    std::int32_t optimizationLevel,
    std::int32_t debugInfoLevel,
    std::int32_t richDiagnosticsEnabled) noexcept
{
    return std::array<SlangCompilerOption, 6>{
        SlangCompilerOption{.name = slang::CompilerOptionName::EmitSpirvDirectly, .intValue0 = 1},
        SlangCompilerOption{.name = slang::CompilerOptionName::VulkanUseEntryPointName, .intValue0 = 1},
        SlangCompilerOption{.name = slang::CompilerOptionName::UseUpToDateBinaryModule, .intValue0 = 1},
        SlangCompilerOption{.name = slang::CompilerOptionName::Optimization, .intValue0 = optimizationLevel},
        SlangCompilerOption{.name = slang::CompilerOptionName::DebugInformation, .intValue0 = debugInfoLevel},
        SlangCompilerOption{.name = slang::CompilerOptionName::EnableRichDiagnostics, .intValue0 = richDiagnosticsEnabled},
    };
}

template <bool IsDebugModeValue>
[[nodiscard]] consteval auto defaultCompilerOptions() noexcept
    -> std::conditional_t<IsDebugModeValue, std::array<SlangCompilerOption, 8>, std::array<SlangCompilerOption, 6>>
{
    auto baseOptions = makeBaseCompilerOptions(
        IsDebugModeValue ? SLANG_OPTIMIZATION_LEVEL_NONE : SLANG_OPTIMIZATION_LEVEL_MAXIMAL,
        IsDebugModeValue ? SLANG_DEBUG_INFO_LEVEL_MAXIMAL : SLANG_DEBUG_INFO_LEVEL_NONE,
        IsDebugModeValue ? 1 : 0);

    using ResultType = std::conditional_t<IsDebugModeValue, std::array<SlangCompilerOption, 8>, std::array<SlangCompilerOption, 6>>;
    ResultType options{};
    std::ranges::copy(baseOptions, options.begin());

    if constexpr (IsDebugModeValue)
    {
        // Dump a repro package on any compilation error so the exact failing
        // input can be replayed offline via slangc --load-repro.
        options[options.size() - 2] = SlangCompilerOption{
            .name = slang::CompilerOptionName::DumpReproOnError,
            .intValue0 = 1,
        };
        options.back() = SlangCompilerOption{
            .name = slang::CompilerOptionName::WarningsAsErrors,
            .kind = slang::CompilerOptionValueKind::String,
            .stringValue0 = "all",
        };
    }

    return options;
}

inline constexpr auto kDefaultSlangCompileOptions = []() consteval {
    constexpr auto compilerOptions = defaultCompilerOptions<isDebugMode>();
    auto options = SlangCompileOptions<1, 0, compilerOptions.size()>{
        .searchPaths = {shaderRoot},
        .macros = {},
        .target = SLANG_SPIRV,
        .profile = "SPIRV_1_6",
        .compilerOptions = compilerOptions,
    };
    return finalizeCompileOptions(options);
}();

/**
 * @brief Vulkan sampler creation parameters used by Slang-related APIs.
 */
struct SlangSamplerDesc
{
    vk::Filter magFilter = vk::Filter::eLinear;
    vk::Filter minFilter = vk::Filter::eLinear;
    vk::SamplerMipmapMode mipmapMode = vk::SamplerMipmapMode::eLinear;
    vk::SamplerAddressMode addressModeU = vk::SamplerAddressMode::eRepeat;
    vk::SamplerAddressMode addressModeV = vk::SamplerAddressMode::eRepeat;
    vk::SamplerAddressMode addressModeW = vk::SamplerAddressMode::eRepeat;
    float mipLodBias = 0.0f;
    bool anisotropyEnable = false;
    float maxAnisotropy = 1.0f;
    bool compareEnable = false;
    vk::CompareOp compareOp = vk::CompareOp::eAlways;
    float minLod = 0.0f;
    float maxLod = vk::LodClampNone;
    vk::BorderColor borderColor = vk::BorderColor::eFloatTransparentBlack;
    bool unnormalizedCoordinates = false;
};

/**
 * @brief RAII wrapper for a Vulkan sampler used in Slang descriptor workflows.
 */
class SlangSampler
{
  public:
    SlangSampler() = default;
    SlangSampler(const SlangSampler &) = delete;
    SlangSampler &operator=(const SlangSampler &) = delete;
    SlangSampler(SlangSampler &&) noexcept = default;
    SlangSampler &operator=(SlangSampler &&) noexcept = default;

    /**
     * @brief Create a Vulkan sampler from `SlangSamplerDesc`.
     */
    [[nodiscard]] static SlangSampler create(const vk::raii::Device &device, SlangSamplerDesc desc = {}, std::string_view debugName = {});

    /**
     * @brief Return whether this sampler owns a valid Vulkan handle.
     */
    [[nodiscard]] bool valid() const noexcept;

    /**
     * @brief Get the underlying RAII sampler handle, or nullptr if invalid.
     */
    [[nodiscard]] const vk::raii::Sampler *handle() const noexcept;

    /**
     * @brief Get the raw Vulkan sampler handle.
     */
    [[nodiscard]] vk::Sampler raw() const noexcept;

  private:
    vk::raii::Sampler sampler_ = {nullptr};
    std::string debugName_;
};

/**
 * @brief Immutable sampler declaration for backend pipeline creation.
 */
struct SlangImmutableSamplerBinding
{
    std::uint32_t set = 0;
    std::uint32_t binding = 0;
    std::uint32_t descriptorCount = 1;
    SlangSamplerDesc samplerDesc{};
};

/**
 * @brief Compiled and reflected data for one linked entrypoint.
 */
struct SlangEntryPointData
{
    std::uint32_t linkedEntryPointIndex = 0;
    std::string entryPointName;
    SlangStage stage = SLANG_STAGE_NONE;
    Slang::ComPtr<slang::IBlob> codeBlob;

    [[nodiscard]] bool valid() const noexcept;
};

/**
 * @brief Result of compiling/loading a single module from cache or source.
 */
struct SlangCompiledModule
{
    std::string moduleName;
    std::filesystem::path sourcePath;
    Slang::ComPtr<slang::IModule> module;

    /**
     * @brief Return true when `module` is a valid Slang module handle.
     */
    [[nodiscard]] bool valid() const noexcept;
};

/**
 * @brief Linked shader program exposed to pipeline creation code.
 */
class SlangProgram
{
  public:
    /**
     * @brief Return whether the linked component and entrypoint blobs are available.
     */
    [[nodiscard]] bool valid() const noexcept;

    /**
     * @brief Number of linked entrypoints available in this program.
     */
    [[nodiscard]] std::size_t entryPointCount() const noexcept;

    /**
     * @brief Access all linked entrypoint payloads.
     */
    [[nodiscard]] std::span<const SlangEntryPointData> entryPoints() const noexcept;

    /**
     * @brief Find entrypoint payload by name.
     */
    [[nodiscard]] const SlangEntryPointData *entryPointData(std::string_view entryPointName) const noexcept;

    /**
     * @brief Get linked entrypoint reflection by name.
     */
    [[nodiscard]] slang::EntryPointReflection *entryPointLayout(std::string_view entryPointName) const noexcept;

    /**
     * @brief Get linked entrypoint stage by name.
     */
    [[nodiscard]] std::optional<SlangStage> entryPointStage(std::string_view entryPointName) const noexcept;

    /**
     * @brief Get linked entrypoint code blob by name.
     */
    [[nodiscard]] slang::IBlob *entryPointBlob(std::string_view entryPointName) const noexcept;

    /**
     * @brief Access the underlying linked Slang component type.
     */
    [[nodiscard]] slang::IComponentType *componentType() const noexcept;

    /**
     * @brief Access the linked Slang program layout reflection.
     */
    [[nodiscard]] slang::ProgramLayout *programLayout() const noexcept;

  private:
    friend class ShaderService;

    [[nodiscard]] const SlangEntryPointData *findEntryPointDataCached(std::string_view entryPointName) const noexcept;

    [[nodiscard]] bool buildEntryPointCache() const noexcept;

    Slang::ComPtr<slang::IComponentType> linkedProgram_;
    mutable bool hasQueriedProgramLayout_ = false;
    mutable slang::ProgramLayout *cachedProgramLayout_ = nullptr;
    mutable bool entryPointCacheBuilt_ = false;
    mutable std::vector<SlangEntryPointData> entryPoints_;
    mutable std::map<std::string, std::size_t> entryPointIndexByName_;
};

struct SlangProgramCompileFileRequest
{
    // Required input form:
    // - test/utils/useFlag
    std::filesystem::path sourcePath;
};

struct RuntimeSlangMacro
{
    std::string name;
    std::string value;
};

struct RuntimeSlangCompilerOption
{
    slang::CompilerOptionName name = slang::CompilerOptionName::VulkanBindShiftAll;
    slang::CompilerOptionValueKind kind = slang::CompilerOptionValueKind::Int;
    std::int32_t intValue0 = 0;
    std::int32_t intValue1 = 0;
    std::string stringValue0;
    std::string stringValue1;
};

struct RuntimeSlangCompileOptions
{
    std::vector<std::string> searchPaths;
    std::vector<RuntimeSlangMacro> macros;
    SlangCompileTarget target = SLANG_SPIRV;
    std::string profile = "SPIRV_1_6";
    std::vector<RuntimeSlangCompilerOption> compilerOptions;
};

/**
 * @brief Process-wide Slang frontend service used by NR RHI.
 *
 * Responsibilities:
 * - Session configuration
 * - Module compile/load with cache
 * - Program link and deferred entrypoint query
 * - Hot-reload/cache freshness checks
 */
class ShaderService
{
  public:
    /**
     * @brief Get the global singleton instance.
     */
    [[nodiscard]] static ShaderService &instance();

    /**
     * @brief Configure Slang session options and reset in-memory caches.
     */
    template <std::size_t SearchPathCount, std::size_t MacroCount, std::size_t CompilerOptionCount>
    void configure(const SlangCompileOptions<SearchPathCount, MacroCount, CompilerOptionCount> &options)
    {
        std::scoped_lock lock(m_mutex);
        applyCompileOptionsLocked(options);
    }

    /**
     * @brief Configure Slang session with project default compile options.
     */
    void configure();

    [[nodiscard]] SlangProgram compileProgramByFile(const SlangProgramCompileFileRequest &request);

  private:
    ShaderService() = default;

    static void writeModuleCacheBlobAsync(Slang::ComPtr<slang::IModule> module, const std::filesystem::path &moduleBlobPath);

    [[nodiscard]] std::optional<std::string> validateModulePathOrganizationLocked(std::string_view moduleName, std::string_view modulePath) const;

    template <std::size_t SearchPathCount, std::size_t MacroCount, std::size_t CompilerOptionCount>
    [[nodiscard]] static RuntimeSlangCompileOptions materializeRuntimeOptions(
        const SlangCompileOptions<SearchPathCount, MacroCount, CompilerOptionCount> &options)
    {
        RuntimeSlangCompileOptions runtimeOptions;
        runtimeOptions.target = options.target;
        runtimeOptions.profile = std::string(options.profile);
        runtimeOptions.searchPaths = options.searchPaths |
                                     std::views::transform([](std::string_view value) { return std::string(value); }) |
                                     std::ranges::to<std::vector>();
        runtimeOptions.macros = options.macros |
                                std::views::transform([](const SlangMacro &macro) {
                                    return RuntimeSlangMacro{
                                        .name = std::string(macro.name),
                                        .value = std::string(macro.value),
                                    };
                                }) |
                                std::ranges::to<std::vector>();
        runtimeOptions.compilerOptions = options.compilerOptions |
                                         std::views::transform([](const SlangCompilerOption &option) {
                                             return RuntimeSlangCompilerOption{
                                                 .name = option.name,
                                                 .kind = option.kind,
                                                 .intValue0 = option.intValue0,
                                                 .intValue1 = option.intValue1,
                                                 .stringValue0 = std::string(option.stringValue0),
                                                 .stringValue1 = std::string(option.stringValue1),
                                             };
                                         }) |
                                         std::ranges::to<std::vector>();
        return runtimeOptions;
    }

    template <std::size_t SearchPathCount, std::size_t MacroCount, std::size_t CompilerOptionCount>
    void applyCompileOptionsLocked(const SlangCompileOptions<SearchPathCount, MacroCount, CompilerOptionCount> &options)
    {
        auto runtimeOptions = materializeRuntimeOptions(options);
        auto resolvedShaderRootPath = detail::resolveShaderRootPath();
        runtimeOptions.searchPaths = {resolvedShaderRootPath.generic_string()};

        auto hashHex = std::string(hash::toHexView(options.hashHex));
        if (m_session && m_optionsHashHex == hashHex)
        {
            return;
        }

        m_options = std::move(runtimeOptions);
        m_shaderRootPath = std::move(resolvedShaderRootPath);
        m_optionsHashValue = options.hashValue;
        m_optionsHashHex = std::move(hashHex);

        ensureGlobalSessionLocked();
        recreateSessionLocked();

    }

    // Requires m_mutex.
    void ensureGlobalSessionLocked();

    void ensureConfiguredLocked();

    [[nodiscard]] std::uint64_t computeOptionsHashValueLocked() const noexcept;

    [[nodiscard]] std::string_view optionsHashLocked() const noexcept;

    [[nodiscard]] std::filesystem::path moduleCacheRootLocked() const;

    void invalidateStaleModuleCacheLocked(std::string_view modulePath, const std::filesystem::path &moduleBlobPath);

    void recreateSessionLocked();

    void emitDiagnosticsLocked(slang::IBlob *diagnostics, std::string_view context) const;

    /**
     * @brief Resolve canonical dotted module name from relative module-path input.
     *
     * Internal Input/Output examples:
    * - input:  test/utils/useFlag
    *   output: test.utils.useFlag
     * - input valid, but declared module token conflicts with derived leaf token
     *   output: "" (empty, hard fail)
     */
    [[nodiscard]] std::string resolveModuleNameLocked(std::string_view modulePath) const;

    SlangCompiledModule loadOrCompileModuleLocked(const std::string &moduleName, std::optional<std::string> explicitModulePath);


  public:
    // Locking policy: all access to Slang session/global-session/module/component APIs
    // is serialized by m_mutex because those interfaces are treated as non-thread-safe
    // in this renderer integration. Methods with `Locked` suffix require the caller to
    // hold m_mutex.
    mutable std::mutex m_mutex;
    Slang::ComPtr<slang::IGlobalSession> m_globalSession;
    Slang::ComPtr<slang::ISession> m_session;

    RuntimeSlangCompileOptions m_options;
    std::uint64_t m_optionsHashValue = kDefaultSlangCompileOptions.hashValue;
    std::string m_optionsHashHex = std::string(hash::toHexView(kDefaultSlangCompileOptions.hashHex));
    std::filesystem::path m_shaderRootPath = detail::resolveShaderRootPath();

    std::vector<std::string> m_effectiveSearchPaths;
    std::vector<const char *> m_searchPathPointers;
    std::vector<slang::PreprocessorMacroDesc> m_macroDescs;
    std::vector<slang::CompilerOptionEntry> m_compilerOptionEntries;
    slang::TargetDesc m_targetDesc{};

};

} // namespace nr::rhi
