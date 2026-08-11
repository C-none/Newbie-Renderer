module nr.rhi;
import :slang;
import dependency.slang;
import dependency.vulkan;
import nr.utils;
import std;

namespace nr::rhi::detail
{
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

[[nodiscard]] std::vector<std::byte> readBinaryFile(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        return {};

    auto const endOffset = static_cast<std::streamoff>(file.tellg());
    if (endOffset < 0 || static_cast<std::uintmax_t>(endOffset) > std::numeric_limits<std::size_t>::max())
    {
        return {};
    }
    auto const size = static_cast<std::size_t>(endOffset);
    file.seekg(0, std::ios::beg);
    if (!file)
    {
        return {};
    }

    std::vector<std::byte> bytes(size);
    if (size > 0)
    {
        file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(size));
        if (!file)
        {
            return {};
        }
    }
    return bytes;
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
    static const std::regex kDeclRegex(
        R"((?:^|[\r\n])\s*(?:module|implementing)\s+((?:[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*)|(?:\"[^\"]+\"))\s*;)");

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

    auto hasParentTraversal =
        std::ranges::any_of(normalizedPath, [](const std::filesystem::path &part) { return part == ".."; });
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
    nrAssert(!ec && isDirectory, "Invalid shader root path from CMake: '{}'.", rootPath.generic_string());
    return rootPath;
}

[[nodiscard]] std::filesystem::path makeModuleBinaryPath(const std::filesystem::path &cacheRoot,
                                                         std::string_view moduleName)
{
    // Keep this path aligned with Slang's `loadModule` lookup logic:
    // `<searchPath>/<module-path>.slang-module`.
    return cacheRoot / (moduleNameToPath(moduleName) + ".slang-module");
}

[[nodiscard]] std::string makeSlangFloatLiteral(float value)
{
    nrAssert(std::isfinite(value), "Slang float32 variant assignments must be finite.");

    std::array<char, 64> buffer{};
    auto [ptr, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, std::chars_format::general,
                                   std::numeric_limits<float>::max_digits10);
    nrAssert(ec == std::errc{}, "Failed to serialize Slang float32 variant assignment.");

    auto literal = std::string(buffer.data(), ptr);
    auto hasDecimalSyntax = literal.find('.') != std::string::npos || literal.find('e') != std::string::npos ||
                            literal.find('E') != std::string::npos;
    if (!hasDecimalSyntax)
    {
        literal += ".0";
    }
    literal += "f";
    return literal;
}

[[nodiscard]] bool isSlangVariantScalarType(std::string_view type) noexcept
{
    return type == "bool" || type == "int" || type == "uint" || type == "float";
}

[[nodiscard]] bool slangVariantAssignmentValueMatchesType(const SlangVariantAssignment &assignment) noexcept
{
    return std::visit(
        [&](const auto &value) noexcept {
            using ValueT = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<ValueT, bool>)
            {
                return assignment.type == "bool";
            }
            else if constexpr (std::same_as<ValueT, std::int32_t>)
            {
                return assignment.type == "int";
            }
            else if constexpr (std::same_as<ValueT, std::uint32_t>)
            {
                return assignment.type == "uint";
            }
            else if constexpr (std::same_as<ValueT, float>)
            {
                return assignment.type == "float";
            }
            else
            {
                return !assignment.type.empty() && !isSlangVariantScalarType(assignment.type);
            }
        },
        assignment.value);
}

[[nodiscard]] bool slangVariantAssignmentIsTypeAlias(const SlangVariantAssignment &assignment) noexcept
{
    return std::holds_alternative<std::string>(assignment.value);
}

[[nodiscard]] std::string slangVariantAssignmentValueLiteral(const SlangVariantAssignmentValue &value)
{
    return std::visit(
        [](const auto &typedValue) {
            using ValueT = std::remove_cvref_t<decltype(typedValue)>;
            if constexpr (std::same_as<ValueT, bool>)
            {
                return typedValue ? std::string{"true"} : std::string{"false"};
            }
            else if constexpr (std::same_as<ValueT, std::int32_t>)
            {
                return std::to_string(typedValue);
            }
            else if constexpr (std::same_as<ValueT, std::uint32_t>)
            {
                return std::format("{}u", typedValue);
            }
            else if constexpr (std::same_as<ValueT, float>)
            {
                return makeSlangFloatLiteral(typedValue);
            }
            else
            {
                return typedValue;
            }
        },
        value);
}

void hashAppendSlangVariantAssignmentValue(std::uint64_t &state, const SlangVariantAssignmentValue &value) noexcept
{
    hash::hashAppend(state, static_cast<std::uint32_t>(value.index()));
    std::visit(
        [&](const auto &typedValue) noexcept {
            using ValueT = std::remove_cvref_t<decltype(typedValue)>;
            if constexpr (std::same_as<ValueT, std::string>)
            {
                hash::hashAppendString(state, typedValue);
            }
            else if constexpr (std::same_as<ValueT, float>)
            {
                hash::hashAppend(state, std::bit_cast<std::uint32_t>(typedValue));
            }
            else
            {
                hash::hashAppend(state, typedValue);
            }
        },
        value);
}

[[nodiscard]] bool isSlangQualifiedIdentifier(std::string_view value)
{
    if (value.empty())
    {
        return false;
    }

    static const std::regex kIdentifierRegex(R"([A-Za-z_][A-Za-z0-9_]*(\.[A-Za-z_][A-Za-z0-9_]*)*)");
    return std::regex_match(std::string(value), kIdentifierRegex);
}

[[nodiscard]] bool validateSlangVariantDesc(const SlangProgramVariantDesc &variant, std::string_view moduleName)
{
    for (auto const &[name, assignment] : variant.assignments)
    {
        if (!isSlangQualifiedIdentifier(name))
        {
            nrLog<LogLevel::warning>(
                "[ShaderService::compileProgramsByFile] invalid link-time variant name='{}' for module='{}'.", name,
                moduleName);
            return false;
        }

        if (!slangVariantAssignmentValueMatchesType(assignment))
        {
            nrLog<LogLevel::warning>(
                "[ShaderService::compileProgramsByFile] link-time variant value type mismatch: module='{}', name='{}', "
                "type='{}'.",
                moduleName, name, assignment.type);
            return false;
        }

        if (slangVariantAssignmentIsTypeAlias(assignment))
        {
            auto const *concreteTypeName = std::get_if<std::string>(&assignment.value);
            if (!isSlangQualifiedIdentifier(assignment.type) || concreteTypeName == nullptr ||
                concreteTypeName->empty())
            {
                nrLog<LogLevel::warning>(
                    "[ShaderService::compileProgramsByFile] invalid link-time type alias for module='{}': "
                    "name='{}', interface='{}', concrete='{}'.",
                    moduleName, name, assignment.type,
                    concreteTypeName != nullptr ? *concreteTypeName : std::string{});
                return false;
            }
        }
    }

    return true;
}

inline constexpr std::size_t kMaxSyntheticVariantLabelLength = 64;

[[nodiscard]] bool isAsciiAlphaNumeric(char value) noexcept
{
    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9');
}

[[nodiscard]] bool isAsciiWhitespace(char value) noexcept
{
    return value == ' ' || value == '\t' || value == '\n' || value == '\r' || value == '\f' || value == '\v';
}

[[nodiscard]] std::string withoutAsciiWhitespace(std::string_view value)
{
    std::string result;
    result.reserve(value.size());
    std::ranges::for_each(value, [&](char ch) {
        if (!isAsciiWhitespace(ch))
        {
            result.push_back(ch);
        }
    });
    return result;
}

