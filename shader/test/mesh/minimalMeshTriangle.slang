module minimalMeshTriangle;
import common;

struct CameraData
{
    float4 origin;
    float4 right;
    float4 up;
    float4 forward;
};

ConstantBuffer<CameraData> camera;

struct MeshInputVertex
{
    float4 position;
    float4 color;
};

StructuredBuffer<MeshInputVertex> meshVertices;
StructuredBuffer<uint> meshIndices;

struct TaskPayload
{
    float3 tint;
};

struct MeshVertex
{
    float4 position : SV_Position;
    float3 color : COLOR0;
};

[shader("amplification")]
[numthreads(1, 1, 1)]
void taskMain(uint laneIndex : SV_GroupIndex)
{
    TaskPayload payload;
    payload.tint = normalize(abs(camera.forward.xyz) + float3(0.2, 0.4, 0.6));
    DispatchMesh(1, 1, 1, payload);
}

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(3, 1, 1)]
void meshMain(
    uint laneIndex : SV_GroupIndex,
    in payload TaskPayload payload,
    OutputVertices<MeshVertex, 3> vertices,
    OutputIndices<uint3, 1> triangles)
{
    SetMeshOutputCounts(3, 1);

    if (laneIndex < 3)
    {
        MeshInputVertex srcVertex = meshVertices[laneIndex];
        float2 projected = camera.origin.xy + srcVertex.position.x * camera.right.xy + srcVertex.position.y * camera.up.xy;
        vertices[laneIndex].position = float4(projected, srcVertex.position.z, srcVertex.position.w);
        vertices[laneIndex].color = srcVertex.color.xyz * payload.tint;
    }

    if (laneIndex == 0)
    {
        triangles[0] = uint3(meshIndices[0], meshIndices[1], meshIndices[2]);
    }
}

[shader("fragment")]
float4 fragmentMain(MeshVertex input) : SV_Target0
{
    return float4(input.color, 1.0);
}
