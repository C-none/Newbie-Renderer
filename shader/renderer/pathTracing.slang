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
    payload.hit = false;
    // Camera-primary and later-bounce path segments both use RayKind.material, so the shared miss
    // entry returns the selected environment radiance for either kind of path escape.
    if (payload.kind == RayKind.material)
    {
        payload.missRadiance = sampleEnvironment(WorldRayDirection());
    }
}

[shader("anyhit")]
void ahMaterialPolicy(inout RayPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    handleAnyHit(payload, attributes);
}

[shader("closesthit")]
void chMain(inout RayPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    if (payload.kind != RayKind.material)
    {
        return;
    }

    CHS chs = CHS();
    ClosestHitInput input = makeClosestHitInput(attributes);
    // Copy every shared-lifetime input before the result overwrites the same physical payload slots.
    input.scatterRandomValues = payload.scatterRandomValues;
    input.materialFilterSequence = payload.materialFilterSequence;
    input.incidentMediumIor = payload.incidentMediumIor;
    input.exteriorMediumIor = payload.exteriorMediumIor;
    ClosestHitResult result = chs.handleClosestHit(input);
    if (result.hit != 0u)
    {
        writeResolvedMaterialRayPayload(
            payload,
            result.material,
            result.scatter);
    }
    else
    {
        payload.hit = false;
    }
}
