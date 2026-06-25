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

    auto const size = static_cast<std::size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

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

namespace nr::rhi
{
[[nodiscard]] SlangSampler SlangSampler::create(const vk::raii::Device &device, SlangSamplerDesc desc, std::string_view debugName)
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
        return !entryPointName.empty() && stage != SLANG_STAGE_NONE && codeBlob != nullptr;
    }

[[nodiscard]] bool SlangCompiledModule::valid() const noexcept
{
        return module != nullptr;
    }

[[nodiscard]] bool SlangProgram::valid() const noexcept
{
        return linkedProgram_ != nullptr && entryPointCount() > 0;
    }

[[nodiscard]] std::size_t SlangProgram::entryPointCount() const noexcept
{
        if (!buildEntryPointCache())
        {
            return 0;
        }
        return entryPoints_.size();
    }

[[nodiscard]] std::span<const SlangEntryPointData> SlangProgram::entryPoints() const noexcept
{
        if (!buildEntryPointCache())
        {
            return {};
        }
        return entryPoints_;
    }

[[nodiscard]] const SlangEntryPointData *SlangProgram::entryPointData(std::string_view entryPointName) const noexcept
{
        if (!buildEntryPointCache())
        {
            return nullptr;
        }

        return findEntryPointDataCached(entryPointName);
    }

[[nodiscard]] slang::EntryPointReflection *SlangProgram::entryPointLayout(std::string_view entryPointName) const noexcept
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

[[nodiscard]] std::optional<SlangStage> SlangProgram::entryPointStage(std::string_view entryPointName) const noexcept
{
        auto const *entryPoint = entryPointData(entryPointName);
        if (!entryPoint)
        {
            return std::nullopt;
        }
        return entryPoint->stage;
    }

[[nodiscard]] slang::IBlob *SlangProgram::entryPointBlob(std::string_view entryPointName) const noexcept
{
        auto const *entryPoint = entryPointData(entryPointName);
        return entryPoint ? entryPoint->codeBlob.get() : nullptr;
    }

[[nodiscard]] slang::IComponentType *SlangProgram::componentType() const noexcept
{
        return linkedProgram_.get();
    }

[[nodiscard]] slang::ProgramLayout *SlangProgram::programLayout() const noexcept
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

[[nodiscard]] const SlangEntryPointData *SlangProgram::findEntryPointDataCached(std::string_view entryPointName) const noexcept
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

[[nodiscard]] bool SlangProgram::buildEntryPointCache() const noexcept
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

        entryPoints_.reserve(static_cast<std::size_t>(linkedEntryPointCount));

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
                            static_cast<std::int32_t>(bindingType)));
                });
            }

            Slang::ComPtr<slang::IBlob> codeBlob;
            Slang::ComPtr<slang::IBlob> diagnostics;
            try
            {
                auto compileResult = linkedProgram_->getEntryPointCode(static_cast<SlangInt>(entryIndex), 0, codeBlob.writeRef(), diagnostics.writeRef());
                if (diagnostics)
                {
                    auto text = std::string_view(static_cast<const char *>(diagnostics->getBufferPointer()), diagnostics->getBufferSize());
                    if (!text.empty())
                    {
                        nrInfo<nr::LogLevel::warning>(std::format("[SlangProgram::buildEntryPointCache] entrypoint='{}' diagnostics:\n{}", entryName, text));
                    }
                }
                if (!detail::slangSucceeded(compileResult) || !codeBlob)
                {
                    return false;
                }
            }
            catch (...)
            {
                nrInfo<nr::LogLevel::error>(std::format(
                    "[SlangProgram::buildEntryPointCache] Slang threw an internal exception during getEntryPointCode for entry='{}'. "
                    "Attach a debugger and break on Slang::InternalError to inspect the Message field.",
                    entryName));
                nrAssert(false, "Slang::IComponentType::getEntryPointCode threw an internal exception.");
            }

            SlangEntryPointData entryPointData{};
            entryPointData.linkedEntryPointIndex = static_cast<std::uint32_t>(entryIndex);
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

[[nodiscard]] ShaderService &ShaderService::instance()
{
        static ShaderService service;
        return service;
    }

void ShaderService::configure()
{
        std::scoped_lock lock(m_mutex);
        applyCompileOptionsLocked(kDefaultSlangCompileOptions);
    }

[[nodiscard]] SlangProgram ShaderService::compileProgramByFile(const SlangProgramCompileFileRequest &request)
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
        entryPointComponents.reserve(static_cast<std::size_t>(definedEntryPointCount));

        std::vector<slang::IComponentType *> components;
        components.reserve(static_cast<std::size_t>(definedEntryPointCount) + 1);
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
        try
        {
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
        }
        catch (...)
        {
            emitDiagnosticsLocked(diagnostics.get(), "createCompositeComponentType(exception-path)");
            nrInfo<nr::LogLevel::error>(std::format(
                "[ShaderService::compileProgramByFile] Slang threw an internal exception during createCompositeComponentType for module='{}'. "
                "Attach a debugger and break on Slang::InternalError to inspect the Message field.",
                moduleName));
            nrAssert(false, "Slang::ISession::createCompositeComponentType threw an internal exception.");
        }

        Slang::ComPtr<slang::IComponentType> linkedProgram;
        diagnostics = nullptr;
        try
        {
            auto linkResult = compositeProgram->link(linkedProgram.writeRef(), diagnostics.writeRef());
            emitDiagnosticsLocked(diagnostics.get(), "link");
            if (!detail::slangSucceeded(linkResult) || !linkedProgram)
            {
                nrInfo<LogLevel::warning>(std::format("[ShaderService::compileProgramByFile] link failed for module='{}'.", moduleName));
                return result;
            }
        }
        catch (...)
        {
            emitDiagnosticsLocked(diagnostics.get(), "link(exception-path)");
            nrInfo<nr::LogLevel::error>(std::format(
                "[ShaderService::compileProgramByFile] Slang threw an internal exception during link for module='{}'. "
                "Attach a debugger and break on Slang::InternalError to inspect the Message field.",
                moduleName));
            nrAssert(false, "Slang::IComponentType::link threw an internal exception.");
        }

        result.linkedProgram_ = linkedProgram;

        nrInfo<>(std::format("[ShaderService::compileProgramByFile] finished: module='{}'", moduleName));
        return result;
    }

