module shadowMiss;
import renderer.pathTracing.shadowPayload;

[shader("miss")]
void msShadow(inout ShadowRayPayload payload)
{
    payload.occluded = 0u;
}
