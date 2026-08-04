export module dependency.processLease;

import std;

export namespace dependency::process
{
enum class ExclusiveDirectoryLeaseError : std::uint8_t
{
    alreadyOwned,
    systemError,
};

struct ExclusiveDirectoryLeaseFailure
{
    ExclusiveDirectoryLeaseError error = ExclusiveDirectoryLeaseError::systemError;
    std::string detail{};
};

class ExclusiveDirectoryLease
{
  public:
    ExclusiveDirectoryLease() noexcept;
    ~ExclusiveDirectoryLease();

    ExclusiveDirectoryLease(const ExclusiveDirectoryLease &) = delete;
    ExclusiveDirectoryLease &operator=(const ExclusiveDirectoryLease &) = delete;
    ExclusiveDirectoryLease(ExclusiveDirectoryLease &&) noexcept;
    ExclusiveDirectoryLease &operator=(ExclusiveDirectoryLease &&) noexcept;

    [[nodiscard]] std::expected<void, ExclusiveDirectoryLeaseFailure> acquire(
        const std::filesystem::path &canonicalDirectory);
    void release() noexcept;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_{};
};
} // namespace dependency::process
