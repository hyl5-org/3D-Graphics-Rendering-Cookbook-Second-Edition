#include "Bindless.slang"
#include "PBR.slang"

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
    float4 cameraPos[2];
    uint sceneColor;
    uint gbuffer1;
    uint gbuffer2;
    uint gbuffer3;
    uint depth;
    uint texSkyboxIrradiance;
    uint texSkybox;
    uint texBRDF_LUT;
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
    float4 g2 = textureBindless2DArray(pc.gbuffer2, pc.smpl, input.uv, input.viewId);

    half4 g3 = half4(textureBindless2DArray(pc.gbuffer3, pc.smpl, input.uv, input.viewId));

    float3 n = decodeOctahedron(float2(g1.xy));

    half3 baseColor = g3.rgb;

    float3 worldPos = reconstructWorldPosition(input.uv, depth, input.viewId);
    float shadowVisibility = 1.0;
    float ao = float(g3.a);
    float4 shadowCoords = mul(pc.light->viewProjBias, float4(worldPos, 1.0));
    shadowVisibility = shadow(shadowCoords, pc.light->shadowTexture, pc.light->shadowSampler);

    float3 l = -normalize(pc.light->lightDir.xyz);
    float3 v = normalize(pc.cameraPos[input.viewId].xyz - worldPos);
    float metallic = saturate(g2.x);
    float roughness = saturate(g2.z);

    float3 sky = float3(-n.x, n.y, -n.z);
    half3 ibl = half3(textureBindlessCube(pc.texSkyboxIrradiance, 0, sky).rgb);

    float NdotV = saturate(dot(n, v));
    float3 f0 = pbrF0(float3(baseColor), metallic);
    float3 F = pbrFresnel(f0, NdotV);
    float3 indirectDiffuse = pbrIndirectDiffuse(float3(baseColor), metallic, F, float3(ibl), ao * pc.light->iblIntensity);
    float3 lightRadiance = pc.light->lightColorIntensity.rgb * pc.light->lightColorIntensity.a;
    float3 direct = pbrMetallicRoughness(float3(baseColor), metallic, roughness, n, v, l) * lightRadiance * shadowVisibility;
    float3 r = reflect(-v, n);
    float lod = roughness * textureBindlessCubeMaxLod(pc.texSkybox);
    float3 radiance = textureBindlessCubeLod(pc.texSkybox, pc.smpl, float3(-r.x, r.y, -r.z), lod).rgb;
    float2 brdf = textureBindless2DLod(pc.texBRDF_LUT, pc.smpl, float2(NdotV, roughness), 0).rg;
    float3 reflection = pbrIndirectSpecular(f0, brdf, radiance) * pc.light->iblIntensity;
    float3 color = float3(scene) + indirectDiffuse + direct + reflection;
    output.out_FragColor = float4(float3(color), 1.0);

    return output;
}
