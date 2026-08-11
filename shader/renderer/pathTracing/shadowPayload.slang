module shadowPayload;

public struct ShadowRayPayload
{
    public uint occluded = 1u;

    public __init()
    {
    }

    public __init(uint initialOccluded)
    {
        occluded = initialOccluded;
    }
};
