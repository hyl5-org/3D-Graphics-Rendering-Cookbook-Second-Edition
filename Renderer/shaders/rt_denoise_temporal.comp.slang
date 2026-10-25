#include "Math.slang"
#define RENDERER_COMPUTE 1
#include "Bindless.slang"

[[vk::binding(2, 0)]][format("rg8")] RWTexture2D<float2> kTextures2DOut[];

struct PushConstants
{
    float4x4 invViewProj;
    float4x4 prevViewProj;
    uint texCurrent;
    uint texHistory;
    uint texDepth;
    uint texPrevDepth;
    uint texOut;
    uint smpl;
    float alpha;
    float depthThreshold;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

[shader("compute")][numthreads(16, 16, 1)] void main(uint3 dispatchId : SV_DispatchThreadID) {
    int2 size = textureBindlessSize2D(pc.texCurrent);
    int2 xy = int2(dispatchId.xy);

    if (xy.x >= size.x || xy.y >= size.y)
    {
        return;
    }

    float2 uv = (float2(xy) + float2(0.5)) / float2(size);

    half2 current = half2(textureBindless2D(pc.texCurrent, pc.smpl, uv).rg);

    float depth = textureBindless2D(pc.texDepth, pc.smpl, uv).r;

    if (depth >= 0.9999)
    {
        kTextures2DOut[pc.texOut][xy] = (float4(1.0, 1.0, 0.0, 1.0)).xy;
        return;
    }

    float3 worldPos = reconstructWorldPosition(uv, depth, pc.invViewProj);
    float2 prevUV = reprojectToPrevUV(worldPos, pc.prevViewProj);

    const float margin = 2.0 / float(size.x);
    bool valid = all((prevUV > float2(margin))) && all((prevUV < float2(1.0 - margin)));

    if (valid)
    {
        float prevFrameDepth = textureBindless2D(pc.texPrevDepth, pc.smpl, prevUV).r;
        float4 prevClip = mul(pc.prevViewProj, float4(worldPos, 1.0));
        float expectedDepth = prevClip.z / prevClip.w;
        if (abs(expectedDepth - prevFrameDepth) > pc.depthThreshold)
        {
            valid = false;
        }
    }

    half2 nMin = current;
    half2 nMax = current;
    float2 texelSize = float2(1.0) / float2(size);
    for (int dy = -1; dy <= 1; dy++)
    {
        for (int dx = -1; dx <= 1; dx++)
        {
            half2 s = half2(textureBindless2D(pc.texCurrent, pc.smpl, uv + float2(dx, dy) * texelSize).rg);
            nMin = min(nMin, s);
            nMax = max(nMax, s);
        }
    }

    half2 result;
    if (valid)
    {
        half2 history = half2(textureBindless2D(pc.texHistory, pc.smpl, prevUV).rg);

        history = clamp(history, nMin, nMax);

        result = lerp(history, current, half(pc.alpha));
    }
    else
    {
        result = current;
    }

    kTextures2DOut[pc.texOut][xy] = (float4(result, 0.0h, 1.0h)).xy;
}
