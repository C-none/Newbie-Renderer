#include <slang-com-ptr.h>
#include <slang.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
struct Options
{
    std::filesystem::path shaderRoot{};
    std::filesystem::path shareRoot{};
    std::filesystem::path output{};
    std::string rootModule{};
    std::string moduleName{};
    std::string namespaceName{};
};

struct SourceSite
{
    std::filesystem::path path{};
    std::string normalizedPath{};
    SlangInt line = -1;
    SlangInt column = -1;
};

enum class CppTypeKind : std::uint8_t
{
    scalar,
    enumType,
    structType,
    vector,
};

struct CppType
{
    std::string name{};
    CppTypeKind kind = CppTypeKind::scalar;
    slang::TypeReflection::ScalarType scalarType = slang::TypeReflection::ScalarType::None;
};

struct EnumCase
{
    std::string name{};
    std::uint32_t value = 0;
};

struct EnumInfo
{
    std::string name{};
    SourceSite site{};
    bool flags = false;
    std::vector<EnumCase> cases{};
};

struct ConstantInfo
{
    std::string name{};
    CppType type{};
    SourceSite site{};
    std::string value{};
};

struct FieldInfo
{
    std::string name{};
    CppType type{};
    std::optional<std::string> defaultValue{};
    std::size_t offset = 0;
};

struct StructInfo
{
    std::string name{};
    SourceSite site{};
    std::size_t size = 0;
    std::size_t alignment = 0;
    std::vector<FieldInfo> fields{};
};

struct ShareDecl
{
    slang::DeclReflection* decl = nullptr;
    SourceSite site{};
};

struct Model
{
    std::vector<ShareDecl> declarations{};
    std::vector<ConstantInfo> constants{};
    std::vector<EnumInfo> enums{};
    std::vector<StructInfo> structs{};
    std::set<std::string> enumNames{};
    std::set<std::string> structNames{};
};

[[nodiscard]] bool failed(SlangResult result) noexcept
{
    return SLANG_FAILED(result);
}

[[nodiscard]] std::string blobText(slang::IBlob* blob)
{
    if (!blob || !blob->getBufferPointer() || blob->getBufferSize() == 0)
    {
        return {};
    }

    return std::string(static_cast<const char*>(blob->getBufferPointer()), blob->getBufferSize());
}

[[nodiscard]] std::string trim(std::string_view text)
{
    auto begin = std::size_t{0};
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0)
    {
        ++begin;
    }

    auto end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0)
    {
        --end;
    }

    return std::string{text.substr(begin, end - begin)};
}

[[nodiscard]] std::filesystem::path normalizedPath(std::filesystem::path path)
{
    auto ec = std::error_code{};
    auto result = std::filesystem::weakly_canonical(path, ec);
    if (ec)
    {
        ec.clear();
        result = std::filesystem::absolute(path, ec);
    }
    if (ec)
    {
        result = std::move(path);
    }
    return result.lexically_normal();
}

[[nodiscard]] std::string genericPathString(const std::filesystem::path& path)
{
    return path.generic_string();
}

[[nodiscard]] bool pathIsUnder(const std::filesystem::path& path, const std::filesystem::path& root)
{
    auto ec = std::error_code{};
    auto relative = std::filesystem::relative(path, root, ec);
    if (ec || relative.empty())
    {
        return false;
    }

    auto iterator = relative.begin();
    return iterator != relative.end() && *iterator != "..";
}

[[nodiscard]] std::string locationText(const SourceSite& site)
{
    auto stream = std::ostringstream{};
    stream << site.normalizedPath;
    if (site.line >= 0)
    {
        stream << ':' << site.line;
        if (site.column >= 0)
        {
            stream << ':' << site.column;
        }
    }
    return stream.str();
}

[[nodiscard]] std::string declName(slang::DeclReflection* decl)
{
    if (!decl || !decl->getName())
    {
        return {};
    }
    return decl->getName();
}

[[nodiscard]] std::string typeName(slang::TypeReflection* type)
{
    if (!type || !type->getName())
    {
        return {};
    }

    auto name = std::string{type->getName()};
    auto const dotPos = name.find_last_of('.');
    auto const colonPos = name.rfind("::");
    auto splitPos = std::string::npos;
    if (dotPos != std::string::npos)
    {
        splitPos = dotPos;
    }
    if (colonPos != std::string::npos)
    {
        splitPos = splitPos == std::string::npos ? colonPos + 1 : std::max(splitPos, colonPos + 1);
    }
    if (splitPos != std::string::npos)
    {
        name.erase(0, splitPos + 1);
    }
    return name;
}

