import std;
import dependency.math;
import nr.load;
import nr.resource;
import nr.rhi;
import nr.scene;
import nr.test;

namespace
{
template <typename RecordT>
concept HasSceneGpuLifecycle = requires(RecordT &record) {
    record.gpuVersion;
    record.gpuState;
    record.gpu;
    record.retiredGpu;
};

template <typename RecordT>
concept HasCpuVersion = requires(RecordT &record) { record.cpuVersion; };

static_assert(HasSceneGpuLifecycle<nr::scene::MeshAssetRecord>);
static_assert(HasSceneGpuLifecycle<nr::scene::TextureAssetRecord>);
static_assert(!HasSceneGpuLifecycle<nr::scene::MaterialAssetRecord>);
static_assert(!HasSceneGpuLifecycle<nr::scene::CameraAssetRecord>);
static_assert(!HasSceneGpuLifecycle<nr::scene::LightAssetRecord>);
static_assert(HasCpuVersion<nr::scene::MaterialAssetRecord>);
static_assert(!HasCpuVersion<nr::scene::CameraAssetRecord>);
static_assert(!HasCpuVersion<nr::scene::LightAssetRecord>);

[[nodiscard]] bool almostEqual(float lhs, float rhs, float epsilon = 1e-4f) noexcept
{
    return std::abs(lhs - rhs) <= epsilon;
}

[[nodiscard]] nr::scene::SceneLightGpuRecord makeAliasTableTestLight(DirectX::XMFLOAT3 color,
                                                                       float intensity) noexcept
{
    auto record = nr::scene::SceneLightGpuRecord{};
    record.colorIntensity = DirectX::XMFLOAT4{color.x, color.y, color.z, intensity};
    return record;
}

const nr::test::CaseRegistrar sceneLightAliasGpuAbiCase{
    "scene light alias table gpu abi and energy weighting are stable", [] {
        nr::test::requireEqual(nr::scene::kSceneLightGpuAbiVersion, std::uint32_t{3u});
        nr::test::requireEqual(sizeof(nr::scene::SceneLightGpuHeader), std::size_t{16u});
        nr::test::requireEqual(sizeof(nr::scene::SceneLightGpuRecord), std::size_t{80u});
        nr::test::requireEqual(sizeof(nr::scene::SceneLightAliasGpuRecord), std::size_t{32u});

        nr::test::require(almostEqual(nr::scene::sceneLightAliasEnergy(DirectX::XMFLOAT3{1.0f, 1.0f, 1.0f}, 2.0f), 2.0f),
                          "white light alias energy should equal intensity");
        nr::test::require(almostEqual(nr::scene::sceneLightAliasEnergy(DirectX::XMFLOAT3{0.0f, 1.0f, 0.0f}, 2.0f), 1.4304f),
                          "green light alias energy should use Rec.709 luminance");
        nr::test::require(almostEqual(nr::scene::sceneLightAliasEnergy(DirectX::XMFLOAT3{1.0f, 1.0f, 1.0f}, -2.0f), 0.0f),
                          "negative light intensity should have zero alias energy");

        auto emptyTable = nr::scene::buildSceneLightAliasTable(std::span<const nr::scene::SceneLightGpuRecord>{});
        nr::test::requireEqual(emptyTable.aliasCount, std::uint32_t{0u});
        nr::test::require(almostEqual(emptyTable.totalEnergy, 0.0f), "empty alias table total energy should be zero");
        nr::test::requireEqual(emptyTable.records.size(), std::size_t{1u});

        auto zeroEnergyRecords = std::array{
            makeAliasTableTestLight(DirectX::XMFLOAT3{1.0f, 1.0f, 1.0f}, 0.0f),
            makeAliasTableTestLight(DirectX::XMFLOAT3{}, 5.0f),
        };
        auto zeroEnergyTable = nr::scene::buildSceneLightAliasTable(zeroEnergyRecords);
        nr::test::requireEqual(zeroEnergyTable.aliasCount, std::uint32_t{0u});
        nr::test::require(almostEqual(zeroEnergyTable.totalEnergy, 0.0f),
                          "zero-energy alias table total should be zero");
        nr::test::requireEqual(zeroEnergyTable.records.size(), std::size_t{1u});

        auto weightedRecords = std::array{
            makeAliasTableTestLight(DirectX::XMFLOAT3{1.0f, 1.0f, 1.0f}, 1.0f),
            makeAliasTableTestLight(DirectX::XMFLOAT3{1.0f, 1.0f, 1.0f}, 3.0f),
        };
        auto weightedTable = nr::scene::buildSceneLightAliasTable(weightedRecords);
        nr::test::requireEqual(weightedTable.aliasCount, std::uint32_t{2u});
        nr::test::require(almostEqual(weightedTable.totalEnergy, 4.0f),
                          "weighted alias table total energy should sum weights");
        nr::test::requireEqual(weightedTable.records.size(), std::size_t{2u});

        auto const &firstAlias = weightedTable.records[0];
        nr::test::requireEqual(firstAlias.meta.x, std::uint32_t{0u});
        nr::test::requireEqual(firstAlias.meta.y, std::uint32_t{1u});
        nr::test::require(almostEqual(firstAlias.probabilities.x, 0.5f), "first alias accept threshold should be 0.5");
        nr::test::require(almostEqual(firstAlias.probabilities.y, 0.25f), "first alias primary pdf should be 0.25");
        nr::test::require(almostEqual(firstAlias.probabilities.z, 0.75f), "first alias secondary pdf should be 0.75");

        auto const &secondAlias = weightedTable.records[1];
        nr::test::requireEqual(secondAlias.meta.x, std::uint32_t{1u});
        nr::test::requireEqual(secondAlias.meta.y, std::uint32_t{1u});
        nr::test::require(almostEqual(secondAlias.probabilities.x, 1.0f), "second alias should always accept primary");
        nr::test::require(almostEqual(secondAlias.probabilities.y, 0.75f), "second alias primary pdf should be 0.75");
        nr::test::require(almostEqual(secondAlias.probabilities.z, 0.75f), "second alias secondary pdf should be 0.75");
    }};

} // namespace
