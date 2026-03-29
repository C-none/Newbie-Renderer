module embeddedTriangle;

struct EmbeddedTriangleFrameUniforms
{
    column_major float4x4 viewProjection;
};

ConstantBuffer<EmbeddedTriangleFrameUniforms> gFrame;

struct VSOutput
{
    float4 position : SV_Position;
    float3 color : COLOR0;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID)
{
    float3 positions[3] = {
        float3(-0.75f, -0.55f, 0.15f),
        float3(0.8f, -0.35f, -0.55f),
        float3(0.05f, 0.9f, 0.45f),
    };

    float3 colors[3] = {
        float3(1.0f, 0.2f, 0.2f),
        float3(0.2f, 1.0f, 0.2f),
        float3(0.2f, 0.4f, 1.0f),
    };

    uint index = vertexId % 3u;

    VSOutput output;
    output.position = mul(gFrame.viewProjection, float4(positions[index], 1.0f));
    output.color = colors[index];
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0
{
    return float4(input.color, 1.0f);
}
