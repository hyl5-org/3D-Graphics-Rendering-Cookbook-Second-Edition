#define RENDERER_COMPUTE 1
#include "Bindless.slang"

[[vk::binding(2, 0)]][format("rgba16f")] RWTexture2D<float4> kTextures2DOutRGBA[];
[[vk::binding(2, 0)]][format("r16f")] RWTexture2D<float> kTextures2DOutR[];

struct PushConstants
{
    uint texColor;
    uint texOut;
    uint texLuminance;
    uint smpl;
    float exposure;
    uint enableBloom;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

float4 textureBindless2D(uint textureid, float2 uv)
{
    return kTextures2D[NonUniformResourceIndex(textureid)].SampleLevel(kSamplers[NonUniformResourceIndex(pc.smpl)], uv,
                                                                       0);
}

[shader("compute")][numthreads(16, 16, 1)] void main(uint3 dispatchId : SV_DispatchThreadID) {
    const float2 sizeIn = textureBindlessSize2D(pc.texColor).xy;
    const float2 sizeOut = textureBindlessSize2D(pc.texOut).xy;

    const float2 xy = dispatchId.xy;
    const float2 uv0 = (dispatchId.xy + float2(0)) / sizeOut;
    const float2 uv1 = (dispatchId.xy + float2(1)) / sizeOut;

    if (xy.x >= sizeOut.x || xy.y >= sizeOut.y)
    {
        return;
    }

    float2 dxdy = (uv1 - uv0) / 3;

    float4 color = float4(0);

    for (int v = 0; v != 3; v++)
    {
        for (int u = 0; u != 3; u++)
        {
            color += textureBindless2D(pc.texColor, uv0 + float2(u, v) * dxdy);
        }
    }

    float luminance = pc.exposure * dot(color.rgb / 9, float3(0.2126, 0.7152, 0.0722));

    if (pc.enableBloom != 0)
    {
        float3 rgb = luminance > 1.0 ? color.rgb : float3(0);
        kTextures2DOutRGBA[pc.texOut][int2(xy)] = (float4(rgb, 1.0));
    }
    kTextures2DOutR[pc.texLuminance][int2(xy)] = (float4(luminance)).x;
}
