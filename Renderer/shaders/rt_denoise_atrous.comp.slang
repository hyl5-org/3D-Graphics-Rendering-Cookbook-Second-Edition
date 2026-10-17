#define RENDERER_COMPUTE 1
#include "Bindless.slang"

[[vk::binding(2, 0)]][format("rg8")] RWTexture2D<float2> kTextures2DOut[];

[[vk::constant_id(0)]] const uint kStep = 1u;

static const float2 kCrossOffsets[4] = {float2(1.0, 0.0), float2(-1.0, 0.0), float2(0.0, 1.0), float2(0.0, -1.0)};

static const float2 kDiagonalOffsets[4] = {float2(1.0, 1.0), float2(-1.0, 1.0), float2(1.0, -1.0), float2(-1.0, -1.0)};

struct PushConstants
{
    uint texIn;
    uint texOut;
    uint texDepth;
    uint texGBuffer1;
    uint smpl;
    float depthSigma;
    float normalSigma;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

float edgeWeight(float centerDepth, float sampleDepth, float2 centerNormalOct, float2 sampleNormalOct)
{
    half2 normalDiff = half2(sampleNormalOct - centerNormalOct);
    float depthWeight = exp2(-abs(sampleDepth - centerDepth) / max(pc.depthSigma, 1e-6));

    half normalWeight = exp2(-dot(normalDiff, normalDiff) * half(pc.normalSigma));
    return depthWeight * float(normalWeight);
}

float edgeWeightFast(float centerDepth, float sampleDepth)
{
    return exp2(-abs(sampleDepth - centerDepth) / max(pc.depthSigma, 1e-6));
}

[shader("compute")][numthreads(16, 16, 1)] void main(uint3 dispatchId : SV_DispatchThreadID) {
    int2 size = textureBindlessSize2D(pc.texIn);
    int2 xy = int2(dispatchId.xy);

    if (xy.x >= size.x || xy.y >= size.y)
    {
        return;
    }

    float2 uv = (float2(xy) + float2(0.5)) / float2(size);
    float2 texelSize = float2(1.0) / float2(size);

    half2 centerColor = half2(textureBindless2D(pc.texIn, pc.smpl, uv).rg);
    float centerDepth = textureBindless2D(pc.texDepth, pc.smpl, uv).r;

    float2 stepTexel = texelSize * float(kStep);

    half2 sumColor = centerColor * 0.5h;
    float totalWeight = 0.5;

    for (int i = 0; i < 4; ++i)
    {
        float2 offset = (kStep == 1u ? kCrossOffsets[i] : kDiagonalOffsets[i]) * stepTexel;
        float2 sampleUV = uv + offset;
        half2 sampleColor = half2(textureBindless2D(pc.texIn, pc.smpl, sampleUV).rg);
        float sampleDepth = textureBindless2D(pc.texDepth, pc.smpl, sampleUV).r;

        float w = 0.125 * edgeWeightFast(centerDepth, sampleDepth);
        sumColor += sampleColor * half(w);
        totalWeight += w;
    }

    half2 result = totalWeight > 0.001 ? sumColor / half(totalWeight) : centerColor;
    kTextures2DOut[pc.texOut][xy] = (float4(result, 0.0h, 1.0h)).xy;
}
