module visibility;
import common;
import renderer.pathTracing.params;
import renderer.pathTracing.resources;
import renderer.pathTracing.shadowPayload;

public bool tracePathVisibilityRay(float3 origin, float3 direction, float maxDistance)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = kRayEpsilon;
    ray.TMax = max(kRayEpsilon, maxDistance - kRayEpsilon);

    ShadowRayPayload payload = {};
    TraceRay(scene,
             RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
                 RAY_FLAG_CULL_BACK_FACING_TRIANGLES,
             0xFF, uint(RtRayType.shadow), uint(RtRayType.count), uint(RtRayType.shadow), ray, payload);
    return payload.occluded == 0u;
}
