module reference;
import common;
import include.neuralAppearance.fixture;
import include.neuralAppearance.layout;

// The supervised target is the reflective part of the base surface lobe. The
// template intentionally omits RtMaterialLayerFlag.transmission so the whole
// transmission block is removed at compile time and the returned components stay
// pure reflection.
public ResolvedMaterialPayload neuralAppearanceFixturePayload(NeuralAppearanceFixture fixture)
{
    return ResolvedMaterialPayload(
        RtMaterialLayerFlag.baseSurface | RtMaterialLayerFlag.anisotropicBaseLobe,
        fixture.geometryNormal, fixture.shadingNormal, fixture.anisotropyTangent,
        saturate(fixture.anisotropyStrength), float4(saturate(fixture.baseColor), 1.0f),
        saturate(fixture.metallic), max(kMaterialMinRoughness, saturate(fixture.roughness)));
}

public NeuralAppearanceLobes neuralAppearanceReferenceLobes(
    NeuralAppearanceFixture fixture, float3 viewDirection, float3 lightDirection)
{
    BaseSurfaceBsdfLobe<
        RtMaterialLayerFlag.baseSurface | RtMaterialLayerFlag.anisotropicBaseLobe> baseSurface;
    ResolvedMaterialBsdfComponents components = baseSurface.evaluate(
        neuralAppearanceFixturePayload(fixture), viewDirection, lightDirection);
    return NeuralAppearanceLobes(components.diffuseProjected, components.specularProjected);
}
