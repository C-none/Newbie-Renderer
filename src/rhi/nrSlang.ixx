module;

#include "slang.h"

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
constexpr SlangResult kSlangFail = makeSlangError(0, 0x4005);
constexpr SlangResult kSlangNoInterface = makeSlangError(0, 0x4002);
constexpr SlangResult kSlangInvalidArg = makeSlangError(7, 0x57);
constexpr SlangResult kSlangNotFound = makeSlangError(0x200, 5);

constexpr SlangInt kSlangUnboundedSize = static_cast<SlangInt>(~size_t(0));
constexpr SlangInt kSlangUnknownSize = static_cast<SlangInt>(~size_t(0) - size_t(1));

void appendIntCompilerOption(std::vector<slang::CompilerOptionEntry> &options, slang::CompilerOptionName name, int32_t value)
{
    options.push_back(slang::CompilerOptionEntry{
        .name = name,
        .value =
            slang::CompilerOptionValue{
                .kind = slang::CompilerOptionValueKind::Int,
                .intValue0 = value,
            },
    });
}

void appendStringCompilerOption(std::vector<slang::CompilerOptionEntry> &options, slang::CompilerOptionName name, const char *value)
{
    options.push_back(slang::CompilerOptionEntry{
        .name = name,
        .value =
            slang::CompilerOptionValue{
                .kind = slang::CompilerOptionValueKind::String,
                .stringValue0 = value,
            },
    });
}

[[nodiscard]] std::vector<slang::CompilerOptionEntry> defaultCompilerOptions()
{
    std::vector<slang::CompilerOptionEntry> options;
    options.reserve(6);

    appendIntCompilerOption(options, slang::CompilerOptionName::EmitSpirvDirectly, 1);
    appendIntCompilerOption(options, slang::CompilerOptionName::UseUpToDateBinaryModule, 1);
    appendIntCompilerOption(options, slang::CompilerOptionName::Optimization, isDebugMode ? SLANG_OPTIMIZATION_LEVEL_NONE : SLANG_OPTIMIZATION_LEVEL_MAXIMAL);
    appendIntCompilerOption(options, slang::CompilerOptionName::DebugInformation, isDebugMode ? SLANG_DEBUG_INFO_LEVEL_MAXIMAL : SLANG_DEBUG_INFO_LEVEL_NONE);
    appendIntCompilerOption(options, slang::CompilerOptionName::EnableRichDiagnostics, isDebugMode ? 1 : 0);

    if constexpr (isDebugMode)
    {
        appendStringCompilerOption(options, slang::CompilerOptionName::WarningsAsErrors, "all");
    }

    return options;
}

[[nodiscard]] bool cStringEqual(const char *lhs, const char *rhs)
{
    if (lhs == rhs)
    {
        return true;
    }
    if (!lhs || !rhs)
    {
        return false;
    }
    return std::string_view(lhs) == std::string_view(rhs);
}

[[nodiscard]] bool compilerOptionEqual(const slang::CompilerOptionEntry &lhs, const slang::CompilerOptionEntry &rhs)
{
    return lhs.name == rhs.name && lhs.value.kind == rhs.value.kind && lhs.value.intValue0 == rhs.value.intValue0 && lhs.value.intValue1 == rhs.value.intValue1 && cStringEqual(lhs.value.stringValue0, rhs.value.stringValue0) && cStringEqual(lhs.value.stringValue1, rhs.value.stringValue1);
}

[[nodiscard]] std::optional<vk::DescriptorType> mapBindingTypeToDescriptorType(slang::BindingType bindingType)
{
    switch (bindingType)
    {
    case slang::BindingType::Sampler:
        return vk::DescriptorType::eSampler;
    case slang::BindingType::CombinedTextureSampler:
        return vk::DescriptorType::eCombinedImageSampler;
    case slang::BindingType::Texture:
        return vk::DescriptorType::eSampledImage;
    case slang::BindingType::MutableTexture:
        return vk::DescriptorType::eStorageImage;
    case slang::BindingType::TypedBuffer:
        return vk::DescriptorType::eUniformTexelBuffer;
    case slang::BindingType::MutableTypedBuffer:
        return vk::DescriptorType::eStorageTexelBuffer;
    case slang::BindingType::RawBuffer:
    case slang::BindingType::MutableRawBuffer:
        return vk::DescriptorType::eStorageBuffer;
    case slang::BindingType::InputRenderTarget:
        return vk::DescriptorType::eInputAttachment;
    case slang::BindingType::InlineUniformData:
        return vk::DescriptorType::eInlineUniformBlockEXT;
    case slang::BindingType::RayTracingAccelerationStructure:
        return vk::DescriptorType::eAccelerationStructureKHR;
    case slang::BindingType::ConstantBuffer:
        return vk::DescriptorType::eUniformBuffer;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] bool sameGuid(const SlangUUID &a, const SlangUUID &b)
{
    return std::memcmp(&a, &b, sizeof(SlangUUID)) == 0;
}

class OwnedBlob final : public slang::IBlob
{
  public:
    explicit OwnedBlob(std::vector<std::byte> bytes)
        : m_bytes(std::move(bytes))
    {
    }

    SLANG_NO_THROW SlangResult SLANG_MCALL queryInterface(SlangUUID const &uuid, void **outObject) override
    {
        if (!outObject)
        {
            return kSlangInvalidArg;
        }

        *outObject = nullptr;
        if (sameGuid(uuid, slang::IBlob::getTypeGuid()) || sameGuid(uuid, ISlangUnknown::getTypeGuid()))
        {
            *outObject = static_cast<slang::IBlob *>(this);
            addRef();
            return kSlangOk;
        }

        return kSlangNoInterface;
    }

    SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override
    {
        return ++m_refCount;
    }

    SLANG_NO_THROW uint32_t SLANG_MCALL release() override
    {
        auto const count = --m_refCount;
        if (count == 0)
        {
            delete this;
        }
        return count;
    }

    SLANG_NO_THROW void const *SLANG_MCALL getBufferPointer() override
    {
        return m_bytes.empty() ? nullptr : m_bytes.data();
    }

    SLANG_NO_THROW size_t SLANG_MCALL getBufferSize() override
    {
        return m_bytes.size();
    }

  private:
    std::atomic<uint32_t> m_refCount = 1;
    std::vector<std::byte> m_bytes;
};

[[nodiscard]] std::vector<std::byte> readBinaryFile(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        return {};
    }

    auto const size = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    std::vector<std::byte> bytes(size);
    if (size > 0)
    {
        file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(size));
    }
    return bytes;
}

