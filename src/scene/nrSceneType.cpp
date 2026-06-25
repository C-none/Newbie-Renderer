module nr.scene;
import :type;
import dependency.math;
import dependency.ecs;
import dependency.vulkan;
import nr.load;
import nr.resource;
import nr.rhi;
import std;

namespace nr::scene
{
[[nodiscard]] bool SceneBridgeDrawGeometry::hasVertexBuffer() const noexcept
{
        return vertexBuffer.buffer.has_value();
    }

[[nodiscard]] bool SceneBridgeDrawGeometry::hasIndexBuffer() const noexcept
{
        return indexBuffer.buffer.has_value() && indexCount > 0;
    }
} // namespace nr::scene
