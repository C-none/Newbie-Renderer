module hitSurface;
import common;

public static const uint kRtVertexOffsetPosition = 0u;
public static const uint kRtVertexOffsetNormal = 12u;
public static const uint kRtVertexOffsetTangent = 24u;
public static const uint kRtVertexOffsetTexCoord0 = 40u;
public static const uint kRtVertexOffsetTexCoord1 = 48u;
public static const uint kRtVertexOffsetColor0 = 56u;

public struct RtHitSurface
{
    public RtInstanceMetadata instance;
    public RtGeometryMetadata geometry;
    public RtSurfaceData surface;
    public bool geometryValid;
};

public RtGeometryMetadata nrDefaultRtGeometryMetadata()
{
    RtGeometryMetadata geometry;
    geometry.materialIndex = kRtMaterialFallbackIndex;
    geometry.geometryIndex = 0u;
    geometry.primitiveOffset = 0u;
    geometry.firstVertex = 0u;
    geometry.primitiveCount = 0u;
    geometry.flags = 0u;
    geometry.reserved0 = 0u;
    geometry.reserved1 = 0u;
    return geometry;
}

public RtSurfaceData nrDefaultRtSurface(float3 viewDirection)
{
    RtSurfaceData surface;
    surface.position = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    surface.normal = viewDirection;
    surface.tangent = float3(1.0f, 0.0f, 0.0f);
    surface.tangentSign = 1.0f;
    surface.texCoord0 = float2(0.5f, 0.5f);
    surface.texCoord1 = surface.texCoord0;
    surface.color0 = float4(1.0f);
    return surface;
}

public RtInstanceMetadata nrCurrentRtInstance(StructuredBuffer<RtInstanceMetadata> instanceMetadata)
{
    return instanceMetadata[uint(InstanceID())];
}

public RtGeometryMetadata nrCurrentRtGeometry(
    StructuredBuffer<RtGeometryMetadata> geometryMetadata,
    RtInstanceMetadata instance,
    out bool geometryValid)
{
    uint geometryIndex = uint(GeometryIndex());
    geometryValid = geometryIndex < instance.geometryCount;
    if (geometryValid)
    {
        return geometryMetadata[instance.geometryOffset + geometryIndex];
    }

    return nrDefaultRtGeometryMetadata();
}

public float3 nrLoadRtVertexFloat3(ByteAddressBuffer vertexData, uint vertexByteOffset, uint fieldOffset)
{
    return asfloat(vertexData.Load3(vertexByteOffset + fieldOffset));
}

public float4 nrLoadRtVertexFloat4(ByteAddressBuffer vertexData, uint vertexByteOffset, uint fieldOffset)
{
    return asfloat(vertexData.Load4(vertexByteOffset + fieldOffset));
}

public float2 nrLoadRtVertexFloat2(ByteAddressBuffer vertexData, uint vertexByteOffset, uint fieldOffset)
{
    return asfloat(vertexData.Load2(vertexByteOffset + fieldOffset));
}

public uint nrRtVertexByteOffset(RtInstanceMetadata instance, uint vertexIndex)
{
    return (instance.vertexBase + vertexIndex) * instance.vertexStride;
}

public uint nrLoadRtTriangleVertexIndex(
    ByteAddressBuffer indexData,
    RtInstanceMetadata instance,
    RtGeometryMetadata geometry,
    uint primitiveIndex,
    uint lane)
{
    uint triangleVertex = primitiveIndex * 3u + lane;
    if ((geometry.flags & kRtGeometryFlagIndexed) != 0u)
    {
        uint indexByteOffset = (instance.indexBase + geometry.primitiveOffset + triangleVertex) * 4u;
        return indexData.Load(indexByteOffset) + geometry.firstVertex;
    }

    return geometry.primitiveOffset + triangleVertex;
}

public float3 nrTransformRtObjectVectorToWorld(float3 objectVector)
{
    float3x4 objectToWorld = ObjectToWorld3x4();
    return float3(
        dot(objectToWorld[0].xyz, objectVector),
        dot(objectToWorld[1].xyz, objectVector),
        dot(objectToWorld[2].xyz, objectVector));
}

public float3 nrTransformRtObjectPointToWorld(float3 objectPoint)
{
    float3x4 objectToWorld = ObjectToWorld3x4();
    return float3(
        dot(objectToWorld[0].xyz, objectPoint) + objectToWorld[0].w,
        dot(objectToWorld[1].xyz, objectPoint) + objectToWorld[1].w,
        dot(objectToWorld[2].xyz, objectPoint) + objectToWorld[2].w);
}

