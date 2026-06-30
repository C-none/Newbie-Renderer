module nr.renderer;
import :rendererType;
import nr.rhi;
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

[[nodiscard]] nr::rhi::QueueRole rhiQueueRoleFromDomain(QueueDomain queue) noexcept
{
    if (queue == QueueDomain::Graphics)
    {
        return nr::rhi::QueueRole::Graphics;
    }
    if (queue == QueueDomain::Compute)
    {
        return nr::rhi::QueueRole::Compute;
    }
    return nr::rhi::QueueRole::Transfer;
}
} // namespace nr::renderer
