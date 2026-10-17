#include "Bindless.slang"

#include "OIT.slang"

[[vk::binding(2, 0)]][format("r32ui")] RWTexture2D<uint> kTextures2DIn[];

struct PushConstants
{
    TransparencyListsBuffer oitLists;
    uint texColor;
    uint texHeadsOIT;
    float time;
    float opacityBoost;
    uint showHeatmap;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

#define MAX_FRAGMENTS 64

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

    TransparentFragment frags[MAX_FRAGMENTS];

    uint numFragments = 0;
    uint idx = uint4(kTextures2DIn[pc.texHeadsOIT][int2(input.position.xy)], 0, 0, 0).r;

    while (idx != 0xFFFFFFFF && numFragments < MAX_FRAGMENTS)
    {
        frags[numFragments] = pc.oitLists[idx];
        numFragments++;
        idx = pc.oitLists[idx].next;
    }

    for (int i = 1; i < numFragments; i++)
    {
        TransparentFragment toInsert = frags[i];
        uint j = i;
        while (j > 0 && toInsert.depth > frags[j - 1].depth)
        {
            frags[j] = frags[j - 1];
            j--;
        }
        frags[j] = toInsert;
    }

    float4 color = textureBindless2D(pc.texColor, 0, input.uv);

    for (uint i = 0; i < numFragments; i++)
    {
        color = lerp(color, float4(frags[i].color), clamp(float(frags[i].color.a + pc.opacityBoost), 0.0, 1.0));
    }

    if (pc.showHeatmap > 0 && numFragments > 0)
    {
        color = (1.0 + sin(5.0 * pc.time)) * float4(float3(numFragments + 1, numFragments + 1, 0), 0.0) / 16.0;
    }

    output.out_FragColor = color;

    return output;
}
