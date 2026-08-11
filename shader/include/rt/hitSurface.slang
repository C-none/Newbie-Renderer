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
    public RtInstanceMetadata instance = {};
    public RtGeometryMetadata geometry = {};
    public RtSurfaceData surface = RtSurfaceData();
    public float3 geometryNormal = float3(0.0f, 1.0f, 0.0f);
    public bool frontFace = true;
    public bool geometryValid = false;

    public __init(RtInstanceMetadata instance, RtGeometryMetadata geometry, RtSurfaceData surface, float3 geometryNormal,
           bool frontFace, bool geometryValid)
    {
        this.instance = instance;
        this.geometry = geometry;
        this.surface = surface;
        this.geometryNormal = geometryNormal;
        this.frontFace = frontFace;
        this.geometryValid = geometryValid;
    }
};

public RtInstanceMetadata currentRtInstance()
{
    return rtInstanceMetadata[uint(InstanceID())];
}

public RtGeometryMetadata currentRtGeometry(RtInstanceMetadata instance, out bool geometryValid)
{
    uint geometryIndex = uint(GeometryIndex());
    geometryValid = geometryIndex < instance.geometryCount;
    if (geometryValid)
    {
        return rtGeometryMetadata[instance.geometryOffset + geometryIndex];
    }

    return {};
}

public float3 loadRtVertexFloat3(uint vertexByteOffset, uint fieldOffset)
{
    return asfloat(rtVertexData.Load3(vertexByteOffset + fieldOffset));
}

public float4 loadRtVertexFloat4(uint vertexByteOffset, uint fieldOffset)
{
    return asfloat(rtVertexData.Load4(vertexByteOffset + fieldOffset));
}

public float2 loadRtVertexFloat2(uint vertexByteOffset, uint fieldOffset)
{
    return asfloat(rtVertexData.Load2(vertexByteOffset + fieldOffset));
}

public uint rtVertexByteOffset(RtInstanceMetadata instance, uint vertexIndex)
{
    return (instance.vertexBase + vertexIndex) * instance.vertexStride;
}

public uint loadRtTriangleVertexIndex(RtInstanceMetadata instance, RtGeometryMetadata geometry, uint primitiveIndex,
                                      uint lane)
{
    uint triangleVertex = primitiveIndex * 3u + lane;
    if (uint(geometry.flags & RtGeometryFlag.indexed) != 0u)
    {
        uint indexByteOffset = (instance.indexBase + geometry.primitiveOffset + triangleVertex) * 4u;
        return rtIndexData.Load(indexByteOffset) + geometry.firstVertex;
    }

    return geometry.primitiveOffset + triangleVertex;
}

public float3 transformRtObjectVectorToWorld(float3 objectVector)
{
    // Vulkan ray-tracing built-ins remain in their API-defined 3x4 column-vector convention. Keep the conversion
    // localized here rather than mixing that convention with project row-vector CPU/GPU buffer ABI.
    float3x4 objectToWorld = ObjectToWorld3x4();
    return float3(dot(objectToWorld[0].xyz, objectVector), dot(objectToWorld[1].xyz, objectVector),
                  dot(objectToWorld[2].xyz, objectVector));
}

public float3 transformRtObjectNormalToWorld(float3 objectNormal)
{
    float3x4 worldToObject = WorldToObject3x4();
    return float3(dot(float3(worldToObject[0].x, worldToObject[1].x, worldToObject[2].x), objectNormal),
                  dot(float3(worldToObject[0].y, worldToObject[1].y, worldToObject[2].y), objectNormal),
                  dot(float3(worldToObject[0].z, worldToObject[1].z, worldToObject[2].z), objectNormal));
}

public float3 transformRtObjectPointToWorld(float3 objectPoint)
{
    float3x4 objectToWorld = ObjectToWorld3x4();
    return float3(dot(objectToWorld[0].xyz, objectPoint) + objectToWorld[0].w,
                  dot(objectToWorld[1].xyz, objectPoint) + objectToWorld[1].w,
                  dot(objectToWorld[2].xyz, objectPoint) + objectToWorld[2].w);
}

public float rtObjectToWorldHandedness()
{
    float3x4 objectToWorld = ObjectToWorld3x4();
    float determinant = dot(objectToWorld[0].xyz, cross(objectToWorld[1].xyz, objectToWorld[2].xyz));
    return determinant < 0.0f ? -1.0f : 1.0f;
}

