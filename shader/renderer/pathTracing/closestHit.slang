module closestHit;
import common;
import renderer.pathTracing.params;
import renderer.pathTracing.pathState;
import renderer.pathTracing.hitShaders;

[shader("closesthit")]
void chMain(
    inout RayPayload payload,
    BuiltInTriangleIntersectionAttributes attributes)
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
