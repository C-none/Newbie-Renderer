module abi;
import common;

public struct CameraData
{
    public float4 origin;
    public float4 right;
    public float4 up;
    public float4 forward;
};

[[vk::binding(0, 4)]]
public RaytracingAccelerationStructure scene;

[[vk::binding(0, 2)]]
public RWTexture2D<float4> outputImage;

[[vk::binding(8, 3)]]
public ConstantBuffer<CameraData> camera;

public struct RayPayload
{
    public float3 color = float3(0.0f);

    public __init(float3 initialColor)
    {
        color = initialColor;
    }
};
