implementing common;

public interface IMultiLayerMaterial
{
    MaterialShadingResult evaluateDirect(
        StructuredBuffer<RtMaterialTextureRef> textureRefs,
        StructuredBuffer<RtMaterialLayerRecord> layerRecords,
        RtMaterialHeader material,
        RtSurfaceData surface,
        MaterialDirectLightingInput lighting);

    float alpha(
        StructuredBuffer<RtMaterialTextureRef> textureRefs,
        RtMaterialHeader material,
        RtSurfaceData surface);
}
