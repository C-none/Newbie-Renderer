
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

[[nodiscard]] std::filesystem::path resolveShaderRootPath();

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

    std::ranges::for_each(options.searchPaths,
                          [&](std::string_view path) constexpr noexcept { hash::hashAppendString(state, path); });
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

static_assert(shaderOptimizationLevel >= SLANG_OPTIMIZATION_LEVEL_NONE &&
                  shaderOptimizationLevel <= SLANG_OPTIMIZATION_LEVEL_MAXIMAL,
              "shaderOptimizationLevel must map to a valid Slang optimization level.");
static_assert(shaderDebugInfoLevel >= SLANG_DEBUG_INFO_LEVEL_NONE &&
                  shaderDebugInfoLevel <= SLANG_DEBUG_INFO_LEVEL_MAXIMAL,
              "shaderDebugInfoLevel must map to a valid Slang debug information level.");

[[nodiscard]] consteval std::array<SlangCompilerOption, 8> makeBaseCompilerOptions() noexcept
{
    return std::array<SlangCompilerOption, 8>{
        SlangCompilerOption{.name = slang::CompilerOptionName::EmitSpirvDirectly, .intValue0 = 1},
        SlangCompilerOption{.name = slang::CompilerOptionName::VulkanUseEntryPointName, .intValue0 = 1},
        SlangCompilerOption{.name = slang::CompilerOptionName::UseUpToDateBinaryModule, .intValue0 = 1},
        SlangCompilerOption{.name = slang::CompilerOptionName::MatrixLayoutRow, .intValue0 = 1},
        SlangCompilerOption{
            .name = slang::CompilerOptionName::LanguageVersion,
            .intValue0 = static_cast<std::int32_t>(slang::languageVersion2026),
        },
        SlangCompilerOption{.name = slang::CompilerOptionName::Optimization, .intValue0 = shaderOptimizationLevel},
        SlangCompilerOption{.name = slang::CompilerOptionName::DebugInformation, .intValue0 = shaderDebugInfoLevel},
        SlangCompilerOption{
            .name = slang::CompilerOptionName::EnableRichDiagnostics,
            .intValue0 = shaderRichDiagnosticsEnabled ? 1 : 0,
        },
    };
}

inline constexpr std::size_t kDefaultSlangCompilerOptionCount =
    std::size_t{8} + (shaderDumpReproOnError ? std::size_t{1} : std::size_t{0}) +
    (!shaderWarningsAsErrors.empty() ? std::size_t{1} : std::size_t{0});

[[nodiscard]] consteval auto defaultCompilerOptions() noexcept
    -> std::array<SlangCompilerOption, kDefaultSlangCompilerOptionCount>
{
    auto baseOptions = makeBaseCompilerOptions();
    auto options = std::array<SlangCompilerOption, kDefaultSlangCompilerOptionCount>{};
    std::ranges::copy(baseOptions, options.begin());
    auto writeCursor = baseOptions.size();

    if constexpr (shaderDumpReproOnError)
    {
        // Dump a repro package on any compilation error so the exact failing
        // input can be replayed offline via slangc --load-repro.
        options[writeCursor] = SlangCompilerOption{
            .name = slang::CompilerOptionName::DumpReproOnError,
            .intValue0 = 1,
        };
        ++writeCursor;
    }

    if constexpr (!shaderWarningsAsErrors.empty())
    {
        options[writeCursor] = SlangCompilerOption{
            .name = slang::CompilerOptionName::WarningsAsErrors,
            .kind = slang::CompilerOptionValueKind::String,
            .stringValue0 = shaderWarningsAsErrors,
        };
    }

    static_cast<void>(writeCursor);
    return options;
}

inline constexpr auto kDefaultSlangCompileOptions = []() consteval {
    constexpr auto compilerOptions = defaultCompilerOptions();
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
    [[nodiscard]] static SlangSampler create(const vk::raii::Device &device, SlangSamplerDesc desc = {},
                                             std::string_view debugName = {});

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
    std::string entryPointName;
    std::string debugName;
    SlangStage stage = SLANG_STAGE_NONE;
    std::shared_ptr<const std::vector<std::uint32_t>> spirv{};

    [[nodiscard]] bool valid() const noexcept;
};

/**
 * @brief Linked shader program exposed to pipeline creation code.
 */
class SlangProgram
{
  public:
    /**
     * @brief Return whether the linked single-entry program and SPIR-V are available.
     */
    [[nodiscard]] bool valid() const noexcept;

    /**
     * @brief Access the program's only entrypoint.
     */
    [[nodiscard]] const SlangEntryPointData *entryPoint() const noexcept;

    /**
     * @brief Access the linked Slang program layout reflection.
     */
    [[nodiscard]] slang::ProgramLayout *programLayout() const noexcept;

  private:
    friend class ShaderService;

    struct State
    {
        Slang::ComPtr<slang::IComponentType> linkedProgram{};
        slang::ProgramLayout *programLayout = nullptr;
        SlangEntryPointData entryPoint{};
    };

    std::shared_ptr<const State> state_{};
};

using SlangVariantAssignmentValue = std::variant<bool, std::int32_t, std::uint32_t, float, std::string>;

struct SlangVariantAssignment
{
    std::string type{};
    SlangVariantAssignmentValue value = false;
};

struct SlangProgramVariantDesc
{
    std::map<std::string, SlangVariantAssignment> assignments{};

