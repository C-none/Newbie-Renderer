module anyHit;
import common;
import renderer.pathTracing.pathState;
import renderer.pathTracing.anyHitPolicy;

[shader("anyhit")]
void ahMaterialPolicy(inout MaterialRayPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    handleAnyHitPolicy(attributes);
}
