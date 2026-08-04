module hitShaders;
import common;
import include.rt.hitSurface;

public ClosestHitInput makeClosestHitInput(BuiltInTriangleIntersectionAttributes attributes)
{
    float3 viewDirection = normalize(-WorldRayDirection());
    ClosestHitInput input = {};
    input.viewDirection = viewDirection;

    RtHitSurface hit = currentRtHitSurface(attributes, viewDirection);
    input.geometryValid = hit.geometryValid;
    input.materialIndex = hit.geometry.materialIndex;
    // InstanceID is the dense, collision-free RT metadata index written to Vulkan
    // InstanceCustomIndex. GeometryIndex is unique within that instance's BLAS.
    input.instanceMetadataIndex = uint(InstanceID());
    input.geometryIndex = uint(GeometryIndex());
    input.geometryNormal = hit.geometryNormal;
    input.frontFace = hit.frontFace;
    input.surface = hit.surface;
    if (!hit.geometryValid)
    {
        return input;
    }

    RtMaterialHeader material = rtMaterialHeaders[hit.geometry.materialIndex];
    orientRtSurfaceForMaterial(hit.surface, material, viewDirection);
    input.material = material;
    input.surface = hit.surface;
    return input;
}
