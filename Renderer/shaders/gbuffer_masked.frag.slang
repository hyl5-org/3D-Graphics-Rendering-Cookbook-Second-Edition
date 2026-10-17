#include "Bindless.slang"

#include "Mesh.slang"
#include "Surface.slang"

float2 signNotZero(float2 v)
{
    return float2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
}

float2 encodeOctahedron(float3 n)
{
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    float2 p = n.xy;
    if (n.z < 0.0)
    {
        p = (1.0 - abs(p.yx)) * signNotZero(p);
    }
    return p * 0.5 + 0.5;
}

struct FragmentInput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float3 normal : TEXCOORD1;
    [[vk::location(2)]] float3 worldPos : TEXCOORD2;
    [[vk::location(3)]] nointerpolation uint materialId : TEXCOORD3;
};
struct FragmentOutput
{
    float4 outSceneColor : SV_Target0;
    float4 outGBuffer1 : SV_Target1;
    float4 outGBuffer2 : SV_Target2;
    float4 outGBuffer3 : SV_Target3;
};
[shader("fragment")] FragmentOutput main(FragmentInput input) {
    FragmentOutput output;

    MetallicRoughnessDataGPU mat = pc.materials[input.materialId];

    float4 baseColor = mat.baseColorFactor;
    if (mat.baseColorTexture > 0)
    {
        baseColor *= textureBindless2D(mat.baseColorTexture, mat.baseColorTextureSampler, float2(input.uv));
    }

    runAlphaTest(baseColor.a, mat.emissiveFactorAlphaCutoff.w / max(32.0 * fwidth(float(input.uv.x)), 1.0),
                 input.position.xy);

    float3 n = normalize(float3(input.normal));
    if (mat.normalTexture > 0)
    {
        float3 normalSample = textureBindless2D(mat.normalTexture, mat.normalTextureSampler, input.uv).xyz;
        if (length(normalSample) > 0.5)
        {
            n = perturbNormal(n, input.worldPos, normalSample, float2(input.uv));
        }
    }

    float3 emissive = float3(0.0);
    if (mat.emissiveTexture > 0)
    {
        emissive = mat.emissiveFactorAlphaCutoff.rgb *
                   textureBindless2D(mat.emissiveTexture, mat.emissiveTextureSampler, float2(input.uv)).rgb;
    }

    const float specular = 0.5;
    const float metallic = 0.0;
    const float roughness = 1.0;
    const float ao = 1.0;
    const float shadingModelId = 0.0;

    output.outSceneColor = float4(emissive, 1.0);
    output.outGBuffer1 = float4(encodeOctahedron(n), 0.0, 0.0);
    output.outGBuffer2 = float4(metallic, specular, roughness, shadingModelId);
    output.outGBuffer3 = float4(baseColor.rgb, ao);

    return output;
}
