module raygen;
import common;
import renderer.pathTracing.scheduler;

[require(spvCooperativeVectorNV)]
[shader("raygeneration")]
void rgMain()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 dimensions = DispatchRaysDimensions().xy;
    Scheduler scheduler;
    scheduler.traceSample(pixel, dimensions);
}
