module vertex;
import renderer.appUi.abi;

[shader("vertex")]
UiVertexOutput vertexMain(UiVertexInput input)
{
    return UiVertexOutput(float4(input.position * gUiPush.scale + gUiPush.translate, 0.0f, 1.0f), input.uv,
                          input.color);
}
