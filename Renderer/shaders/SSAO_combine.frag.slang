#include "Bindless.slang"

struct PushConstants
{
    uint texColor;
    uint texSSAO;
    uint smpl;
    float scale;
    float bias;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

struct FragmentInput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};
struct FragmentOutput
{
    float4 out_FragColor : SV_Target0;
};
[shader("fragment")] FragmentOutput main(FragmentInput input) {
    FragmentOutput output;

    float4 color = textureBindless2D(pc.texColor, pc.smpl, input.uv);
    float ssao = clamp(textureBindless2D(pc.texSSAO, pc.smpl, input.uv).x + pc.bias, 0.0, 1.0);

    output.out_FragColor = float4(lerp(color, color * ssao, pc.scale).rgb, 1.0);

    return output;
}
