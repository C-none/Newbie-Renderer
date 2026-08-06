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
[[nodiscard]] bool SceneBridgeGeometryBuffers::hasVertexBuffer() const noexcept
{
    return vertexBuffer.buffer.has_value();
}

[[nodiscard]] bool SceneBridgeGeometryBuffers::hasIndexBuffer() const noexcept
{
    return indexBuffer.buffer.has_value();
}

[[nodiscard]] bool SceneBridgeGeometryBuffers::valid() const noexcept
{
    return hasVertexBuffer();
}

[[nodiscard]] bool SceneBridgeDrawGeometry::indexed() const noexcept
{
    return indexCount > 0u;
}

[[nodiscard]] bool SceneBridgeDrawGeometry::valid() const noexcept
{
    return indexed() != (vertexCount > 0u);
}

[[nodiscard]] bool SceneAccelerationStructureMesh::hasVertexBuffer() const noexcept
{
    return vertexBuffer.buffer.has_value();
}

[[nodiscard]] bool SceneAccelerationStructureMesh::hasIndexBuffer() const noexcept
{
    return indexBuffer.buffer.has_value();
}
} // namespace nr::scene
