module scheduler;
import common;
import renderer.pathTracing.params;
import renderer.pathTracing.pathState;
import renderer.pathTracing.resources;
import renderer.pathTracing.core;

public void tracePathTracingMaterialRay(inout PathState path, out PathTracingRayPayload payload)
{
    path.vertexIndex += 1u;
    payload = defaultPathTracingRayPayload(kPathTracingRayKindMaterial);

    // TLAS instance flags encode glTF source winding, double-sided culling, and transform determinant.
    TraceRay(
        scene,
        RAY_FLAG_CULL_BACK_FACING_TRIANGLES,
        0xFF,
        0,
        0,
        0,
        pathStateRayDesc(path),
        payload);
}

public void tracePathTracingSample(uint2 pixel, uint2 dimensions)
{
    PathState path = generatePathTracingPath(pixel, dimensions);
    while (pathStateIsActive(path))
    {
        PathTracingRayPayload payload;
        tracePathTracingMaterialRay(path, payload);
        if (payload.hit != 0u)
        {
            handlePathTracingHit(path, payload.material);
        }
        else
        {
            handlePathTracingMiss(path);
        }
    }

    writePathTracingOutput(path);
}

public void runPathTracingScheduler(uint2 pixel, uint2 dimensions)
{
    tracePathTracingSample(pixel, dimensions);
}
