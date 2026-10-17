#include "Bindless.slang"

struct PerFrameData
{
    float4x4 mvp[2];
    uint texSkybox;
};
[[vk::push_constant]] ConstantBuffer<PerFrameData> pc;

struct FragmentInput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float3 dir : TEXCOORD0;
};
struct FragmentOutput
{
    float4 out_FragColor : SV_Target0;
};
[shader("fragment")] FragmentOutput main(FragmentInput input) {
    FragmentOutput output;

    float3 sky = float3(-input.dir.x, input.dir.y, -input.dir.z);

    output.out_FragColor = textureBindlessCube(pc.texSkybox, 0, sky);

    return output;
}
