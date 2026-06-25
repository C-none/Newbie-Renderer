module nr.renderer;
import :rendererType;
import std;

namespace nr::renderer
{
[[nodiscard]] ResourceOwnershipDomain ownershipDomainFromQueue(QueueDomain queue) noexcept
{
    if (queue == QueueDomain::Graphics)
    {
        return ResourceOwnershipDomain::Graphics;
    }
    if (queue == QueueDomain::Compute)
    {
        return ResourceOwnershipDomain::Compute;
    }
    return ResourceOwnershipDomain::Transfer;
}
} // namespace nr::renderer
