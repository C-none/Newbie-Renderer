module vertex;
import renderer.appUi.abi;

[shader("vertex")]
UiVertexOutput vertexMain(UiVertexInput input)
{
    UiVertexOutput output;
    output.position = float4(input.position * gUiPush.scale + gUiPush.translate, 0.0f, 1.0f);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}
