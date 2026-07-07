module visibility;
import common;
import renderer.pathTracing.params;
import renderer.pathTracing.pathState;
import renderer.pathTracing.resources;

public bool tracePathVisibilityRay(float3 origin, float3 direction, float maxDistance)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = kPathTracingRayEpsilon;
    ray.TMax = max(kPathTracingRayEpsilon, maxDistance - kPathTracingRayEpsilon);

    PathTracingRayPayload payload = {};
    payload.rayKind = PathTracingRayKind.visibility;
    payload.hit = 1u;
    TraceRay(
        scene,
        RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
        0xFF,
        0,
        1,
        0,
        ray,
        payload);
    return payload.hit == 0u;
}

public bool tracePathVisibilityRayToLight(float3 origin, SceneLightContribution contribution)
{
    return tracePathVisibilityRay(origin, contribution.direction, kPathTracingRayTMax);
}
