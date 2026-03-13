module twoGraphicsEntrypoints;

struct VsOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VsOutput vsMain(uint vertexId : SV_VertexID)
{
    float2 corners[3] = {
        float2(-1.0, -1.0),
        float2(3.0, -1.0),
        float2(-1.0, 3.0),
    };

    VsOutput output;
    output.position = float4(corners[vertexId], 0.0, 1.0);
    output.uv = output.position.xy * 0.5 + 0.5;
    return output;
}

[shader("fragment")]
float4 fsMain() : SV_Target0
{
    return float4(0.25, 0.5, 0.0, 1.0);
}
