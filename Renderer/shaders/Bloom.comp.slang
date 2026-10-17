#define RENDERER_COMPUTE 1
#include "Bindless.slang"

[[vk::binding(5, 0)]][format("rgba16f")] RWTexture2DArray<float4> kTextures2DOut[];

struct PushConstants
{
    uint texIn;
    uint texOut;
    uint smpl;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

float4 textureBindless2D(uint textureid, float2 uv, uint viewId)
{
    return kTextures2DArray[NonUniformResourceIndex(textureid)].SampleLevel(
        kSamplers[NonUniformResourceIndex(pc.smpl)], float3(uv, float(viewId)), 0);
}

[[vk::constant_id(0)]] const bool kIsHorizontal = true;

static const int kFilterSize = 17;

static const float gaussWeights[kFilterSize] = {
    0.00001525878906, 0.0002441406250, 0.001831054688, 0.008544921875,  0.02777099609,   0.06665039063,
    0.1221923828,     0.1745605469,    0.1963806152,   0.1745605469,    0.1221923828,    0.06665039063,
    0.02777099609,    0.008544921875,  0.001831054688, 0.0002441406250, 0.00001525878906};

[shader("compute")][numthreads(16, 16, 1)] void main(uint3 dispatchId : SV_DispatchThreadID) {
    const uint3 dimensions = uint3(textureBindlessSize2DArray(pc.texIn));
    const float2 size = float2(dimensions.xy);
    const float2 xy = dispatchId.xy;

    if (dispatchId.x >= dimensions.x || dispatchId.y >= dimensions.y || dispatchId.z >= dimensions.z)
    {
        return;
    }

    const float2 texCoord = (dispatchId.xy + float2(0.5)) / size;
    const float2 texel = 1.0 / size;

    const float radius = kIsHorizontal ? 1.5 : 2.5;

    float3 c = float3(0.0);
    c += textureBindless2D(pc.texIn, texCoord + texel * float2(-radius, 0.0), dispatchId.z).rgb;
    c += textureBindless2D(pc.texIn, texCoord + texel * float2(radius, 0.0), dispatchId.z).rgb;
    c += textureBindless2D(pc.texIn, texCoord + texel * float2(0.0, -radius), dispatchId.z).rgb;
    c += textureBindless2D(pc.texIn, texCoord + texel * float2(0.0, radius), dispatchId.z).rgb;
    c += 2.0 * textureBindless2D(pc.texIn, texCoord + texel * float2(-radius, -radius), dispatchId.z).rgb;
    c += 2.0 * textureBindless2D(pc.texIn, texCoord + texel * float2(radius, -radius), dispatchId.z).rgb;
    c += 2.0 * textureBindless2D(pc.texIn, texCoord + texel * float2(-radius, radius), dispatchId.z).rgb;
    c += 2.0 * textureBindless2D(pc.texIn, texCoord + texel * float2(radius, radius), dispatchId.z).rgb;
    c *= 1.0 / 12.0;

    kTextures2DOut[pc.texOut][int3(dispatchId)] = (float4(c, 1.0));
}