[[nodiscard]] std::optional<SourceSite> sourceSite(slang::ISession* session, slang::DeclReflection* decl)
{
    auto location = slang::SourceLocation{};
    if (!session || !decl || failed(session->getDeclSourceLocation(decl, &location)) || !location.filePath)
    {
        return std::nullopt;
    }

    auto path = normalizedPath(std::filesystem::path{location.filePath});
    return SourceSite{
        .path = path,
        .normalizedPath = genericPathString(path),
        .line = location.line,
        .column = location.column,
    };
}

[[nodiscard]] std::vector<std::string> readLines(const std::filesystem::path& path)
{
    auto input = std::ifstream{path};
    auto result = std::vector<std::string>{};
    auto line = std::string{};
    while (std::getline(input, line))
    {
        result.push_back(line);
    }
    return result;
}

[[nodiscard]] bool hasFlagsAttribute(
    const SourceSite& site,
    std::map<std::string, std::vector<std::string>>& sourceLineCache)
{
    if (site.line <= 0)
    {
        return false;
    }

    auto [it, inserted] = sourceLineCache.try_emplace(site.normalizedPath);
    if (inserted)
    {
        it->second = readLines(site.path);
    }

    auto const& lines = it->second;
    auto lineIndex = static_cast<std::size_t>(site.line - 1);
    if (lineIndex > lines.size())
    {
        lineIndex = lines.size();
    }

    auto checked = std::size_t{0};
    while (lineIndex > 0 && checked < 8)
    {
        --lineIndex;
        ++checked;

        auto text = trim(lines[lineIndex]);
        if (text.empty() || text.starts_with("//"))
        {
            continue;
        }
        if (text.find("[Flags]") != std::string::npos)
        {
            return true;
        }
        if (text.starts_with('['))
        {
            continue;
        }
        break;
    }

    return false;
}

[[nodiscard]] bool sourceLineContains(
    const SourceSite& site,
    std::map<std::string, std::vector<std::string>>& sourceLineCache,
    std::string_view needle)
{
    if (site.line <= 0)
    {
        return false;
    }

    auto [it, inserted] = sourceLineCache.try_emplace(site.normalizedPath);
    if (inserted)
    {
        it->second = readLines(site.path);
    }

    auto const lineIndex = static_cast<std::size_t>(site.line - 1);
    return lineIndex < it->second.size() && it->second[lineIndex].find(needle) != std::string::npos;
}

[[nodiscard]] std::string scalarCppName(slang::TypeReflection::ScalarType scalarType)
{
    switch (scalarType)
    {
    case slang::TypeReflection::ScalarType::Int32: return "std::int32_t";
    case slang::TypeReflection::ScalarType::UInt32: return "std::uint32_t";
    case slang::TypeReflection::ScalarType::Int64: return "std::int64_t";
    case slang::TypeReflection::ScalarType::UInt64: return "std::uint64_t";
    case slang::TypeReflection::ScalarType::Float32: return "float";
    default: return {};
    }
}

[[nodiscard]] std::string vectorCppName(slang::TypeReflection::ScalarType scalarType, std::size_t count)
{
    if (count < 2 || count > 4)
    {
        return {};
    }

    auto suffix = std::to_string(count);
    switch (scalarType)
    {
    case slang::TypeReflection::ScalarType::Float32: return "glm::vec" + suffix;
    case slang::TypeReflection::ScalarType::UInt32: return "glm::uvec" + suffix;
    case slang::TypeReflection::ScalarType::Int32: return "glm::ivec" + suffix;
    default: return {};
    }
}

