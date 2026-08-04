implementing common;

[[vk::binding(1, 3)]]
public StructuredBuffer<RtInstanceMetadata> rtInstanceMetadata;

[[vk::binding(2, 3)]]
public StructuredBuffer<RtGeometryMetadata> rtGeometryMetadata;

[[vk::binding(3, 3)]]
public StructuredBuffer<RtMaterialHeader> rtMaterialHeaders;

[[vk::binding(4, 3)]]
public StructuredBuffer<RtMaterialLayerRecord> rtMaterialLayers;

[[vk::binding(5, 3)]]
public StructuredBuffer<RtMaterialTextureRef> rtMaterialTextureRefs;

[[vk::binding(6, 3)]]
public ByteAddressBuffer rtVertexData;

[[vk::binding(7, 3)]]
public ByteAddressBuffer rtIndexData;
