module vertex;

[shader("vertex")]
float4 pipelineBinaryVertexMain(uint vertexId: SV_VertexID) : SV_Position
{
    float2 positions[3] = {
        float2(-0.5f, -0.5f),
        float2(0.5f, -0.5f),
        float2(0.0f, 0.5f),
    };
    return float4(positions[vertexId], 0.0f, 1.0f);
}