[[nodiscard]] std::optional<CppType> mapType(
    slang::TypeReflection* type,
    const Model& model,
    std::string& error)
{
    if (!type)
    {
        error = "missing Slang type reflection";
        return std::nullopt;
    }

    auto const kind = type->getKind();
    if (kind == slang::TypeReflection::Kind::Scalar)
    {
        auto const scalarType = type->getScalarType();
        auto name = scalarCppName(scalarType);
        if (name.empty())
        {
            error = "unsupported scalar type";
            return std::nullopt;
        }
        return CppType{.name = std::move(name), .kind = CppTypeKind::scalar, .scalarType = scalarType};
    }

    if (kind == slang::TypeReflection::Kind::Vector)
    {
        auto* elementType = type->getElementType();
        auto const scalarType = elementType ? elementType->getScalarType() : slang::TypeReflection::ScalarType::None;
        auto const count = type->getElementCount();
        auto name = vectorCppName(scalarType, count);
        if (name.empty())
        {
            error = "unsupported vector type";
            return std::nullopt;
        }
        return CppType{.name = std::move(name), .kind = CppTypeKind::vector, .scalarType = scalarType};
    }

    if (kind == slang::TypeReflection::Kind::Enum)
    {
        auto name = typeName(type);
        if (!model.enumNames.contains(name))
        {
            error = "enum type '" + name + "' is not declared in shader/include/share";
            return std::nullopt;
        }
        return CppType{.name = std::move(name), .kind = CppTypeKind::enumType};
    }

    if (kind == slang::TypeReflection::Kind::Struct)
    {
        auto name = typeName(type);
        if (!model.structNames.contains(name))
        {
            error = "struct type '" + name + "' is not declared in shader/include/share";
            return std::nullopt;
        }
        return CppType{.name = std::move(name), .kind = CppTypeKind::structType};
    }

    error = "unsupported non-ABI type";
    return std::nullopt;
}

[[nodiscard]] std::string formatFloat(float value)
{
    auto stream = std::ostringstream{};
    stream << std::setprecision(9) << value;
    auto text = stream.str();
    if (text.find_first_of(".eE") == std::string::npos)
    {
        text += ".0";
    }
    text += "f";
    return text;
}

[[nodiscard]] std::optional<std::string> defaultLiteral(
    slang::VariableReflection* variable,
    const CppType& type,
    const std::vector<EnumInfo>& enums)
{
    if (!variable || !variable->hasDefaultValue())
    {
        return std::nullopt;
    }

    if (type.kind == CppTypeKind::scalar)
    {
        if (type.scalarType == slang::TypeReflection::ScalarType::Float32)
        {
            auto value = 0.0f;
            if (failed(variable->getDefaultValueFloat(&value)))
            {
                return std::nullopt;
            }
            return formatFloat(value);
        }

        auto value = std::int64_t{0};
        if (failed(variable->getDefaultValueInt(&value)))
        {
            return std::nullopt;
        }

        if (type.scalarType == slang::TypeReflection::ScalarType::UInt32)
        {
            if (value < 0)
            {
                return std::nullopt;
            }
            return std::to_string(static_cast<std::uint32_t>(value)) + "u";
        }
        if (type.scalarType == slang::TypeReflection::ScalarType::UInt64)
        {
            if (value < 0)
            {
                return std::nullopt;
            }
            return std::to_string(static_cast<std::uint64_t>(value)) + "ull";
        }
        return std::to_string(value);
    }

    if (type.kind == CppTypeKind::enumType)
    {
        auto value = std::int64_t{0};
        if (failed(variable->getDefaultValueInt(&value)) || value < 0)
        {
            return std::nullopt;
        }

        auto const enumValue = static_cast<std::uint32_t>(value);
        auto const enumIt = std::ranges::find_if(enums, [&](const EnumInfo& info) {
            return info.name == type.name;
        });
        if (enumIt != enums.end())
        {
            auto const caseIt = std::ranges::find_if(enumIt->cases, [&](const EnumCase& enumCase) {
                return enumCase.value == enumValue;
            });
            if (caseIt != enumIt->cases.end())
            {
                return type.name + "::" + caseIt->name;
            }
        }
        return "static_cast<" + type.name + ">(" + std::to_string(enumValue) + "u)";
    }

    return std::nullopt;
}

[[nodiscard]] bool parseOptions(int argc, char** argv, Options& options)
{
    auto takeValue = [&](int& index, std::string_view optionName) -> const char* {
        if (index + 1 >= argc)
        {
            std::cerr << optionName << " requires a value.\n";
            return nullptr;
        }
        ++index;
        return argv[index];
    };

    for (auto index = 1; index < argc; ++index)
    {
        auto argument = std::string_view{argv[index]};
        if (argument == "--shader-root")
        {
            if (auto value = takeValue(index, argument))
            {
                options.shaderRoot = value;
            }
            else
            {
                return false;
            }
        }
        else if (argument == "--share-root")
        {
            if (auto value = takeValue(index, argument))
            {
                options.shareRoot = value;
            }
            else
            {
                return false;
            }
        }
        else if (argument == "--root-module")
        {
            if (auto value = takeValue(index, argument))
            {
                options.rootModule = value;
            }
            else
            {
                return false;
            }
        }
        else if (argument == "--output")
        {
            if (auto value = takeValue(index, argument))
            {
                options.output = value;
            }
            else
            {
                return false;
            }
        }
        else if (argument == "--module")
        {
            if (auto value = takeValue(index, argument))
            {
                options.moduleName = value;
            }
            else
            {
                return false;
            }
        }
        else if (argument == "--namespace")
        {
            if (auto value = takeValue(index, argument))
            {
                options.namespaceName = value;
            }
            else
            {
                return false;
            }
        }
        else
        {
            std::cerr << "Unknown argument: " << argument << "\n";
            return false;
        }
    }

    if (options.shaderRoot.empty() || options.shareRoot.empty() || options.rootModule.empty() ||
        options.output.empty() || options.moduleName.empty() || options.namespaceName.empty())
    {
        std::cerr << "Missing required arguments.\n";
        return false;
    }

    options.shaderRoot = normalizedPath(options.shaderRoot);
    options.shareRoot = normalizedPath(options.shareRoot);
    options.output = normalizedPath(options.output);
    return true;
}

