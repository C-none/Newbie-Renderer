module abi;

public struct EmbeddedTriangleVertexOutput
{
    public float4 position : SV_Position = float4(0.0f, 0.0f, 0.0f, 1.0f);
    public float3 color : COLOR0 = float3(1.0f, 1.0f, 1.0f);

    public __init(float4 positionValue, float3 colorValue)
    {
        position = positionValue;
        color = colorValue;
    }
};
