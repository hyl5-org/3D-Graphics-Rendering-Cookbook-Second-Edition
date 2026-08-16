#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_ray_query : require
#extension GL_EXT_ray_flags_primitive_culling : require

#include <data/shaders/math.sp>

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 out_FragColor;

layout(std430, buffer_reference) readonly buffer LightBuffer
{
    mat4 viewProjBias;
    vec4 lightDir;
    uint frameIndex;
};

layout(push_constant) uniform PushConstants
{
    mat4 invViewProj;
    uint gbuffer1;
    uint depth;
    uint smpl;
    uint texBlueNoise;
    LightBuffer light;
}
pc;

f16vec2 signNotZero16(f16vec2 v)
{
    return f16vec2(v.x >= float16_t(0.0) ? float16_t(1.0) : float16_t(-1.0),
                   v.y >= float16_t(0.0) ? float16_t(1.0) : float16_t(-1.0));
}

f16vec3 decodeOctahedron16(f16vec2 e)
{
    f16vec2 f = e * float16_t(2.0) - float16_t(1.0);
    f16vec3 n = f16vec3(f.x, f.y, float16_t(1.0) - abs(f.x) - abs(f.y));
    if (n.z < float16_t(0.0))
    {
        n.xy = (float16_t(1.0) - abs(n.yx)) * signNotZero16(n.xy);
    }
    return normalize(n);
}

vec3 reconstructWorldPosition(vec2 uv, float depth)
{
    vec2 ndc = uv * vec2(2.0, -2.0) + vec2(-1.0, 1.0);
    vec4 world = pc.invViewProj * vec4(ndc, depth, 1.0);
    return world.xyz / world.w;
}

float16_t traceRTAO16(vec3 p, f16vec3 n)
{
    mat3 basis = getTangentBasis(n);

    float16_t occluded = float16_t(0.0);
    for (uint i = 0; i != pc.light.rtAOSamples; ++i)
    {
        vec2 rnd = blueNoiseVec2(pc.texBlueNoise, pc.smpl, gl_FragCoord.xy, pc.light.frameIndex + i);
        vec3 dir = normalize(basis * cosineSampleHemisphere(rnd).xyz);

        rayQueryEXT rq;
        rayQueryInitializeEXT(rq, kTLAS[0], gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipAABBEXT, 0xff,
                              p + vec3(n) * 0.03, 0.03, dir, pc.light.rtAORadius);
        rayQueryProceedEXT(rq);
        occluded += (rayQueryGetIntersectionTypeEXT(rq, true) != gl_RayQueryCommittedIntersectionNoneEXT)
                        ? float16_t(1.0)
                        : float16_t(0.0);
    }

    float16_t ao = float16_t(1.0) - occluded / float16_t(pc.light.rtAOSamples);
    return float16_t(pow(clamp(float(ao), 0.0, 1.0), pc.light.rtAOPower));
}

void main()
{
    float d = textureBindless2D(pc.depth, pc.smpl, uv).r;
    if (d >= 0.9999)
    {
        out_FragColor = vec4(1.0);
        return;
    }

    f16vec4 g1 = f16vec4(textureBindless2D(pc.gbuffer1, pc.smpl, uv));
    f16vec3 n = decodeOctahedron16(g1.xy);
    vec3 worldPos = reconstructWorldPosition(uv, d);

    float16_t shadow = float16_t(1.0);
    float16_t ao = float16_t(1.0);

    if (pc.light.rtShadowEnabled != 0)
    {
        vec3 l = -normalize(pc.light.lightDir.xyz);
        vec2 rnd = blueNoiseVec2(pc.texBlueNoise, pc.smpl, gl_FragCoord.xy, pc.light.frameIndex);
        vec3 rayDir = generateSoftShadowRayDir(l, pc.light.rtShadowRadius, rnd);

        if (dot(vec3(n), rayDir) > 0.0)
        {
            rayQueryEXT rq;
            // TODO(luhanyang): Adjust trace distance by the frustum far plane.
            rayQueryInitializeEXT(rq, kTLAS[0],
                                  gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipAABBEXT,
                                  0xff, worldPos + vec3(n) * 0.03, 0.03, rayDir, 1e4);
            rayQueryProceedEXT(rq);
            shadow = (rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionNoneEXT)
                         ? float16_t(1.0)
                         : float16_t(0.0);
        }
        else
        {
            shadow = float16_t(0.0);
        }
    }

    if (pc.light.rtAOEnabled != 0)
    {
        ao = traceRTAO16(worldPos, n);
    }

    out_FragColor = vec4(shadow, ao, float16_t(0.0), float16_t(1.0));
}
