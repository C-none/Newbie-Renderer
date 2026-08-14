export module dependency.crypto;

import std;

export namespace dependency::crypto
{
using Sha256Digest = std::array<std::byte, 32u>;

[[nodiscard]] std::optional<Sha256Digest> sha256(std::span<const std::byte> bytes) noexcept;
} // namespace dependency::crypto
