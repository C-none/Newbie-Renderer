module materialFilterAfterShadingContract;
import common;
import renderer.pathTracing.pathState;

ConstantBuffer<RtMaterialHeader> gMaterialHeader;
RWStructuredBuffer<uint> gContractResults;

public uint verifyMaterialFilterPacketAdvance()
{
    RandomSequence reference;
    reference.sampleIndex = 0x13579bdfu;
    reference.sampleSeed = 0x2468ace0u;
    RandomSequence advanced = reference;

    float4 discarded0 = reference.rand4();
    float4 discarded1 = reference.rand4();
    float4 discarded2 = reference.rand4();
    advanced.advanceThreeRand4Packets();

    // Keep the discarded values live so this contract compiles both paths.
    uint finiteMask =
        uint(all(isfinite(discarded0))) &
        uint(all(isfinite(discarded1))) &
        uint(all(isfinite(discarded2)));
    return
        finiteMask &
        uint(reference.sampleIndex == advanced.sampleIndex) &
        uint(reference.sampleSeed == advanced.sampleSeed);
}

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    ClosestHitInput input = {};
    input.material = gMaterialHeader;
    input.geometryValid = gMaterialHeader.textureRefCount != 0xffffffffu;
    MaterialCHS<RtMaterialLayerFlag(31u)> fasChs;
    ClosestHitResult hit = fasChs.handleClosestHit(input);

    gContractResults[0] = verifyMaterialFilterPacketAdvance();
    gContractResults[1] =
        hit.scatter.valid
            ? 1u
            : 0u;

    RandomSequence filterSequence;
    filterSequence.sampleIndex = 0x12345678u;
    filterSequence.sampleSeed = 0x9abcdef0u;
    RayPayload rayPayload;
    initializeMaterialRayPayload(
        rayPayload,
        float4(0.125f, 0.25f, 0.5f, 0.75f),
        filterSequence,
        1.0f,
        1.5f);
    uint inputRoundTrip =
        uint(all(rayPayload.scatterRandomValues ==
            float4(0.125f, 0.25f, 0.5f, 0.75f))) &
        uint(rayPayload.materialFilterSequence.sampleIndex ==
            filterSequence.sampleIndex) &
        uint(rayPayload.materialFilterSequence.sampleSeed ==
            filterSequence.sampleSeed) &
        uint(rayPayload.incidentMediumIor == 1.0f) &
        uint(rayPayload.exteriorMediumIor == 1.5f);
    writeResolvedMaterialRayPayload(rayPayload, hit.material, hit.scatter);
    ResolvedMaterialPayload materialRoundTrip =
        resolvedMaterialFromRayPayload(rayPayload, 1.0f, 1.5f);
    ResolvedMaterialScatter scatterRoundTrip =
        resolvedMaterialScatterFromRayPayload(rayPayload);
    float3 expectedScatterDirection = safePayloadDirection(
        hit.scatter.direction,
        float3(0.0f, 1.0f, 0.0f));
    gContractResults[2] =
        inputRoundTrip &
        uint(rayPayload.hit) &
        uint(materialRoundTrip.materialIndex == hit.material.materialIndex) &
        uint(materialRoundTrip.incidentMediumIor == 1.0f) &
        uint(materialRoundTrip.exteriorMediumIor == 1.5f) &
        uint(dot(scatterRoundTrip.direction, expectedScatterDirection) > 0.999f);
}
