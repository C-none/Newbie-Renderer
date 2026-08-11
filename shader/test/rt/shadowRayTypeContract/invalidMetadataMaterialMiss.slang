module invalidMetadataMaterialMiss;
import renderer.pathTracing.pathState;
import test.rt.shadowRayTypeContract.abi;

[shader("miss")]
void msInvalidMetadataContract(inout MaterialRayPayload payload)
{
    payload.hit = false;
    gShadowRayTypeResults[kInvalidMetadataContractControlMaterialResult] = kInvalidMetadataContractMaterialMiss;
}
