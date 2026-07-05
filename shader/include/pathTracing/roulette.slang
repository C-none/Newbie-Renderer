implementing common;

public static const uint kPathTracingRussianRouletteStartVertexIndex = 1u;
public static const float kPathTracingRussianRouletteMinSurvivalProbability = 0.05f;
public static const float kPathTracingRussianRouletteMaxSurvivalProbability = 0.95f;

public interface IPathTracingRussianRoulettePolicy
{
    bool shouldApply(uint vertexIndex);
    float survivalProbability(float3 throughput);
    bool survives(float randomValue, float survivalProbability);
    float throughputScale(float survivalProbability);
}

public struct PathTracingRussianRouletteEnabledPolicy : IPathTracingRussianRoulettePolicy
{
    public bool shouldApply(uint vertexIndex)
    {
        return vertexIndex >= kPathTracingRussianRouletteStartVertexIndex;
    }

    public float survivalProbability(float3 throughput)
    {
        float probability = max(max(throughput.x, throughput.y), throughput.z);
        if (probability <= 0.0f)
        {
            return 0.0f;
        }

        return clamp(
            probability,
            kPathTracingRussianRouletteMinSurvivalProbability,
            kPathTracingRussianRouletteMaxSurvivalProbability);
    }

    public bool survives(float randomValue, float survivalProbability)
    {
        return randomValue < survivalProbability;
    }

    public float throughputScale(float survivalProbability)
    {
        return 1.0f / survivalProbability;
    }
}

public struct PathTracingRussianRouletteDisabledPolicy : IPathTracingRussianRoulettePolicy
{
    public bool shouldApply(uint vertexIndex)
    {
        return false;
    }

    public float survivalProbability(float3 throughput)
    {
        return 1.0f;
    }

    public bool survives(float randomValue, float survivalProbability)
    {
        return true;
    }

    public float throughputScale(float survivalProbability)
    {
        return 1.0f;
    }
}

public extern struct PathTracingRussianRoulettePolicy : IPathTracingRussianRoulettePolicy;
