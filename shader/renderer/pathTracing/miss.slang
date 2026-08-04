module miss;
import common;
import renderer.pathTracing.pathState;
import renderer.pathTracing.environment;

[shader("miss")]
void msMaterial(inout MaterialRayPayload payload)
{
    payload.hit = false;
    payload.missRadiance = sampleEnvironment(WorldRayDirection());
}
