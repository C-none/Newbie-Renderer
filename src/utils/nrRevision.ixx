export module nr.utils:revision;

import std;

export namespace nr::revision
{
struct RevisionValue
{
    std::uint64_t value = 1u;

    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return value != 0u;
    }

    [[nodiscard]] friend constexpr auto operator<=>(RevisionValue, RevisionValue) noexcept = default;
};

template <typename Domain>
concept RevisionDomain = std::is_enum_v<Domain> && requires { Domain::count; };

template <RevisionDomain Domain> inline constexpr auto revisionDomainCount = static_cast<std::size_t>(Domain::count);

template <RevisionDomain Domain> struct RevisionMask
{
    std::array<bool, revisionDomainCount<Domain>> values{};

    template <Domain... Domains> [[nodiscard]] static consteval RevisionMask of() noexcept
    {
        auto mask = RevisionMask{};
        ((mask.values[static_cast<std::size_t>(Domains)] = true), ...);
        return mask;
    }

    [[nodiscard]] constexpr bool contains(Domain domain) const noexcept
    {
        return values[static_cast<std::size_t>(domain)];
    }

    [[nodiscard]] constexpr bool any() const noexcept
    {
        return std::ranges::any_of(values, std::identity{});
    }

    constexpr RevisionMask &operator|=(const RevisionMask &other) noexcept
    {
        auto indices = std::views::iota(std::size_t{0}, values.size());
        std::ranges::for_each(indices,
                              [&](std::size_t index) { values[index] = values[index] || other.values[index]; });
        return *this;
    }

    [[nodiscard]] friend constexpr RevisionMask operator|(RevisionMask lhs, const RevisionMask &rhs) noexcept
    {
        lhs |= rhs;
        return lhs;
    }

    [[nodiscard]] friend constexpr bool operator==(const RevisionMask &, const RevisionMask &) noexcept = default;
};

template <RevisionDomain Domain> struct RevisionSnapshot
{
    std::array<RevisionValue, revisionDomainCount<Domain>> values = [] {
        auto result = std::array<RevisionValue, revisionDomainCount<Domain>>{};
        result.fill(RevisionValue{});
        return result;
    }();

    template <Domain Value> [[nodiscard]] constexpr RevisionValue get() const noexcept
    {
        return values[static_cast<std::size_t>(Value)];
    }

    [[nodiscard]] constexpr RevisionValue get(Domain value) const noexcept
    {
        return values[static_cast<std::size_t>(value)];
    }

    [[nodiscard]] friend constexpr bool operator==(const RevisionSnapshot &,
                                                   const RevisionSnapshot &) noexcept = default;
};

template <RevisionDomain Domain> class RevisionSet
{
  public:
    constexpr RevisionSet() noexcept
    {
        values_.fill(RevisionValue{});
    }

    template <Domain Value> [[nodiscard]] constexpr RevisionValue get() const noexcept
    {
        return values_[static_cast<std::size_t>(Value)];
    }

    [[nodiscard]] constexpr RevisionValue get(Domain value) const noexcept
    {
        return values_[static_cast<std::size_t>(value)];
    }

    template <Domain... Values> constexpr void advance() noexcept
    {
        (advanceOne(Values), ...);
    }

    constexpr void advance(const RevisionMask<Domain> &mask) noexcept
    {
        auto indices = std::views::iota(std::size_t{0}, values_.size());
        std::ranges::for_each(indices, [&](std::size_t index) {
            if (mask.values[index])
            {
                advanceOne(static_cast<Domain>(index));
            }
        });
    }

    [[nodiscard]] constexpr RevisionSnapshot<Domain> snapshot() const noexcept
    {
        return RevisionSnapshot<Domain>{.values = values_};
    }

  private:
    constexpr void advanceOne(Domain domain) noexcept
    {
        auto &revision = values_[static_cast<std::size_t>(domain)].value;
        if (revision == std::numeric_limits<std::uint64_t>::max())
        {
            std::terminate();
        }
        ++revision;
    }

    std::array<RevisionValue, revisionDomainCount<Domain>> values_{};
};

template <RevisionDomain... Domains> struct RevisionBundleSnapshot
{
    std::tuple<RevisionSnapshot<Domains>...> sets{};

    template <RevisionDomain Domain> [[nodiscard]] constexpr const RevisionSnapshot<Domain> &get() const noexcept
    {
        return std::get<RevisionSnapshot<Domain>>(sets);
    }

    [[nodiscard]] friend constexpr bool operator==(const RevisionBundleSnapshot &,
                                                   const RevisionBundleSnapshot &) noexcept = default;
};

