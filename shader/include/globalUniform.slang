implementing common;

public struct GlobalFrameUniforms
{
    public column_major float4x4 view;
    public column_major float4x4 projection;
    public column_major float4x4 viewProjection;
    public column_major float4x4 inverseViewProjection;
    public float4 cameraWorld;
};

[[vk::binding(0, 5)]]
public ConstantBuffer<GlobalFrameUniforms> gFrame;