void ShaderService::writeModuleCacheBlobAsync(Slang::ComPtr<slang::IModule> module, const std::filesystem::path &moduleBlobPath)
{
        auto pathText = moduleBlobPath.string();
        std::thread([module = std::move(module), pathText = std::move(pathText)]() mutable {
            auto writeResult = module->writeToFile(pathText.c_str());
            if (!detail::slangSucceeded(writeResult))
            {
                nrInfo<nr::LogLevel::warning>(std::format("[ShaderService::writeModuleCacheBlobAsync] writeToFile failed: path='{}', result={}", pathText, static_cast<std::int32_t>(writeResult)));
                return;
            }
        }).detach();
    }

[[nodiscard]] std::optional<std::string> ShaderService::validateModulePathOrganizationLocked(std::string_view moduleName, std::string_view modulePath) const
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
                nrInfo<nr::LogLevel::error>(
                    "[ShaderService::ensureGlobalSessionLocked] Slang threw an internal exception during createGlobalSession. "
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
    }

[[nodiscard]] std::uint64_t ShaderService::computeOptionsHashValueLocked() const noexcept
{
        return m_optionsHashValue;
    }

[[nodiscard]] std::string_view ShaderService::optionsHashLocked() const noexcept
{
        return m_optionsHashHex;
    }

[[nodiscard]] std::filesystem::path ShaderService::moduleCacheRootLocked() const
{
        return std::filesystem::path(std::string(shaderCacheRoot)) / std::string(optionsHashLocked());
    }

void ShaderService::invalidateStaleModuleCacheLocked(std::string_view modulePath, const std::filesystem::path &moduleBlobPath)
{
        auto binaryBytes = detail::readBinaryFile(moduleBlobPath);
        if (binaryBytes.empty())
        {
            return;
        }

        Slang::ComPtr<slang::IBlob> binaryBlob(slang_createBlob(binaryBytes.data(), binaryBytes.size()));
        if (!binaryBlob)
        {
            return;
        }

        auto const sourceModulePath = std::string(modulePath) + ".slang";
        if (m_session->isBinaryModuleUpToDate(sourceModulePath.c_str(), binaryBlob.get()))
        {
            return;
        }

        std::error_code removeEc;
        auto removed = std::filesystem::remove(moduleBlobPath, removeEc);
        if (removeEc)
        {
            nrInfo<nr::LogLevel::warning>(std::format(
                "[ShaderService::invalidateStaleModuleCacheLocked] stale cache removal failed: path='{}', error='{}'",
                moduleBlobPath.generic_string(),
                removeEc.message()));
            return;
        }

        if (removed)
        {
            nrInfo<>(std::format(
                "[ShaderService::invalidateStaleModuleCacheLocked] removed stale cache blob: path='{}'",
                moduleBlobPath.generic_string()));
        }
    }

void ShaderService::recreateSessionLocked()
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
        sessionDesc.compilerOptionEntryCount = static_cast<std::uint32_t>(m_compilerOptionEntries.size());

        try
        {
            auto result = m_globalSession->createSession(sessionDesc, m_session.writeRef());
            nrAssert(detail::slangSucceeded(result), "Failed to create Slang session.");
        }
        catch (...)
        {
            nrInfo<nr::LogLevel::error>(
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

        auto text = std::string_view(static_cast<const char *>(diagnostics->getBufferPointer()), diagnostics->getBufferSize());

        if (!text.empty())
        {
            nrInfo<nr::LogLevel::warning>(std::format("[Slang:{}]\n{}", context, text));
        }
    }

[[nodiscard]] std::string ShaderService::resolveModuleNameLocked(std::string_view modulePath) const
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

SlangCompiledModule ShaderService::loadOrCompileModuleLocked(const std::string &moduleName, std::optional<std::string> explicitModulePath)
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

        auto cacheExists = std::filesystem::exists(moduleBlobPath, moduleBlobEc);
        if (!moduleBlobEc && cacheExists)
        {
            invalidateStaleModuleCacheLocked(normalizedModulePath, moduleBlobPath);
        }

        SlangCompiledModule result;
        result.moduleName = normalizedModuleName;
        result.sourcePath = std::filesystem::path(sourceModulePath);

        Slang::ComPtr<slang::IBlob> diagnostics;
        Slang::ComPtr<slang::IModule> loadedModule;
        diagnostics = nullptr;
        try
        {
            loadedModule = Slang::ComPtr<slang::IModule>(m_session->loadModule(normalizedModulePath.c_str(), diagnostics.writeRef()));
        }
        catch (...)
        {
            nrInfo<nr::LogLevel::error>(std::format(
                "[ShaderService::loadOrCompileModuleLocked] Slang threw an internal exception during loadModule for module='{}' path='{}'. "
                "Attach a debugger and break on Slang::InternalError to inspect the Message field.",
                normalizedModuleName, normalizedModulePath));
            emitDiagnosticsLocked(diagnostics.get(), "loadModule(exception-path)");
            nrAssert(false, "Slang::ISession::loadModule threw an internal exception.");
        }
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
} // namespace nr::rhi
