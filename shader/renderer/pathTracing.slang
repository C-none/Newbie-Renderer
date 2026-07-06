module pathTracing;
import common;
import renderer.pathTracing.params;
import renderer.pathTracing.pathState;
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
void msMain(inout PathTracingRayPayload payload)
{
    payload.hit = 0u;
}

[shader("anyhit")]
void ahAlphaMask(inout PathTracingRayPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    handlePathTracingAnyHit(payload, attributes);
}

[shader("closesthit")]
void chMain(inout PathTracingRayPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    if (payload.rayKind != kPathTracingRayKindMaterial)
    {
        return;
    }

    CHS chs = CHS();
    PathTracingClosestHitResult result = chs.handleClosestHit(makePathTracingClosestHitInput(attributes));
    payload.hit = result.hit;
    payload.material = result.material;
}