[[nodiscard]] bool isBinaryModuleBlobUpToDate(slang::ISession *session, std::string_view sourceModulePath, const std::filesystem::path &moduleBlobPath)
{
    if (!session)
    {
        return false;
    }

    auto binaryBytes = readBinaryFile(moduleBlobPath);
    if (binaryBytes.empty())
    {
        return false;
    }

    auto *binaryBlob = new OwnedBlob(std::move(binaryBytes));
    auto sourcePath = std::string(sourceModulePath);
    auto upToDate = session->isBinaryModuleUpToDate(sourcePath.c_str(), binaryBlob);
    binaryBlob->release();
    return upToDate;
}

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

    auto normalizedModulePath = moduleNameToPath(modulePathToName(normalizedPath.generic_string()));
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

[[nodiscard]] uint64_t fnv1a64(std::span<const std::byte> bytes)
{
    constexpr uint64_t offsetBasis = 14695981039346656037ull;
    constexpr uint64_t prime = 1099511628211ull;
    uint64_t hash = offsetBasis;
    std::ranges::for_each(bytes, [&](auto b) {
        hash ^= static_cast<uint64_t>(std::to_integer<uint8_t>(b));
        hash *= prime;
    });
    return hash;
}

template <typename T> void hashAppend(uint64_t &state, const T &value)
{
    auto bytes = std::as_bytes(std::span{&value, 1});
    auto mixed = fnv1a64(bytes);
    state ^= mixed + 0x9e3779b97f4a7c15ull + (state << 6) + (state >> 2);
}

void hashAppendString(uint64_t &state, std::string_view value)
{
    auto bytes = std::as_bytes(std::span{value.data(), value.size()});
    auto mixed = fnv1a64(bytes);
    state ^= mixed + 0x9e3779b97f4a7c15ull + (state << 6) + (state >> 2);
}

[[nodiscard]] std::string toHex(uint64_t value)
{
    return std::format("{:016x}", value);
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

struct VulkanBindingMappingPolicy
{
    [[nodiscard]] uint32_t mapSpaceToSet(slang::TypeLayoutReflection *typeLayout, SlangInt descriptorSetIndex) const{
        auto setSpace = typeLayout->getDescriptorSetSpaceOffset(descriptorSetIndex);
        nrAssert(setSpace != kSlangUnknownSize && setSpace >= 0, "Invalid Slang descriptor set space offset for Vulkan mapping.");
        return static_cast<uint32_t>(setSpace);
    }

    [[nodiscard]] uint32_t mapRangeIndexToBinding(slang::TypeLayoutReflection *typeLayout, SlangInt descriptorSetIndex, SlangInt descriptorRangeIndex) const{
        auto bindingIndex = typeLayout->getDescriptorSetDescriptorRangeIndexOffset(descriptorSetIndex, descriptorRangeIndex);
        nrAssert(bindingIndex != kSlangUnknownSize && bindingIndex >= 0, "Invalid Slang descriptor range binding offset for Vulkan mapping.");
        return static_cast<uint32_t>(bindingIndex);
    }

    [[nodiscard]] uint32_t normalizeDescriptorCount(SlangInt descriptorCount) const{
        if (descriptorCount == kSlangUnknownSize || descriptorCount == kSlangUnboundedSize || descriptorCount < 0)
        {
            return 1u;
        }
        return std::max(static_cast<uint32_t>(descriptorCount), 1u);
    }
};

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
    //   module test.utils.utils -> shader/test/utils/utils.slang
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
    std::string name;
    std::string value;
};

/**
 * @brief Global compile configuration shared by all shader programs.
 *
 * This project uses one unified Slang session configuration for all modules.
 */
