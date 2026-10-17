#define RENDERER_COMPUTE 1
#include "Bindless.slang"

[[vk::binding(5, 0)]][format("r16f")] RWTexture2DArray<float> kTextures2DIn[];
[[vk::binding(5, 0)]][format("r16f")] RWTexture2DArray<float> kTextures2DOut[];

struct PushConstants
{
    uint texCurrSceneLuminance;
    uint texPrevAdaptedLuminance;
    uint texAdaptedOut;
    float adaptationSpeed;
    uint enableAdaptation;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

[shader("compute")][numthreads(1, 1, 1)] void main(uint3 dispatchId : SV_DispatchThreadID) {
    uint width, height, layers;
    kTextures2DIn[pc.texCurrSceneLuminance].GetDimensions(width, height, layers);
    float lumCurr = 0.0;
    for (uint layer = 0; layer != layers; ++layer)
    {
        lumCurr += kTextures2DIn[pc.texCurrSceneLuminance][int3(0, 0, int(layer))];
    }
    lumCurr /= float(layers);
    float lumPrev = kTextures2DIn[pc.texPrevAdaptedLuminance][int3(0, 0, 0)];

    float factor = 1.0 - exp(-pc.adaptationSpeed);
    float newAdaptation = pc.enableAdaptation != 0 ? lumPrev + (lumCurr - lumPrev) * factor : lumCurr;

    kTextures2DOut[pc.texAdaptedOut][int3(0, 0, int(dispatchId.z))] = newAdaptation;
}
