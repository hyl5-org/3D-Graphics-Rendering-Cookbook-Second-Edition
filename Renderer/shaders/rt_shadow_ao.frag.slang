// Descriptor indices match acceleration-structure handles, including preceding BLAS slots.
[[vk::binding(4, 0)]] RaytracingAccelerationStructure kTLAS[];
#include "Bindless.slang"

#include "Math.slang"
#include "Material.slang"
#include "Geometry.slang"
#include "Lighting.slang"
#include "ray_tracing_material.slang"

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
    float rtShadowRadius;
    uint frameIndex;
    float environmentIntensity;
};
typealias LightBuffer = LightBufferData *;

struct PushConstants
{
    float4x4 invViewProj;
    uint gbuffer1;
    uint depth;
    uint smpl;
    uint texBlueNoise;
    uint tlas;
    uint shadow_alpha_test_method;
    uint retrace_max_depth;
    Materials *materials;
    Indices *indices;
    VerticesPositions *verticesPosition;
    VerticesAttributes *verticesAttributes;
    RayTracingMeshData *meshes;
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
        rq.TraceRayInline(kTLAS[pc.tlas], RAY_FLAG_NONE, 0xff, ray);
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
            if (pc.shadow_alpha_test_method != 2)
            {
                RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
                        RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES>
                    rq;

                RayDesc ray = {worldPos + float3(n) * 0.03, 0.03, rayDir, 1e4};
                rq.TraceRayInline(kTLAS[pc.tlas], RAY_FLAG_NONE, 0xff, ray);
                while (rq.Proceed())
                {
                    uint customIndex = uint(rq.CandidateRayInstanceCustomIndex());
                    if (pc.shadow_alpha_test_method == 0 || (customIndex & kRayTracingMaskedBit) == 0)
                    {
                        rq.CommitNonOpaqueTriangleHit();
                    }
                    else
                    {
                        RayTracingMeshData mesh = pc.meshes[customIndex & kRayTracingMeshIndexMask];
                        float alpha = sampleRayTracingAlpha(mesh, rq.CandidatePrimitiveIndex(),
                            rq.CandidateTriangleBarycentrics(), pc.indices, pc.verticesAttributes, pc.materials);
                        if (alpha >= pc.materials->mtl[mesh.materialId].emissiveFactorAlphaCutoff.w)
                        {
                            rq.CommitNonOpaqueTriangleHit();
                        }
                    }
                }

                if (rq.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
                {
                    // e.g. to get distance:
                    // const float dist = rayQueryGetIntersectionTEXT(query, false);
                    shadow = half(0.0);
                }
                else
                {
                    shadow = half(1.0);
                }
            }
            else
            {
                // Experimental first-hit retrace: the returned hit may not be the nearest intersection.
                RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> rq;
                RayDesc ray = {worldPos + float3(n) * 0.03, 0.03, rayDir, 1e4};
                // Conservatively keep the shadow if the trace budget is exhausted.
                shadow = half(0.0);
                for (uint depth = 0; depth < pc.retrace_max_depth; ++depth)
                {
                    rq.TraceRayInline(kTLAS[pc.tlas], RAY_FLAG_NONE, 0xff, ray);
                    while (rq.Proceed())
                    {
                    }
                    if (rq.CommittedStatus() == COMMITTED_NOTHING)
                    {
                        shadow = half(1.0);
                        break;
                    }
                    uint customIndex = uint(rq.CommittedRayInstanceCustomIndex());
                    if ((customIndex & kRayTracingMaskedBit) == 0)
                    {
                        break;
                    }
                    RayTracingMeshData mesh = pc.meshes[customIndex & kRayTracingMeshIndexMask];
                    float alpha = sampleRayTracingAlpha(mesh, rq.CommittedPrimitiveIndex(),
                        rq.CommittedTriangleBarycentrics(), pc.indices, pc.verticesAttributes, pc.materials);
                    if (alpha >= pc.materials->mtl[mesh.materialId].emissiveFactorAlphaCutoff.w)
                    {
                        break;
                    }
                    // Positive ray distances: advance one representable float without a fixed gap.
                    ray.TMin = asfloat(asuint(rq.CommittedRayT()) + 1u);
                    if (ray.TMin >= ray.TMax)
                    {
                        shadow = half(1.0);
                        break;
                    }
                }
            }
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
