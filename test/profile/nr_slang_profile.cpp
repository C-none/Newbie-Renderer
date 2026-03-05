#include "slang.h"

import std;

namespace
{
[[nodiscard]] bool sameGuid(const SlangUUID &lhs, const SlangUUID &rhs)
{
    return std::memcmp(&lhs, &rhs, sizeof(SlangUUID)) == 0;
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
            return SLANG_E_INVALID_ARG;
        }
        *outObject = nullptr;
        if (sameGuid(uuid, slang::IBlob::getTypeGuid()) || sameGuid(uuid, ISlangUnknown::getTypeGuid()))
        {
            *outObject = static_cast<slang::IBlob *>(this);
            addRef();
            return SLANG_OK;
        }
        return SLANG_E_NO_INTERFACE;
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

[[nodiscard]] auto makeProjectRoot() -> std::filesystem::path
{
    return std::filesystem::path{NR_PROJECT_ROOT_DIR};
}

[[nodiscard]] auto makeSourceShaderRoot() -> std::filesystem::path
{
    return makeProjectRoot() / "shader";
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

template <typename T>
void releaseIfNeeded(T *&ptr)
{
    ptr = nullptr;
}

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

void printDiagnostics(std::string_view stage, slang::IBlob *diagnostics)
{
    if (!diagnostics || diagnostics->getBufferSize() == 0)
    {
        return;
    }

    auto text = std::string_view(static_cast<const char *>(diagnostics->getBufferPointer()), diagnostics->getBufferSize());
    std::println("[diag:{}]\n{}", stage, text);
}

template <typename TUnknown>
void printModuleDependencies(std::string_view tag, TUnknown *owner)
{
    if (!owner)
    {
        std::println("[deps:{}] owner is null", tag);
        return;
    }

    slang::IModulePrecompileService_Experimental *precompileService = nullptr;
    auto queryResult = owner->queryInterface(
        slang::IModulePrecompileService_Experimental::getTypeGuid(),
        reinterpret_cast<void **>(&precompileService));
    if (queryResult < 0 || !precompileService)
    {
        std::println("[deps:{}] IModulePrecompileService_Experimental not available", tag);
        return;
    }

    auto dependencyCount = precompileService->getModuleDependencyCount();
    std::println("[deps:{}] count={}", tag, dependencyCount);

    for (auto dependencyIndex : std::views::iota(SlangInt{0}, dependencyCount))
    {
        slang::IModule *dependencyModule = nullptr;
        slang::IBlob *diagnostics = nullptr;
        auto result = precompileService->getModuleDependency(dependencyIndex, &dependencyModule, &diagnostics);
        printDiagnostics("getModuleDependency", diagnostics);
        releaseIfNeeded(diagnostics);

        if (result < 0 || !dependencyModule)
        {
            std::println("  - [{}] <failed>, result={}", dependencyIndex, static_cast<int32_t>(result));
            continue;
        }

        auto moduleName = dependencyModule->getName() ? dependencyModule->getName() : "<unnamed>";
        auto filePath = dependencyModule->getFilePath() ? dependencyModule->getFilePath() : "<no-file>";
        std::println("  - [{}] name='{}', file='{}'", dependencyIndex, moduleName, filePath);
        releaseIfNeeded(dependencyModule);
    }

    releaseIfNeeded(precompileService);
}
} // namespace

int main()
{
    try
    {
        auto const sourceShaderRoot = std::filesystem::weakly_canonical(makeSourceShaderRoot());
        auto const shaderCacheRoot = makeProjectRoot() / "build" / "shader_cache";
        if (!std::filesystem::exists(sourceShaderRoot))
        {
            std::println("[error] source shader root not found: {}", sourceShaderRoot.string());
            return 2;
        }

        slang::IGlobalSession *globalSession = nullptr;
        auto createGlobalSessionResult = slang::createGlobalSession(&globalSession);
        if (createGlobalSessionResult < 0 || !globalSession)
        {
            std::println("[error] createGlobalSession failed: {}", static_cast<int32_t>(createGlobalSessionResult));
            return 3;
        }

        auto const cacheSearchPath = shaderCacheRoot.generic_string();
        auto const sourceSearchPath = sourceShaderRoot.generic_string();
        char const *searchPaths[] = {
            cacheSearchPath.c_str(),
            sourceSearchPath.c_str(),
        };

        std::vector<slang::CompilerOptionEntry> options;
        options.reserve(2);
        appendIntCompilerOption(options, slang::CompilerOptionName::EmitSpirvDirectly, 1);
        appendIntCompilerOption(options, slang::CompilerOptionName::UseUpToDateBinaryModule, 1);

        slang::TargetDesc targetDesc{};
        targetDesc.format = SLANG_SPIRV;
        targetDesc.profile = globalSession->findProfile("SPIRV_1_6");

        slang::SessionDesc sessionDesc{};
        sessionDesc.targets = &targetDesc;
        sessionDesc.targetCount = 1;
        sessionDesc.searchPaths = searchPaths;
        sessionDesc.searchPathCount = 2;
        sessionDesc.compilerOptionEntries = options.data();
        sessionDesc.compilerOptionEntryCount = static_cast<uint32_t>(options.size());

        slang::ISession *session = nullptr;
        auto createSessionResult = globalSession->createSession(sessionDesc, &session);
        if (createSessionResult < 0 || !session)
        {
            std::println("[error] createSession failed: {}", static_cast<int32_t>(createSessionResult));
            releaseIfNeeded(globalSession);
            return 4;
        }

        std::println("[setup] session search paths:");
        std::println("  [0] {}", cacheSearchPath);
        std::println("  [1] {}", sourceSearchPath);

        constexpr std::string_view moduleQuery = "test/main/main";
        auto modulePath = std::string(moduleQuery);
        constexpr std::string_view entryPointName = "csMain";

        slang::IBlob *diagnostics = nullptr;
        slang::IModule *module = session->loadModule(moduleQuery.data(), &diagnostics);
        printDiagnostics("loadModule(slash)", diagnostics);
        releaseIfNeeded(diagnostics);
        if (!module)
        {
            std::println("[error] loadModule failed for '{}'.", moduleQuery);
            releaseIfNeeded(session);
            releaseIfNeeded(globalSession);
            return 5;
        }
        auto const loadedFrom = module->getFilePath() ? module->getFilePath() : "<no-file>";
        std::println("[ok] module loaded: '{}', file='{}'", moduleQuery, loadedFrom);

        auto const moduleCachePath = shaderCacheRoot / (modulePath + ".slang-module");
        std::error_code fsError;
        std::filesystem::create_directories(moduleCachePath.parent_path(), fsError);
        if (fsError)
        {
            std::println("[error] create_directories failed for '{}': {}", moduleCachePath.parent_path().generic_string(), fsError.message());
            releaseIfNeeded(module);
            releaseIfNeeded(session);
            releaseIfNeeded(globalSession);
            return 10;
        }

        std::atomic<SlangResult> asyncWriteResult{SLANG_FAIL};
        module->addRef();
        std::thread writeThread([module, &asyncWriteResult, moduleCachePath]() {
            asyncWriteResult.store(module->writeToFile(moduleCachePath.generic_string().c_str()));
            module->release();
        });

        auto const sourceModulePath = modulePath + ".slang";
        auto const initialBinaryBytes = readBinaryFile(moduleCachePath);
        if (!initialBinaryBytes.empty())
        {
            auto *binaryBlob = new OwnedBlob(initialBinaryBytes);
            auto const upToDate = session->isBinaryModuleUpToDate(sourceModulePath.c_str(), binaryBlob);
            std::println("[ok] isBinaryModuleUpToDate(before async write): module='{}', upToDate={}", sourceModulePath, upToDate);
            binaryBlob->release();
        }
        else
        {
            std::println("[warn] skip isBinaryModuleUpToDate(before async write): cache blob not found '{}'.", moduleCachePath.generic_string());
        }

        writeThread.join();

        auto const writeResult = asyncWriteResult.load();
        if (writeResult < 0)
        {
            std::println("[error] writeToFile failed: path='{}', result={}", moduleCachePath.generic_string(), static_cast<int32_t>(writeResult));
            releaseIfNeeded(module);
            releaseIfNeeded(session);
            releaseIfNeeded(globalSession);
            return 11;
        }

        auto const cacheExists = std::filesystem::exists(moduleCachePath, fsError);
        if (fsError || !cacheExists)
        {
            std::println("[error] cache file missing after writeToFile: '{}'", moduleCachePath.generic_string());
            releaseIfNeeded(module);
            releaseIfNeeded(session);
            releaseIfNeeded(globalSession);
            return 12;
        }

        auto const cacheSize = std::filesystem::file_size(moduleCachePath, fsError);
        std::println("[ok] writeToFile: '{}' (bytes={})", moduleCachePath.generic_string(), fsError ? 0 : cacheSize);

        auto const binaryBytes = readBinaryFile(moduleCachePath);
        if (!binaryBytes.empty())
        {
            auto *binaryBlob = new OwnedBlob(binaryBytes);
            auto const upToDate = session->isBinaryModuleUpToDate(sourceModulePath.c_str(), binaryBlob);
            std::println("[ok] isBinaryModuleUpToDate(after async write): module='{}', upToDate={}", sourceModulePath, upToDate);
            binaryBlob->release();
        }

        slang::IEntryPoint *entryPoint = nullptr;
        auto findEntryResult = module->findAndCheckEntryPoint(entryPointName.data(), SLANG_STAGE_COMPUTE, &entryPoint, &diagnostics);
        printDiagnostics("findAndCheckEntryPoint", diagnostics);
        releaseIfNeeded(diagnostics);
        if (findEntryResult < 0 || !entryPoint)
        {
            std::println("[error] findAndCheckEntryPoint failed for '{}::{}'", moduleQuery, entryPointName);
            releaseIfNeeded(module);
            releaseIfNeeded(session);
            releaseIfNeeded(globalSession);
            return 6;
        }
        std::println("[ok] entrypoint found: '{}'", entryPointName);

        std::array<slang::IComponentType *, 2> components = {
            module,
            entryPoint,
        };

        slang::IComponentType *compositeProgram = nullptr;
        auto compositeResult = session->createCompositeComponentType(
            components.data(),
            static_cast<SlangInt>(components.size()),
            &compositeProgram,
            &diagnostics);
        printDiagnostics("createCompositeComponentType", diagnostics);
        releaseIfNeeded(diagnostics);
        if (compositeResult < 0 || !compositeProgram)
        {
            std::println("[error] createCompositeComponentType failed");
            releaseIfNeeded(entryPoint);
            releaseIfNeeded(module);
            releaseIfNeeded(session);
            releaseIfNeeded(globalSession);
            return 7;
        }

        printModuleDependencies("entryPoint", entryPoint);

        slang::IComponentType *linkedProgram = nullptr;
        auto linkResult = compositeProgram->link(&linkedProgram, &diagnostics);
        printDiagnostics("link", diagnostics);
        releaseIfNeeded(diagnostics);
        if (linkResult < 0 || !linkedProgram)
        {
            std::println("[error] link failed");
            releaseIfNeeded(compositeProgram);
            releaseIfNeeded(entryPoint);
            releaseIfNeeded(module);
            releaseIfNeeded(session);
            releaseIfNeeded(globalSession);
            return 8;
        }

        printModuleDependencies("linked", linkedProgram);

        slang::IBlob *entryCode = nullptr;
        auto codeResult = linkedProgram->getEntryPointCode(0, 0, &entryCode, &diagnostics);
        printDiagnostics("getEntryPointCode", diagnostics);
        releaseIfNeeded(diagnostics);
        if (codeResult < 0 || !entryCode)
        {
            std::println("[error] getEntryPointCode failed");
            releaseIfNeeded(linkedProgram);
            releaseIfNeeded(compositeProgram);
            releaseIfNeeded(entryPoint);
            releaseIfNeeded(module);
            releaseIfNeeded(session);
            releaseIfNeeded(globalSession);
            return 9;
        }

        auto const byteCount = entryCode->getBufferSize();
        auto const wordCount = byteCount / sizeof(uint32_t);
        std::println("[ok] entrypoint generated: module='{}', entry='{}', bytes={}, words={}",
            moduleQuery,
            entryPointName,
            byteCount,
            wordCount);

        releaseIfNeeded(entryCode);
        releaseIfNeeded(linkedProgram);
        releaseIfNeeded(compositeProgram);
        releaseIfNeeded(entryPoint);
        releaseIfNeeded(module);
        releaseIfNeeded(session);
        releaseIfNeeded(globalSession);
        return 0;
    }
    catch (std::exception const &e)
    {
        std::println("[error] exception: {}", e.what());
        return 1;
    }
}
