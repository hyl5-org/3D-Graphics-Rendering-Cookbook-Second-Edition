#define RENDERER_COMPUTE 1
#include "Bindless.slang"
#include "Exposure.slang"

[[vk::binding(2, 0)]][format("rgba32f")] RWTexture2D<float4> kTextures2DOutRGBA[];
[[vk::binding(2, 0)]][format("r16f")] RWTexture2D<float> kTextures2DOutR[];

struct PushConstants
{
    uint texColor;
    uint texOut;
    uint texLuminance;
    uint smpl;
    float exposure;
    uint enableBloom;
    uint texAdaptedLuminance;
    uint useAutoExposure;
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
            color += textureBindless2D(pc.texColor, uv0 + (float2(u, v) + 0.5) * dxdy);
        }
    }

    color /= 9.0;
    float luminance = max(dot(color.rgb, float3(0.2126, 0.7152, 0.0722)), 0.0001);
    float averageLuminance = textureBindless2D(pc.texAdaptedLuminance, float2(0.5)).r;
    float scale = pc.useAutoExposure != 0 ? exposureScale(averageLuminance, pc.exposure) : pc.exposure;
    float3 exposed = color.rgb * scale;
    float exposedLuminance = luminance * scale;
    // Soft knee around an exposure-relative threshold of 1.
    float knee = clamp(exposedLuminance - 0.5, 0.0, 1.0);
    float contribution = max(exposedLuminance - 1.0, knee * knee * 0.5);
    float3 bright = exposed * (contribution / max(exposedLuminance, 0.0001));
    kTextures2DOutRGBA[pc.texOut][int2(xy)] = float4(pc.enableBloom != 0 ? bright : float3(0.0), 1.0);
    // Mip averaging in log space yields a geometric mean for exposure metering.
    kTextures2DOutR[pc.texLuminance][int2(xy)] = log2(luminance);
}
