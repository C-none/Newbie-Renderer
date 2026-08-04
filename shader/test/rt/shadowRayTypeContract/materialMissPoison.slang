module materialMissPoison;
import renderer.pathTracing.shadowPayload;
import test.rt.shadowRayTypeContract.abi;

[shader("miss")]
void msMaterialPoison(inout ShadowRayPayload payload)
{
    payload.occluded = kShadowRayTypeContractMaterialMissPoison;
    gShadowRayTypeResults[DispatchRaysIndex().x] = kShadowRayTypeContractMaterialMissPoison;
}
