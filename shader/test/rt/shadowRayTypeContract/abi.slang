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
