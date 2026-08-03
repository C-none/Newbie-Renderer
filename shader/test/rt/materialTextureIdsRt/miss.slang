module miss;
import common;
import test.rt.materialTextureIdsRt.abi;

[shader("miss")]
void msMain(inout RayPayload payload)
{
    payload.color = float3(0.0f, 0.0f, 0.0f);
}
