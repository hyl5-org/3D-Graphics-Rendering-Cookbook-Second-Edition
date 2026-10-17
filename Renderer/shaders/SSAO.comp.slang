#define RENDERER_COMPUTE 1
#include "Bindless.slang"

[[vk::binding(2, 0)]][format("rgba8")] RWTexture2D<float4> kTextures2DOut[];

struct PushConstants
{
    uint texDepth;
    uint texRotation;
    uint texOut;
    uint smpl;
    float zNear;
    float zFar;
    float radius;
    float attScale;
    float distScale;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

float4 textureBindless2D(uint textureid, float2 uv)
{
    return kTextures2D[NonUniformResourceIndex(textureid)].SampleLevel(kSamplers[NonUniformResourceIndex(pc.smpl)], uv,
                                                                       0);
}

static const float3 offsets[8] = {float3(-0.5, -0.5, -0.5), float3(0.5, -0.5, -0.5), float3(-0.5, 0.5, -0.5),
                                  float3(0.5, 0.5, -0.5),   float3(-0.5, -0.5, 0.5), float3(0.5, -0.5, 0.5),
                                  float3(-0.5, 0.5, 0.5),   float3(0.5, 0.5, 0.5)};

float scaleZ(float smpl)
{
    return (pc.zFar * pc.zNear) / (smpl * (pc.zFar - pc.zNear) - pc.zFar);
}

[shader("compute")][numthreads(16, 16, 1)] void main(uint3 dispatchId : SV_DispatchThreadID) {
    const float2 size = textureBindlessSize2D(pc.texDepth).xy;

    const float2 xy = dispatchId.xy;
    const float2 uv = (dispatchId.xy + float2(0.5)) / size;

    if (xy.x >= size.x || xy.y >= size.y)
    {
        return;
    }

    const float Z = scaleZ(textureBindless2D(pc.texDepth, uv).x);
    const float3 plane = textureBindless2D(pc.texRotation, xy / 4.0).xyz - float3(1.0);

    float att = 0.0;

    for (int i = 0; i < 8; i++)
    {
        float3 rSample = reflect(offsets[i], plane);
        float zSample = scaleZ(textureBindless2D(pc.texDepth, uv + pc.radius * rSample.xy / Z).x);
        float dist = max(zSample - Z, 0.0) / pc.distScale;
        float occl = 15.0 * max(dist * (2.0 - dist), 0.0);
        att += 1.0 / (1.0 + occl * occl);
    }

    att = clamp(att * att / 64.0 + 0.45, 0.0, 1.0) * pc.attScale;

    kTextures2DOut[pc.texOut][int2(xy)] = (float4(float3(att), 1.0));
}
