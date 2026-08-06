module vertex;
import common;
import renderer.embeddedTriangle.abi;

[shader("vertex")]
EmbeddedTriangleVertexOutput vertexMain(uint vertexId: SV_VertexID)
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

    EmbeddedTriangleVertexOutput output;
    output.position = mul(gFrame.viewProjection, float4(positions[vertexId], 1.0f));
    output.color = colors[vertexId];
    return output;
}
