#define RENDERER_COMPUTE 1
#include "Bindless.slang"

[[vk::binding(2, 0)]][format("rgba8")] RWTexture2D<float4> kTextures2DOut[];

float4 textureBindless2D(uint textureid, float2 uv)
{
    return kTextures2D[NonUniformResourceIndex(textureid)].SampleLevel(kSamplers[NonUniformResourceIndex(0)], uv, 0);
}

[[vk::constant_id(0)]] const bool kIsHorizontal = true;

struct PushConstants
{
    uint texDepth;
    uint texIn;
    uint texOut;
    float depthThreshold;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

static const int kFilterSize = 17;

static const float gaussWeights[kFilterSize] = {
    0.00001525878906, 0.0002441406250, 0.001831054688, 0.008544921875,  0.02777099609,   0.06665039063,
    0.1221923828,     0.1745605469,    0.1963806152,   0.1745605469,    0.1221923828,    0.06665039063,
    0.02777099609,    0.008544921875,  0.001831054688, 0.0002441406250, 0.00001525878906};

[shader("compute")][numthreads(16, 16, 1)] void main(uint3 dispatchId : SV_DispatchThreadID) {
    const float2 size = textureBindlessSize2D(pc.texIn).xy;
    const float2 xy = dispatchId.xy;

    if (xy.x >= size.x || xy.y >= size.y)
    {
        return;
    }

    const float2 texCoord = (dispatchId.xy + float2(0.5)) / size;

    const float texScaler = 1.0 / (kIsHorizontal ? size.x : size.y);

    float3 c = float3(0.0);

    float3 fragColor = textureBindless2D(pc.texIn, texCoord).rgb;
    float fragDepth = textureBindless2D(pc.texDepth, texCoord).r;

    for (int i = 0; i != kFilterSize; i++)
    {
        float offset = float(i - kFilterSize / 2);
        float2 uv = texCoord + texScaler * (kIsHorizontal ? float2(offset, 0) : float2(0, offset));
        float3 color = textureBindless2D(pc.texIn, uv).rgb;
        float depth = textureBindless2D(pc.texDepth, uv).r;

        float weight = clamp(abs(depth - fragDepth) * pc.depthThreshold, 0.0, 1.0);
        c += lerp(color, fragColor, weight) * gaussWeights[i];
    }

    kTextures2DOut[pc.texOut][int2(xy)] = (float4(c, 1.0));
}
