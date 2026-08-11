module scheduler;
import common;
import renderer.pathTracing.pathState;
import renderer.pathTracing.core;

public struct Scheduler
{
    public void traceSample(uint2 pixel, uint2 dimensions)
    {
        Pt pt = Pt(pixel, dimensions);
        while (pt.isActive())
        {
            MaterialRayPayload payload = MaterialRayPayload();
            pt.traceMaterialRay(payload);
            pt.handleTraceResult(payload);
        }

        pt.writeOutput();
    }
};