public RtSurfaceData nrInterpolateCurrentRtSurface(
    ByteAddressBuffer vertexData,
    ByteAddressBuffer indexData,
    RtInstanceMetadata instance,
    RtGeometryMetadata geometry,
    BuiltInTriangleIntersectionAttributes attributes)
{
    float3 barycentrics = float3(
        1.0f - attributes.barycentrics.x - attributes.barycentrics.y,
        attributes.barycentrics.x,
        attributes.barycentrics.y);
    uint primitiveIndex = uint(PrimitiveIndex());
    uint vertexIndex0 = nrLoadRtTriangleVertexIndex(indexData, instance, geometry, primitiveIndex, 0u);
    uint vertexIndex1 = nrLoadRtTriangleVertexIndex(indexData, instance, geometry, primitiveIndex, 1u);
    uint vertexIndex2 = nrLoadRtTriangleVertexIndex(indexData, instance, geometry, primitiveIndex, 2u);
    uint vertexOffset0 = nrRtVertexByteOffset(instance, vertexIndex0);
    uint vertexOffset1 = nrRtVertexByteOffset(instance, vertexIndex1);
    uint vertexOffset2 = nrRtVertexByteOffset(instance, vertexIndex2);

    float3 objectPosition =
        nrLoadRtVertexFloat3(vertexData, vertexOffset0, kRtVertexOffsetPosition) * barycentrics.x +
        nrLoadRtVertexFloat3(vertexData, vertexOffset1, kRtVertexOffsetPosition) * barycentrics.y +
        nrLoadRtVertexFloat3(vertexData, vertexOffset2, kRtVertexOffsetPosition) * barycentrics.z;
    float3 objectNormal =
        nrLoadRtVertexFloat3(vertexData, vertexOffset0, kRtVertexOffsetNormal) * barycentrics.x +
        nrLoadRtVertexFloat3(vertexData, vertexOffset1, kRtVertexOffsetNormal) * barycentrics.y +
        nrLoadRtVertexFloat3(vertexData, vertexOffset2, kRtVertexOffsetNormal) * barycentrics.z;
    float4 objectTangent0 = nrLoadRtVertexFloat4(vertexData, vertexOffset0, kRtVertexOffsetTangent);
    float4 objectTangent1 = nrLoadRtVertexFloat4(vertexData, vertexOffset1, kRtVertexOffsetTangent);
    float4 objectTangent2 = nrLoadRtVertexFloat4(vertexData, vertexOffset2, kRtVertexOffsetTangent);
    float3 objectTangent =
        objectTangent0.xyz * barycentrics.x +
        objectTangent1.xyz * barycentrics.y +
        objectTangent2.xyz * barycentrics.z;
    float tangentSign =
        objectTangent0.w * barycentrics.x +
        objectTangent1.w * barycentrics.y +
        objectTangent2.w * barycentrics.z;

    RtSurfaceData surface;
    surface.position = nrTransformRtObjectPointToWorld(objectPosition);
    surface.normal = nrSafeNormalize(nrTransformRtObjectVectorToWorld(objectNormal), float3(0.0f, 0.0f, 1.0f));
    surface.tangent = nrSafeNormalize(nrTransformRtObjectVectorToWorld(objectTangent), float3(1.0f, 0.0f, 0.0f));
    surface.tangentSign = tangentSign < 0.0f ? -1.0f : 1.0f;
    surface.texCoord0 =
        nrLoadRtVertexFloat2(vertexData, vertexOffset0, kRtVertexOffsetTexCoord0) * barycentrics.x +
        nrLoadRtVertexFloat2(vertexData, vertexOffset1, kRtVertexOffsetTexCoord0) * barycentrics.y +
        nrLoadRtVertexFloat2(vertexData, vertexOffset2, kRtVertexOffsetTexCoord0) * barycentrics.z;
    surface.texCoord1 =
        nrLoadRtVertexFloat2(vertexData, vertexOffset0, kRtVertexOffsetTexCoord1) * barycentrics.x +
        nrLoadRtVertexFloat2(vertexData, vertexOffset1, kRtVertexOffsetTexCoord1) * barycentrics.y +
        nrLoadRtVertexFloat2(vertexData, vertexOffset2, kRtVertexOffsetTexCoord1) * barycentrics.z;
    surface.color0 =
        nrLoadRtVertexFloat4(vertexData, vertexOffset0, kRtVertexOffsetColor0) * barycentrics.x +
        nrLoadRtVertexFloat4(vertexData, vertexOffset1, kRtVertexOffsetColor0) * barycentrics.y +
        nrLoadRtVertexFloat4(vertexData, vertexOffset2, kRtVertexOffsetColor0) * barycentrics.z;
    return surface;
}

public RtHitSurface nrCurrentRtHitSurface(
    StructuredBuffer<RtInstanceMetadata> instanceMetadata,
    StructuredBuffer<RtGeometryMetadata> geometryMetadata,
    ByteAddressBuffer vertexData,
    ByteAddressBuffer indexData,
    BuiltInTriangleIntersectionAttributes attributes,
    float3 viewDirection)
{
    RtHitSurface hit;
    hit.instance = nrCurrentRtInstance(instanceMetadata);
    hit.geometry = nrCurrentRtGeometry(geometryMetadata, hit.instance, hit.geometryValid);
    hit.surface = nrDefaultRtSurface(viewDirection);
    if (hit.geometryValid)
    {
        hit.surface = nrInterpolateCurrentRtSurface(
            vertexData,
            indexData,
            hit.instance,
            hit.geometry,
            attributes);
    }
    return hit;
}

public void nrOrientRtSurfaceForMaterial(inout RtSurfaceData surface, RtMaterialHeader material, float3 viewDirection)
{
    if (hasRtMaterialFeature(material, kRtMaterialFeatureDoubleSided) && dot(surface.normal, viewDirection) < 0.0f)
    {
        surface.normal = -surface.normal;
        surface.tangentSign = -surface.tangentSign;
    }
}
