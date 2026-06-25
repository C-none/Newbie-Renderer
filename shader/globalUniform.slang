implementing common;

public struct GlobalFrameUniforms
{
    public column_major float4x4 view;
    public column_major float4x4 projection;
    public column_major float4x4 viewProjection;
};

[[vk::binding(0, 7)]]
public ConstantBuffer<GlobalFrameUniforms> gFrame;
