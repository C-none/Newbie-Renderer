module resources;
import common;

public RaytracingAccelerationStructure scene;
public RWTexture2D<float4> outputImage;
public RWTexture2D<float> depthImage;
public RWTexture2D<float4> diffuseAlbedoImage;
public RWTexture2D<float4> specularAlbedoImage;
public RWTexture2D<float4> normalRoughnessImage;
public RWTexture2D<float2> motionVectorsImage;
public RWTexture2D<float> specularHitDistanceImage;