[[nodiscard]] std::string joinDebugNameParts(const std::vector<std::string> &parts, char separator)
{
    std::ostringstream output;
    auto isFirst = true;
    std::ranges::for_each(parts, [&](const std::string &part) {
        if (!isFirst)
        {
            output << separator;
        }
        output << part;
        isFirst = false;
    });
    return output.str();
}

[[nodiscard]] std::string compactSlangVariantDebugLabel(const SlangProgramVariantDesc &variant)
{
    if (variant.empty())
    {
        return "default";
    }

    std::vector<std::string> parts;
    parts.reserve(variant.assignments.size());

    std::ranges::for_each(variant.assignments, [&](auto const &entry) {
        auto const &[name, assignment] = entry;
        auto value = slangVariantAssignmentValueLiteral(assignment.value);
        if (slangVariantAssignmentIsTypeAlias(assignment))
        {
            value = withoutAsciiWhitespace(value);
        }
        parts.push_back(std::format("{}={}", moduleLeafName(name), value));
    });

    return joinDebugNameParts(parts, ',');
}

[[nodiscard]] std::string sanitizeSlangIdentifierFragment(std::string_view value)
{
    std::string result;
    result.reserve(std::min(value.size(), kMaxSyntheticVariantLabelLength));
    std::ranges::for_each(value, [&](char ch) {
        if (result.size() >= kMaxSyntheticVariantLabelLength)
        {
            return;
        }

        auto const output = isAsciiAlphaNumeric(ch) ? ch : '_';
        if (output == '_' && (result.empty() || result.back() == '_'))
        {
            return;
        }
        result.push_back(output);
    });

    while (!result.empty() && result.back() == '_')
    {
        result.pop_back();
    }

    return result.empty() ? std::string{"variant"} : result;
}

[[nodiscard]] std::string makeSlangVariantSyntheticModuleName(std::string_view variantLabel,
                                                              std::string_view variantHash)
{
    // The readable label intentionally omits assignment type details, so the identity suffix must
    // come from the complete variant hash rather than from the label.
    return std::format("variant_{}_{}", sanitizeSlangIdentifierFragment(variantLabel), variantHash);
}

[[nodiscard]] std::string makeSlangVariantSyntheticPath(std::string_view syntheticModuleName)
{
    return std::format("generated/{}.slang", syntheticModuleName);
}

[[nodiscard]] std::string makeSlangProgramDebugNamePrefix(std::string_view baseName,
                                                          const SlangProgramVariantDesc &variant)
{
    auto normalizedBaseName = baseName.empty() ? std::string{"shader"} : std::string{baseName};
    if (variant.empty())
    {
        return normalizedBaseName;
    }

    return std::format("{}[{}]", normalizedBaseName, compactSlangVariantDebugLabel(variant));
}

[[nodiscard]] std::string makeSlangProgramCacheKey(std::uint64_t sessionGeneration, std::string_view optionsHash,
                                                   std::string_view modulePath, std::string_view variantHash)
{
    return std::format("generation={}|options={}|module={}|variant={}", sessionGeneration, optionsHash, modulePath,
                       variantHash);
}

[[nodiscard]] std::string describeSlangVariantForLog(const SlangProgramVariantDesc &variant)
{
    auto hashHex = nr::hash::toHexString(variant.hashValue());
    if (variant.empty())
    {
        return std::format("default/{}", hashHex);
    }
    return std::format("{}/{}", compactSlangVariantDebugLabel(variant), hashHex);
}

inline constexpr std::uint32_t kSpirvMagic = 0x07230203u;
inline constexpr std::uint32_t kSpirvCacheMagic = 0x4353524eu;
inline constexpr std::uint32_t kSpirvCacheSchema = 1u;

struct SpirvCacheHeader
{
    std::uint32_t magic = kSpirvCacheMagic;
    std::uint32_t schema = kSpirvCacheSchema;
    std::uint32_t hashByteCount = 0;
    std::uint32_t spirvWordCount = 0;
    std::uint64_t checksum = 0;
};

static_assert(std::is_trivially_copyable_v<SpirvCacheHeader>);

struct SpirvCacheReadResult
{
    std::shared_ptr<const std::vector<std::uint32_t>> spirv{};
    bool corrupt = false;
};

[[nodiscard]] std::string opaqueHashHex(std::span<const std::byte> hashBytes)
{
    constexpr std::array<char, 16> digits{'0', '1', '2', '3', '4', '5', '6', '7',
                                          '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    std::string result(hashBytes.size() * 2u, '0');
    std::ranges::for_each(std::views::iota(std::size_t{0}, hashBytes.size()), [&](std::size_t index) {
        auto value = std::to_integer<std::uint8_t>(hashBytes[index]);
        result[index * 2u] = digits[value >> 4u];
        result[index * 2u + 1u] = digits[value & 0x0fu];
    });
    return result;
}

[[nodiscard]] std::filesystem::path makeSpirvCachePath(const std::filesystem::path &root,
                                                       std::span<const std::byte> hashBytes)
{
    auto hashHex = opaqueHashHex(hashBytes);
    auto shard = hashHex.size() >= 2u ? hashHex.substr(0, 2) : std::string{"00"};
    return root / shard / (hashHex + ".nrspv");
}

[[nodiscard]] SpirvCacheReadResult readSpirvCache(const std::filesystem::path &path,
                                                  std::span<const std::byte> expectedHash)
{
    std::error_code existsError;
    if (!std::filesystem::exists(path, existsError) || existsError)
    {
        return {};
    }

    auto bytes = readBinaryFile(path);
    auto corrupt = [&]() { return SpirvCacheReadResult{.corrupt = true}; };
    if (bytes.size() < sizeof(SpirvCacheHeader))
    {
        return corrupt();
    }

    SpirvCacheHeader header{};
    std::memcpy(std::addressof(header), bytes.data(), sizeof(header));
    if (header.magic != kSpirvCacheMagic || header.schema != kSpirvCacheSchema ||
        header.hashByteCount != expectedHash.size() || header.spirvWordCount == 0)
    {
        return corrupt();
    }

    auto hashOffset = sizeof(SpirvCacheHeader);
    auto spirvOffset = hashOffset + static_cast<std::size_t>(header.hashByteCount);
    auto spirvByteCount = static_cast<std::size_t>(header.spirvWordCount) * sizeof(std::uint32_t);
    if (spirvOffset > bytes.size() || spirvByteCount > bytes.size() - spirvOffset)
    {
        return corrupt();
    }
    if (spirvOffset + spirvByteCount != bytes.size())
    {
        return corrupt();
    }

    auto storedHash = std::span<const std::byte>{bytes}.subspan(hashOffset, header.hashByteCount);
    if (!std::ranges::equal(storedHash, expectedHash))
    {
        return corrupt();
    }

    auto spirvBytes = std::span<const std::byte>{bytes}.subspan(spirvOffset, spirvByteCount);
    if (hash::fnv1a64(spirvBytes) != header.checksum)
    {
        return corrupt();
    }

    auto spirv = std::make_shared<std::vector<std::uint32_t>>(header.spirvWordCount);
    std::memcpy(spirv->data(), spirvBytes.data(), spirvByteCount);
    if (spirv->front() != kSpirvMagic)
    {
        return corrupt();
    }
    return SpirvCacheReadResult{.spirv = std::move(spirv)};
}

enum class AtomicWriteResult : std::uint8_t
{
    published,
    targetExists,
    failed,
};

[[nodiscard]] AtomicWriteResult writeBytesAtomically(const std::filesystem::path &path,
                                                     std::span<const std::byte> bytes)
{
    std::error_code directoryError;
    std::filesystem::create_directories(path.parent_path(), directoryError);
    if (directoryError)
    {
        return AtomicWriteResult::failed;
    }

    static std::atomic_uint64_t sequence = 0;
    auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto threadIdentity = std::hash<std::thread::id>{}(std::this_thread::get_id());
    auto temporaryPath = path;
    temporaryPath += std::format(".tmp.{}.{}.{}", timestamp, threadIdentity, sequence.fetch_add(1));

    {
        std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            return AtomicWriteResult::failed;
        }
        output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!output)
        {
            std::error_code removeError;
            std::filesystem::remove(temporaryPath, removeError);
            return AtomicWriteResult::failed;
        }
    }

    std::error_code renameError;
    std::filesystem::rename(temporaryPath, path, renameError);
    if (!renameError)
    {
        return AtomicWriteResult::published;
    }

    std::error_code targetError;
    auto targetExists = std::filesystem::exists(path, targetError) && !targetError;
    std::error_code removeError;
    std::filesystem::remove(temporaryPath, removeError);
    return targetExists ? AtomicWriteResult::targetExists : AtomicWriteResult::failed;
}

