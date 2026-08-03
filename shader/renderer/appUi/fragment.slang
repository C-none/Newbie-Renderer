module fragment;
import renderer.appUi.abi;

[shader("fragment")]
float4 fragmentMain(UiVertexOutput input) : SV_Target0
{
    return input.color * gUiTextures[gUiPush.textureIndex].Sample(input.uv);
}
