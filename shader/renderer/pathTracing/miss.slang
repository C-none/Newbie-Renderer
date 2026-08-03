module miss;
import common;
import renderer.pathTracing.params;
import renderer.pathTracing.pathState;
import renderer.pathTracing.environment;

[shader("miss")]
void msMain(inout RayPayload payload)
{
    payload.hit = false;
    // Camera-primary and later-bounce path segments both use RayKind.material, so the shared miss
    // entry returns the selected environment radiance for either kind of path escape.
    if (payload.kind == RayKind.material)
    {
        payload.missRadiance = sampleEnvironment(WorldRayDirection());
    }
}