[[nodiscard]] bool writeSpirvCache(const std::filesystem::path &path, std::span<const std::byte> hashBytes,
                                   std::span<const std::uint32_t> spirv)
{
    if (spirv.empty() || spirv.front() != kSpirvMagic)
    {
        return false;
    }

    auto spirvBytes = std::as_bytes(spirv);
    auto header = SpirvCacheHeader{
        .hashByteCount = static_cast<std::uint32_t>(hashBytes.size()),
        .spirvWordCount = static_cast<std::uint32_t>(spirv.size()),
        .checksum = hash::fnv1a64(spirvBytes),
    };
    auto bytes = std::vector<std::byte>(sizeof(header) + hashBytes.size() + spirvBytes.size());
    std::memcpy(bytes.data(), std::addressof(header), sizeof(header));
    std::ranges::copy(hashBytes, bytes.begin() + static_cast<std::ptrdiff_t>(sizeof(header)));
    std::ranges::copy(spirvBytes, bytes.begin() + static_cast<std::ptrdiff_t>(sizeof(header) + hashBytes.size()));
    auto writeResult = writeBytesAtomically(path, bytes);
    if (writeResult == AtomicWriteResult::published)
    {
        return true;
    }
    if (writeResult == AtomicWriteResult::targetExists)
    {
        return readSpirvCache(path, hashBytes).spirv != nullptr;
    }
    return false;
}

[[nodiscard]] std::shared_ptr<const std::vector<std::uint32_t>> copySpirvBlob(slang::IBlob &blob)
{
    auto byteCount = blob.getBufferSize();
    if (byteCount == 0 || byteCount % sizeof(std::uint32_t) != 0)
    {
        return {};
    }

    auto spirv = std::make_shared<std::vector<std::uint32_t>>(byteCount / sizeof(std::uint32_t));
    std::memcpy(spirv->data(), blob.getBufferPointer(), byteCount);
    if (spirv->empty() || spirv->front() != kSpirvMagic)
    {
        return {};
    }
    return spirv;
}
} // namespace nr::rhi::detail

namespace nr::rhi
{
SlangProgramVariantDesc &SlangProgramVariantDesc::assign(std::string_view name, std::string_view type,
                                                         SlangVariantAssignmentValue value)
{
    nrAssert(!name.empty(), "SlangProgramVariantDesc::assign requires a non-empty name.");
    nrAssert(!type.empty(), "SlangProgramVariantDesc assignment '{}' requires a non-empty type.", name);

    auto const nameString = std::string{name};
    auto [assignmentIt, inserted] = assignments.try_emplace(nameString, SlangVariantAssignment{
                                                                            .type = std::string{type},
                                                                            .value = std::move(value),
                                                                        });
    (void)assignmentIt;
    nrAssert(inserted, "SlangProgramVariantDesc assignment '{}' is already defined.", nameString);
    return *this;
}

[[nodiscard]] bool SlangProgramVariantDesc::empty() const noexcept
{
    return assignments.empty();
}

[[nodiscard]] std::uint64_t SlangProgramVariantDesc::hashValue() const noexcept
{
    std::uint64_t state = hash::fnv1a64OffsetBasis;
    hash::hashAppendString(state, "SlangProgramVariantDesc.v2");

    std::ranges::for_each(assignments, [&](auto const &entry) {
        auto const &[name, assignment] = entry;
        hash::hashAppendString(state, name);
        hash::hashAppendString(state, assignment.type);
        detail::hashAppendSlangVariantAssignmentValue(state, assignment.value);
    });

    return state;
}

[[nodiscard]] std::string SlangProgramVariantDesc::sourceText(std::string_view moduleName) const
{
    nrAssert(detail::isSlangQualifiedIdentifier(moduleName),
             "SlangProgramVariantDesc::sourceText requires a valid module name, got '{}'.", moduleName);

    std::ostringstream source;
    source << "module " << moduleName << ";\n";
    source << "// Generated by Newbie Renderer ShaderService. Do not edit.\n";
    auto hasTypeAlias = std::ranges::any_of(
        assignments, [](auto const &entry) { return detail::slangVariantAssignmentIsTypeAlias(entry.second); });
    if (hasTypeAlias)
    {
        source << "import common;\n";
    }

    std::ranges::for_each(assignments, [&](auto const &entry) {
        auto const &[name, assignment] = entry;
        if (detail::slangVariantAssignmentIsTypeAlias(assignment))
        {
            source << "export struct " << name << " : " << assignment.type << " = "
                   << detail::slangVariantAssignmentValueLiteral(assignment.value) << ";\n";
            return;
        }

        source << "export static const " << assignment.type << " " << name << " = "
               << detail::slangVariantAssignmentValueLiteral(assignment.value) << ";\n";
    });

    return source.str();
}

[[nodiscard]] SlangSampler SlangSampler::create(const vk::raii::Device &device, SlangSamplerDesc desc,
                                                std::string_view debugName)
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
    if constexpr (gpuDebugNamesEnabled)
    {
        if (!sampler.debugName_.empty())
        {
            vk::DebugUtilsObjectNameInfoEXT objectNameInfo{};
            objectNameInfo.objectType = vk::ObjectType::eSampler;
            const auto rawHandle = *sampler.sampler_;
            static_assert(sizeof(rawHandle) == sizeof(std::uint64_t),
                          "vk::Sampler handle size must match std::uint64_t for debug naming.");
            objectNameInfo.objectHandle = std::bit_cast<std::uint64_t>(rawHandle);
            objectNameInfo.pObjectName = sampler.debugName_.c_str();
            try
            {
                device.setDebugUtilsObjectNameEXT(objectNameInfo);
            }
            catch (const vk::SystemError &error)
            {
                nrLog<LogLevel::warning>("SlangSampler::create failed to set debug name '{}': {}", sampler.debugName_,
                                        error.what());
                nrAssert(false, "SlangSampler::create failed to set a Vulkan debug object name.");
            }
        }
    }
    return sampler;
}

[[nodiscard]] bool SlangSampler::valid() const noexcept
{
    return *sampler_ != nullptr;
}

[[nodiscard]] const vk::raii::Sampler *SlangSampler::handle() const noexcept
{
    return valid() ? &sampler_ : nullptr;
}

[[nodiscard]] vk::Sampler SlangSampler::raw() const noexcept
{
    return valid() ? *sampler_ : vk::Sampler{};
}

[[nodiscard]] bool SlangEntryPointData::valid() const noexcept
{
    return !entryPointName.empty() && stage != SLANG_STAGE_NONE && spirv && !spirv->empty();
}

[[nodiscard]] bool SlangProgram::valid() const noexcept
{
    return state_ && state_->linkedProgram && state_->programLayout && state_->entryPoint.valid();
}

