#define RENDERER_COMPUTE 1
#include "Bindless.slang"

[[vk::binding(2, 0)]][format("rg8")] RWTexture2D<float2> kTextures2DOut[];

[[vk::constant_id(0)]] const bool kIsHorizontal = true;

struct PushConstants
{
    uint texIn;
    uint texOut;
    uint texDepth;
    uint smpl;
    float depthThreshold;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

static const int kFilterSize = 5;

static const half gaussWeights[kFilterSize] = {0.0625h, 0.25h, 0.375h, 0.25h, 0.0625h};

[shader("compute")][numthreads(16, 16, 1)] void main(uint3 dispatchId : SV_DispatchThreadID) {
    int2 size = textureBindlessSize2D(pc.texIn);
    int2 xy = int2(dispatchId.xy);

    if (xy.x >= size.x || xy.y >= size.y)
    {
        return;
    }

    float2 texCoord = (float2(xy) + float2(0.5)) / float2(size);
    float texScaler = 1.0 / (kIsHorizontal ? float(size.x) : float(size.y));

    half2 fragColor = half2(textureBindless2D(pc.texIn, pc.smpl, texCoord).rg);
    float fragDepth = textureBindless2D(pc.texDepth, pc.smpl, texCoord).r;

    half2 c = half2(0.0h);
    half totalWeight = 0.0h;

    for (int i = 0; i < kFilterSize; i++)
    {
        float offset = float(i - kFilterSize / 2);
        float2 uv = texCoord + texScaler * (kIsHorizontal ? float2(offset, 0.0) : float2(0.0, offset));

        half2 color = half2(textureBindless2D(pc.texIn, pc.smpl, uv).rg);
        float depth = textureBindless2D(pc.texDepth, pc.smpl, uv).r;

        float depthDiff = abs(depth - fragDepth);
        half w = gaussWeights[i] * half(1.0 - clamp(depthDiff * pc.depthThreshold, 0.0, 1.0));

        c += color * w;
        totalWeight += w;
    }

    half2 result = totalWeight > 0.001h ? c / totalWeight : fragColor;
    kTextures2DOut[pc.texOut][xy] = (float4(result, 0.0h, 1.0h)).xy;
}
