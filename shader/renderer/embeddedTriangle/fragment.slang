module fragment;
import common;
import renderer.embeddedTriangle.abi;

[shader("fragment")]
float4 fragmentMain(EmbeddedTriangleVertexOutput input) : SV_Target0
{
    return float4(input.color, 1.0f);
}