[[nodiscard]] const SlangEntryPointData *SlangProgram::entryPoint() const noexcept
{
    return valid() ? std::addressof(state_->entryPoint) : nullptr;
}

[[nodiscard]] slang::ProgramLayout *SlangProgram::programLayout() const noexcept
{
    return state_ ? state_->programLayout : nullptr;
}

[[nodiscard]] ShaderService &ShaderService::instance()
{
    static ShaderService service;
    return service;
}

void ShaderService::configure(ShaderServiceConfig config)
{
    std::scoped_lock lock(m_mutex);
    config.backendWorkerCount = std::clamp(config.backendWorkerCount, 1u, nr::maxThreads);
    if (m_backendPool && config.backendWorkerCount != m_serviceConfig.backendWorkerCount)
    {
        m_backendPool->waitIdle();
        m_backendPool.reset();
    }
    m_serviceConfig = config;
    applyCompileOptionsLocked(kDefaultSlangCompileOptions);
    ensureBackendPoolLocked();
}

void ShaderService::reloadSession()
{
    std::scoped_lock lock(m_mutex);
    if (!m_session)
    {
        applyCompileOptionsLocked(kDefaultSlangCompileOptions);
        ensureBackendPoolLocked();
        return;
    }

    ensureGlobalSessionLocked();
    recreateSessionLocked();
    nrLog<LogLevel::info>("[ShaderService::reloadSession] rebuilt Slang session generation={}", m_sessionGeneration);
}

[[nodiscard]] std::uint64_t ShaderService::sessionGeneration() const
{
    std::scoped_lock lock(m_mutex);
    return m_sessionGeneration;
}

[[nodiscard]] ShaderCompileBatchStats ShaderService::lastCompileBatchStats() const
{
    std::scoped_lock lock(m_mutex);
    return m_lastCompileBatchStats;
}

[[nodiscard]] SlangProgram ShaderService::compileProgramByFile(const SlangProgramCompileFileRequest &request)
{
    auto requests = std::array{request};
    auto programs = compileProgramsByFile(requests);
    return programs.empty() ? SlangProgram{} : std::move(programs.front());
}

