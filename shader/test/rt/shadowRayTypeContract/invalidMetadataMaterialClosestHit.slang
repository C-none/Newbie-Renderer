module invalidMetadataMaterialClosestHit;
import renderer.pathTracing.pathState;
import test.rt.shadowRayTypeContract.abi;

[shader("closesthit")]
void chInvalidMetadataContract(inout MaterialRayPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    if (uint(GeometryIndex()) != kInvalidMetadataContractNearGeometryIndex)
    {
        gShadowRayTypeResults[kInvalidMetadataContractPrimaryMaterialResult] =
            kShadowRayTypeContractFixtureFailure;
        return;
    }

    payload.hit = true;
    gShadowRayTypeResults[kInvalidMetadataContractPrimaryMaterialResult] = kInvalidMetadataContractNearAccepted;
}
