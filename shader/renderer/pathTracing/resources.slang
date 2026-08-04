module resources;
import common;

[[vk::binding(0, 4)]]
public RaytracingAccelerationStructure scene;

[[vk::binding(0, 2)]]
public RWTexture2D<float4> outputImage;

[[vk::binding(1, 2)]]
public RWTexture2D<float> depthImage;

[[vk::binding(2, 2)]]
public RWTexture2D<float4> diffuseAlbedoImage;

[[vk::binding(3, 2)]]
public RWTexture2D<float4> specularAlbedoImage;

[[vk::binding(4, 2)]]
public RWTexture2D<float4> normalRoughnessImage;

[[vk::binding(5, 2)]]
public RWTexture2D<float2> motionVectorsImage;

[[vk::binding(6, 2)]]
public RWTexture2D<float> specularHitDistanceImage;