    SlangProgramVariantDesc &assign(std::string_view name, std::string_view type, SlangVariantAssignmentValue value);

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::uint64_t hashValue() const noexcept;
    /** Generate a standalone specialization module with the supplied valid Slang module name. */
    [[nodiscard]] std::string sourceText(std::string_view moduleName) const;
};

struct SlangProgramCompileFileRequest
{
    // Shader-root-relative module path. The referenced file must define exactly one entrypoint.
    std::filesystem::path sourcePath;
    SlangProgramVariantDesc variant{};
};

struct ShaderServiceConfig
{
    std::uint32_t backendWorkerCount = nr::threading::resolveWorkerCount(0, nr::maxThreads);
    bool persistentSpirvCache = true;
};

struct ShaderCompileBatchStats
{
    // Input and in-memory reuse counts are request-based. Batch-local duplicate requests count as
    // memory hits because they reuse the first prepared program in the same call.
    std::size_t requestCount = 0;
    std::size_t memoryHitCount = 0;
    // Persistent hits and backend compilations count unique opaque Slang entry hashes. A failed
    // backend attempt contributes to neither count and leaves its requested programs invalid.
    std::size_t persistentHitCount = 0;
    std::size_t backendCompilationCount = 0;
    std::size_t corruptCacheEntryCount = 0;
    std::uint32_t workerCount = 0;
    std::chrono::milliseconds frontendElapsed{};
    std::chrono::milliseconds backendElapsed{};
    std::chrono::milliseconds elapsed{};
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
 * - Single-entry program link and SPIR-V generation
 * - Persistent target artifact caching
 * - Bounded parallel backend compilation
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
        ensureBackendPoolLocked();
    }

    /**
     * @brief Configure Slang session with project default compile options.
     */
    void configure(ShaderServiceConfig config = {});

    void reloadSession();

    [[nodiscard]] std::uint64_t sessionGeneration() const;

    [[nodiscard]] ShaderCompileBatchStats lastCompileBatchStats() const;

    [[nodiscard]] SlangProgram compileProgramByFile(const SlangProgramCompileFileRequest &request);

    [[nodiscard]] std::vector<SlangProgram> compileProgramsByFile(
        std::span<const SlangProgramCompileFileRequest> requests);

  private:
    ShaderService() = default;

    void enqueueModuleCacheWrite(std::vector<std::byte> bytes, std::filesystem::path moduleBlobPath);

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
        runtimeOptions.macros = options.macros | std::views::transform([](const SlangMacro &macro) {
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

        auto hashHex = hash::toHexString(options.hashHex);
        if (m_session && m_optionsHashHex == hashHex)
        {
            return;
        }

        m_linkedProgramCache.clear();
        m_options = std::move(runtimeOptions);
        m_shaderRootPath = std::move(resolvedShaderRootPath);
        m_optionsHashHex = std::move(hashHex);

        ensureGlobalSessionLocked();
        recreateSessionLocked();
    }

    // Requires m_mutex.
    void ensureGlobalSessionLocked();

    void ensureConfiguredLocked();

    void ensureBackendPoolLocked();

    [[nodiscard]] std::string_view optionsHashLocked() const noexcept;

    [[nodiscard]] std::filesystem::path moduleCacheRootLocked() const;

    [[nodiscard]] std::filesystem::path spirvCacheRootLocked() const;

    void invalidateStaleModuleCacheLocked(std::string_view modulePath, const std::filesystem::path &moduleBlobPath);

    void recreateSessionLocked();

    void emitDiagnosticsLocked(slang::IBlob *diagnostics, std::string_view context) const;

    /**
     * @brief Resolve canonical dotted module name from relative module-path input.
     *
     * Internal Input/Output examples:
    * - input:  renderer/pathTracing/core
    *   output: renderer.pathTracing.core
     * - input valid, but declared module token conflicts with derived leaf token
     *   output: "" (empty, hard fail)
     */
    [[nodiscard]] std::string resolveModuleNameLocked(std::string_view modulePath) const;

    [[nodiscard]] Slang::ComPtr<slang::IModule> loadOrCompileModuleLocked(const std::string &moduleName);

    // Locking policy: frontend Slang operations are serialized by m_mutex. Backend workers only
    // call getEntryPointCode() on fully linked components whose lifetimes are owned by the active
    // batch. Methods with `Locked` suffix require the caller to hold m_mutex.
    mutable std::mutex m_mutex;
    Slang::ComPtr<slang::IGlobalSession> m_globalSession;
    Slang::ComPtr<slang::ISession> m_session;
    std::uint64_t m_sessionGeneration = 0;

    RuntimeSlangCompileOptions m_options;
    std::string m_optionsHashHex = hash::toHexString(kDefaultSlangCompileOptions.hashHex);
    std::filesystem::path m_shaderRootPath = detail::resolveShaderRootPath();

    std::vector<std::string> m_effectiveSearchPaths;
    std::vector<const char *> m_searchPathPointers;
    std::vector<slang::PreprocessorMacroDesc> m_macroDescs;
    std::vector<slang::CompilerOptionEntry> m_compilerOptionEntries;
    slang::TargetDesc m_targetDesc{};
    std::map<std::string, SlangProgram> m_linkedProgramCache;
    ShaderServiceConfig m_serviceConfig{};
    ShaderCompileBatchStats m_lastCompileBatchStats{};
    std::unique_ptr<nr::threading::StaticThreadPool> m_backendPool{};
};

} // namespace nr::rhi
