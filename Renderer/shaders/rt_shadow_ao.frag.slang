// Precompiled SPIR-V bypasses LVK's Adreno workaround for runtime-sized TLAS arrays.
[[vk::binding(4, 0)]] RaytracingAccelerationStructure kTLAS[128];
#include "Bindless.slang"

#include "Math.slang"

struct LightBufferData
{
    float4x4 viewProjBias;
    float4 lightDir;
    uint shadowTexture;
    uint shadowSampler;
    uint rtShadowEnabled;
    uint rtAOEnabled;
    uint rtAOSamples;
    float rtAORadius;
    float rtAOPower;
    float rtShadowStrength;
    float rtShadowRadius;
    uint frameIndex;
};
typealias LightBuffer = LightBufferData *;

struct PushConstants
{
    float4x4 invViewProj;
    uint gbuffer1;
    uint depth;
    uint smpl;
    uint texBlueNoise;
    LightBuffer light;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

half traceRTAO16(float3 p, half3 n, float2 pixel)
{
    float3x3 basis = getTangentBasis(n);

    half occluded = half(0.0);
    for (uint i = 0; i != pc.light->rtAOSamples; ++i)
    {
        float2 rnd = blueNoiseVec2(pc.texBlueNoise, pc.smpl, pixel, pc.light->frameIndex + i);
        float3 dir = normalize(mul(basis, cosineSampleHemisphere(rnd).xyz));

        RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES>
            rq;
        RayDesc ray = {p + float3(n) * 0.03, 0.03, dir, pc.light->rtAORadius};
        rq.TraceRayInline(kTLAS[0], RAY_FLAG_NONE, 0xff, ray);
        while (rq.Proceed())
        {
        }
        occluded += (rq.CommittedStatus() != COMMITTED_NOTHING) ? half(1.0) : half(0.0);
    }

    half ao = half(1.0) - occluded / half(pc.light->rtAOSamples);
    return half(pow(clamp(float(ao), 0.0, 1.0), pc.light->rtAOPower));
}

struct FragmentInput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};
struct FragmentOutput
{
    float4 out_FragColor : SV_Target0;
};
[shader("fragment")] FragmentOutput main(FragmentInput input) {
    FragmentOutput output;

    float d = textureBindless2D(pc.depth, pc.smpl, input.uv).r;
    if (d >= 0.9999)
    {
        output.out_FragColor = float4(1.0);
        return output;
    }

    half4 g1 = half4(textureBindless2D(pc.gbuffer1, pc.smpl, input.uv));
    half3 n = decodeOctahedron16(g1.xy);
    float3 worldPos = reconstructWorldPosition(input.uv, d, pc.invViewProj);

    half shadow = half(1.0);
    half ao = half(1.0);

    if (pc.light->rtShadowEnabled != 0)
    {
        float3 l = -normalize(pc.light->lightDir.xyz);
        float2 rnd = blueNoiseVec2(pc.texBlueNoise, pc.smpl, input.position.xy, pc.light->frameIndex);
        float3 rayDir = generateSoftShadowRayDir(l, pc.light->rtShadowRadius, rnd);

        if (dot(float3(n), rayDir) > 0.0)
        {
            RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_FORCE_OPAQUE |
                     RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES>
                rq;

            RayDesc ray = {worldPos + float3(n) * 0.03, 0.03, rayDir, 1e4};
            rq.TraceRayInline(kTLAS[0], RAY_FLAG_NONE, 0xff, ray);
            while (rq.Proceed())
            {
            }
            shadow = (rq.CommittedStatus() == COMMITTED_NOTHING) ? half(1.0) : half(0.0);
        }
        else
        {
            shadow = half(0.0);
        }
    }

    if (pc.light->rtAOEnabled != 0)
    {
        ao = traceRTAO16(worldPos, n, input.position.xy);
    }

    output.out_FragColor = float4(shadow, ao, half(0.0), half(1.0));

    return output;
}
