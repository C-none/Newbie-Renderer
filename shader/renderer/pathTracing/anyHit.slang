module anyHit;
import common;
import renderer.pathTracing.pathState;
import renderer.pathTracing.hitShaders;

[shader("anyhit")]
void ahMaterialPolicy(
    inout RayPayload payload,
    BuiltInTriangleIntersectionAttributes attributes)
{
    handleAnyHit(payload, attributes);
}