[[nodiscard]] std::vector<SlangProgram> ShaderService::compileProgramsByFile(
    std::span<const SlangProgramCompileFileRequest> requests)
{
    std::scoped_lock lock(m_mutex);
    auto const startedAt = std::chrono::steady_clock::now();
    using PhaseDuration = std::chrono::duration<double, std::milli>;
    struct PhaseTimings
    {
        PhaseDuration configure{};
        PhaseDuration resolveCache{};
        PhaseDuration moduleLoad{};
        PhaseDuration entryPointLoad{};
        PhaseDuration variantModule{};
        PhaseDuration compose{};
        PhaseDuration link{};
        PhaseDuration reflection{};
        PhaseDuration entryHashPrepare{};
        PhaseDuration backendWall{};
        PhaseDuration backendCacheReadWorkSum{};
        PhaseDuration backendCacheReadWorkMax{};
        PhaseDuration backendCodegenWorkSum{};
        PhaseDuration backendCodegenWorkMax{};
        PhaseDuration backendArtifactWorkSum{};
        PhaseDuration backendArtifactWorkMax{};
        PhaseDuration publish{};
    };
    struct ScopedPhaseTimer
    {
        PhaseDuration &elapsed;
        std::chrono::steady_clock::time_point startedAt = std::chrono::steady_clock::now();
        bool active = true;

        void stop() noexcept
        {
            if (!active)
            {
                return;
            }
            elapsed += std::chrono::duration_cast<PhaseDuration>(std::chrono::steady_clock::now() - startedAt);
            active = false;
        }

        ~ScopedPhaseTimer() noexcept { stop(); }
    };

    auto phaseTimings = PhaseTimings{};
    auto configureTimer = ScopedPhaseTimer{phaseTimings.configure};
    ensureConfiguredLocked();
    configureTimer.stop();
    auto programs = std::vector<SlangProgram>(requests.size());
    if (requests.empty())
    {
        auto const elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startedAt);
        m_lastCompileBatchStats = ShaderCompileBatchStats{
            .workerCount = m_serviceConfig.backendWorkerCount,
            .frontendElapsed = elapsed,
            .elapsed = elapsed,
        };
        return programs;
    }

    struct PreparedProgram
    {
        std::vector<std::size_t> requestIndices{};
        std::string memoryCacheKey{};
        std::string moduleName{};
        std::string variantLogLabel{};
        Slang::ComPtr<slang::IComponentType> linkedProgram{};
        slang::ProgramLayout *programLayout = nullptr;
        std::vector<std::byte> cacheKey{};
        std::filesystem::path cachePath{};
        std::string entryPointName{};
        std::string debugName{};
        SlangStage stage = SLANG_STAGE_NONE;
    };

    auto preparedPrograms = std::vector<PreparedProgram>{};
    preparedPrograms.reserve(requests.size());
    auto preparedIndexByMemoryKey = std::map<std::string, std::size_t>{};
    auto memoryHitCount = std::size_t{0};
    auto variantRequestCount = std::size_t{0};

    std::ranges::for_each(std::views::iota(std::size_t{0}, requests.size()), [&](std::size_t requestIndex) {
        auto const &request = requests[requestIndex];
        auto resolveCacheTimer = ScopedPhaseTimer{phaseTimings.resolveCache};
        variantRequestCount += request.variant.empty() ? 0u : 1u;
        auto modulePath = detail::normalizeRequestModulePath(request.sourcePath);
        if (!modulePath.has_value())
        {
            nrLog<LogLevel::warning>("[ShaderService::compileProgramsByFile] invalid sourcePath='{}'. "
                                      "Expected a shader-root-relative module path.",
                                      request.sourcePath.generic_string());
            return;
        }

        auto moduleName = resolveModuleNameLocked(*modulePath);
        if (moduleName.empty() || !detail::validateSlangVariantDesc(request.variant, moduleName))
        {
            return;
        }

        auto variantHashHex = nr::hash::toHexString(request.variant.hashValue());
        auto memoryCacheKey =
            detail::makeSlangProgramCacheKey(m_sessionGeneration, m_optionsHashHex, *modulePath, variantHashHex);
        if (auto cached = m_linkedProgramCache.find(memoryCacheKey); cached != m_linkedProgramCache.end())
        {
            programs[requestIndex] = cached->second;
            ++memoryHitCount;
            return;
        }
        if (auto prepared = preparedIndexByMemoryKey.find(memoryCacheKey); prepared != preparedIndexByMemoryKey.end())
        {
            preparedPrograms[prepared->second].requestIndices.push_back(requestIndex);
            ++memoryHitCount;
            return;
        }

        resolveCacheTimer.stop();
        auto moduleLoadTimer = ScopedPhaseTimer{phaseTimings.moduleLoad};
        auto rootModule = loadOrCompileModuleLocked(moduleName);
        moduleLoadTimer.stop();
        if (!rootModule)
        {
            return;
        }

        auto entryPointLoadTimer = ScopedPhaseTimer{phaseTimings.entryPointLoad};
        auto definedEntryPointCount = SlangInt32{0};
        try
        {
            definedEntryPointCount = std::max<SlangInt32>(0, rootModule->getDefinedEntryPointCount());
        }
        catch (...)
        {
            nrLog<LogLevel::warning>(
                "[ShaderService::compileProgramsByFile] Slang threw while enumerating entrypoints for module='{}'.",
                moduleName);
            nrAssert(false, "Slang entrypoint enumeration threw an internal exception.");
            return;
        }
        if (definedEntryPointCount != 1)
        {
            nrLog<LogLevel::error>("[ShaderService::compileProgramsByFile] module='{}' defines {} "
                                    "entrypoints; every shader file must define exactly one.",
                                    moduleName, definedEntryPointCount);
            return;
        }

        Slang::ComPtr<slang::IEntryPoint> entryPointComponent;
        auto entryPointResult = SlangResult{};
        try
        {
            entryPointResult = rootModule->getDefinedEntryPoint(0, entryPointComponent.writeRef());
        }
        catch (...)
        {
            nrLog<LogLevel::warning>(
                "[ShaderService::compileProgramsByFile] Slang threw while loading the sole entrypoint for module='{}'.",
                moduleName);
            nrAssert(false, "Slang entrypoint loading threw an internal exception.");
            return;
        }
        if (!detail::slangSucceeded(entryPointResult) || !entryPointComponent)
        {
            nrLog<LogLevel::error>(
                "[ShaderService::compileProgramsByFile] unable to load the only entrypoint for module='{}'.",
                moduleName);
            return;
        }
        entryPointLoadTimer.stop();

        auto components = std::vector<slang::IComponentType *>{rootModule.get(), entryPointComponent.get()};
        Slang::ComPtr<slang::IModule> variantModule;
        auto variantLogLabel = detail::describeSlangVariantForLog(request.variant);
        if (!request.variant.empty())
        {
            auto variantModuleTimer = ScopedPhaseTimer{phaseTimings.variantModule};
            auto variantLabel = detail::compactSlangVariantDebugLabel(request.variant);
            auto syntheticModuleName = detail::makeSlangVariantSyntheticModuleName(variantLabel, variantHashHex);
            auto syntheticPath = detail::makeSlangVariantSyntheticPath(syntheticModuleName);
            auto variantSource = request.variant.sourceText(syntheticModuleName);
            Slang::ComPtr<slang::IBlob> diagnostics;
            try
            {
                variantModule = Slang::ComPtr<slang::IModule>(m_session->loadModuleFromSourceString(
                    syntheticModuleName.c_str(), syntheticPath.c_str(), variantSource.c_str(), diagnostics.writeRef()));
            }
            catch (...)
            {
                emitDiagnosticsLocked(diagnostics.get(), "loadModuleFromSourceString(exception-path)");
                nrLog<LogLevel::warning>("[ShaderService::compileProgramsByFile] Slang threw while loading "
                                        "variant for module='{}', variant='{}'.",
                                        moduleName, variantLogLabel);
                nrAssert(false, "Slang variant module loading threw an internal exception.");
            }
            emitDiagnosticsLocked(diagnostics.get(), "loadModuleFromSourceString(variant)");
            if (!variantModule)
            {
                nrLog<LogLevel::error>(
                    "[ShaderService::compileProgramsByFile] failed to load variant for module='{}', variant='{}'.",
                    moduleName, variantLogLabel);
                return;
            }
            components.push_back(variantModule.get());
        }

        Slang::ComPtr<slang::IComponentType> compositeProgram;
        Slang::ComPtr<slang::IBlob> diagnostics;
        auto composeTimer = ScopedPhaseTimer{phaseTimings.compose};
        try
        {
            auto composeResult =
                m_session->createCompositeComponentType(components.data(), static_cast<SlangInt>(components.size()),
                                                        compositeProgram.writeRef(), diagnostics.writeRef());
            emitDiagnosticsLocked(diagnostics.get(), "createCompositeComponentType(single-entry)");
            if (!detail::slangSucceeded(composeResult) || !compositeProgram)
            {
                nrLog<LogLevel::warning>(
                    "[ShaderService::compileProgramsByFile] composition failed for module='{}', variant='{}'.",
                    moduleName, variantLogLabel);
                return;
            }
        }
        catch (...)
        {
            emitDiagnosticsLocked(diagnostics.get(), "createCompositeComponentType(exception-path)");
            nrLog<LogLevel::error>(
                "[ShaderService::compileProgramsByFile] Slang threw while composing module='{}', variant='{}'.",
                moduleName, variantLogLabel);
            nrAssert(false, "Slang component composition threw an internal exception.");
        }
        composeTimer.stop();

        Slang::ComPtr<slang::IComponentType> linkedProgram;
        auto linkTimer = ScopedPhaseTimer{phaseTimings.link};
        diagnostics = nullptr;
        try
        {
            auto linkResult = compositeProgram->link(linkedProgram.writeRef(), diagnostics.writeRef());
            emitDiagnosticsLocked(diagnostics.get(), "link(single-entry)");
            if (!detail::slangSucceeded(linkResult) || !linkedProgram)
            {
                nrLog<LogLevel::warning>(
                    "[ShaderService::compileProgramsByFile] link failed for module='{}', variant='{}'.", moduleName,
                    variantLogLabel);
                return;
            }
        }
        catch (...)
        {
            emitDiagnosticsLocked(diagnostics.get(), "link(exception-path)");
            nrLog<LogLevel::error>(
                "[ShaderService::compileProgramsByFile] Slang threw while linking module='{}', variant='{}'.",
                moduleName, variantLogLabel);
            nrAssert(false, "Slang component linking threw an internal exception.");
        }
        linkTimer.stop();

        diagnostics = nullptr;
        auto *programLayout = static_cast<slang::ProgramLayout *>(nullptr);
        auto reflectionTimer = ScopedPhaseTimer{phaseTimings.reflection};
        try
        {
            programLayout = linkedProgram->getLayout(0, diagnostics.writeRef());
        }
        catch (...)
        {
            emitDiagnosticsLocked(diagnostics.get(), "getLayout(exception-path)");
            nrLog<LogLevel::warning>(
                "[ShaderService::compileProgramsByFile] Slang threw while reflecting module='{}', variant='{}'.",
                moduleName, variantLogLabel);
            nrAssert(false, "Slang program reflection threw an internal exception.");
            return;
        }
        emitDiagnosticsLocked(diagnostics.get(), "getLayout(single-entry)");
        if (!programLayout || programLayout->getEntryPointCount() != 1)
        {
            nrLog<LogLevel::error>(
                "[ShaderService::compileProgramsByFile] linked module='{}' does not expose exactly one entrypoint.",
                moduleName);
            return;
        }

        auto *entryPointLayout = programLayout->getEntryPointByIndex(0);
        auto entryPointName =
            entryPointLayout && entryPointLayout->getName() ? std::string{entryPointLayout->getName()} : std::string{};
        auto stage = entryPointLayout ? entryPointLayout->getStage() : SLANG_STAGE_NONE;
        if (entryPointName.empty() || stage == SLANG_STAGE_NONE)
        {
            nrLog<LogLevel::error>(
                "[ShaderService::compileProgramsByFile] invalid entrypoint reflection for module='{}'.", moduleName);
            return;
        }

        auto *entryScopeVarLayout = entryPointLayout->getVarLayout();
        auto *entryScopeTypeLayout = entryScopeVarLayout ? entryScopeVarLayout->getTypeLayout() : nullptr;
        auto entryBindingRangeCount =
            entryScopeTypeLayout ? std::max<SlangInt>(0, entryScopeTypeLayout->getBindingRangeCount()) : 0;
        std::ranges::for_each(std::views::iota(SlangInt{0}, entryBindingRangeCount), [&](SlangInt rangeIndex) {
            auto bindingType = entryScopeTypeLayout->getBindingRangeType(rangeIndex);
            auto isStageIo =
                bindingType == slang::BindingType::VaryingInput || bindingType == slang::BindingType::VaryingOutput;
            nrAssert(isStageIo,
                     "Entry-point descriptor binding is forbidden. entry='{}', rangeIndex={}, bindingType={}",
                     entryPointName, rangeIndex, static_cast<std::int32_t>(bindingType));
        });
        reflectionTimer.stop();

        Slang::ComPtr<slang::IBlob> hashBlob;
        auto entryHashPrepareTimer = ScopedPhaseTimer{phaseTimings.entryHashPrepare};
        try
        {
            linkedProgram->getEntryPointHash(0, 0, hashBlob.writeRef());
        }
        catch (...)
        {
            nrLog<LogLevel::warning>(
                "[ShaderService::compileProgramsByFile] Slang threw while hashing module='{}', entry='{}'.", moduleName,
                entryPointName);
            nrAssert(false, "Slang entrypoint hashing threw an internal exception.");
            return;
        }
        if (!hashBlob || hashBlob->getBufferSize() == 0)
        {
            nrLog<LogLevel::error>(
                "[ShaderService::compileProgramsByFile] failed to compute backend cache key for module='{}'.",
                moduleName);
            return;
        }

        auto cacheKey = std::vector<std::byte>(hashBlob->getBufferSize());
        std::memcpy(cacheKey.data(), hashBlob->getBufferPointer(), cacheKey.size());
        auto cachePath = m_serviceConfig.persistentSpirvCache
                              ? detail::makeSpirvCachePath(spirvCacheRootLocked(), cacheKey)
                              : std::filesystem::path{};
        auto const preparedIndex = preparedPrograms.size();
        preparedPrograms.push_back(PreparedProgram{
            .requestIndices = {requestIndex},
            .memoryCacheKey = std::move(memoryCacheKey),
            .moduleName = std::move(moduleName),
            .variantLogLabel = std::move(variantLogLabel),
            .linkedProgram = std::move(linkedProgram),
            .programLayout = programLayout,
            .cacheKey = std::move(cacheKey),
            .cachePath = std::move(cachePath),
            .entryPointName = std::move(entryPointName),
            .debugName = detail::makeSlangProgramDebugNamePrefix(*modulePath, request.variant),
            .stage = stage,
        });
        preparedIndexByMemoryKey.emplace(preparedPrograms.back().memoryCacheKey, preparedIndex);
        entryHashPrepareTimer.stop();
    });
    auto const frontendFinishedAt = std::chrono::steady_clock::now();

    struct BackendCompileResult
    {
        std::shared_ptr<const std::vector<std::uint32_t>> spirv{};
        bool cacheHit = false;
        bool cacheCorrupt = false;
        PhaseDuration cacheReadWork{};
        PhaseDuration codegenWork{};
        PhaseDuration artifactWork{};
    };

    auto leaderByHash = std::map<std::string, std::size_t>{};
    std::ranges::for_each(std::views::iota(std::size_t{0}, preparedPrograms.size()), [&](std::size_t index) {
        leaderByHash.try_emplace(detail::opaqueHashHex(preparedPrograms[index].cacheKey), index);
    });

    auto futures = std::map<std::string, std::future<BackendCompileResult>>{};
    std::ranges::for_each(leaderByHash, [&](auto const &entry) {
        auto const &[hashHex, preparedIndex] = entry;
        auto *prepared = std::addressof(preparedPrograms[preparedIndex]);
        auto *linkedProgram = prepared->linkedProgram.get();
        auto cachePath = prepared->cachePath;
        auto cacheKey = prepared->cacheKey;
        auto entryPointName = prepared->entryPointName;
        auto persistentCacheEnabled = m_serviceConfig.persistentSpirvCache;
        futures.emplace(hashHex, m_backendPool->submit([linkedProgram, cachePath = std::move(cachePath),
                                                         cacheKey = std::move(cacheKey),
                                                         entryPointName = std::move(entryPointName),
                                                         persistentCacheEnabled]() mutable -> BackendCompileResult {
            auto result = BackendCompileResult{};
            if (persistentCacheEnabled)
            {
                auto cacheReadTimer = ScopedPhaseTimer{result.cacheReadWork};
                auto cached = detail::readSpirvCache(cachePath, cacheKey);
                if (cached.spirv)
                {
                    result.spirv = std::move(cached.spirv);
                    result.cacheHit = true;
                    cacheReadTimer.stop();
                    return result;
                }
                result.cacheCorrupt = cached.corrupt;
                if (result.cacheCorrupt)
                {
                    std::error_code removeError;
                    std::filesystem::remove(cachePath, removeError);
                    if (removeError)
                    {
                        nrLog<LogLevel::warning>(
                            "[ShaderService::backend] failed to remove corrupt SPIR-V cache '{}': {}",
                            cachePath.generic_string(), removeError.message());
                    }
                }
            }

            Slang::ComPtr<slang::IBlob> codeBlob;
            Slang::ComPtr<slang::IBlob> diagnostics;
            auto codegenTimer = ScopedPhaseTimer{result.codegenWork};
            try
            {
                auto compileResult =
                    linkedProgram->getEntryPointCode(0, 0, codeBlob.writeRef(), diagnostics.writeRef());
                if (diagnostics)
                {
                    auto text = std::string_view{static_cast<const char *>(diagnostics->getBufferPointer()),
                                                 diagnostics->getBufferSize()};
                    if (!text.empty())
                    {
                        nrLog<LogLevel::warning>("[ShaderService::backend] entry='{}' diagnostics:\n{}", entryPointName,
                                                  text);
                    }
                }
                if (!detail::slangSucceeded(compileResult) || !codeBlob)
                {
                    codegenTimer.stop();
                    return result;
                }
            }
            catch (...)
            {
                nrLog<LogLevel::error>(
                    "[ShaderService::backend] Slang threw during getEntryPointCode for entry='{}'.", entryPointName);
                codegenTimer.stop();
                return result;
            }
            codegenTimer.stop();

            auto artifactTimer = ScopedPhaseTimer{result.artifactWork};
            auto spirv = detail::copySpirvBlob(*codeBlob);
            if (!spirv)
            {
                nrLog<LogLevel::error>("[ShaderService::backend] entry='{}' produced invalid SPIR-V.",
                                        entryPointName);
                artifactTimer.stop();
                return result;
            }
            if (persistentCacheEnabled && !detail::writeSpirvCache(cachePath, cacheKey, *spirv))
            {
                nrLog<LogLevel::warning>("[ShaderService::backend] failed to persist SPIR-V cache '{}'.",
                                          cachePath.generic_string());
            }
            result.spirv = std::move(spirv);
            artifactTimer.stop();
            return result;
        }));
    });

    auto backendResults = std::map<std::string, BackendCompileResult>{};
    std::ranges::for_each(futures, [&](auto &entry) { backendResults.emplace(entry.first, entry.second.get()); });
    auto const backendFinishedAt = std::chrono::steady_clock::now();
    phaseTimings.backendWall = std::chrono::duration_cast<PhaseDuration>(backendFinishedAt - frontendFinishedAt);

    auto diskHitCount = std::size_t{0};
    auto backendCompileCount = std::size_t{0};
    auto corruptCount = std::size_t{0};
    auto publishTimer = ScopedPhaseTimer{phaseTimings.publish};
    std::ranges::for_each(backendResults, [&](auto const &entry) {
        diskHitCount += entry.second.cacheHit ? 1u : 0u;
        backendCompileCount += !entry.second.cacheHit && entry.second.spirv ? 1u : 0u;
        corruptCount += entry.second.cacheCorrupt ? 1u : 0u;
        phaseTimings.backendCacheReadWorkSum += entry.second.cacheReadWork;
        phaseTimings.backendCacheReadWorkMax =
            std::max(phaseTimings.backendCacheReadWorkMax, entry.second.cacheReadWork);
        phaseTimings.backendCodegenWorkSum += entry.second.codegenWork;
        phaseTimings.backendCodegenWorkMax = std::max(phaseTimings.backendCodegenWorkMax, entry.second.codegenWork);
        phaseTimings.backendArtifactWorkSum += entry.second.artifactWork;
        phaseTimings.backendArtifactWorkMax = std::max(phaseTimings.backendArtifactWorkMax, entry.second.artifactWork);
    });

    std::ranges::for_each(preparedPrograms, [&](PreparedProgram &prepared) {
        auto hashHex = detail::opaqueHashHex(prepared.cacheKey);
        auto backend = backendResults.find(hashHex);
        if (backend == backendResults.end() || !backend->second.spirv)
        {
            nrLog<LogLevel::error>(
                "[ShaderService::compileProgramsByFile] backend failed for module='{}', entry='{}', variant='{}'.",
                prepared.moduleName, prepared.entryPointName, prepared.variantLogLabel);
            return;
        }

        auto state = std::make_shared<SlangProgram::State>();
        state->linkedProgram = std::move(prepared.linkedProgram);
        state->programLayout = prepared.programLayout;
        state->entryPoint = SlangEntryPointData{
            .entryPointName = std::move(prepared.entryPointName),
            .debugName = std::move(prepared.debugName),
            .stage = prepared.stage,
            .spirv = backend->second.spirv,
        };
        SlangProgram program;
        program.state_ = std::move(state);
        std::ranges::for_each(prepared.requestIndices,
                              [&](std::size_t requestIndex) { programs[requestIndex] = program; });
        m_linkedProgramCache.insert_or_assign(prepared.memoryCacheKey, std::move(program));
    });
    auto validProgramCount = std::ranges::count_if(programs, &SlangProgram::valid);
    auto invalidProgramCount = programs.size() - validProgramCount;
    publishTimer.stop();

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startedAt);
    auto frontendElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(frontendFinishedAt - startedAt);
    auto backendElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(backendFinishedAt - frontendFinishedAt);
    m_lastCompileBatchStats = ShaderCompileBatchStats{
        .requestCount = requests.size(),
        .memoryHitCount = memoryHitCount,
        .persistentHitCount = diskHitCount,
        .backendCompilationCount = backendCompileCount,
        .corruptCacheEntryCount = corruptCount,
        .workerCount = m_serviceConfig.backendWorkerCount,
        .frontendElapsed = frontendElapsed,
        .backendElapsed = backendElapsed,
        .elapsed = elapsed,
    };
    nrLog<LogLevel::info>("[ShaderService::compileProgramsByFile] requests={}, memoryHits={}, diskHits={}, "
            "backendCompiles={}, corruptEntries={}, workers={}, frontendMs={}, backendMs={}, elapsedMs={}, "
            "prepared={}, variantRequests={}, valid={}, invalid={}, phaseMs={{configure={:.3f}, "
            "resolveCache={:.3f}, moduleLoad={:.3f}, entryPointLoad={:.3f}, variantModule={:.3f}, "
            "compose={:.3f}, link={:.3f}, reflection={:.3f}, entryHashPrepare={:.3f}, "
            "backendWall={:.3f}, backendCacheReadWorkSum={:.3f}, backendCacheReadWorkMax={:.3f}, "
            "backendCodegenWorkSum={:.3f}, backendCodegenWorkMax={:.3f}, "
            "backendArtifactWorkSum={:.3f}, backendArtifactWorkMax={:.3f}, publish={:.3f}}}",
            requests.size(), memoryHitCount, diskHitCount, backendCompileCount, corruptCount,
            m_serviceConfig.backendWorkerCount, frontendElapsed.count(), backendElapsed.count(), elapsed.count(),
            preparedPrograms.size(), variantRequestCount, validProgramCount, invalidProgramCount,
            phaseTimings.configure.count(), phaseTimings.resolveCache.count(), phaseTimings.moduleLoad.count(),
            phaseTimings.entryPointLoad.count(), phaseTimings.variantModule.count(), phaseTimings.compose.count(),
            phaseTimings.link.count(), phaseTimings.reflection.count(), phaseTimings.entryHashPrepare.count(),
            phaseTimings.backendWall.count(), phaseTimings.backendCacheReadWorkSum.count(),
            phaseTimings.backendCacheReadWorkMax.count(), phaseTimings.backendCodegenWorkSum.count(),
            phaseTimings.backendCodegenWorkMax.count(), phaseTimings.backendArtifactWorkSum.count(),
            phaseTimings.backendArtifactWorkMax.count(), phaseTimings.publish.count());
    return programs;
}

