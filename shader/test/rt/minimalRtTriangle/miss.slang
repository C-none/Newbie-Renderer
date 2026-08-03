module miss;
import common;
import test.rt.minimalRtTriangle.abi;

[shader("miss")]
void msMain(inout RayPayload payload)
{
    payload.color = float3(0.1, 0.2, 0.35);
}
