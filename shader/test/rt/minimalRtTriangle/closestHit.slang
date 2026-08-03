module closestHit;
import common;
import test.rt.minimalRtTriangle.abi;

[shader("closesthit")]
void chMain(
    inout RayPayload payload,
    BuiltInTriangleIntersectionAttributes attributes)
{
    float3 barycentrics = float3(
        1.0 - attributes.barycentrics.x - attributes.barycentrics.y,
        attributes.barycentrics.x,
        attributes.barycentrics.y);
    payload.color = barycentrics;
}
