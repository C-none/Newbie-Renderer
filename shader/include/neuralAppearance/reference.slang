module reference;
import common;
import include.neuralAppearance.fixture;

public ResolvedMaterialPayload neuralAppearanceFixtureMaterial(float2 uv)
{
    NeuralAppearanceFixture fixture = neuralAppearanceFixture(uv);

    return ResolvedMaterialPayload(
        RtMaterialLayerFlag.baseSurface | RtMaterialLayerFlag.anisotropicBaseLobe, fixture.geometryNormal,
        fixture.shadingNormal, fixture.anisotropyTangent, fixture.anisotropyStrength,
        float4(saturate(fixture.f0), 1.0f), 1.0f, max(kMaterialMinRoughness, saturate(fixture.roughness)));
}

public float3 neuralAppearanceReferenceSpecularProjected(
    float2 uv, float3 viewDirection, float3 lightDirection)
{
    ResolvedMaterialPayload material = neuralAppearanceFixtureMaterial(uv);
    BaseSurfaceBsdfLobe<
        RtMaterialLayerFlag.baseSurface | RtMaterialLayerFlag.anisotropicBaseLobe> baseSurface;
    return baseSurface.evaluate(material, viewDirection, lightDirection).specularProjected;
}
