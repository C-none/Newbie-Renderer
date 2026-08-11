module abi;
import common;

public static const uint kShadowRayTypeContractLaneCount = 5u;
public static const float kShadowRayTypeContractLaneSpacing = 2.0f;
public static const float kShadowRayTypeContractFirstLaneX = -4.0f;
public static const float kShadowRayTypeContractRayOriginZ = 1.0f;
public static const float kShadowRayTypeContractRayDistance = 2.0f;

public static const uint kShadowRayTypeContractPending = 0xfffffffcu;
public static const uint kShadowRayTypeContractFixtureFailure = 0xfffffffdu;
public static const uint kShadowRayTypeContractMaterialMissPoison = 0xfffffffeu;
public static const uint kShadowRayTypeContractMaterialAnyHitPoison = 0xffffffffu;

public static const uint kInvalidMetadataContractResultCount = 4u;
public static const uint kInvalidMetadataContractPrimaryMaterialResult = 0u;
public static const uint kInvalidMetadataContractPrimaryShadowResult = 1u;
public static const uint kInvalidMetadataContractControlMaterialResult = 2u;
public static const uint kInvalidMetadataContractControlShadowResult = 3u;
public static const uint kInvalidMetadataContractNearAccepted = 0x4e454152u;
public static const uint kInvalidMetadataContractMaterialMiss = 0x4d495353u;
public static const float kInvalidMetadataContractPrimaryRayOriginZ = 1.0f;
public static const float kInvalidMetadataContractControlRayOriginZ = 0.25f;
public static const float kInvalidMetadataContractRayDistance = 2.0f;
public static const uint kInvalidMetadataContractValidGeometryCount = 1u;
public static const uint kInvalidMetadataContractNearGeometryIndex = 1u;

[[vk::binding(7, 2)]]
public RWStructuredBuffer<uint> gShadowRayTypeResults;

public float3 shadowRayTypeContractOrigin(uint lane)
{
    return float3(kShadowRayTypeContractFirstLaneX + float(lane) * kShadowRayTypeContractLaneSpacing, 0.0f,
                  kShadowRayTypeContractRayOriginZ);
}

public float3 shadowRayTypeContractDirection()
{
    return float3(0.0f, 0.0f, -1.0f);
}
