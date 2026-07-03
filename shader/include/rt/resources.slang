module resources;
import common;

public StructuredBuffer<RtInstanceMetadata> rtInstanceMetadata;
public StructuredBuffer<RtGeometryMetadata> rtGeometryMetadata;
public StructuredBuffer<RtMaterialHeader> rtMaterialHeaders;
public StructuredBuffer<RtMaterialLayerRecord> rtMaterialLayers;
public StructuredBuffer<RtMaterialTextureRef> rtMaterialTextureRefs;
public ByteAddressBuffer rtVertexData;
public ByteAddressBuffer rtIndexData;