void ShaderService::enqueueModuleCacheWrite(std::vector<std::byte> bytes, std::filesystem::path moduleBlobPath)
{
    ensureBackendPoolLocked();
    [[maybe_unused]] auto writeFuture =
        m_backendPool->submit([bytes = std::move(bytes), moduleBlobPath = std::move(moduleBlobPath)]() {
            if (detail::writeBytesAtomically(moduleBlobPath, bytes) == detail::AtomicWriteResult::failed)
            {
                nrLog<LogLevel::warning>("[ShaderService::moduleCache] failed to persist '{}'.",
                                          moduleBlobPath.generic_string());
            }
        });
}

void ShaderService::ensureGlobalSessionLocked()
{
    if (!m_globalSession)
    {
        try
        {
            auto result = slang::createGlobalSession(m_globalSession.writeRef());
            nrAssert(detail::slangSucceeded(result), "Failed to create Slang global session.");
        }
        catch (...)
        {
            nrLog<nr::LogLevel::warning>(
                "[ShaderService::ensureGlobalSessionLocked] Slang threw an internal exception during "
                "createGlobalSession. "
                "Attach a debugger and break on Slang::InternalError to inspect the Message field.");
            nrAssert(false, "Slang::createGlobalSession threw an internal exception.");
        }
    }
}