struct SlangCompileOptions
{
    // Runtime session search paths are fixed to:
    //   1) <shaderCacheRoot>/<optionsHash>
    //   2) <shaderRoot> (provided by CMake)
    std::vector<std::string> searchPaths;
    std::vector<SlangMacro> macros;
    SlangCompileTarget target = SLANG_SPIRV;
    std::string profile = "SPIRV_1_6";
    std::vector<slang::CompilerOptionEntry> compilerOptions;
};

/**
 * @brief Reflected descriptor binding information used by Vulkan layout creation.
 */
struct SlangDescriptorBinding
{
    uint32_t binding = 0;
    vk::DescriptorType descriptorType = vk::DescriptorType::eSampler;
    uint32_t descriptorCount = 1;
    vk::ShaderStageFlags stageFlags = vk::ShaderStageFlagBits::eAll;
};

/**
 * @brief One descriptor set and all reflected bindings in that set.
 */
struct SlangDescriptorSetLayoutInfo
{
    uint32_t set = 0;
    std::vector<SlangDescriptorBinding> bindings;
};

/**
 * @brief Reflected push-constant range.
 */
struct SlangPushConstantRange
{
    uint32_t offset = 0;
    uint32_t size = 0;
    vk::ShaderStageFlags stageFlags = vk::ShaderStageFlagBits::eAll;
};

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
 * @brief Immutable sampler declaration attached to a reflected descriptor binding.
 */
struct SlangImmutableSamplerBinding
{
    uint32_t set = 0;
    uint32_t binding = 0;
    uint32_t descriptorCount = 1;
    SlangSamplerDesc samplerDesc{};
};

/**
 * @brief Reflection payload consumed by `ReflectedPipelineLayout`.
 */
struct SlangReflectionLayout
{
    struct CursorBinding
    {
        std::string path;
        uint32_t set = 0;
        uint32_t binding = 0;
        vk::DescriptorType descriptorType = vk::DescriptorType::eSampler;
        uint32_t descriptorCount = 1;
        vk::ShaderStageFlags stageFlags = vk::ShaderStageFlagBits::eAll;
    };

    std::vector<SlangDescriptorSetLayoutInfo> descriptorSets;
    std::vector<SlangPushConstantRange> pushConstantRanges;
    std::vector<SlangImmutableSamplerBinding> immutableSamplers;
    std::vector<CursorBinding> cursorBindings;
};

/**
 * @brief One compiled entry point payload (SPIR-V + stage + name).
 */
struct SlangEntryPointBinary
{
    std::string entryPointName;
    SlangStage stage = SLANG_STAGE_NONE;
    std::vector<uint32_t> spirv;
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
        return linkedProgram_ != nullptr && !entryPoints_.empty();
    }

    /**
     * @brief Enumerate compiled entry point binaries in link order.
     */
    [[nodiscard]] std::span<const SlangEntryPointBinary> entryPoints() const noexcept
    {
        return entryPoints_;
    }

    /**
     * @brief Get descriptor and push-constant reflection for this program.
     */
    [[nodiscard]] const SlangReflectionLayout &reflection() const noexcept
    {
        return reflection_;
    }

    /**
     * @brief Access the underlying linked Slang component type.
     */
    [[nodiscard]] slang::IComponentType *componentType() const noexcept
    {
        return linkedProgram_.get();
    }

  private:
    friend class ShaderService;

    Slang::ComPtr<slang::IComponentType> linkedProgram_;
    std::vector<SlangEntryPointBinary> entryPoints_;
    SlangReflectionLayout reflection_;
};

struct SlangFileEntryCompileRequest
{
    // Required input form:
    // - test/utils/utils
    std::filesystem::path sourcePath;
    std::string entryPoint;
};

struct SlangFileEntryCompileResult
{
    bool succeeded = false;
    std::string moduleName;
    std::string entryPoint;
    SlangStage stage = SLANG_STAGE_NONE;
    std::vector<uint32_t> spirvCode;
    std::vector<SlangReflectionLayout::CursorBinding> cursorBindings;
};