[[nodiscard]] Slang::ComPtr<slang::ISession> createSession(const Options& options)
{
    Slang::ComPtr<slang::IGlobalSession> globalSession;
    if (failed(slang::createGlobalSession(globalSession.writeRef())) || !globalSession)
    {
        std::cerr << "Failed to create Slang global session.\n";
        return {};
    }

    auto shaderRootString = genericPathString(options.shaderRoot);
    auto searchPaths = std::vector<const char*>{shaderRootString.c_str()};

    auto targetDesc = slang::TargetDesc{};
    targetDesc.format = SLANG_SPIRV;
    targetDesc.profile = globalSession->findProfile("SPIRV_1_6");
    targetDesc.forceGLSLScalarBufferLayout = true;

    auto sessionDesc = slang::SessionDesc{};
    sessionDesc.targets = &targetDesc;
    sessionDesc.targetCount = 1;
    sessionDesc.searchPaths = searchPaths.data();
    sessionDesc.searchPathCount = static_cast<SlangInt>(searchPaths.size());

    Slang::ComPtr<slang::ISession> session;
    try
    {
        if (failed(globalSession->createSession(sessionDesc, session.writeRef())) || !session)
        {
            std::cerr << "Failed to create Slang session.\n";
            return {};
        }
    }
    catch (...)
    {
        std::cerr << "Slang threw while creating the shader share reflection session.\n";
        return {};
    }

    return session;
}

[[nodiscard]] Slang::ComPtr<slang::IModule> loadRootModule(slang::ISession* session, const std::string& rootModule)
{
    Slang::ComPtr<slang::IBlob> diagnostics;
    Slang::ComPtr<slang::IModule> module;
    try
    {
        module = Slang::ComPtr<slang::IModule>(session->loadModule(rootModule.c_str(), diagnostics.writeRef()));
    }
    catch (...)
    {
        std::cerr << "Slang threw while loading root module '" << rootModule << "'.\n";
        auto diagnosticsText = blobText(diagnostics.get());
        if (!diagnosticsText.empty())
        {
            std::cerr << diagnosticsText << "\n";
        }
        return {};
    }

    auto diagnosticsText = blobText(diagnostics.get());
    if (!diagnosticsText.empty())
    {
        std::cerr << diagnosticsText << "\n";
    }

    if (!module)
    {
        std::cerr << "Failed to load root module '" << rootModule << "'.\n";
    }
    return module;
}

[[nodiscard]] bool collectShareDeclarationsFromModule(
    slang::ISession* session,
    slang::DeclReflection* moduleDecl,
    const std::filesystem::path& shareRoot,
    Model& model,
    std::set<std::string>& seenDeclarations)
{
    if (!moduleDecl)
    {
        return false;
    }

    for (auto childIndex = 0u; childIndex < moduleDecl->getChildrenCount(); ++childIndex)
    {
        auto* child = moduleDecl->getChild(childIndex);
        auto site = sourceSite(session, child);
        auto const kind = child->getKind();
        auto const name = declName(child);
        if (site.has_value() && pathIsUnder(site->path, shareRoot) &&
            (kind != slang::DeclReflection::Kind::Unsupported || !name.empty()))
        {
            auto key = locationText(*site) + ":" + name;
            if (!seenDeclarations.insert(key).second)
            {
                continue;
            }

            model.declarations.push_back(ShareDecl{.decl = child, .site = *site});
            if (kind == slang::DeclReflection::Kind::Enum)
            {
                model.enumNames.insert(name);
            }
            else if (kind == slang::DeclReflection::Kind::Struct)
            {
                model.structNames.insert(name);
            }

            continue;
        }

        if (kind != slang::DeclReflection::Kind::Struct && kind != slang::DeclReflection::Kind::Enum &&
            kind != slang::DeclReflection::Kind::Variable && kind != slang::DeclReflection::Kind::Func)
        {
            if (!collectShareDeclarationsFromModule(session, child, shareRoot, model, seenDeclarations))
            {
                return false;
            }
        }
    }

    return true;
}

