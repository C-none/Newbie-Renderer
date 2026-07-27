import std;
import nr.test;
import nr.utils;

namespace
{
enum class TestDomain : std::uint8_t
{
    topology,
    transform,
    material,
    count,
};

enum class OtherDomain : std::uint8_t
{
    allocation,
    count,
};

enum class TestMutation : std::uint8_t
{
    move,
    replaceMaterial,
};

struct TestPolicy
{
    [[nodiscard]] static constexpr nr::revision::RevisionMask<TestDomain> mask(TestMutation mutation) noexcept
    {
        using Mask = nr::revision::RevisionMask<TestDomain>;
        switch (mutation)
        {
        case TestMutation::move:
            return Mask::of<TestDomain::transform>();
        case TestMutation::replaceMaterial:
            return Mask::of<TestDomain::topology, TestDomain::material>();
        default:
            return {};
        }
    }
};

struct TrackedState : nr::revision::RevisionSyntax
{
    nr::revision::RevisionBundle<TestDomain> revisions{};
};

static_assert(sizeof(nr::revision::RevisionSet<TestDomain>) == sizeof(std::uint64_t) * 3u);
static_assert(sizeof(nr::revision::RevisionMask<TestDomain>) == sizeof(bool) * 3u);

const nr::test::CaseRegistrar revisionSetCase{"utils revision set snapshots and projections use exact fixed-storage values", [] {
                                                  auto state = TrackedState{};
                                                  auto before = state.revisionSnapshot().get<TestDomain>();
                                                  nr::test::require(before.get<TestDomain::topology>().valid());

                                                  state.revisions.advance<TestDomain::transform>();
                                                  auto after = state.revisionSnapshot().get<TestDomain>();
                                                  auto changed = nr::revision::diff(before, after);
                                                  nr::test::require(changed.contains(TestDomain::transform));
                                                  nr::test::require(!changed.contains(TestDomain::topology));

                                                  auto dynamicProjection = state.revisionProjection<TestDomain::transform>();
                                                  auto stableProjection = state.revisionProjection<TestDomain::topology, TestDomain::material>();
                                                  nr::test::requireEqual(dynamicProjection.values[0].value, std::uint64_t{2u});
                                                  nr::test::requireEqual(stableProjection.values[0].value, std::uint64_t{1u});
                                                  nr::test::requireEqual(stableProjection.values[1].value, std::uint64_t{1u});
                                              }};

const nr::test::CaseRegistrar revisionBatchCase{"utils revision batches advance policy domains only after explicit commit", [] {
                                                    auto revisions = nr::revision::RevisionSet<TestDomain>{};
                                                    {
                                                        auto batch = nr::revision::RevisionBatch<TestDomain, TestMutation, TestPolicy>{revisions};
                                                        batch.apply<TestMutation::replaceMaterial>();
                                                    }
                                                    nr::test::requireEqual(revisions.get<TestDomain::topology>().value, std::uint64_t{1u});

                                                    auto batch = nr::revision::RevisionBatch<TestDomain, TestMutation, TestPolicy>{revisions};
                                                    batch.apply<TestMutation::replaceMaterial>();
                                                    batch.apply<TestMutation::replaceMaterial>();
                                                    batch.commit();
                                                    nr::test::requireEqual(revisions.get<TestDomain::topology>().value, std::uint64_t{2u});
                                                    nr::test::requireEqual(revisions.get<TestDomain::material>().value, std::uint64_t{2u});
                                                    nr::test::requireEqual(revisions.get<TestDomain::transform>().value, std::uint64_t{1u});
                                                }};

const nr::test::CaseRegistrar revisionBundleCase{"utils revision bundles aggregate heterogeneous domains without custom owner logic", [] {
                                                     auto revisions = nr::revision::RevisionBundle<TestDomain, OtherDomain>{};
                                                     revisions.advance<TestDomain::topology, OtherDomain::allocation>();
                                                     auto snapshot = revisions.snapshot();
                                                     nr::test::requireEqual(snapshot.get<TestDomain>().get<TestDomain::topology>().value, std::uint64_t{2u});
                                                     nr::test::requireEqual(snapshot.get<OtherDomain>().get<OtherDomain::allocation>().value, std::uint64_t{2u});
                                                 }};
} // namespace