void ShaderService::ensureConfiguredLocked()
{
    if (!m_session)
    {
        applyCompileOptionsLocked(kDefaultSlangCompileOptions);
    }
    ensureBackendPoolLocked();
}

void ShaderService::ensureBackendPoolLocked()
{
    if (!m_backendPool)
    {
        m_backendPool = std::make_unique<nr::threading::StaticThreadPool>();
    }
    m_backendPool->ensureWorkerCount(m_serviceConfig.backendWorkerCount);
}

[[nodiscard]] std::string_view ShaderService::optionsHashLocked() const noexcept
{
    return m_optionsHashHex;
}

[[nodiscard]] std::filesystem::path ShaderService::moduleCacheRootLocked() const
{
    return std::filesystem::path(std::string(shaderCacheRoot)) / std::string(optionsHashLocked());
}

[[nodiscard]] std::filesystem::path ShaderService::spirvCacheRootLocked() const
{
    return moduleCacheRootLocked() / "spirv" / "v1";
}

void ShaderService::invalidateStaleModuleCacheLocked(std::string_view modulePath,
                                                     const std::filesystem::path &moduleBlobPath)
{
    auto removeStaleCache = [&]() {
        std::error_code removeError;
        auto removed = std::filesystem::remove(moduleBlobPath, removeError);
        if (removeError)
        {
            nrLog<nr::LogLevel::warning>(
                "[ShaderService::invalidateStaleModuleCacheLocked] stale cache removal failed: path='{}', error='{}'",
                moduleBlobPath.generic_string(), removeError.message());
            return;
        }
        if (removed)
        {
            nrLog<LogLevel::info>("[ShaderService::invalidateStaleModuleCacheLocked] removed stale cache blob: path='{}'",
                    moduleBlobPath.generic_string());
        }
    };

    auto binaryBytes = detail::readBinaryFile(moduleBlobPath);
    if (binaryBytes.empty())
    {
        removeStaleCache();
        return;
    }

    Slang::ComPtr<slang::IBlob> binaryBlob(slang_createBlob(binaryBytes.data(), binaryBytes.size()));
    if (!binaryBlob)
    {
        removeStaleCache();
        return;
    }

    auto const sourceModulePath = std::string(modulePath) + ".slang";
    auto isUpToDate = false;
    try
    {
        isUpToDate = m_session->isBinaryModuleUpToDate(sourceModulePath.c_str(), binaryBlob.get());
    }
    catch (...)
    {
        nrLog<LogLevel::warning>(
            "[ShaderService::invalidateStaleModuleCacheLocked] Slang threw while validating '{}'.",
            moduleBlobPath.generic_string());
        nrAssert(false, "Slang module-cache validation threw an internal exception.");
    }
    if (isUpToDate)
    {
        return;
    }

    removeStaleCache();
}

