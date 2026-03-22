module;

export module nr.rhi:slang;

import dependency;
import nr.utils;
import std;

namespace nr::rhi::detail
{
[[nodiscard]] constexpr bool slangSucceeded(SlangResult result) noexcept
{
    return result >= 0;
}

[[nodiscard]] constexpr SlangResult makeSlangError(int32_t facility, int32_t code) noexcept
{
    return (facility << 16) | code | int32_t(0x80000000);
}

constexpr SlangResult kSlangOk = 0;
// constexpr SlangResult kSlangFail = makeSlangError(0, 0x4005);
constexpr SlangResult kSlangNoInterface = makeSlangError(0, 0x4002);
constexpr SlangResult kSlangInvalidArg = makeSlangError(7, 0x57);
constexpr SlangResult kSlangNotFound = makeSlangError(0x200, 5);

[[nodiscard]] std::string moduleNameToPath(std::string_view moduleName)
{
    std::string out(moduleName);
    for (auto &ch : out)
    {
        if (ch == '.')
        {
            ch = '/';
        }
    }
    return out;
}

[[nodiscard]] std::string modulePathToName(std::string_view modulePath)
{
    std::string pathString(modulePath);
    for (auto &ch : pathString)
    {
        if (ch == '\\')
        {
            ch = '/';
        }
    }

    if (pathString.ends_with(".slang"))
    {
        pathString.erase(pathString.size() - std::string(".slang").size());
    }

    while (pathString.starts_with("./"))
    {
        pathString.erase(0, 2);
    }

    for (auto &ch : pathString)
    {
        if (ch == '/')
        {
            ch = '.';
        }
    }
    return pathString;
}

[[nodiscard]] std::string readTextFile(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return {};

    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}
[[nodiscard]] std::filesystem::path normalizePath(const std::filesystem::path &path)
{
    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(path, ec);
    if (!ec)
        return canonical;

    auto absolute = std::filesystem::absolute(path, ec);
    if (!ec)
        return absolute;

    return path;
}

[[nodiscard]] std::string normalizeModuleNameFromSourceToken(std::string token)
{
    if (token.size() >= 2 && token.front() == '"' && token.back() == '"')
    {
        token = token.substr(1, token.size() - 2);
        return modulePathToName(token);
    }
    return token;
}

[[nodiscard]] std::optional<std::string> extractDeclaredModuleNameFromSource(std::string_view sourceText)
{
    static const std::regex kDeclRegex(R"((?:^|[\r\n])\s*(?:module|implementing)\s+((?:[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*)|(?:\"[^\"]+\"))\s*;)");

    std::string text(sourceText);
    std::smatch match;
    if (std::regex_search(text, match, kDeclRegex) && match.size() > 1)
    {
        auto moduleName = normalizeModuleNameFromSourceToken(match[1].str());
        if (!moduleName.empty())
        {
            return moduleName;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string> deriveModuleNameFromSourcePath(const std::filesystem::path &sourcePath, std::span<const std::string> searchPaths)
{
    auto normalizedSourcePath = normalizePath(sourcePath);
    for (auto const &searchPath : searchPaths)
    {
        auto normalizedSearchPath = normalizePath(searchPath);
        std::error_code ec;
        auto relativePath = std::filesystem::relative(normalizedSourcePath, normalizedSearchPath, ec);
        if (ec || relativePath.empty())
        {
            continue;
        }
        auto relativeText = relativePath.generic_string();
        if (relativeText.starts_with(".."))
        {
            continue;
        }

        if (relativePath.extension() == ".slang")
        {
            relativePath.replace_extension();
        }

        auto moduleName = modulePathToName(relativePath.generic_string());
        if (!moduleName.empty())
        {
            return moduleName;
        }
    }

    // Responsibility boundary:
    // This function only derives module name from filesystem path relation.
    // It must not parse shader source declarations.
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string> normalizeRequestModulePath(const std::filesystem::path &requestPath)
{
    if (requestPath.empty() || requestPath.is_absolute())
    {
        return std::nullopt;
    }

    auto pathText = requestPath.generic_string();
    while (pathText.starts_with("./"))
    {
        pathText.erase(0, 2);
    }

    if (pathText.ends_with(".slang"))
    {
        pathText.erase(pathText.size() - std::string{".slang"}.size());
    }

    std::filesystem::path normalizedPath(pathText);
    normalizedPath = normalizedPath.lexically_normal();
    if (normalizedPath.empty())
        return std::nullopt;

    auto hasParentTraversal = std::ranges::any_of(normalizedPath, [](const std::filesystem::path &part) {
        return part == "..";
    });
    if (hasParentTraversal)
        return std::nullopt;

    auto normalizedModuleName = modulePathToName(normalizedPath.generic_string());
    if (normalizedModuleName.empty())
    {
        return std::nullopt;
    }

    auto normalizedModulePath = moduleNameToPath(normalizedModuleName);
    if (normalizedModulePath.empty())
        return std::nullopt;
        
    return normalizedModulePath;
}

[[nodiscard]] std::string moduleLeafName(std::string_view moduleName)
{
    auto const pos = moduleName.rfind('.');
    if (pos == std::string_view::npos)
    {
        return std::string(moduleName);
    }
    return std::string(moduleName.substr(pos + 1));
}

[[nodiscard]] std::filesystem::path resolveShaderRootPath()
{
    auto rootPath = normalizePath(std::filesystem::path(std::string(shaderRoot)));
    std::error_code ec;
    auto exists = std::filesystem::exists(rootPath, ec);
    auto isDirectory = exists && std::filesystem::is_directory(rootPath, ec);
    nrAssert(!ec && isDirectory, std::format("Invalid shader root path from CMake: '{}'.", rootPath.generic_string()));
    return rootPath;
}

[[nodiscard]] std::filesystem::path makeModuleBinaryPath(const std::filesystem::path &cacheRoot, std::string_view moduleName)
{
    // Keep this path aligned with Slang's `loadModule` lookup logic:
    // `<searchPath>/<module-path>.slang-module`.
    return cacheRoot / (moduleNameToPath(moduleName) + ".slang-module");
}

[[nodiscard]] std::vector<std::filesystem::path> makeModuleSourceSuffixes(
    std::string_view moduleName)
{
    // Business mapping is strict and 1:1 with `shader/` tree:
    //   module test.utils.useFlag -> shader/test/utils/useFlag.slang
    return {std::filesystem::path(moduleNameToPath(moduleName) + ".slang")};
}

[[nodiscard]] std::vector<std::filesystem::path> makeModulePathCandidates(
    std::string_view moduleName,
    std::optional<std::filesystem::path> explicitSourcePath,
    std::span<const std::string> searchPaths)
{
    // Module source lookup convention:
    // - Use only configured search roots (shader root + session cache root).
    // - Source path mirrors module path: <root>/<module-path>.slang

    std::vector<std::filesystem::path> result;
    if (explicitSourcePath.has_value())
    {
        result.push_back(*explicitSourcePath);
        return result;
    }

    auto suffixes = makeModuleSourceSuffixes(moduleName);

    for (auto const &searchPath : searchPaths)
    {
        for (auto const &suffix : suffixes)
        {
            if (suffix.empty())
            {
                continue;
            }
            // Normalize path to use forward slashes for consistent comparisons
            auto candidate = std::filesystem::path(searchPath) / suffix;
            result.push_back(normalizePath(candidate));
        }
    }
    return result;
}

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
    int32_t intValue0 = 0;
    int32_t intValue1 = 0;
    std::string_view stringValue0{};
    std::string_view stringValue1{};
};

/**
 * @brief Global compile configuration shared by all shader programs.
 *
 * This project uses one unified Slang session configuration for all modules.
 */
template <size_t SearchPathCount, size_t MacroCount, size_t CompilerOptionCount>
struct SlangCompileOptions
{
    std::array<std::string_view, SearchPathCount> searchPaths;
    std::array<SlangMacro, MacroCount> macros;
    SlangCompileTarget target = SLANG_SPIRV;
    std::string_view profile = "SPIRV_1_6";
    std::array<SlangCompilerOption, CompilerOptionCount> compilerOptions;
    uint64_t hashValue = hash::fnv1a64OffsetBasis;
    std::array<char, 16> hashHex = hash::toHexChars(hash::fnv1a64OffsetBasis);
};

template <size_t SearchPathCount, size_t MacroCount, size_t CompilerOptionCount>
[[nodiscard]] consteval uint64_t computeCompileOptionsHashValue(
    const SlangCompileOptions<SearchPathCount, MacroCount, CompilerOptionCount> &options) noexcept
{
    uint64_t state = hash::fnv1a64OffsetBasis;
    hash::hashAppend(state, static_cast<uint32_t>(options.target));
    hash::hashAppendString(state, options.profile);

    std::ranges::for_each(options.searchPaths, [&](std::string_view path) constexpr noexcept {
        hash::hashAppendString(state, path);
    });
    std::ranges::for_each(options.macros, [&](const SlangMacro &macro) constexpr noexcept {
        hash::hashAppendString(state, macro.name);
        hash::hashAppendString(state, macro.value);
    });
    std::ranges::for_each(options.compilerOptions, [&](const SlangCompilerOption &option) constexpr noexcept {
        hash::hashAppend(state, static_cast<uint32_t>(option.name));
        hash::hashAppend(state, static_cast<uint32_t>(option.kind));
        hash::hashAppend(state, option.intValue0);
        hash::hashAppend(state, option.intValue1);
        hash::hashAppendString(state, option.stringValue0);
        hash::hashAppendString(state, option.stringValue1);
    });
    return state;
}

template <size_t SearchPathCount, size_t MacroCount, size_t CompilerOptionCount>
[[nodiscard]] consteval SlangCompileOptions<SearchPathCount, MacroCount, CompilerOptionCount> finalizeCompileOptions(
    SlangCompileOptions<SearchPathCount, MacroCount, CompilerOptionCount> options) noexcept
{
    options.hashValue = computeCompileOptionsHashValue(options);
    options.hashHex = hash::toHexChars(options.hashValue);
    return options;
}

[[nodiscard]] consteval std::array<SlangCompilerOption, 6> makeBaseCompilerOptions(
    int32_t optimizationLevel,
    int32_t debugInfoLevel,
    int32_t richDiagnosticsEnabled) noexcept
{
    return std::array<SlangCompilerOption, 6>{
        SlangCompilerOption{.name = slang::CompilerOptionName::EmitSpirvDirectly, .kind = slang::CompilerOptionValueKind::Int, .intValue0 = 1},
        SlangCompilerOption{.name = slang::CompilerOptionName::VulkanUseEntryPointName, .kind = slang::CompilerOptionValueKind::Int, .intValue0 = 1},
        SlangCompilerOption{.name = slang::CompilerOptionName::UseUpToDateBinaryModule, .kind = slang::CompilerOptionValueKind::Int, .intValue0 = 1},
        SlangCompilerOption{.name = slang::CompilerOptionName::Optimization, .kind = slang::CompilerOptionValueKind::Int, .intValue0 = optimizationLevel},
        SlangCompilerOption{.name = slang::CompilerOptionName::DebugInformation, .kind = slang::CompilerOptionValueKind::Int, .intValue0 = debugInfoLevel},
        SlangCompilerOption{.name = slang::CompilerOptionName::EnableRichDiagnostics, .kind = slang::CompilerOptionValueKind::Int, .intValue0 = richDiagnosticsEnabled},
    };
}

template <bool IsDebugModeValue>
[[nodiscard]] consteval auto defaultCompilerOptions() noexcept
    -> std::conditional_t<IsDebugModeValue, std::array<SlangCompilerOption, 7>, std::array<SlangCompilerOption, 6>>
{
    auto baseOptions = makeBaseCompilerOptions(
        IsDebugModeValue ? SLANG_OPTIMIZATION_LEVEL_NONE : SLANG_OPTIMIZATION_LEVEL_MAXIMAL,
        IsDebugModeValue ? SLANG_DEBUG_INFO_LEVEL_MAXIMAL : SLANG_DEBUG_INFO_LEVEL_NONE,
        IsDebugModeValue ? 1 : 0);

    using ResultType = std::conditional_t<IsDebugModeValue, std::array<SlangCompilerOption, 7>, std::array<SlangCompilerOption, 6>>;
    ResultType options{};
    std::ranges::copy(baseOptions, options.begin());

    if constexpr (IsDebugModeValue)
    {
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
    [[nodiscard]] static SlangSampler create(const vk::raii::Device &device, SlangSamplerDesc desc = {}, std::string_view debugName = {})
    {
        SlangSampler sampler;

        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = desc.magFilter;
        samplerInfo.minFilter = desc.minFilter;
        samplerInfo.mipmapMode = desc.mipmapMode;
        samplerInfo.addressModeU = desc.addressModeU;
        samplerInfo.addressModeV = desc.addressModeV;
        samplerInfo.addressModeW = desc.addressModeW;
        samplerInfo.mipLodBias = desc.mipLodBias;
        samplerInfo.anisotropyEnable = desc.anisotropyEnable ? vk::True : vk::False;
        samplerInfo.maxAnisotropy = desc.maxAnisotropy;
        samplerInfo.compareEnable = desc.compareEnable ? vk::True : vk::False;
        samplerInfo.compareOp = desc.compareOp;
        samplerInfo.minLod = desc.minLod;
        samplerInfo.maxLod = desc.maxLod;
        samplerInfo.borderColor = desc.borderColor;
        samplerInfo.unnormalizedCoordinates = desc.unnormalizedCoordinates ? vk::True : vk::False;

        sampler.sampler_ = vk::raii::Sampler(device, samplerInfo);
        sampler.debugName_ = std::string(debugName);
        return sampler;
    }

    /**
     * @brief Return whether this sampler owns a valid Vulkan handle.
     */
    [[nodiscard]] bool valid() const noexcept
    {
        return sampler_ != nullptr;
    }

    /**
     * @brief Get the underlying RAII sampler handle, or nullptr if invalid.
     */
    [[nodiscard]] const vk::raii::Sampler *handle() const noexcept
    {
        return valid() ? &sampler_ : nullptr;
    }

    /**
     * @brief Get the raw Vulkan sampler handle.
     */
    [[nodiscard]] vk::Sampler raw() const noexcept
    {
        return valid() ? *sampler_ : vk::Sampler{};
    }

  private:
    vk::raii::Sampler sampler_ = {nullptr};
    std::string debugName_;
};

/**
 * @brief Immutable sampler declaration for backend pipeline creation.
 */
struct SlangImmutableSamplerBinding
{
    uint32_t set = 0;
    uint32_t binding = 0;
    uint32_t descriptorCount = 1;
    SlangSamplerDesc samplerDesc{};
};

/**
 * @brief Compiled and reflected data for one linked entrypoint.
 */
struct SlangEntryPointData
{
    uint32_t linkedEntryPointIndex = 0;
    std::string entryPointName;
    SlangStage stage = SLANG_STAGE_NONE;
    Slang::ComPtr<slang::IBlob> codeBlob;

    [[nodiscard]] bool valid() const noexcept
    {
        return !entryPointName.empty() && stage != SLANG_STAGE_NONE && codeBlob != nullptr;
    }
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
    [[nodiscard]] bool valid() const noexcept
    {
        return module != nullptr;
    }
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
    [[nodiscard]] bool valid() const noexcept
    {
        return linkedProgram_ != nullptr && entryPointCount() > 0;
    }

    /**
     * @brief Number of linked entrypoints available in this program.
     */
    [[nodiscard]] size_t entryPointCount() const noexcept
    {
        if (!buildEntryPointCache())
        {
            return 0;
        }
        return entryPoints_.size();
    }

    /**
     * @brief Access all linked entrypoint payloads.
     */
    [[nodiscard]] std::span<const SlangEntryPointData> entryPoints() const noexcept
    {
        if (!buildEntryPointCache())
        {
            return {};
        }
        return entryPoints_;
    }

    /**
     * @brief Find entrypoint payload by name.
     */
    [[nodiscard]] const SlangEntryPointData *entryPointData(std::string_view entryPointName) const noexcept
    {
        if (!buildEntryPointCache())
        {
            return nullptr;
        }

        return findEntryPointDataCached(entryPointName);
    }

    /**
     * @brief Get linked entrypoint reflection by name.
     */
    [[nodiscard]] slang::EntryPointReflection *entryPointLayout(std::string_view entryPointName) const noexcept
    {
        if (!buildEntryPointCache())
        {
            return nullptr;
        }

        auto const *entryPoint = findEntryPointDataCached(entryPointName);
        auto *layout = programLayout();
        if (!entryPoint || !layout)
        {
            return nullptr;
        }

        return layout->getEntryPointByIndex(entryPoint->linkedEntryPointIndex);
    }

    /**
     * @brief Get linked entrypoint stage by name.
     */
    [[nodiscard]] std::optional<SlangStage> entryPointStage(std::string_view entryPointName) const noexcept
    {
        auto const *entryPoint = entryPointData(entryPointName);
        if (!entryPoint)
        {
            return std::nullopt;
        }
        return entryPoint->stage;
    }

    /**
     * @brief Get linked entrypoint code blob by name.
     */
    [[nodiscard]] slang::IBlob *entryPointBlob(std::string_view entryPointName) const noexcept
    {
        auto const *entryPoint = entryPointData(entryPointName);
        return entryPoint ? entryPoint->codeBlob.get() : nullptr;
    }

    /**
     * @brief Access the underlying linked Slang component type.
     */
    [[nodiscard]] slang::IComponentType *componentType() const noexcept
    {
        return linkedProgram_.get();
    }

    /**
     * @brief Access the linked Slang program layout reflection.
     */
    [[nodiscard]] slang::ProgramLayout *programLayout() const noexcept
    {
        if (hasQueriedProgramLayout_)
        {
            return cachedProgramLayout_;
        }

        hasQueriedProgramLayout_ = true;
        if (!linkedProgram_)
        {
            return nullptr;
        }

        Slang::ComPtr<slang::IBlob> diagnostics;
        cachedProgramLayout_ = linkedProgram_->getLayout(0, diagnostics.writeRef());
        return cachedProgramLayout_;
    }

  private:
    friend class ShaderService;

    [[nodiscard]] const SlangEntryPointData *findEntryPointDataCached(std::string_view entryPointName) const noexcept
    {
        auto it = entryPointIndexByName_.find(std::string(entryPointName));
        if (it == entryPointIndexByName_.end())
        {
            return nullptr;
        }

        auto index = it->second;
        if (index >= entryPoints_.size())
        {
            return nullptr;
        }
        return &entryPoints_[index];
    }

    [[nodiscard]] bool buildEntryPointCache() const noexcept
    {
        if (entryPointCacheBuilt_)
        {
            return true;
        }

        entryPoints_.clear();
        entryPointIndexByName_.clear();

        if (!linkedProgram_)
        {
            return false;
        }

        auto *layout = programLayout();
        if (!layout)
        {
            return false;
        }

        auto linkedEntryPointCount = std::max<SlangUInt>(0u, layout->getEntryPointCount());
        if (linkedEntryPointCount == 0)
        {
            return false;
        }

        entryPoints_.reserve(static_cast<size_t>(linkedEntryPointCount));

        for (SlangUInt entryIndex = 0; entryIndex < linkedEntryPointCount; ++entryIndex)
        {
            auto *entryLayout = layout->getEntryPointByIndex(entryIndex);
            if (!entryLayout)
            {
                return false;
            }

            auto entryName = std::string(entryLayout->getName() ? entryLayout->getName() : "");
            if (entryName.empty())
            {
                entryName = std::format("entrypoint_{}", entryIndex);
            }

            auto reflectedStage = entryLayout->getStage();
            if (reflectedStage == SLANG_STAGE_NONE)
            {
                return false;
            }

            auto *entryScopeVarLayout = entryLayout->getVarLayout();
            auto *entryScopeTypeLayout = entryScopeVarLayout ? entryScopeVarLayout->getTypeLayout() : nullptr;
            auto entryBindingRangeCount = entryScopeTypeLayout ? std::max<SlangInt>(0, entryScopeTypeLayout->getBindingRangeCount()) : 0;

            if (entryScopeTypeLayout && entryBindingRangeCount > 0)
            {
                std::ranges::for_each(std::views::iota(SlangInt{0}, entryBindingRangeCount), [&](SlangInt rangeIndex) {
                    auto bindingType = entryScopeTypeLayout->getBindingRangeType(rangeIndex);
                    auto isStageIo = bindingType == slang::BindingType::VaryingInput || bindingType == slang::BindingType::VaryingOutput;
                    nrAssert(
                        isStageIo,
                        std::format(
                            "Entry-point descriptor binding is forbidden. Keep bindable resources in global scope only. entry='{}', rangeIndex={}, bindingType={}",
                            entryName,
                            rangeIndex,
                            static_cast<int32_t>(bindingType)));
                });
            }

            Slang::ComPtr<slang::IBlob> codeBlob;
            Slang::ComPtr<slang::IBlob> diagnostics;
            auto compileResult = linkedProgram_->getEntryPointCode(static_cast<SlangInt>(entryIndex), 0, codeBlob.writeRef(), diagnostics.writeRef());
            if (!detail::slangSucceeded(compileResult) || !codeBlob)
            {
                return false;
            }

            SlangEntryPointData entryPointData{};
            entryPointData.linkedEntryPointIndex = static_cast<uint32_t>(entryIndex);
            entryPointData.entryPointName = std::move(entryName);
            entryPointData.stage = reflectedStage;
            entryPointData.codeBlob = std::move(codeBlob);

            auto [_, inserted] = entryPointIndexByName_.try_emplace(entryPointData.entryPointName, entryPoints_.size());
            if (!inserted)
            {
                return false;
            }

            entryPoints_.push_back(std::move(entryPointData));
        }

        entryPointCacheBuilt_ = true;
        return true;
    }

    Slang::ComPtr<slang::IComponentType> linkedProgram_;
    mutable bool hasQueriedProgramLayout_ = false;
    mutable slang::ProgramLayout *cachedProgramLayout_ = nullptr;
    mutable bool entryPointCacheBuilt_ = false;
    mutable std::vector<SlangEntryPointData> entryPoints_;
    mutable std::map<std::string, size_t> entryPointIndexByName_;
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
    int32_t intValue0 = 0;
    int32_t intValue1 = 0;
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
    [[nodiscard]] static ShaderService &instance()
    {
        static ShaderService service;
        return service;
    }

    /**
     * @brief Configure Slang session options and reset in-memory caches.
     */
    template <size_t SearchPathCount, size_t MacroCount, size_t CompilerOptionCount>
    void configure(const SlangCompileOptions<SearchPathCount, MacroCount, CompilerOptionCount> &options)
    {
        std::scoped_lock lock(m_mutex);
        applyCompileOptionsLocked(options);
    }

    /**
     * @brief Configure Slang session with project default compile options.
     */
    void configure()
    {
        std::scoped_lock lock(m_mutex);
        applyCompileOptionsLocked(kDefaultSlangCompileOptions);
    }

    [[nodiscard]] SlangProgram compileProgramByFile(const SlangProgramCompileFileRequest &request)
    {
        std::scoped_lock lock(m_mutex);
        ensureConfiguredLocked();

        SlangProgram result;

        auto modulePath = detail::normalizeRequestModulePath(request.sourcePath);
        if (!modulePath.has_value())
        {
            nrInfo<LogLevel::warning>(std::format("[ShaderService::compileProgramByFile] invalid request.sourcePath='{}'. expected relative module-path form like 'test/utils/useFlag'.", request.sourcePath.string()));
            return result;
        }

        auto moduleName = resolveModuleNameLocked(*modulePath);
        if (moduleName.empty())
        {
            nrInfo<LogLevel::warning>(std::format("[ShaderService::compileProgramByFile] unable to resolve module name from request.sourcePath='{}'.", request.sourcePath.string()));
            return result;
        }

        auto rootModule = loadOrCompileModuleLocked(moduleName, modulePath);
        if (!rootModule.valid())
        {
            return result;
        }

        auto definedEntryPointCount = std::max<SlangInt32>(0, rootModule.module->getDefinedEntryPointCount());
        if (definedEntryPointCount == 0)
        {
            nrInfo<LogLevel::warning>(std::format("[ShaderService::compileProgramByFile] module='{}' defines no entrypoints.", moduleName));
            return result;
        }

        std::vector<Slang::ComPtr<slang::IEntryPoint>> entryPointComponents;
        entryPointComponents.reserve(static_cast<size_t>(definedEntryPointCount));

        std::vector<slang::IComponentType *> components;
        components.reserve(static_cast<size_t>(definedEntryPointCount) + 1);
        components.push_back(rootModule.module.get());

        for (SlangInt32 index = 0; index < definedEntryPointCount; ++index)
        {
            Slang::ComPtr<slang::IEntryPoint> entryPointComponent;
            auto getEntryResult = rootModule.module->getDefinedEntryPoint(index, entryPointComponent.writeRef());
            if (!detail::slangSucceeded(getEntryResult) || !entryPointComponent)
            {
                nrInfo<LogLevel::warning>(std::format("[ShaderService::compileProgramByFile] getDefinedEntryPoint failed: module='{}', index={}", moduleName, index));
                return result;
            }

            components.push_back(entryPointComponent.get());
            entryPointComponents.push_back(std::move(entryPointComponent));
        }

        Slang::ComPtr<slang::IComponentType> compositeProgram;
        Slang::ComPtr<slang::IBlob> diagnostics;
        auto createResult = m_session->createCompositeComponentType(
            components.data(),
            static_cast<SlangInt>(components.size()),
            compositeProgram.writeRef(),
            diagnostics.writeRef());
        emitDiagnosticsLocked(diagnostics.get(), "createCompositeComponentType");
        if (!detail::slangSucceeded(createResult) || !compositeProgram)
        {
            nrInfo<LogLevel::warning>(std::format("[ShaderService::compileProgramByFile] createCompositeComponentType failed for module='{}'.", moduleName));
            return result;
        }

        Slang::ComPtr<slang::IComponentType> linkedProgram;
        diagnostics = nullptr;
        auto linkResult = compositeProgram->link(linkedProgram.writeRef(), diagnostics.writeRef());
        emitDiagnosticsLocked(diagnostics.get(), "link");
        if (!detail::slangSucceeded(linkResult) || !linkedProgram)
        {
            nrInfo<LogLevel::warning>(std::format("[ShaderService::compileProgramByFile] link failed for module='{}'.", moduleName));
            return result;
        }

        result.linkedProgram_ = linkedProgram;

        nrInfo<>(std::format("[ShaderService::compileProgramByFile] finished: module='{}'", moduleName));
        return result;
    }

  private:
    ShaderService() = default;

    static void writeModuleCacheBlobAsync(Slang::ComPtr<slang::IModule> module, const std::filesystem::path &moduleBlobPath)
    {
        auto pathText = moduleBlobPath.string();
        std::thread([module = std::move(module), pathText = std::move(pathText)]() mutable {
            auto writeResult = module->writeToFile(pathText.c_str());
            if (!detail::slangSucceeded(writeResult))
            {
                nrInfo<nr::LogLevel::warning>(std::format("[ShaderService::writeModuleCacheBlobAsync] writeToFile failed: path='{}', result={}", pathText, static_cast<int32_t>(writeResult)));
                return;
            }
        }).detach();
    }

    [[nodiscard]] std::optional<std::string> validateModulePathOrganizationLocked(std::string_view moduleName, std::string_view modulePath) const
    {
        if (moduleName.empty())
        {
            return std::nullopt;
        }

        auto expectedPath = detail::moduleNameToPath(moduleName);
        auto normalizedModulePath = detail::moduleNameToPath(detail::modulePathToName(std::string(modulePath)));
        if (normalizedModulePath == expectedPath)
        {
            return std::nullopt;
        }

        return std::format(
            "Module '{}' path '{}' violates shader organization rule. Expected exact module path '{}'.",
            moduleName,
            normalizedModulePath,
            expectedPath);
    }

    template <size_t SearchPathCount, size_t MacroCount, size_t CompilerOptionCount>
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

    template <size_t SearchPathCount, size_t MacroCount, size_t CompilerOptionCount>
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
    void ensureGlobalSessionLocked()
    {
        if (!m_globalSession)
        {
            auto result = slang::createGlobalSession(m_globalSession.writeRef());
            nrAssert(detail::slangSucceeded(result), "Failed to create Slang global session.");
        }
    }

    void ensureConfiguredLocked()
    {
        if (!m_session)
        {
            applyCompileOptionsLocked(kDefaultSlangCompileOptions);
        }
    }

    [[nodiscard]] uint64_t computeOptionsHashValueLocked() const noexcept
    {
        return m_optionsHashValue;
    }

    [[nodiscard]] std::string_view optionsHashLocked() const noexcept
    {
        return m_optionsHashHex;
    }

    [[nodiscard]] std::filesystem::path moduleCacheRootLocked() const
    {
        return std::filesystem::path(std::string(shaderCacheRoot)) / std::string(optionsHashLocked());
    }

    void recreateSessionLocked()
    {
        m_session = nullptr;

        m_effectiveSearchPaths.clear();
        m_effectiveSearchPaths.reserve(2);
        m_effectiveSearchPaths.push_back(moduleCacheRootLocked().string());
        m_effectiveSearchPaths.push_back(m_shaderRootPath.generic_string());

        m_searchPathPointers.clear();
        m_searchPathPointers.reserve(m_effectiveSearchPaths.size());
        for (auto const &path : m_effectiveSearchPaths)
        {
            m_searchPathPointers.push_back(path.c_str());
        }

        m_macroDescs.clear();
        m_macroDescs.reserve(m_options.macros.size());
        for (auto const &macro : m_options.macros)
        {
            m_macroDescs.push_back(slang::PreprocessorMacroDesc{
                .name = macro.name.c_str(),
                .value = macro.value.c_str(),
            });
        }

        m_compilerOptionEntries.clear();
        m_compilerOptionEntries.reserve(m_options.compilerOptions.size());
        std::ranges::for_each(m_options.compilerOptions, [&](const RuntimeSlangCompilerOption &option) {
            m_compilerOptionEntries.push_back(slang::CompilerOptionEntry{
                .name = option.name,
                .value = slang::CompilerOptionValue{
                    .kind = option.kind,
                    .intValue0 = option.intValue0,
                    .intValue1 = option.intValue1,
                    .stringValue0 = option.stringValue0.empty() ? nullptr : option.stringValue0.c_str(),
                    .stringValue1 = option.stringValue1.empty() ? nullptr : option.stringValue1.c_str(),
                },
            });
        });

        m_targetDesc = {};
        m_targetDesc.format = m_options.target;
        m_targetDesc.profile = m_globalSession->findProfile(m_options.profile.c_str());
        m_targetDesc.forceGLSLScalarBufferLayout = true;

        if (m_targetDesc.profile == SLANG_PROFILE_UNKNOWN)
        {
            m_targetDesc.profile = m_globalSession->findProfile("SPIRV_1_6");
        }

        slang::SessionDesc sessionDesc{};
        sessionDesc.targets = &m_targetDesc;
        sessionDesc.targetCount = 1;
        sessionDesc.searchPaths = m_searchPathPointers.empty() ? nullptr : m_searchPathPointers.data();
        sessionDesc.searchPathCount = static_cast<SlangInt>(m_searchPathPointers.size());
        sessionDesc.preprocessorMacros = m_macroDescs.empty() ? nullptr : m_macroDescs.data();
        sessionDesc.preprocessorMacroCount = static_cast<SlangInt>(m_macroDescs.size());
        sessionDesc.compilerOptionEntries = m_compilerOptionEntries.empty() ? nullptr : m_compilerOptionEntries.data();
        sessionDesc.compilerOptionEntryCount = static_cast<uint32_t>(m_compilerOptionEntries.size());
        sessionDesc.fileSystem = nullptr;

        auto result = m_globalSession->createSession(sessionDesc, m_session.writeRef());
        nrAssert(detail::slangSucceeded(result), "Failed to create Slang session.");
    }

    void emitDiagnosticsLocked(slang::IBlob *diagnostics, std::string_view context) const
    {
        if (!diagnostics)
        {
            return;
        }

        auto text = std::string_view(static_cast<const char *>(diagnostics->getBufferPointer()), diagnostics->getBufferSize());

        if (!text.empty())
        {
            nrInfo<nr::LogLevel::warning>(std::format("[Slang:{}]\n{}", context, text));
        }
    }

    /**
     * @brief Resolve canonical dotted module name from relative module-path input.
     *
     * Internal Input/Output examples:
    * - input:  test/utils/useFlag
    *   output: test.utils.useFlag
     * - input valid, but declared module token conflicts with derived leaf token
     *   output: "" (empty, hard fail)
     */
    [[nodiscard]] std::string resolveModuleNameLocked(std::string_view modulePath) const
    {
            auto derivedModuleName = detail::modulePathToName(modulePath);
            if (derivedModuleName.empty())
            {
                nrInfo<nr::LogLevel::warning>(std::format("[ShaderService::resolveModuleNameLocked] unable to derive module name from modulePath='{}'.", std::string(modulePath)));
                return {};
            }

            auto sourcePath = detail::normalizePath(m_shaderRootPath / (detail::moduleNameToPath(derivedModuleName) + ".slang"));
            auto sourceText = detail::readTextFile(sourcePath);
            if (auto declaredInSource = detail::extractDeclaredModuleNameFromSource(sourceText); declaredInSource.has_value())
            {
                auto declaredModule = *declaredInSource;
                auto expectedLeaf = detail::moduleLeafName(derivedModuleName);
                auto declaredMatches = false;

                // Slang source may use leaf declaration (`module useFlag;`) while runtime
                // identity is full path-derived module name (`test.utils.useFlag`).
                if (declaredModule.find('.') == std::string::npos)
                {
                    declaredMatches = (declaredModule == expectedLeaf);
                }
                else
                {
                    declaredMatches = (declaredModule == derivedModuleName);
                }

                if (!declaredMatches)
                {
                    auto message = std::format(
                        "[ShaderService::resolveModuleNameLocked] module declaration mismatch for modulePath='{}': declared='{}', expected='{}' (leaf='{}').",
                        std::string(modulePath),
                        declaredModule,
                        derivedModuleName,
                        expectedLeaf);
                    nrInfo<nr::LogLevel::warning>(message);
                    return {};
                }
            }
            return derivedModuleName;
    }

    SlangCompiledModule loadOrCompileModuleLocked(const std::string &moduleName, std::optional<std::string> explicitModulePath)
    {
        auto normalizedModuleName = moduleName;
        auto normalizedModulePath = detail::moduleNameToPath(normalizedModuleName);

        if (explicitModulePath.has_value())
        {
            if (auto violation = validateModulePathOrganizationLocked(normalizedModuleName, *explicitModulePath); violation.has_value())
            {
                nrInfo<LogLevel::warning>(std::string(violation.value()));
                return {};
            }
        }

        auto cacheRootPath = detail::normalizePath(moduleCacheRootLocked());
        auto moduleBlobPath = detail::makeModuleBinaryPath(cacheRootPath, normalizedModuleName);
        std::error_code moduleBlobEc;
        auto sourceModulePath = normalizedModulePath + ".slang";

        SlangCompiledModule result;
        result.moduleName = normalizedModuleName;
        result.sourcePath = std::filesystem::path(sourceModulePath);

        Slang::ComPtr<slang::IBlob> diagnostics;
        Slang::ComPtr<slang::IModule> loadedModule;
        diagnostics = nullptr;
        loadedModule = Slang::ComPtr<slang::IModule>(m_session->loadModule(normalizedModulePath.c_str(), diagnostics.writeRef()));
        emitDiagnosticsLocked(diagnostics.get(), "loadModule(module-path)");

        if (!loadedModule)
        {
            auto message = std::format("[ShaderService::loadOrCompileModuleLocked] failed to load module='{}' via loadModule(module-path='{}'). Expected slash form like 'test/utils/useFlag'.", normalizedModuleName, normalizedModulePath);
            nrInfo<LogLevel::warning>(message);
            return {};
        }

        std::error_code createDirEc;
        std::filesystem::create_directories(moduleBlobPath.parent_path(), createDirEc);

            writeModuleCacheBlobAsync(loadedModule, moduleBlobPath);

        result.module = loadedModule;
        return result;
    }


  public:
    // Locking policy: all access to Slang session/global-session/module/component APIs
    // is serialized by m_mutex because those interfaces are treated as non-thread-safe
    // in this renderer integration. Methods with `Locked` suffix require the caller to
    // hold m_mutex.
    mutable std::mutex m_mutex;
    Slang::ComPtr<slang::IGlobalSession> m_globalSession;
    Slang::ComPtr<slang::ISession> m_session;

    RuntimeSlangCompileOptions m_options;
    uint64_t m_optionsHashValue = kDefaultSlangCompileOptions.hashValue;
    std::string m_optionsHashHex = std::string(hash::toHexView(kDefaultSlangCompileOptions.hashHex));
    std::filesystem::path m_shaderRootPath = detail::resolveShaderRootPath();

    std::vector<std::string> m_effectiveSearchPaths;
    std::vector<const char *> m_searchPathPointers;
    std::vector<slang::PreprocessorMacroDesc> m_macroDescs;
    std::vector<slang::CompilerOptionEntry> m_compilerOptionEntries;
    slang::TargetDesc m_targetDesc{};

};

} // namespace nr::rhi
