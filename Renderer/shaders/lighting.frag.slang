#include "Bindless.slang"

#include "Shadows.slang"

struct LightBufferData
{
    float4x4 viewProjBias;
    float4 lightDir;
    float4 lightColorIntensity;
    uint shadowTexture;
    uint shadowSampler;
    uint frameIndex;
    float iblIntensity;
};
typealias LightBuffer = LightBufferData *;

struct PushConstants
{
    float4x4 invViewProj[2];
    uint sceneColor;
    uint gbuffer1;
    uint gbuffer2;
    uint gbuffer3;
    uint depth;
    uint texSkyboxIrradiance;
    uint smpl;
    LightBuffer light;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

half2 signNotZero16(half2 v)
{
    return half2(v.x >= half(0.0) ? half(1.0) : half(-1.0), v.y >= half(0.0) ? half(1.0) : half(-1.0));
}

float3 decodeOctahedron(float2 e)
{
    half2 f = half2(e) * half(2.0) - half(1.0);
    half3 n = half3(f.x, f.y, half(1.0) - abs(f.x) - abs(f.y));
    if (n.z < half(0.0))
    {
        n.xy = (half(1.0) - abs(n.yx)) * signNotZero16(n.xy);
    }
    return normalize(float3(n));
}

float3 reconstructWorldPosition(float2 uv, float depth, uint viewId)
{
    float2 ndc = uv * float2(2.0, -2.0) + float2(-1.0, 1.0);
    float4 world = mul(pc.invViewProj[viewId], float4(ndc, depth, 1.0));
    return world.xyz / world.w;
}

struct FragmentInput
{
    float4 position : SV_Position;
    uint viewId : SV_ViewID;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};
struct FragmentOutput
{
    float4 out_FragColor : SV_Target0;
};
[shader("fragment")] FragmentOutput main(FragmentInput input) {
    FragmentOutput output;

    half3 scene = half3(textureBindless2DArray(pc.sceneColor, pc.smpl, input.uv, input.viewId).rgb);
    float depth = textureBindless2DArray(pc.depth, pc.smpl, input.uv, input.viewId).r;
    if (depth >= 0.9999)
    {
        output.out_FragColor = float4(float3(scene), 1.0);
        return output;
    }

    half4 g1 = half4(textureBindless2DArray(pc.gbuffer1, pc.smpl, input.uv, input.viewId));

    half4 g3 = half4(textureBindless2DArray(pc.gbuffer3, pc.smpl, input.uv, input.viewId));

    float3 n = decodeOctahedron(float2(g1.xy));

    half3 baseColor = g3.rgb;

    float3 worldPos = reconstructWorldPosition(input.uv, depth, input.viewId);
    float shadowVisibility = 1.0;
    float ao = float(g3.a);
    float4 shadowCoords = mul(pc.light->viewProjBias, float4(worldPos, 1.0));
    shadowVisibility = shadow(shadowCoords, pc.light->shadowTexture, pc.light->shadowSampler);

    float3 l = -normalize(pc.light->lightDir.xyz);
    half nDotL = half(max(dot(n, l), 0.0));

    float3 sky = float3(-n.x, n.y, -n.z);
    half3 ibl = half3(textureBindlessCube(pc.texSkyboxIrradiance, 0, sky).rgb);

    const half3 f0 = half3(0.04);
    half3 diffuseAlbedo = baseColor * (half3(1.0) - f0);
    half3 indirectDiffuse = ibl * diffuseAlbedo * half(clamp(ao, 0.0, 1.0) * pc.light->iblIntensity);
    half3 lightColor = half3(pc.light->lightColorIntensity.rgb);
    half lightIntensity = half(pc.light->lightColorIntensity.a);
    half3 directDiffuse = lightColor * lightIntensity * half3(nDotL) * diffuseAlbedo * half(shadowVisibility);
    half3 color = scene + indirectDiffuse + directDiffuse;
    output.out_FragColor = float4(float3(color), 1.0);

    return output;
}
