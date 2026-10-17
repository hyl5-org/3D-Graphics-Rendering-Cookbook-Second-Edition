#define RENDERER_COMPUTE 1
#include "Bindless.slang"

[[vk::binding(2, 0)]][format("r16f")] RWTexture2D<float> kTextures2DIn[];
[[vk::binding(2, 0)]][format("r16f")] RWTexture2D<float> kTextures2DOut[];

struct PushConstants
{
    uint texCurrSceneLuminance;
    uint texPrevAdaptedLuminance;
    uint texAdaptedOut;
    float adaptationSpeed;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

[shader("compute")][numthreads(1, 1, 1)] void main(uint3 dispatchId : SV_DispatchThreadID) {
    float lumCurr = float4(kTextures2DIn[pc.texCurrSceneLuminance][int2(0, 0)], 0, 0, 0).x;
    float lumPrev = float4(kTextures2DIn[pc.texPrevAdaptedLuminance][int2(0, 0)], 0, 0, 0).x;

    float factor = 1.0 - exp(-pc.adaptationSpeed);
    float newAdaptation = lumPrev + (lumCurr - lumPrev) * factor;

    kTextures2DOut[pc.texAdaptedOut][int2(0, 0)] = (float4(newAdaptation)).x;
}