[[nodiscard]] bool collectShareDeclarations(
    slang::ISession* session,
    const std::filesystem::path& shareRoot,
    Model& model)
{
    auto seenDeclarations = std::set<std::string>{};
    auto const moduleCount = session->getLoadedModuleCount();
    for (auto moduleIndex = SlangInt{0}; moduleIndex < moduleCount; ++moduleIndex)
    {
        auto* module = session->getLoadedModule(moduleIndex);
        if (!module)
        {
            continue;
        }
        if (!collectShareDeclarationsFromModule(
                session,
                module->getModuleReflection(),
                shareRoot,
                model,
                seenDeclarations))
        {
            std::cerr << "A loaded Slang module did not expose declaration reflection.\n";
            return false;
        }
    }

    if (model.declarations.empty())
    {
        std::cerr << "No declarations from shader/include/share were found in the root module.\n";
        return false;
    }

    return true;
}

[[nodiscard]] bool buildEnumInfo(
    const ShareDecl& shareDecl,
    std::map<std::string, std::vector<std::string>>& sourceLineCache,
    EnumInfo& result)
{
    auto* type = shareDecl.decl->getType();
    if (!type || type->getKind() != slang::TypeReflection::Kind::Enum)
    {
        std::cerr << locationText(shareDecl.site) << ": enum declaration has no enum type reflection.\n";
        return false;
    }
    if (!sourceLineContains(shareDecl.site, sourceLineCache, ": uint"))
    {
        std::cerr << locationText(shareDecl.site) << ": only enum : uint is supported in shader/include/share.\n";
        return false;
    }

    result.name = declName(shareDecl.decl);
    result.site = shareDecl.site;
    result.flags = hasFlagsAttribute(shareDecl.site, sourceLineCache);

    for (auto fieldIndex = 0u; fieldIndex < type->getFieldCount(); ++fieldIndex)
    {
        auto* field = type->getFieldByIndex(fieldIndex);
        if (!field || !field->getName())
        {
            std::cerr << locationText(shareDecl.site) << ": enum has an unnamed case.\n";
            return false;
        }

        auto value = std::int64_t{0};
        if (failed(field->getDefaultValueInt(&value)) || value < 0)
        {
            std::cerr << locationText(shareDecl.site) << ": enum case '" << field->getName()
                      << "' needs a non-negative integer value.\n";
            return false;
        }
        if (static_cast<std::uint64_t>(value) > std::numeric_limits<std::uint32_t>::max())
        {
            std::cerr << locationText(shareDecl.site) << ": enum case '" << field->getName()
                      << "' exceeds uint32.\n";
            return false;
        }

        result.cases.push_back(EnumCase{
            .name = field->getName(),
            .value = static_cast<std::uint32_t>(value),
        });
    }

    return true;
}

[[nodiscard]] bool buildConstantInfo(const ShareDecl& shareDecl, const Model& model, ConstantInfo& result)
{
    auto* variable = shareDecl.decl->asVariable();
    if (!variable)
    {
        std::cerr << locationText(shareDecl.site) << ": variable declaration has no variable reflection.\n";
        return false;
    }
    if (!variable->findModifier(slang::Modifier::Static) || !variable->findModifier(slang::Modifier::Const))
    {
        std::cerr << locationText(shareDecl.site) << ": only public static const variables are allowed in shader/include/share.\n";
        return false;
    }

    auto error = std::string{};
    auto type = mapType(variable->getType(), model, error);
    if (!type.has_value())
    {
        std::cerr << locationText(shareDecl.site) << ": constant '" << declName(shareDecl.decl)
                  << "' has unsupported type: " << error << ".\n";
        return false;
    }

    auto value = defaultLiteral(variable, *type, model.enums);
    if (!value.has_value())
    {
        std::cerr << locationText(shareDecl.site) << ": constant '" << declName(shareDecl.decl)
                  << "' needs a scalar or enum initializer Slang reflection can resolve.\n";
        return false;
    }

    result = ConstantInfo{
        .name = declName(shareDecl.decl),
        .type = *type,
        .site = shareDecl.site,
        .value = *value,
    };
    return true;
}

