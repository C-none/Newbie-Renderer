module materialAnisotropyContract;
import common;

ConstantBuffer<RtMaterialHeader> gMaterialHeader;
RWStructuredBuffer<uint> gInstantiationResults;

uint instantiateMaterialChs<
    let LayerFlags : RtMaterialLayerFlag,
    let BaseLobeVariant : RtBaseLobeVariant>(
    ClosestHitInput input)
{
    MaterialCHS<LayerFlags, BaseLobeVariant> chs;
    return chs.handleClosestHit(input).scatter.valid;
}

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    ClosestHitInput input = {};
    input.material = gMaterialHeader;
    uint selector = dispatchThreadId.x & 15u;
    switch (selector)
    {
    case 0u: gInstantiationResults[0] = instantiateMaterialChs<RtMaterialLayerFlag(1u), RtBaseLobeVariant.isotropic>(input); break;
    case 1u: gInstantiationResults[1] = instantiateMaterialChs<RtMaterialLayerFlag(1u), RtBaseLobeVariant.anisotropic>(input); break;
    case 2u: gInstantiationResults[2] = instantiateMaterialChs<RtMaterialLayerFlag(3u), RtBaseLobeVariant.isotropic>(input); break;
    case 3u: gInstantiationResults[3] = instantiateMaterialChs<RtMaterialLayerFlag(3u), RtBaseLobeVariant.anisotropic>(input); break;
    case 4u: gInstantiationResults[4] = instantiateMaterialChs<RtMaterialLayerFlag(5u), RtBaseLobeVariant.isotropic>(input); break;
    case 5u: gInstantiationResults[5] = instantiateMaterialChs<RtMaterialLayerFlag(5u), RtBaseLobeVariant.anisotropic>(input); break;
    case 6u: gInstantiationResults[6] = instantiateMaterialChs<RtMaterialLayerFlag(7u), RtBaseLobeVariant.isotropic>(input); break;
    case 7u: gInstantiationResults[7] = instantiateMaterialChs<RtMaterialLayerFlag(7u), RtBaseLobeVariant.anisotropic>(input); break;
    case 8u: gInstantiationResults[8] = instantiateMaterialChs<RtMaterialLayerFlag(9u), RtBaseLobeVariant.isotropic>(input); break;
    case 9u: gInstantiationResults[9] = instantiateMaterialChs<RtMaterialLayerFlag(9u), RtBaseLobeVariant.anisotropic>(input); break;
    case 10u: gInstantiationResults[10] = instantiateMaterialChs<RtMaterialLayerFlag(11u), RtBaseLobeVariant.isotropic>(input); break;
    case 11u: gInstantiationResults[11] = instantiateMaterialChs<RtMaterialLayerFlag(11u), RtBaseLobeVariant.anisotropic>(input); break;
    case 12u: gInstantiationResults[12] = instantiateMaterialChs<RtMaterialLayerFlag(13u), RtBaseLobeVariant.isotropic>(input); break;
    case 13u: gInstantiationResults[13] = instantiateMaterialChs<RtMaterialLayerFlag(13u), RtBaseLobeVariant.anisotropic>(input); break;
    case 14u: gInstantiationResults[14] = instantiateMaterialChs<RtMaterialLayerFlag(15u), RtBaseLobeVariant.isotropic>(input); break;
    default: gInstantiationResults[15] = instantiateMaterialChs<RtMaterialLayerFlag(15u), RtBaseLobeVariant.anisotropic>(input); break;
    }
}
