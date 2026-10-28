#include "Bindless.slang"

struct PerFrameData
{
    float4x4 mvp;
    uint texSkybox;
    float environmentIntensity;
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

    output.out_FragColor = float4(textureBindlessCube(pc.texSkybox, 0, sky).rgb * max(pc.environmentIntensity, 0.0), 1.0);

    return output;
}
