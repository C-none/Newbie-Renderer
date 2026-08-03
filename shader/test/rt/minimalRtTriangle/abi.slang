module abi;
import common;

public struct CameraData
{
    public float4 origin;
    public float4 right;
    public float4 up;
    public float4 forward;
};

public RaytracingAccelerationStructure scene;
public RWTexture2D<float4> outputImage;
public ConstantBuffer<CameraData> camera;

public struct RayPayload
{
    public float3 color;
};
