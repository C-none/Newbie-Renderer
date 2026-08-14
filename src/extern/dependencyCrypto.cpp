module;

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>

module dependency.crypto;

import std;

namespace dependency::crypto
{
[[nodiscard]] std::optional<Sha256Digest> sha256(std::span<const std::byte> bytes) noexcept
{
    if (bytes.size() > std::numeric_limits<ULONG>::max())
    {
        return std::nullopt;
    }
    auto algorithm = BCRYPT_ALG_HANDLE{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0u) < 0)
    {
        return std::nullopt;
    }

    struct AlgorithmCloser
    {
        void operator()(void *handle) const noexcept
        {
            BCryptCloseAlgorithmProvider(static_cast<BCRYPT_ALG_HANDLE>(handle), 0u);
        }
    };
    auto closeAlgorithm = std::unique_ptr<void, AlgorithmCloser>{algorithm};

    auto objectBytes = ULONG{};
    auto bytesWritten = ULONG{};
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(std::addressof(objectBytes)),
                          sizeof(objectBytes), std::addressof(bytesWritten), 0u) < 0 ||
        bytesWritten != sizeof(objectBytes))
    {
        return std::nullopt;
    }

    auto object = std::vector<std::byte>(objectBytes);
    auto hash = BCRYPT_HASH_HANDLE{};
    if (BCryptCreateHash(algorithm, &hash, reinterpret_cast<PUCHAR>(object.data()), objectBytes, nullptr, 0u, 0u) < 0)
    {
        return std::nullopt;
    }

    struct HashCloser
    {
        void operator()(void *handle) const noexcept
        {
            BCryptDestroyHash(static_cast<BCRYPT_HASH_HANDLE>(handle));
        }
    };
    auto destroyHash = std::unique_ptr<void, HashCloser>{hash};

    if (!bytes.empty() &&
        BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<std::byte *>(bytes.data())),
                       static_cast<ULONG>(bytes.size()), 0u) < 0)
    {
        return std::nullopt;
    }

    auto digest = Sha256Digest{};
    if (BCryptFinishHash(hash, reinterpret_cast<PUCHAR>(digest.data()), static_cast<ULONG>(digest.size()), 0u) < 0)
    {
        return std::nullopt;
    }
    return digest;
}
} // namespace dependency::crypto
