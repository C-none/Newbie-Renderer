module main;
import common;
import test.utils.utils;
[shader("compute")]
[numthreads(1, 1, 1)]
void csMain(uint3 tids: SV_DispatchThreadID)
{
    if ((kFlag + tids.x) == 0u)
    {   
    }
}
