#include "Math.slang"
#include "Bindless.slang"
#include "Lighting.slang"

#include "Shadows.slang"

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
    float3 cameraPos;
    uint sceneColor;
    uint gbuffer1;
    uint gbuffer2;
    uint gbuffer3;
    uint depth;
    uint texSkyboxIrradiance;
    uint texSkybox;
    uint texBRDF_LUT;
    uint texRTShadowAO;
    uint rtReflectionTex;
    uint smpl;
    LightBuffer light;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

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

    half3 scene = half3(textureBindless2D(pc.sceneColor, pc.smpl, input.uv).rgb);
    float depth = textureBindless2D(pc.depth, pc.smpl, input.uv).r;
    if (depth >= 0.9999)
    {
        output.out_FragColor = float4(float3(scene), 1.0);
        return output;
    }

    half4 g1 = half4(textureBindless2D(pc.gbuffer1, pc.smpl, input.uv));
    float4 g2 = textureBindless2D(pc.gbuffer2, pc.smpl, input.uv);

    half4 g3 = half4(textureBindless2D(pc.gbuffer3, pc.smpl, input.uv));

    float3 n = decodeOctahedron(float2(g1.xy));

    half3 baseColor = g3.rgb;

    float3 worldPos = reconstructWorldPosition(input.uv, depth, pc.invViewProj);
    float shadowVisibility = 1.0;
    float ao = float(g3.a);
    float4 shadowCoords = mul(pc.light->viewProjBias, float4(worldPos, 1.0));
    if (pc.texRTShadowAO != 0)
    {
        float2 rtData = textureBindless2D(pc.texRTShadowAO, pc.smpl, input.uv).rg;
        if (pc.light->rtShadowEnabled != 0)
        {
            shadowVisibility = rtData.r;
        }
        else
        {
            shadowVisibility = shadow(shadowCoords, pc.light->shadowTexture, pc.light->shadowSampler);
        }
        if (pc.light->rtAOEnabled != 0)
        {
            ao *= rtData.g;
        }
    }
    else
    {
        shadowVisibility = shadow(shadowCoords, pc.light->shadowTexture, pc.light->shadowSampler);
    }

    float3 l = -normalize(pc.light->lightDir.xyz);
    float3 v = normalize(pc.cameraPos.xyz - worldPos);
    float metallic = saturate(g2.x);
    float roughness = saturate(g2.z);

    PBRLighting lighting = evaluatePBRLighting(float3(baseColor), metallic, roughness, n, v, l,
                                               shadowVisibility, ao, pc.texSkyboxIrradiance,
                                               pc.texSkybox, pc.texBRDF_LUT, pc.smpl);
    // RT is currently an ideal-mirror sample, weighted once with the receiving surface's Fresnel.
    // Replace environment specular instead of adding a second copy of the reflected environment.
    if (pc.rtReflectionTex != 0)
        lighting.indirectSpecular = lighting.fresnel * textureBindless2D(pc.rtReflectionTex, pc.smpl, input.uv).rgb;
    float3 color = float3(scene) + lighting.total();
    output.out_FragColor = float4(float3(color), 1.0);

    return output;
}
