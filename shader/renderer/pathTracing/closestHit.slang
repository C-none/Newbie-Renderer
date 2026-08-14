module closestHit;
import common;
import renderer.pathTracing.pathState;
import renderer.pathTracing.neuralMaterial;
import include.rt.hitSurface;

[require(spvCooperativeVectorNV)]
[shader("closesthit")]
void chMain(inout MaterialRayPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    CHS chs = CHS();
    float3 viewDirection = normalize(-WorldRayDirection());
    RtHitSurface hit = currentRtHitSurface(attributes, viewDirection);
    RtMaterialHeader material = {};
    if (hit.geometryValid)
    {
        material = rtMaterialHeaders[hit.geometry.materialIndex];
        orientRtSurfaceForMaterial(hit.surface, viewDirection);
    }

    // Copy every shared-lifetime input before the result overwrites the same physical payload slots.
    ClosestHitInput input = ClosestHitInput(
        hit.geometryValid, material, hit.surface, viewDirection, hit.geometryNormal, hit.frontFace,
        hit.geometry.materialIndex, uint(InstanceID()), uint(GeometryIndex()), payload.scatterRandomValues,
        payload.materialFilterSequence, payload.incidentMediumIor, payload.exteriorMediumIor);

    ClosestHitResult result = chs.handleClosestHit(input);
    if (result.hit != 0u)
    {
        RtMaterialTextureRef baseColorTexture =
            rtMaterialTextureRef(rtMaterialTextureRefs, material, MaterialTextureSlot.baseColor);
        float2 neuralUv = materialTextureUv(baseColorTexture, hit.surface.texCoord0, hit.surface.texCoord1);
        // Evaluate in the exact frame that crosses the material-ray payload boundary:
        // the mesh tangent (rather than the analytic anisotropy fallback) and
        // shading normal are first projected through their transport encodings.
        uint neuralNormalOct32 = packPayloadOct32(result.material.shadingNormal, float3(0.0f, 1.0f, 0.0f));
        float3 neuralNormal = unpackPayloadOct32(neuralNormalOct32, float3(0.0f, 1.0f, 0.0f));
        uint neuralTangentOct32 = packPayloadOct32(hit.surface.tangent, orthogonalTangent(neuralNormal));
        float3 neuralTangent = unpackPayloadOct32(neuralTangentOct32, orthogonalTangent(neuralNormal));
        neuralTangent = safeNormalize(neuralTangent - neuralNormal * dot(neuralNormal, neuralTangent),
                                      orthogonalTangent(neuralNormal));
        // Keep the analytic sampler/PDF untouched. At this point the CHS still
        // owns the full-precision proposal density, so continuation can replace
        // only the projected specular numerator before payload compaction.
        if (rtNeuralMaterialEnabled(result.material.materialIndex) && rtNeuralP0Eligible(result.material) &&
            result.scatter.valid && !result.scatter.delta && result.scatter.pdf > 1.0e-6f)
        {
            bool neuralValid = false;
            float3 neuralProjected = evaluateRtNeuralProjected(
                packRtNeuralRepeatUv(neuralUv), neuralTangent, neuralNormal,
                hit.surface.tangentSign, input.viewDirection, result.scatter.direction, neuralValid);
            if (neuralValid)
            {
                replaceRtAnalyticSpecularWithNeuralProjected(result.scatter, neuralProjected);
            }
        }
        writeRtNeuralMaterialRayPayload(payload, result.material, result.scatter, neuralUv, neuralNormalOct32,
                                        neuralTangentOct32, hit.surface.tangentSign);
    }
    else
    {
        payload.hit = false;
    }
}