template <RevisionDomain... Domains> class RevisionBundle
{
  public:
    template <RevisionDomain Domain> [[nodiscard]] constexpr RevisionSet<Domain> &get() noexcept
    {
        return std::get<RevisionSet<Domain>>(sets_);
    }

    template <RevisionDomain Domain> [[nodiscard]] constexpr const RevisionSet<Domain> &get() const noexcept
    {
        return std::get<RevisionSet<Domain>>(sets_);
    }

    template <auto... Values> constexpr void advance() noexcept
    {
        (get<std::remove_cv_t<decltype(Values)>>().template advance<Values>(), ...);
    }

    [[nodiscard]] constexpr RevisionBundleSnapshot<Domains...> snapshot() const noexcept
    {
        return RevisionBundleSnapshot<Domains...>{
            .sets = std::tuple{get<Domains>().snapshot()...},
        };
    }

  private:
    std::tuple<RevisionSet<Domains>...> sets_{};
};

template <auto... Domains> struct RevisionProjection
{
    using Domain = std::remove_cv_t<std::tuple_element_t<0u, std::tuple<decltype(Domains)...>>>;
    static_assert((std::same_as<Domain, std::remove_cv_t<decltype(Domains)>> && ...));

    std::array<RevisionValue, sizeof...(Domains)> values{};

    [[nodiscard]] static constexpr RevisionProjection capture(const RevisionSnapshot<Domain> &snapshot) noexcept
    {
        return RevisionProjection{.values = {snapshot.template get<Domains>()...}};
    }

    [[nodiscard]] friend constexpr auto operator<=>(const RevisionProjection &,
                                                    const RevisionProjection &) noexcept = default;
};

template <RevisionDomain Domain>
[[nodiscard]] constexpr RevisionMask<Domain> diff(const RevisionSnapshot<Domain> &before,
                                                  const RevisionSnapshot<Domain> &after) noexcept
{
    auto result = RevisionMask<Domain>{};
    auto indices = std::views::iota(std::size_t{0}, revisionDomainCount<Domain>);
    std::ranges::for_each(
        indices, [&](std::size_t index) { result.values[index] = before.values[index] != after.values[index]; });
    return result;
}

template <typename... Parts> struct RevisionKey
{
    std::tuple<Parts...> parts{};

    [[nodiscard]] friend constexpr bool operator==(const RevisionKey &, const RevisionKey &) noexcept = default;
};

template <typename Policy, typename Mutation, typename Domain>
concept RevisionMutationPolicyFor = RevisionDomain<Domain> && requires(Mutation mutation) {
    { Policy::mask(mutation) } noexcept -> std::same_as<RevisionMask<Domain>>;
};

template <RevisionDomain Domain, typename Mutation, typename Policy>
    requires RevisionMutationPolicyFor<Policy, Mutation, Domain>
class RevisionBatch
{
  public:
    explicit constexpr RevisionBatch(RevisionSet<Domain> &revisions) noexcept : revisions_(revisions)
    {
    }

    RevisionBatch(const RevisionBatch &) = delete;
    RevisionBatch &operator=(const RevisionBatch &) = delete;
    RevisionBatch(RevisionBatch &&) = delete;
    RevisionBatch &operator=(RevisionBatch &&) = delete;

    template <Mutation Value> constexpr RevisionBatch &apply() noexcept
    {
        pending_ |= Policy::mask(Value);
        return *this;
    }

    constexpr RevisionBatch &apply(Mutation value) noexcept
    {
        pending_ |= Policy::mask(value);
        return *this;
    }

    constexpr void commit() noexcept
    {
        if (!committed_)
        {
            revisions_.get().advance(pending_);
            committed_ = true;
        }
    }

    [[nodiscard]] constexpr RevisionMask<Domain> pending() const noexcept
    {
        return pending_;
    }

  private:
    std::reference_wrapper<RevisionSet<Domain>> revisions_;
    RevisionMask<Domain> pending_{};
    bool committed_ = false;
};

struct RevisionSyntax
{
    template <auto... Domains> [[nodiscard]] constexpr auto revisionProjection(this const auto &self) noexcept
    {
        using Domain = std::remove_cv_t<std::tuple_element_t<0u, std::tuple<decltype(Domains)...>>>;
        return RevisionProjection<Domains...>::capture(self.revisions.template get<Domain>().snapshot());
    }

    [[nodiscard]] constexpr auto revisionSnapshot(this const auto &self) noexcept
    {
        return self.revisions.snapshot();
    }
};
} // namespace nr::revision
