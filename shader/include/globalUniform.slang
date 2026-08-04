implementing common;

public struct GlobalFrameUniforms
{
    public column_major float4x4 view;
    public column_major float4x4 projection;
    public column_major float4x4 viewProjection;
    public column_major float4x4 inverseViewProjection;
    public column_major float4x4 previousView;
    public column_major float4x4 previousViewProjection;
    public float4 cameraWorld;
    // xy: monotonic 64-bit sample-frame ordinal low/high, z: resource frame slot.
    public uint4 frameState;
};

[[vk::binding(0, 3)]]
public ConstantBuffer<GlobalFrameUniforms> gFrame;