[[nodiscard]] bool buildStructInfo(
    slang::ISession* session,
    const ShareDecl& shareDecl,
    const Model& model,
    StructInfo& result)
{
    auto* type = shareDecl.decl->getType();
    if (!type || type->getKind() != slang::TypeReflection::Kind::Struct)
    {
        std::cerr << locationText(shareDecl.site) << ": struct declaration has no struct type reflection.\n";
        return false;
    }

    Slang::ComPtr<slang::IBlob> diagnostics;
    slang::TypeLayoutReflection* typeLayout = nullptr;
    try
    {
        typeLayout = session->getTypeLayout(
            type,
            0,
            slang::LayoutRules::DefaultStructuredBuffer,
            diagnostics.writeRef());
    }
    catch (...)
    {
        std::cerr << "Slang threw while reflecting layout for struct '" << declName(shareDecl.decl) << "'.\n";
        return false;
    }

    auto diagnosticsText = blobText(diagnostics.get());
    if (!diagnosticsText.empty())
    {
        std::cerr << diagnosticsText << "\n";
    }
    if (!typeLayout)
    {
        std::cerr << locationText(shareDecl.site) << ": failed to reflect structured-buffer layout for struct '"
                  << declName(shareDecl.decl) << "'.\n";
        return false;
    }

    result.name = declName(shareDecl.decl);
    result.site = shareDecl.site;
    result.size = typeLayout->getSize();
    auto const alignment = typeLayout->getAlignment();
    if (alignment <= 0)
    {
        std::cerr << locationText(shareDecl.site) << ": struct '" << result.name << "' has invalid reflected alignment.\n";
        return false;
    }
    result.alignment = static_cast<std::size_t>(alignment);

    if (type->getFieldCount() != typeLayout->getFieldCount())
    {
        std::cerr << locationText(shareDecl.site) << ": struct '" << result.name
                  << "' type/layout field counts differ.\n";
        return false;
    }

    for (auto fieldIndex = 0u; fieldIndex < type->getFieldCount(); ++fieldIndex)
    {
        auto* field = type->getFieldByIndex(fieldIndex);
        auto* fieldLayout = typeLayout->getFieldByIndex(fieldIndex);
        if (!field || !fieldLayout || !field->getName())
        {
            std::cerr << locationText(shareDecl.site) << ": struct '" << result.name << "' has an invalid field.\n";
            return false;
        }

        auto error = std::string{};
        auto mappedType = mapType(field->getType(), model, error);
        if (!mappedType.has_value())
        {
            std::cerr << locationText(shareDecl.site) << ": field '" << result.name << '.' << field->getName()
                      << "' has unsupported type: " << error << ".\n";
            return false;
        }

        result.fields.push_back(FieldInfo{
            .name = field->getName(),
            .type = *mappedType,
            .defaultValue = defaultLiteral(field, *mappedType, model.enums),
            .offset = fieldLayout->getOffset(),
        });
    }

    return true;
}

[[nodiscard]] bool buildModel(slang::ISession* session, Model& model)
{
    auto sourceLineCache = std::map<std::string, std::vector<std::string>>{};

    for (auto const& shareDecl : model.declarations)
    {
        auto* decl = shareDecl.decl;
        auto const kind = decl->getKind();
        if (kind == slang::DeclReflection::Kind::Enum)
        {
            auto enumInfo = EnumInfo{};
            if (!buildEnumInfo(shareDecl, sourceLineCache, enumInfo))
            {
                return false;
            }
            model.enums.push_back(std::move(enumInfo));
        }
    }

    for (auto const& shareDecl : model.declarations)
    {
        auto* decl = shareDecl.decl;
        auto const kind = decl->getKind();
        if (kind == slang::DeclReflection::Kind::Variable)
        {
            auto constantInfo = ConstantInfo{};
            if (!buildConstantInfo(shareDecl, model, constantInfo))
            {
                return false;
            }
            model.constants.push_back(std::move(constantInfo));
        }
        else if (kind == slang::DeclReflection::Kind::Struct)
        {
            auto structInfo = StructInfo{};
            if (!buildStructInfo(session, shareDecl, model, structInfo))
            {
                return false;
            }
            model.structs.push_back(std::move(structInfo));
        }
        else if (kind == slang::DeclReflection::Kind::Enum)
        {
            continue;
        }
        else
        {
            std::cerr << locationText(shareDecl.site) << ": declaration '" << declName(decl)
                      << "' is not allowed in shader/include/share.\n";
            return false;
        }
    }

    return true;
}

