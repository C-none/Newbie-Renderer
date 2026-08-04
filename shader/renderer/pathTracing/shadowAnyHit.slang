module shadowAnyHit;
import common;
import renderer.pathTracing.anyHitPolicy;
import renderer.pathTracing.shadowPayload;

[shader("anyhit")]
void ahShadow(inout ShadowRayPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    handleAnyHitPolicy(attributes);
}
