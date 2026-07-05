module common;

// Aggregation module only. Put shared declarations in included subfiles.
__include include.globalUniform;
__include include.sceneTextures;
__include include.sceneLights;
__include include.materialTextureIds;
__include include.pathTracing.roulette;
__include include.pathTracing.mis;
__include include.rtMaterial;
__include include.material.types;
__include include.material.base;
__include include.material.sampling;
__include include.material.bsdf;
__include include.material.payload;
__include include.material.gltfMultiLayer;
