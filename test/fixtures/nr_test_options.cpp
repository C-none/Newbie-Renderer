module nr.test.options;

import nr.test;
import std;

namespace nr::test::options
{
std::shared_ptr<const nr::options::OptionCatalog> buildCatalog(
    std::span<const nr::options::OptionDefinition> definitions)
{
    auto builder = nr::options::OptionCatalogBuilder{};
    std::ranges::for_each(definitions, [&](const nr::options::OptionDefinition &definition) {
        nr::test::require(builder.add(definition), "option definition should be accepted");
    });

    auto result = builder.build();
    nr::test::require(result.valid(), "option catalog should build");
    return std::move(result.catalog);
}

nr::options::OptionAvailabilityMap allAvailable(const nr::options::OptionCatalog &catalog)
{
    auto result = nr::options::OptionAvailabilityMap{};
    std::ranges::for_each(catalog.definitions(), [&](const auto &entry) {
        result.emplace(entry.first, nr::options::OptionAvailability{.available = true, .reason = {}});
    });
    return result;
}

nr::options::OptionFrameSnapshot makeDefaultSnapshot(std::shared_ptr<const nr::options::OptionCatalog> catalog,
                                                     std::string snapshotToken)
{
    auto values = nr::options::OptionValueMap{};
    std::ranges::for_each(catalog->definitions(),
                          [&](const auto &entry) { values.emplace(entry.first, entry.second.defaultValue); });
    auto availability = allAvailable(*catalog);

    return nr::options::OptionFrameSnapshot{
        .catalog = std::move(catalog),
        .values = std::move(values),
        .availability = std::move(availability),
        .frameIndex = 1u,
        .revision = 1u,
        .graphGeneration = 1u,
        .bindingEpoch = 1u,
        .snapshotToken = std::move(snapshotToken),
    };
}
} // namespace nr::test::options
