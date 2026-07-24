module pathTracing;
import common;
import renderer.pathTracing.params;
import renderer.pathTracing.pathState;
import renderer.pathTracing.environment;
import renderer.pathTracing.scheduler;
import renderer.pathTracing.hitShaders;

[shader("raygeneration")]
void rgMain()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 dimensions = DispatchRaysDimensions().xy;
    Scheduler scheduler;
    scheduler.traceSample(pixel, dimensions);
}

[shader("miss")]
void msMain(inout RayPayload payload)
{
    payload.hit = 0u;
    // Camera-primary and later-bounce path segments both use RayKind.material, so the shared miss
    // entry returns the selected environment radiance for either kind of path escape.
    if (payload.rayKind == RayKind.material)
    {
        payload.missRadiance = sampleEnvironment(WorldRayDirection());
    }
}

[shader("anyhit")]
void ahAlphaMask(inout RayPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    handleAnyHit(payload, attributes);
}

[shader("closesthit")]
void chMain(inout RayPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    if (payload.rayKind != RayKind.material)
    {
        return;
    }

    CHS chs = CHS();
    ClosestHitInput input = makeClosestHitInput(attributes);
    input.scatterRandomValues = payload.scatterRandomValues;
    ClosestHitResult result = chs.handleClosestHit(input);
    payload.hit = result.hit;
    payload.material = result.material;
    payload.scatter = result.scatter;
}
