#define RENDERER_COMPUTE 1
#include "Bindless.slang"

[[vk::binding(2, 0)]][format("rgba16f")] RWTexture2D<float4> kTextures2DOut[];

struct PushConstants
{
    uint texIn;
    uint texOut;
    uint smpl;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

float4 textureBindless2D(uint textureid, float2 uv)
{
    return kTextures2D[NonUniformResourceIndex(textureid)].SampleLevel(kSamplers[NonUniformResourceIndex(pc.smpl)], uv,
                                                                       0);
}

[[vk::constant_id(0)]] const bool kIsHorizontal = true;

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
    const float2 texel = 1.0 / size;

    const float radius = kIsHorizontal ? 1.5 : 2.5;

    float3 c = float3(0.0);
    c += textureBindless2D(pc.texIn, texCoord + texel * float2(-radius, 0.0)).rgb;
    c += textureBindless2D(pc.texIn, texCoord + texel * float2(radius, 0.0)).rgb;
    c += textureBindless2D(pc.texIn, texCoord + texel * float2(0.0, -radius)).rgb;
    c += textureBindless2D(pc.texIn, texCoord + texel * float2(0.0, radius)).rgb;
    c += 2.0 * textureBindless2D(pc.texIn, texCoord + texel * float2(-radius, -radius)).rgb;
    c += 2.0 * textureBindless2D(pc.texIn, texCoord + texel * float2(radius, -radius)).rgb;
    c += 2.0 * textureBindless2D(pc.texIn, texCoord + texel * float2(-radius, radius)).rgb;
    c += 2.0 * textureBindless2D(pc.texIn, texCoord + texel * float2(radius, radius)).rgb;
    c *= 1.0 / 12.0;

    kTextures2DOut[pc.texOut][int2(xy)] = (float4(c, 1.0));
}
