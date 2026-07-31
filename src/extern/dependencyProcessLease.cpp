module;

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

module dependency.processLease;

import std;

namespace dependency::process
{
namespace
{
class UniqueHandle
{
  public:
    explicit UniqueHandle(HANDLE handle = nullptr) noexcept : handle_(handle)
    {
    }

    ~UniqueHandle()
    {
        reset();
    }

    UniqueHandle(const UniqueHandle &) = delete;
    UniqueHandle &operator=(const UniqueHandle &) = delete;
    UniqueHandle(UniqueHandle &&other) noexcept : handle_(std::exchange(other.handle_, nullptr))
    {
    }
    UniqueHandle &operator=(UniqueHandle &&other) noexcept
    {
        if (this != std::addressof(other))
        {
            reset();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    [[nodiscard]] HANDLE get() const noexcept
    {
        return handle_;
    }

    [[nodiscard]] HANDLE release() noexcept
    {
        return std::exchange(handle_, nullptr);
    }

    void reset(HANDLE handle = nullptr) noexcept
    {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE)
        {
            static_cast<void>(CloseHandle(handle_));
        }
        handle_ = handle;
    }

  private:
    HANDLE handle_ = nullptr;
};

[[nodiscard]] std::string windowsFailure(std::string_view operation, DWORD error)
{
    return std::format("{} failed: {}", operation, std::error_code{static_cast<int>(error), std::system_category()}.message());
}

[[nodiscard]] std::expected<std::wstring, ExclusiveDirectoryLeaseFailure> finalDirectoryPath(const std::filesystem::path &directory)
{
    auto directoryHandle = UniqueHandle{CreateFileW(directory.c_str(), 0u, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
    if (directoryHandle.get() == INVALID_HANDLE_VALUE)
    {
        auto const error = GetLastError();
        return std::unexpected{ExclusiveDirectoryLeaseFailure{
            .detail = windowsFailure("Opening the canonical log directory", error),
        }};
    }

    auto const requiredCharacters = GetFinalPathNameByHandleW(directoryHandle.get(), nullptr, 0u, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (requiredCharacters == 0u)
    {
        auto const error = GetLastError();
        return std::unexpected{ExclusiveDirectoryLeaseFailure{
            .detail = windowsFailure("Resolving the canonical log directory", error),
        }};
    }

    auto path = std::wstring(static_cast<std::size_t>(requiredCharacters), L'\0');
    auto const writtenCharacters = GetFinalPathNameByHandleW(directoryHandle.get(), path.data(), requiredCharacters, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (writtenCharacters == 0u || writtenCharacters >= requiredCharacters)
    {
        auto const error = GetLastError();
        return std::unexpected{ExclusiveDirectoryLeaseFailure{
            .detail = writtenCharacters == 0u ? windowsFailure("Resolving the canonical log directory", error) : "Resolving the canonical log directory returned an unstable path length.",
        }};
    }
    path.resize(static_cast<std::size_t>(writtenCharacters));
    return path;
}

[[nodiscard]] std::uint64_t hashPath(std::wstring_view path, std::uint64_t seed) noexcept
{
    auto hash = seed;
    std::ranges::for_each(path, [&](wchar_t character) {
        auto const value = static_cast<std::uint16_t>(character);
        hash ^= static_cast<std::uint8_t>(value & 0xffu);
        hash *= 1099511628211ull;
        hash ^= static_cast<std::uint8_t>(value >> 8u);
        hash *= 1099511628211ull;
    });
    return hash;
}

[[nodiscard]] std::wstring kernelObjectName(std::wstring path)
{
    static_cast<void>(CharLowerBuffW(path.data(), static_cast<DWORD>(path.size())));
    auto const firstHash = hashPath(path, 14695981039346656037ull);
    auto const secondHash = hashPath(path, 1099511628211ull);
    return std::format(L"Global\\NewbieRenderer.LogDirectoryLease.v1.{:016x}{:016x}", firstHash, secondHash);
}
} // namespace

class ExclusiveDirectoryLease::Impl
{
  public:
    explicit Impl(HANDLE eventHandle) noexcept : eventHandle_(eventHandle)
    {
    }

  private:
    UniqueHandle eventHandle_;
};

ExclusiveDirectoryLease::ExclusiveDirectoryLease() noexcept = default;
ExclusiveDirectoryLease::~ExclusiveDirectoryLease() = default;
ExclusiveDirectoryLease::ExclusiveDirectoryLease(ExclusiveDirectoryLease &&) noexcept = default;
ExclusiveDirectoryLease &ExclusiveDirectoryLease::operator=(ExclusiveDirectoryLease &&) noexcept = default;

std::expected<void, ExclusiveDirectoryLeaseFailure> ExclusiveDirectoryLease::acquire(const std::filesystem::path &canonicalDirectory)
{
    release();

    auto finalPath = finalDirectoryPath(canonicalDirectory);
    if (!finalPath)
    {
        return std::unexpected{std::move(finalPath.error())};
    }

    auto const name = kernelObjectName(std::move(*finalPath));
    auto eventHandle = UniqueHandle{CreateEventW(nullptr, TRUE, FALSE, name.c_str())};
    auto const createError = GetLastError();
    if (eventHandle.get() == nullptr)
    {
        return std::unexpected{ExclusiveDirectoryLeaseFailure{
            .detail = windowsFailure("Creating the named log-directory lease", createError),
        }};
    }
    if (createError == ERROR_ALREADY_EXISTS)
    {
        return std::unexpected{ExclusiveDirectoryLeaseFailure{
            .error = ExclusiveDirectoryLeaseError::alreadyOwned,
            .detail = "The named log-directory lease is already owned.",
        }};
    }

    impl_ = std::make_unique<Impl>(eventHandle.release());
    return {};
}

void ExclusiveDirectoryLease::release() noexcept
{
    impl_.reset();
}
} // namespace dependency::process
