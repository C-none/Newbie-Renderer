implementing common;

public struct GlobalFrameUniforms
{
    public row_major float4x4 viewProjection;
    public row_major float4x4 inverseViewProjection;
    public row_major float4x4 unjitteredViewProjection;
    public row_major float4x4 previousViewProjection;
    public float4 cameraWorld;
    // xy: monotonic 64-bit sample-frame ordinal low/high, z: resource frame slot.
    public uint4 frameState;
};

[[vk::binding(0, 3)]]
public ConstantBuffer<GlobalFrameUniforms> gFrame;