void emitEnum(std::ostream& output, const EnumInfo& info)
{
    output << "enum class " << info.name << " : std::uint32_t\n";
    output << "{\n";
    for (auto const& enumCase : info.cases)
    {
        output << "    " << enumCase.name << " = " << enumCase.value << "u,\n";
    }
    output << "};\n\n";

    // Enumerator-name metadata consumed by the generic slangEnumLiteral template. Emitted for every
    // shared enum so any translated enum can be turned into its Slang literal spelling without a
    // hand-written switch. Codegen stopgap until C++26 static reflection is available (see prelude).
    output << "template<>\n";
    output << "struct SlangEnumMeta<" << info.name << ">\n";
    output << "{\n";
    output << "    static constexpr std::string_view slangTypeName = \"" << info.name << "\";\n\n";
    output << "    [[nodiscard]] static constexpr std::string_view enumeratorName(" << info.name << " value) noexcept\n";
    output << "    {\n";
    output << "        switch (value)\n";
    output << "        {\n";
    for (auto const& enumCase : info.cases)
    {
        output << "        case " << info.name << "::" << enumCase.name << ":\n";
        output << "            return \"" << enumCase.name << "\";\n";
    }
    output << "        }\n";
    output << "        return {};\n";
    output << "    }\n";
    output << "};\n\n";

    if (!info.flags)
    {
        return;
    }

    // Opt-in marker consumed by the FlagEnum concept in the prelude. The concept finds this tag via
    // ADL on the enum's namespace, so the templated |, &, |= operators apply automatically without
    // duplicating an operator set per flagged enum.
    output << "constexpr void flagEnumTag(" << info.name << ") noexcept {}\n\n";
}

void emitStruct(std::ostream& output, const StructInfo& info)
{
    output << "struct alignas(" << info.alignment << ") " << info.name << "\n";
    output << "{\n";
    for (auto const& field : info.fields)
    {
        output << "    " << field.type.name << ' ' << field.name;
        if (field.defaultValue.has_value())
        {
            output << " = " << *field.defaultValue;
        }
        else
        {
            output << "{}";
        }
        output << ";\n";
    }
    output << "};\n\n";
}

void emitLayoutAssertions(std::ostream& output, const StructInfo& info)
{
    output << "static_assert(sizeof(" << info.name << ") == " << info.size << "u);\n";
    output << "static_assert(alignof(" << info.name << ") == " << info.alignment << "u);\n";
    for (auto const& field : info.fields)
    {
        output << "static_assert(offsetof(" << info.name << ", " << field.name << ") == " << field.offset << "u);\n";
    }
    output << '\n';
}

