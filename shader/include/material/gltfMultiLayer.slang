implementing common;

// Removed: GltfMultiLayerMaterial (legacy IMultiLayerMaterial implementation). RT material shading now
// flows through resolveUnlitMaterialPayload / resolveLitMaterialPayloadVariant<LayerFlags> and the
// layer-flag specialized BSDF variants in payload.slang. This file is intentionally empty and no longer
// included by common.slang.