/**
 * @brief Process-wide Slang frontend service used by NR RHI.
 *
 * Responsibilities:
 * - Session configuration
 * - Module compile/load with cache
 * - Program link and entry point code generation
 * - Reflection extraction and hot-reload checks
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
    void configure(const SlangCompileOptions &options)
    {
        std::scoped_lock lock(m_mutex);

        SlangCompileOptions normalizedOptions = options;
        if (normalizedOptions.compilerOptions.empty())
        {
            normalizedOptions.compilerOptions = detail::defaultCompilerOptions();
        }

        auto resolvedShaderRootPath = detail::resolveShaderRootPath();
        normalizedOptions.searchPaths = {resolvedShaderRootPath.generic_string()};
        if (m_session && compareCompileOptionsLocked(normalizedOptions, m_options))
        {
            return;
        }

        m_options = std::move(normalizedOptions);
        m_shaderRootPath = std::move(resolvedShaderRootPath);

        ensureGlobalSessionLocked();
        recreateSessionLocked();

        nrInfo<>(std::format("[ShaderService::configure] configured: shaderRoot='{}', profile='{}'", m_shaderRootPath.generic_string(), m_options.profile));
    }

    [[nodiscard]] SlangFileEntryCompileResult compileByFileAndEntry(const SlangFileEntryCompileRequest &request)
    {
        std::scoped_lock lock(m_mutex);
        ensureConfiguredLocked();

        SlangFileEntryCompileResult result;

        if (request.entryPoint.empty())
        {
            nrInfo<LogLevel::warning>("[ShaderService::compileByFileAndEntry] entryPoint is empty.");
            return result;
        }

        auto modulePath = detail::normalizeRequestModulePath(request.sourcePath);
        if (!modulePath.has_value())
        {
            nrInfo<LogLevel::warning>(std::format("[ShaderService::compileByFileAndEntry] invalid request.sourcePath='{}'. expected relative module-path form like 'test/utils/utils'.", request.sourcePath.string()));
            return result;
        }

        auto moduleName = resolveModuleNameLocked(*modulePath);
        if (moduleName.empty())
        {
            nrInfo<LogLevel::warning>(std::format("[ShaderService::compileByFileAndEntry] unable to resolve module name from request.sourcePath='{}'.", request.sourcePath.string()));
            return result;
        }

        result.moduleName = moduleName;
        result.entryPoint = request.entryPoint;
        result.stage = SLANG_STAGE_NONE;

        auto rootModule = loadOrCompileModuleLocked(moduleName, modulePath);
        if (!rootModule.valid())
        {
            return result;
        }
        Slang::ComPtr<slang::IEntryPoint> entryPointComponent;
        auto findEntryResult = rootModule.module->findEntryPointByName(request.entryPoint.c_str(), entryPointComponent.writeRef());
        if (!detail::slangSucceeded(findEntryResult) || !entryPointComponent)
        {
            nrInfo<LogLevel::warning>(std::format("[ShaderService::compileByFileAndEntry] failed to resolve entryPoint='{}' in module='{}' via findEntryPointByName.", request.entryPoint, moduleName));
            return result;
        }
        std::vector<slang::IComponentType *> components = {
            rootModule.module.get(),
            entryPointComponent.get(),
        };

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
            nrInfo<LogLevel::warning>(std::format("[ShaderService::compileByFileAndEntry] createCompositeComponentType failed for module='{}', entry='{}'.", moduleName, request.entryPoint));
            return result;
        }

        Slang::ComPtr<slang::IComponentType> linkedProgram;
        diagnostics = nullptr;
        auto linkResult = compositeProgram->link(linkedProgram.writeRef(), diagnostics.writeRef());
        emitDiagnosticsLocked(diagnostics.get(), "link");
        if (!detail::slangSucceeded(linkResult) || !linkedProgram)
        {
            nrInfo<LogLevel::warning>(std::format("[ShaderService::compileByFileAndEntry] link failed for module='{}', entry='{}'.", moduleName, request.entryPoint));
            return result;
        }

        diagnostics = nullptr;
        auto layout = linkedProgram->getLayout(0, diagnostics.writeRef());
        emitDiagnosticsLocked(diagnostics.get(), "getLayout");
        if (!layout)
        {
            nrInfo<LogLevel::warning>(std::format("[ShaderService::compileByFileAndEntry] getLayout failed for module='{}', entry='{}'.", moduleName, request.entryPoint));
            return result;
        }

        Slang::ComPtr<slang::IBlob> codeBlob;
        diagnostics = nullptr;
        auto compileResult = linkedProgram->getEntryPointCode(0, 0, codeBlob.writeRef(), diagnostics.writeRef());
        emitDiagnosticsLocked(diagnostics.get(), "getEntryPointCode");
        if (!detail::slangSucceeded(compileResult) || !codeBlob)
        {
            nrInfo<LogLevel::warning>(std::format("[ShaderService::compileByFileAndEntry] getEntryPointCode failed for module='{}', entry='{}'.", moduleName, request.entryPoint));
            return result;
        }

        auto codeBytes = std::span{
            static_cast<const std::byte *>(codeBlob->getBufferPointer()),
            codeBlob->getBufferSize()};
        auto dwordCount = (codeBytes.size() + sizeof(uint32_t) - 1) / sizeof(uint32_t);
        result.spirvCode.resize(dwordCount, 0u);
        if (!codeBytes.empty())
        {
            std::memcpy(result.spirvCode.data(), codeBytes.data(), codeBytes.size());
        }

        SlangReflectionLayout reflection;
        buildReflectionLayoutLocked(layout, reflection);
        result.cursorBindings = std::move(reflection.cursorBindings);

        auto *entryPointLayout = layout->getEntryPointByIndex(0);
        if (!entryPointLayout || entryPointLayout->getStage() == SLANG_STAGE_NONE)
        {
            nrInfo<LogLevel::warning>(std::format("[ShaderService::compileByFileAndEntry] entryPoint='{}' in module='{}' is missing [shader(...)] stage attribute.", request.entryPoint, moduleName));
            return result;
        }
        result.stage = entryPointLayout->getStage();

        result.succeeded = !result.spirvCode.empty();
        nrInfo<>(std::format("[ShaderService::compileByFileAndEntry] finished: module='{}', entry='{}', stage={}, words={}", moduleName, request.entryPoint, static_cast<int32_t>(result.stage), result.spirvCode.size()));
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

    [[nodiscard]] static std::string resolveEntryPointName(slang::EntryPointLayout *entryPointLayout, std::string_view fallbackName)
    {
        if (entryPointLayout && entryPointLayout->getNameOverride())
        {
            return entryPointLayout->getNameOverride();
        }
        if (entryPointLayout && entryPointLayout->getName())
        {
            return entryPointLayout->getName();
        }
        return std::string(fallbackName);
    }

    [[nodiscard]] static SlangStage resolveEntryPointStage(slang::EntryPointLayout *entryPointLayout)
    {
        return entryPointLayout ? entryPointLayout->getStage() : SLANG_STAGE_NONE;
    }

    [[nodiscard]] static uint32_t normalizeDescriptorCount(uint32_t descriptorCount)
    {
        return std::max(descriptorCount, 1u);
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

    template <typename Fn> void forEachDescriptorRange(slang::TypeLayoutReflection *typeLayout, Fn &&callback)
    {
        if (!typeLayout)
        {
            return;
        }

        detail::VulkanBindingMappingPolicy mappingPolicy;
        auto descriptorSetCount = typeLayout->getDescriptorSetCount();
        for (SlangInt setIndex = 0; setIndex < descriptorSetCount; ++setIndex)
        {
            auto set = mappingPolicy.mapSpaceToSet(typeLayout, setIndex);
            auto rangeCount = typeLayout->getDescriptorSetDescriptorRangeCount(setIndex);
            for (SlangInt rangeIndex = 0; rangeIndex < rangeCount; ++rangeIndex)
            {
                auto descriptorType = detail::mapBindingTypeToDescriptorType(typeLayout->getDescriptorSetDescriptorRangeType(setIndex, rangeIndex));
                if (!descriptorType.has_value())
                {
                    continue;
                }

                auto binding = mappingPolicy.mapRangeIndexToBinding(typeLayout, setIndex, rangeIndex);
                auto descriptorCount = mappingPolicy.normalizeDescriptorCount(typeLayout->getDescriptorSetDescriptorRangeDescriptorCount(setIndex, rangeIndex));
                callback(set, binding, *descriptorType, descriptorCount);
            }
        }
    }

    [[nodiscard]] bool compareCompileOptionsLocked(const SlangCompileOptions &lhs, const SlangCompileOptions &rhs) const
    {
        if (lhs.target != rhs.target || lhs.profile != rhs.profile || lhs.searchPaths != rhs.searchPaths)
        {
            return false;
        }

        if (lhs.macros.size() != rhs.macros.size() || lhs.compilerOptions.size() != rhs.compilerOptions.size())
        {
            return false;
        }

        for (size_t i = 0; i < lhs.macros.size(); ++i)
        {
            if (lhs.macros[i].name != rhs.macros[i].name || lhs.macros[i].value != rhs.macros[i].value)
            {
                return false;
            }
        }

        for (size_t i = 0; i < lhs.compilerOptions.size(); ++i)
        {
            if (!detail::compilerOptionEqual(lhs.compilerOptions[i], rhs.compilerOptions[i]))
            {
                return false;
            }
        }

        return true;
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
            if (m_options.compilerOptions.empty())
            {
                m_options.compilerOptions = detail::defaultCompilerOptions();
            }
            m_shaderRootPath = detail::resolveShaderRootPath();
            m_options.searchPaths = {m_shaderRootPath.generic_string()};
            ensureGlobalSessionLocked();
            recreateSessionLocked();
        }
    }

    [[nodiscard]] uint64_t computeOptionsHashValueLocked() const
    {
        uint64_t hash = 0xcbf29ce484222325ull;
        detail::hashAppend(hash, static_cast<uint32_t>(m_options.target));
        detail::hashAppendString(hash, m_options.profile);
        for (auto const &path : m_options.searchPaths)
        {
            detail::hashAppendString(hash, path);
        }
        for (auto const &macro : m_options.macros)
        {
            detail::hashAppendString(hash, macro.name);
            detail::hashAppendString(hash, macro.value);
        }
        for (auto const &option : m_options.compilerOptions)
        {
            detail::hashAppend(hash, static_cast<uint32_t>(option.name));
            detail::hashAppend(hash, static_cast<uint32_t>(option.value.kind));
            detail::hashAppend(hash, static_cast<int32_t>(option.value.intValue0));
            detail::hashAppend(hash, static_cast<int32_t>(option.value.intValue1));
            if (option.value.stringValue0)
            {
                detail::hashAppendString(hash, option.value.stringValue0);
            }
            if (option.value.stringValue1)
            {
                detail::hashAppendString(hash, option.value.stringValue1);
            }
        }
        return hash;
    }

    [[nodiscard]] std::string optionsHashLocked() const
    {
        return detail::toHex(computeOptionsHashValueLocked());
    }

    [[nodiscard]] std::filesystem::path moduleCacheRootLocked() const
    {
        return std::filesystem::path(std::string(shaderCacheRoot)) / optionsHashLocked();
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
        sessionDesc.compilerOptionEntries = m_options.compilerOptions.empty() ? nullptr : m_options.compilerOptions.data();
        sessionDesc.compilerOptionEntryCount = static_cast<uint32_t>(m_options.compilerOptions.size());
        sessionDesc.fileSystem = nullptr;

        auto result = m_globalSession->createSession(sessionDesc, m_session.writeRef());
        nrAssert(detail::slangSucceeded(result), "Failed to create Slang session.");
        nrInfo<>(std::format("[ShaderService::recreateSessionLocked] session created: profile='{}'", m_options.profile));
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
     * - input:  test/utils/utils
     *   output: test.utils.utils
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

                // Slang source may use leaf declaration (`module utils;`) while runtime
                // identity is full path-derived module name (`test.utils.utils`).
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
        auto moduleBlobExisted = std::filesystem::exists(moduleBlobPath, moduleBlobEc);
        auto sourceModulePath = normalizedModulePath + ".slang";
        auto moduleBlobUpToDate = moduleBlobExisted
            ? detail::isBinaryModuleBlobUpToDate(m_session.get(), sourceModulePath, moduleBlobPath)
            : false;

        SlangCompiledModule result;
        result.moduleName = normalizedModuleName;
        result.sourcePath = std::filesystem::path(sourceModulePath);

        nrInfo<>(std::format("[ShaderService::loadOrCompileModuleLocked] loading module='{}'", normalizedModuleName));

        Slang::ComPtr<slang::IBlob> diagnostics;
        Slang::ComPtr<slang::IModule> loadedModule;
        diagnostics = nullptr;
        loadedModule = Slang::ComPtr<slang::IModule>(m_session->loadModule(normalizedModulePath.c_str(), diagnostics.writeRef()));
        emitDiagnosticsLocked(diagnostics.get(), "loadModule(module-path)");

        if (!loadedModule)
        {
            auto message = std::format("[ShaderService::loadOrCompileModuleLocked] failed to load module='{}' via loadModule(module-path='{}'). Expected slash form like 'test/utils/utils'.", normalizedModuleName, normalizedModulePath);
            nrInfo<LogLevel::warning>(message);
            return {};
        }

        std::error_code createDirEc;
        std::filesystem::create_directories(moduleBlobPath.parent_path(), createDirEc);
        if ( !moduleBlobExisted || !moduleBlobUpToDate)
        {
            writeModuleCacheBlobAsync(loadedModule, moduleBlobPath);
        }

        result.module = loadedModule;
        return result;
    }


    void addDescriptorBinding(SlangReflectionLayout &reflection, uint32_t set, uint32_t binding, vk::DescriptorType descriptorType, uint32_t descriptorCount, vk::ShaderStageFlags stageFlags)
    {
        auto normalizedCount = normalizeDescriptorCount(descriptorCount);

        auto setIt = std::ranges::find_if(reflection.descriptorSets, [set](const SlangDescriptorSetLayoutInfo &info) { return info.set == set; });
        if (setIt == reflection.descriptorSets.end())
        {
            reflection.descriptorSets.push_back(SlangDescriptorSetLayoutInfo{.set = set});
            setIt = std::prev(reflection.descriptorSets.end());
        }

        auto bindingIt = std::ranges::find_if(setIt->bindings, [binding](const SlangDescriptorBinding &info) { return info.binding == binding; });
        if (bindingIt == setIt->bindings.end())
        {
            setIt->bindings.push_back(SlangDescriptorBinding{
                .binding = binding,
                .descriptorType = descriptorType,
                .descriptorCount = normalizedCount,
                .stageFlags = stageFlags,
            });
            return;
        }

        bindingIt->stageFlags |= stageFlags;
        bindingIt->descriptorCount = std::max(bindingIt->descriptorCount, normalizedCount);
    }

    void addPushConstantRange(SlangReflectionLayout &reflection, uint32_t offset, uint32_t size, vk::ShaderStageFlags stageFlags)
    {
        if (size == 0)
        {
            return;
        }

        auto it = std::ranges::find_if(reflection.pushConstantRanges, [offset, size](const SlangPushConstantRange &range) { return range.offset == offset && range.size == size; });
        if (it == reflection.pushConstantRanges.end())
        {
            reflection.pushConstantRanges.push_back(SlangPushConstantRange{
                .offset = offset,
                .size = size,
                .stageFlags = stageFlags,
            });
            return;
        }

        it->stageFlags |= stageFlags;
    }

    void addCursorBinding(SlangReflectionLayout &reflection, std::string_view path, uint32_t set, uint32_t binding, vk::DescriptorType descriptorType, uint32_t descriptorCount, vk::ShaderStageFlags stageFlags)
    {
        if (path.empty())
        {
            return;
        }

        auto normalizedCount = normalizeDescriptorCount(descriptorCount);

        auto it = std::ranges::find_if(reflection.cursorBindings, [path, set, binding](const SlangReflectionLayout::CursorBinding &item) { return item.path == path && item.set == set && item.binding == binding; });
        if (it == reflection.cursorBindings.end())
        {
            reflection.cursorBindings.push_back(SlangReflectionLayout::CursorBinding{
                .path = std::string(path),
                .set = set,
                .binding = binding,
                .descriptorType = descriptorType,
                .descriptorCount = normalizedCount,
                .stageFlags = stageFlags,
            });
            return;
        }

        it->descriptorCount = std::max(it->descriptorCount, normalizedCount);
        it->stageFlags |= stageFlags;
    }

    void collectCursorBindingsFromTypeLayout(slang::TypeLayoutReflection *typeLayout, std::string_view path, vk::ShaderStageFlags stageFlags, SlangReflectionLayout &outReflection)
    {
        if (!typeLayout)
        {
            return;
        }

        // Cursor path contract (Vulkan/SPIR-V profile):
        // - Struct field navigation is represented as dot path: `a.b.c`.
        // - Array navigation is represented as `[]` placeholder segments: `a[].b`.
        // - This mirrors Slang ShaderCursor traversal semantics:
        //      cursor.field("a").element(i).field("b")
        //   where host code may provide element index at bind time while reflection keeps
        //   one canonical path (`[]`) per descriptor leaf.
        // - ParameterBlock/ConstantBuffer wrappers are transparent at path level.
        //
        // Using canonical placeholder paths keeps reflection stable across shader variants
        // and avoids hardcoding backend binding numbers in host code.

        forEachDescriptorRange(typeLayout, [&](uint32_t set, uint32_t binding, vk::DescriptorType descriptorType, uint32_t descriptorCount) {
            addCursorBinding(outReflection, path, set, binding, descriptorType, descriptorCount, stageFlags);
        });

        switch (typeLayout->getKind())
        {
        case slang::TypeReflection::Kind::Struct: {
            auto fieldCount = typeLayout->getFieldCount();
            for (unsigned int fieldIndex = 0; fieldIndex < fieldCount; ++fieldIndex)
            {
                auto *fieldLayout = typeLayout->getFieldByIndex(fieldIndex);
                if (!fieldLayout || !fieldLayout->getTypeLayout())
                {
                    continue;
                }

                auto fieldName = fieldLayout->getName() ? std::string(fieldLayout->getName()) : std::format("field_{}", fieldIndex);
                auto childPath = path.empty() ? fieldName : std::format("{}.{}", path, fieldName);
                collectCursorBindingsFromTypeLayout(fieldLayout->getTypeLayout(), childPath, stageFlags, outReflection);
            }
        }
        break;
        case slang::TypeReflection::Kind::Array: {
            auto *elementTypeLayout = typeLayout->getElementTypeLayout();
            if (elementTypeLayout)
            {
                // Keep array element index abstract here (`[]`), then let runtime bind
                // call provide concrete descriptor array index.
                auto arrayPath = path.empty() ? std::string("[]") : std::format("{}[]", path);
                collectCursorBindingsFromTypeLayout(elementTypeLayout, arrayPath, stageFlags, outReflection);
            }
        }
        break;
        case slang::TypeReflection::Kind::ConstantBuffer:
        case slang::TypeReflection::Kind::ParameterBlock: {
            auto *elementVarLayout = typeLayout->getElementVarLayout();
            if (elementVarLayout && elementVarLayout->getTypeLayout())
            {
                collectCursorBindingsFromTypeLayout(elementVarLayout->getTypeLayout(), path, stageFlags, outReflection);
            }
        }
        break;
        default:
            break;
        }

        auto subObjectRangeCount = typeLayout->getSubObjectRangeCount();
        for (SlangInt subObjectRangeIndex = 0; subObjectRangeIndex < subObjectRangeCount; ++subObjectRangeIndex)
        {
            auto *subObjectOffset = typeLayout->getSubObjectRangeOffset(subObjectRangeIndex);
            if (!subObjectOffset || !subObjectOffset->getTypeLayout())
            {
                continue;
            }

            collectCursorBindingsFromTypeLayout(subObjectOffset->getTypeLayout(), path, stageFlags, outReflection);
        }
    }

    void collectTypeLayoutBindings(slang::TypeLayoutReflection *typeLayout, vk::ShaderStageFlags stageFlags, SlangReflectionLayout &outReflection)
    {
        if (!typeLayout)
        {
            return;
        }

        auto pushConstantSize = static_cast<uint32_t>(typeLayout->getSize(slang::ParameterCategory::PushConstantBuffer));
        if (pushConstantSize > 0)
        {
            addPushConstantRange(outReflection, 0u, pushConstantSize, stageFlags);
        }

        forEachDescriptorRange(typeLayout, [&](uint32_t set, uint32_t binding, vk::DescriptorType descriptorType, uint32_t descriptorCount) {
            addDescriptorBinding(outReflection, set, binding, descriptorType, descriptorCount, stageFlags);
        });

        auto subObjectRangeCount = typeLayout->getSubObjectRangeCount();
        for (SlangInt subObjectRangeIndex = 0; subObjectRangeIndex < subObjectRangeCount; ++subObjectRangeIndex)
        {
            auto subObjectOffset = typeLayout->getSubObjectRangeOffset(subObjectRangeIndex);
            if (!subObjectOffset)
            {
                continue;
            }

            auto subObjectTypeLayout = subObjectOffset->getTypeLayout();
            collectTypeLayoutBindings(subObjectTypeLayout, stageFlags, outReflection);
        }
    }

    void stabilizeReflectionLayoutLocked(SlangReflectionLayout &reflection) const
    {
        for (auto &setInfo : reflection.descriptorSets)
        {
            std::ranges::sort(setInfo.bindings, [](auto const &lhs, auto const &rhs) {
                if (lhs.binding != rhs.binding)
                {
                    return lhs.binding < rhs.binding;
                }
                if (lhs.descriptorType != rhs.descriptorType)
                {
                    return lhs.descriptorType < rhs.descriptorType;
                }
                if (lhs.descriptorCount != rhs.descriptorCount)
                {
                    return lhs.descriptorCount < rhs.descriptorCount;
                }
                return lhs.stageFlags < rhs.stageFlags;
            });
        }
        std::ranges::sort(reflection.descriptorSets, [](auto const &lhs, auto const &rhs) { return lhs.set < rhs.set; });

        std::ranges::sort(reflection.pushConstantRanges, [](auto const &lhs, auto const &rhs) {
            if (lhs.offset != rhs.offset)
            {
                return lhs.offset < rhs.offset;
            }
            if (lhs.size != rhs.size)
            {
                return lhs.size < rhs.size;
            }
            return lhs.stageFlags < rhs.stageFlags;
        });

        std::ranges::sort(reflection.cursorBindings, [](auto const &lhs, auto const &rhs) {
            if (lhs.path != rhs.path)
            {
                return lhs.path < rhs.path;
            }
            if (lhs.set != rhs.set)
            {
                return lhs.set < rhs.set;
            }
            if (lhs.binding != rhs.binding)
            {
                return lhs.binding < rhs.binding;
            }
            if (lhs.descriptorType != rhs.descriptorType)
            {
                return lhs.descriptorType < rhs.descriptorType;
            }
            if (lhs.descriptorCount != rhs.descriptorCount)
            {
                return lhs.descriptorCount < rhs.descriptorCount;
            }
            return lhs.stageFlags < rhs.stageFlags;
        });
    }

    void buildReflectionLayoutLocked(slang::ProgramLayout *layout, SlangReflectionLayout &outReflection)
    {
        outReflection = {};

        vk::ShaderStageFlags allEntryStages{};
        auto entryPointCount = static_cast<uint32_t>(layout->getEntryPointCount());
        for (uint32_t i = 0; i < entryPointCount; ++i)
        {
            auto *entryPoint = layout->getEntryPointByIndex(i);
            if (!entryPoint)
            {
                continue;
            }
            allEntryStages |= toVkShaderStage(entryPoint->getStage());
        }
        if (allEntryStages == vk::ShaderStageFlags{})
        {
            allEntryStages = vk::ShaderStageFlagBits::eAll;
        }

        if (auto globalVarLayout = layout->getGlobalParamsVarLayout())
        {
            auto globalTypeLayout = globalVarLayout->getTypeLayout();
            collectTypeLayoutBindings(globalTypeLayout, allEntryStages, outReflection);
            collectCursorBindingsFromTypeLayout(globalTypeLayout, "Globals", allEntryStages, outReflection);
            collectCursorBindingsFromTypeLayout(globalTypeLayout, "", allEntryStages, outReflection);
            addPushConstantRangeFromVarLayout(globalVarLayout, allEntryStages, outReflection);
        }

        for (uint32_t i = 0; i < entryPointCount; ++i)
        {
            auto entryPoint = layout->getEntryPointByIndex(i);
            if (!entryPoint)
            {
                continue;
            }

            auto stageFlags = toVkShaderStage(resolveEntryPointStage(entryPoint));
            collectTypeLayoutBindings(entryPoint->getTypeLayout(), stageFlags, outReflection);

            auto entryPointName = resolveEntryPointName(entryPoint, std::format("entrypoint_{}", i));
            collectCursorBindingsFromTypeLayout(entryPoint->getTypeLayout(), std::format("EntryPoints.{}", entryPointName), stageFlags, outReflection);

            addPushConstantRangeFromVarLayout(entryPoint->getVarLayout(), stageFlags, outReflection);
        }

        stabilizeReflectionLayoutLocked(outReflection);
    }

    void addPushConstantRangeFromVarLayout(slang::VariableLayoutReflection *varLayout, vk::ShaderStageFlags stageFlags, SlangReflectionLayout &outReflection)
    {
        if (!varLayout)
        {
            return;
        }

        auto pushConstantOffset = varLayout->getOffset(slang::ParameterCategory::PushConstantBuffer);
        auto *typeLayout = varLayout->getTypeLayout();
        auto pushConstantSize = typeLayout ? typeLayout->getSize(slang::ParameterCategory::PushConstantBuffer) : 0;
        if (pushConstantOffset != detail::kSlangUnknownSize && pushConstantSize > 0)
        {
            addPushConstantRange(outReflection, static_cast<uint32_t>(pushConstantOffset), static_cast<uint32_t>(pushConstantSize), stageFlags);
        }
    }

  public:
    // Locking policy: all access to Slang session/global-session/module/component APIs
    // is serialized by m_mutex because those interfaces are treated as non-thread-safe
    // in this renderer integration. Methods with `Locked` suffix require the caller to
    // hold m_mutex.
    mutable std::mutex m_mutex;
    Slang::ComPtr<slang::IGlobalSession> m_globalSession;
    Slang::ComPtr<slang::ISession> m_session;

    SlangCompileOptions m_options;
    std::filesystem::path m_shaderRootPath = detail::resolveShaderRootPath();

    std::vector<std::string> m_effectiveSearchPaths;
    std::vector<const char *> m_searchPathPointers;
    std::vector<slang::PreprocessorMacroDesc> m_macroDescs;
    slang::TargetDesc m_targetDesc{};

};

} // namespace nr::rhi
