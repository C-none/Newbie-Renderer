implementing common;

public enum MaterialTextureSlot : uint
{
    baseColor = 0u,
    normal = 1u,
    metallicRoughness = 2u,
    occlusion = 3u,
    emissive = 4u,
    clearcoat = 5u,
    clearcoatRoughness = 6u,
    clearcoatNormal = 7u,
    sheenColor = 8u,
    sheenRoughness = 9u,
    transmission = 10u,
    anisotropy = 11u,
    count = 12u,
}

[Flags]
public enum MaterialTextureSlotFlag : uint
{
    none = 0u,
    baseColor = 1u << 0u,
    normal = 1u << 1u,
    metallicRoughness = 1u << 2u,
    occlusion = 1u << 3u,
    emissive = 1u << 4u,
    clearcoat = 1u << 5u,
    clearcoatRoughness = 1u << 6u,
    clearcoatNormal = 1u << 7u,
    sheenColor = 1u << 8u,
    sheenRoughness = 1u << 9u,
    transmission = 1u << 10u,
    anisotropy = 1u << 11u,
}

