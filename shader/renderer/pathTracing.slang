module pathTracing;
import renderer.pathTracing.pathState;
import renderer.pathTracing.scheduler;
import renderer.pathTracing.hitShaders;

[shader("raygeneration")]
void rgMain()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 dimensions = DispatchRaysDimensions().xy;
    runPathTracingScheduler(pixel, dimensions);
}

[shader("miss")]
void msMain(inout PathTracingRayPayload payload)
{
    payload.hit = 0u;
}

[shader("anyhit")]
void ahMain(inout PathTracingRayPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    handlePathTracingAnyHit(payload, attributes);
}

[shader("closesthit")]
void chMain(inout PathTracingRayPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    handlePathTracingClosestHit(payload, attributes);
}