void ShaderService::recreateSessionLocked()
{
    if (m_backendPool)
    {
        m_backendPool->waitIdle();
    }
    m_linkedProgramCache.clear();
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
            .value =
                slang::CompilerOptionValue{
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
    sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_ROW_MAJOR;
    sessionDesc.targets = &m_targetDesc;
    sessionDesc.targetCount = 1;
    sessionDesc.searchPaths = m_searchPathPointers.empty() ? nullptr : m_searchPathPointers.data();
    sessionDesc.searchPathCount = static_cast<SlangInt>(m_searchPathPointers.size());
    sessionDesc.preprocessorMacros = m_macroDescs.empty() ? nullptr : m_macroDescs.data();
    sessionDesc.preprocessorMacroCount = static_cast<SlangInt>(m_macroDescs.size());
    sessionDesc.compilerOptionEntries = m_compilerOptionEntries.empty() ? nullptr : m_compilerOptionEntries.data();
    sessionDesc.compilerOptionEntryCount = static_cast<std::uint32_t>(m_compilerOptionEntries.size());

    try
    {
        auto result = m_globalSession->createSession(sessionDesc, m_session.writeRef());
        nrAssert(detail::slangSucceeded(result), "Failed to create Slang session.");
        ++m_sessionGeneration;
    }
    catch (...)
    {
        nrLog<nr::LogLevel::warning>(
            "[ShaderService::recreateSessionLocked] Slang threw an internal exception during createSession. "
            "Attach a debugger and break on Slang::InternalError to inspect the Message field.");
        nrAssert(false, "Slang::IGlobalSession::createSession threw an internal exception.");
    }
}

void ShaderService::emitDiagnosticsLocked(slang::IBlob *diagnostics, std::string_view context) const
{
    if (!diagnostics)
    {
        return;
    }

    auto text =
        std::string_view(static_cast<const char *>(diagnostics->getBufferPointer()), diagnostics->getBufferSize());

    if (!text.empty())
    {
        nrLog<nr::LogLevel::warning>("[Slang:{}]\n{}", context, text);
    }
}

[[nodiscard]] std::string ShaderService::resolveModuleNameLocked(std::string_view modulePath) const
{
    auto derivedModuleName = detail::modulePathToName(modulePath);
    if (derivedModuleName.empty())
    {
        nrLog<nr::LogLevel::warning>(
            "[ShaderService::resolveModuleNameLocked] unable to derive module name from modulePath='{}'.",
            std::string(modulePath));
        return {};
    }

    auto sourcePath =
        detail::normalizePath(m_shaderRootPath / (detail::moduleNameToPath(derivedModuleName) + ".slang"));
    auto sourceText = detail::readTextFile(sourcePath);
    if (auto declaredInSource = detail::extractDeclaredModuleNameFromSource(sourceText); declaredInSource.has_value())
    {
        auto declaredModule = *declaredInSource;
        auto expectedLeaf = detail::moduleLeafName(derivedModuleName);
        auto declaredMatches = false;

        // Slang source may use leaf declaration (`module useFlag;`) while runtime
        // identity is full path-derived module name (`renderer.pathTracing.core`).
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
            nrLog<nr::LogLevel::warning>(
                "[ShaderService::resolveModuleNameLocked] module declaration mismatch for "
                "modulePath='{}': declared='{}', expected='{}' (leaf='{}').",
                std::string(modulePath), declaredModule, derivedModuleName, expectedLeaf);
            return {};
        }
    }
    return derivedModuleName;
}

Slang::ComPtr<slang::IModule> ShaderService::loadOrCompileModuleLocked(const std::string &moduleName)
{
    auto normalizedModulePath = detail::moduleNameToPath(moduleName);

    auto cacheRootPath = detail::normalizePath(moduleCacheRootLocked());
    auto moduleBlobPath = detail::makeModuleBinaryPath(cacheRootPath, moduleName);
    std::error_code moduleBlobEc;

    auto cacheExists = std::filesystem::exists(moduleBlobPath, moduleBlobEc);
    if (!moduleBlobEc && cacheExists)
    {
        invalidateStaleModuleCacheLocked(normalizedModulePath, moduleBlobPath);
    }

    Slang::ComPtr<slang::IBlob> diagnostics;
    Slang::ComPtr<slang::IModule> loadedModule;
    diagnostics = nullptr;
    try
    {
        loadedModule =
            Slang::ComPtr<slang::IModule>(m_session->loadModule(normalizedModulePath.c_str(), diagnostics.writeRef()));
    }
    catch (...)
    {
        nrLog<nr::LogLevel::warning>(
            "[ShaderService::loadOrCompileModuleLocked] Slang threw an internal exception during "
            "loadModule for module='{}' path='{}'. "
            "Attach a debugger and break on Slang::InternalError to inspect the Message field.",
            moduleName, normalizedModulePath);
        emitDiagnosticsLocked(diagnostics.get(), "loadModule(exception-path)");
        nrAssert(false, "Slang::ISession::loadModule threw an internal exception.");
    }
    emitDiagnosticsLocked(diagnostics.get(), "loadModule(module-path)");

    if (!loadedModule)
    {
        nrLog<LogLevel::warning>(
            "[ShaderService::loadOrCompileModuleLocked] failed to load module='{}' via "
            "loadModule(module-path='{}'). Expected slash form like 'renderer/appUi/vertex'.",
            moduleName, normalizedModulePath);
        return {};
    }

    std::error_code cacheStateError;
    auto hasFreshModuleCache = std::filesystem::exists(moduleBlobPath, cacheStateError) && !cacheStateError;
    if (!hasFreshModuleCache)
    {
        Slang::ComPtr<slang::IBlob> serializedModule;
        try
        {
            auto serializeResult = loadedModule->serialize(serializedModule.writeRef());
            if (!detail::slangSucceeded(serializeResult) || !serializedModule)
            {
                nrLog<LogLevel::warning>(
                    "[ShaderService::loadOrCompileModuleLocked] failed to serialize module cache for module='{}'.",
                    moduleName);
            }
        }
        catch (...)
        {
            nrLog<LogLevel::warning>(
                "[ShaderService::loadOrCompileModuleLocked] Slang threw while serializing module='{}'.", moduleName);
            nrAssert(false, "Slang module serialization threw an internal exception.");
        }

        if (serializedModule && serializedModule->getBufferSize() > 0)
        {
            auto serializedBytes = std::vector<std::byte>(serializedModule->getBufferSize());
            std::memcpy(serializedBytes.data(), serializedModule->getBufferPointer(), serializedBytes.size());
            enqueueModuleCacheWrite(std::move(serializedBytes), moduleBlobPath);
        }
    }

    return loadedModule;
}
} // namespace nr::rhi
