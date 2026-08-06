export module nr.test.options;

import nr.options;
import std;

export namespace nr::test::options
{
[[nodiscard]] std::shared_ptr<const nr::options::OptionCatalog> buildCatalog(
    std::span<const nr::options::OptionDefinition> definitions);

[[nodiscard]] nr::options::OptionAvailabilityMap allAvailable(const nr::options::OptionCatalog &catalog);

[[nodiscard]] nr::options::OptionFrameSnapshot makeDefaultSnapshot(
    std::shared_ptr<const nr::options::OptionCatalog> catalog, std::string snapshotToken);
} // namespace nr::test::options
