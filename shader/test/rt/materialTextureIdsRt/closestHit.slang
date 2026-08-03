module closestHit;
import common;
import test.rt.materialTextureIdsRt.abi;

[shader("closesthit")]
void chMain(
    inout RayPayload payload,
    BuiltInTriangleIntersectionAttributes attributes)
{
    payload.color = float3(attributes.barycentrics.x, attributes.barycentrics.y, 0.0f);
}