[[nodiscard]] std::string generatedText(const Options& options, const Model& model)
{
    auto output = std::ostringstream{};
    output << "// Generated by nrShaderShareCodegen from shader/include/share. Do not edit.\n";
    output << "module;\n";
    output << "#include <cstddef>\n\n";
    output << "export module " << options.moduleName << ";\n\n";
    output << "export import dependency.math;\n";
    output << "import std;\n\n";
    output << "export namespace " << options.namespaceName << "\n";
    output << "{\n";

    if (!model.enums.empty())
    {
        // Enumerator-name reflection used to build Slang-side enum literals such as
        // "RtHitAlphaPolicy.opaqueLike". The per-enum SlangEnumMeta specializations emitted by
        // emitEnum are a codegen stopgap: they exist only because the LLVM/clang toolchain does not
        // yet implement C++26 static reflection (P2996, <meta>). Once static reflection is
        // available, drop the generated SlangEnumMeta specializations and derive both slangTypeName
        // and enumeratorName directly via std::meta::identifier_of, so slangEnumLiteral no longer
        // depends on generated metadata. See docs/architecture and AGENTS.md C++26 policy.
        output << "// Enumerator-name reflection for Slang-side enum literals (e.g. \"Type.enumerator\").\n";
        output << "// TODO(C++26 reflection): once LLVM/clang ship P2996 static reflection, delete the generated\n";
        output << "// SlangEnumMeta specializations and derive names via std::meta::identifier_of instead.\n";
        output << "template<class Enum>\n";
        output << "struct SlangEnumMeta;\n\n";
        output << "template<class Enum>\n";
        output << "[[nodiscard]] std::string slangEnumLiteral(Enum value)\n";
        output << "{\n";
        output << "    return std::format(\"{}.{}\", SlangEnumMeta<Enum>::slangTypeName, SlangEnumMeta<Enum>::enumeratorName(value));\n";
        output << "}\n\n";
    }

    if (std::ranges::any_of(model.enums, [](const EnumInfo& info) { return info.flags; }))
    {
        // Bitmask operators for flagged shared enums. A flagged enum opts in by declaring a
        // flagEnumTag overload (emitted by emitEnum); the FlagEnum concept finds it via ADL, so
        // these operators are defined once here instead of being duplicated per enum.
        output << "// Bitmask operators shared by every flagged enum (opt-in via a flagEnumTag overload).\n";
        output << "template<class Enum>\n";
        output << "concept FlagEnum = std::is_scoped_enum_v<Enum> && requires(Enum e) { flagEnumTag(e); };\n\n";
        output << "template<FlagEnum Enum>\n";
        output << "[[nodiscard]] constexpr Enum operator|(Enum lhs, Enum rhs) noexcept\n";
        output << "{\n";
        output << "    return static_cast<Enum>(static_cast<std::underlying_type_t<Enum>>(lhs) | static_cast<std::underlying_type_t<Enum>>(rhs));\n";
        output << "}\n\n";
        output << "template<FlagEnum Enum>\n";
        output << "[[nodiscard]] constexpr Enum operator&(Enum lhs, Enum rhs) noexcept\n";
        output << "{\n";
        output << "    return static_cast<Enum>(static_cast<std::underlying_type_t<Enum>>(lhs) & static_cast<std::underlying_type_t<Enum>>(rhs));\n";
        output << "}\n\n";
        output << "template<FlagEnum Enum>\n";
        output << "constexpr Enum& operator|=(Enum& lhs, Enum rhs) noexcept\n";
        output << "{\n";
        output << "    lhs = lhs | rhs;\n";
        output << "    return lhs;\n";
        output << "}\n\n";
    }

    for (auto const& shareDecl : model.declarations)
    {
        auto const kind = shareDecl.decl->getKind();
        auto const name = declName(shareDecl.decl);
        if (kind == slang::DeclReflection::Kind::Variable)
        {
            auto const it = std::ranges::find_if(model.constants, [&](const ConstantInfo& info) {
                return info.name == name;
            });
            if (it != model.constants.end())
            {
                output << "inline constexpr " << it->type.name << ' ' << it->name << " = " << it->value << ";\n\n";
            }
        }
        else if (kind == slang::DeclReflection::Kind::Enum)
        {
            auto const it = std::ranges::find_if(model.enums, [&](const EnumInfo& info) {
                return info.name == name;
            });
            if (it != model.enums.end())
            {
                emitEnum(output, *it);
            }
        }
        else if (kind == slang::DeclReflection::Kind::Struct)
        {
            auto const it = std::ranges::find_if(model.structs, [&](const StructInfo& info) {
                return info.name == name;
            });
            if (it != model.structs.end())
            {
                emitStruct(output, *it);
            }
        }
    }

    for (auto const& structInfo : model.structs)
    {
        emitLayoutAssertions(output, structInfo);
    }

    output << "} // namespace " << options.namespaceName << "\n";
    return output.str();
}

[[nodiscard]] bool writeOutput(const std::filesystem::path& path, const std::string& text)
{
    auto ec = std::error_code{};
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
    {
        std::cerr << "Failed to create output directory '" << genericPathString(path.parent_path())
                  << "': " << ec.message() << "\n";
        return false;
    }

    auto existing = std::ifstream{path, std::ios::binary};
    if (existing)
    {
        auto stream = std::ostringstream{};
        stream << existing.rdbuf();
        if (stream.str() == text)
        {
            std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now(), ec);
            if (ec)
            {
                std::cerr << "Failed to update generated module timestamp '" << genericPathString(path)
                          << "': " << ec.message() << "\n";
                return false;
            }
            return true;
        }
    }

    auto output = std::ofstream{path, std::ios::binary | std::ios::trunc};
    if (!output)
    {
        std::cerr << "Failed to open generated module output '" << genericPathString(path) << "'.\n";
        return false;
    }
    output << text;
    return true;
}
} // namespace

int main(int argc, char** argv)
{
    auto options = Options{};
    if (!parseOptions(argc, argv, options))
    {
        return EXIT_FAILURE;
    }

    auto session = createSession(options);
    if (!session)
    {
        return EXIT_FAILURE;
    }

    auto module = loadRootModule(session.get(), options.rootModule);
    if (!module)
    {
        return EXIT_FAILURE;
    }

    auto model = Model{};
    if (!collectShareDeclarations(session.get(), options.shareRoot, model))
    {
        return EXIT_FAILURE;
    }
    if (!buildModel(session.get(), model))
    {
        return EXIT_FAILURE;
    }

    auto text = generatedText(options, model);
    if (!writeOutput(options.output, text))
    {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