public RtSurfaceData interpolateCurrentRtSurface(RtInstanceMetadata instance, RtGeometryMetadata geometry,
                                                 BuiltInTriangleIntersectionAttributes attributes,
                                                 out float3 geometryNormal)
{
    float3 barycentrics = float3(1.0f - attributes.barycentrics.x - attributes.barycentrics.y,
                                 attributes.barycentrics.x, attributes.barycentrics.y);
    uint primitiveIndex = uint(PrimitiveIndex());
    uint vertexIndex0 = loadRtTriangleVertexIndex(instance, geometry, primitiveIndex, 0u);
    uint vertexIndex1 = loadRtTriangleVertexIndex(instance, geometry, primitiveIndex, 1u);
    uint vertexIndex2 = loadRtTriangleVertexIndex(instance, geometry, primitiveIndex, 2u);
    uint vertexOffset0 = rtVertexByteOffset(instance, vertexIndex0);
    uint vertexOffset1 = rtVertexByteOffset(instance, vertexIndex1);
    uint vertexOffset2 = rtVertexByteOffset(instance, vertexIndex2);

    float3 objectPosition0 = loadRtVertexFloat3(vertexOffset0, kRtVertexOffsetPosition);
    float3 objectPosition1 = loadRtVertexFloat3(vertexOffset1, kRtVertexOffsetPosition);
    float3 objectPosition2 = loadRtVertexFloat3(vertexOffset2, kRtVertexOffsetPosition);
    float3 objectPosition =
        objectPosition0 * barycentrics.x + objectPosition1 * barycentrics.y + objectPosition2 * barycentrics.z;
    float3 objectNormal = loadRtVertexFloat3(vertexOffset0, kRtVertexOffsetNormal) * barycentrics.x +
                          loadRtVertexFloat3(vertexOffset1, kRtVertexOffsetNormal) * barycentrics.y +
                          loadRtVertexFloat3(vertexOffset2, kRtVertexOffsetNormal) * barycentrics.z;
    float4 objectTangent0 = loadRtVertexFloat4(vertexOffset0, kRtVertexOffsetTangent);
    float4 objectTangent1 = loadRtVertexFloat4(vertexOffset1, kRtVertexOffsetTangent);
    float4 objectTangent2 = loadRtVertexFloat4(vertexOffset2, kRtVertexOffsetTangent);
    float3 objectTangent =
        objectTangent0.xyz * barycentrics.x + objectTangent1.xyz * barycentrics.y + objectTangent2.xyz * barycentrics.z;
    float tangentSign =
        objectTangent0.w * barycentrics.x + objectTangent1.w * barycentrics.y + objectTangent2.w * barycentrics.z;

    float3 position = transformRtObjectPointToWorld(objectPosition);
    float3 normal = safeNormalize(transformRtObjectNormalToWorld(objectNormal), float3(0.0f, 0.0f, 1.0f));
    float3 objectGeometryNormal = cross(objectPosition1 - objectPosition0, objectPosition2 - objectPosition0);
    geometryNormal = safeNormalize(transformRtObjectNormalToWorld(objectGeometryNormal), normal);
    float3 tangent = safeNormalize(transformRtObjectVectorToWorld(objectTangent), float3(1.0f, 0.0f, 0.0f));
    float resolvedTangentSign = (tangentSign < 0.0f ? -1.0f : 1.0f) * rtObjectToWorldHandedness();
    float2 texCoord0 = loadRtVertexFloat2(vertexOffset0, kRtVertexOffsetTexCoord0) * barycentrics.x +
                       loadRtVertexFloat2(vertexOffset1, kRtVertexOffsetTexCoord0) * barycentrics.y +
                       loadRtVertexFloat2(vertexOffset2, kRtVertexOffsetTexCoord0) * barycentrics.z;
    float2 texCoord1 = loadRtVertexFloat2(vertexOffset0, kRtVertexOffsetTexCoord1) * barycentrics.x +
                       loadRtVertexFloat2(vertexOffset1, kRtVertexOffsetTexCoord1) * barycentrics.y +
                       loadRtVertexFloat2(vertexOffset2, kRtVertexOffsetTexCoord1) * barycentrics.z;
    float4 color0 = loadRtVertexFloat4(vertexOffset0, kRtVertexOffsetColor0) * barycentrics.x +
                    loadRtVertexFloat4(vertexOffset1, kRtVertexOffsetColor0) * barycentrics.y +
                    loadRtVertexFloat4(vertexOffset2, kRtVertexOffsetColor0) * barycentrics.z;
    return RtSurfaceData(position, normal, tangent, resolvedTangentSign, texCoord0, texCoord1, color0);
}

public RtHitSurface currentRtHitSurface(BuiltInTriangleIntersectionAttributes attributes, float3 viewDirection)
{
    RtInstanceMetadata instance = currentRtInstance();
    bool geometryValid;
    RtGeometryMetadata geometry = currentRtGeometry(instance, geometryValid);
    float2 fallbackTexCoord = float2(0.5f, 0.5f);
    RtSurfaceData surface = RtSurfaceData(WorldRayOrigin() + WorldRayDirection() * RayTCurrent(), viewDirection,
                                          float3(1.0f, 0.0f, 0.0f), 1.0f, fallbackTexCoord, fallbackTexCoord,
                                          float4(1.0f));
    float3 geometryNormal = viewDirection;
    bool frontFace = HitKind() == HIT_KIND_TRIANGLE_FRONT_FACE;
    if (geometryValid)
    {
        surface = interpolateCurrentRtSurface(instance, geometry, attributes, geometryNormal);
        bool geometryNormalFacesView = dot(geometryNormal, viewDirection) >= 0.0f;
        if (geometryNormalFacesView != frontFace)
        {
            geometryNormal = -geometryNormal;
        }
    }
    return RtHitSurface(instance, geometry, surface, geometryNormal, frontFace, geometryValid);
}

public void orientRtSurfaceForMaterial(inout RtSurfaceData surface, float3 viewDirection)
{
    // Back faces are normally culled. Double-sided and volume-boundary instances keep them, and both
    // use a view-facing shading frame while the untouched face normal drives boundary transitions.
    if (dot(surface.normal, viewDirection) < 0.0f)
    {
        surface.normal = -surface.normal;
        surface.tangent = -surface.tangent;
        surface.tangentSign = -surface.tangentSign;
    }
}
