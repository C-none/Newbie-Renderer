module materialAnyHitPoison;
import renderer.pathTracing.shadowPayload;
import test.rt.shadowRayTypeContract.abi;

[shader("anyhit")]
void ahMaterialPoison(inout ShadowRayPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    payload.occluded = kShadowRayTypeContractMaterialAnyHitPoison;
    gShadowRayTypeResults[DispatchRaysIndex().x] = kShadowRayTypeContractMaterialAnyHitPoison;
    AcceptHitAndEndSearch();
}
