#include "Math.slang"
#include "Bindless.slang"

#include "Mesh.slang"
#include "Surface.slang"

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

    float3 emissive = mat.emissiveFactorAlphaCutoff.rgb;
    if (mat.emissiveTexture > 0)
    {
        emissive *= textureBindless2D(mat.emissiveTexture, mat.emissiveTextureSampler, input.uv).rgb;
    }

    const float specular = 0.5;
    float metallic = mat.metallicRoughnessNormalOcclusion.x;
    float roughness = mat.metallicRoughnessNormalOcclusion.y;
    if (mat.metallicRoughnessTexture != 0)
    {
        float4 mr = textureBindless2D(mat.metallicRoughnessTexture, mat.metallicRoughnessTextureSampler, input.uv);
        metallic *= mr.b;
        roughness *= mr.g;
    }
    float ao = 1.0;
    if (mat.occlusionTexture != 0)
        ao = lerp(1.0, textureBindless2D(mat.occlusionTexture, mat.occlusionTextureSampler, input.uv).r,
                   saturate(mat.metallicRoughnessNormalOcclusion.w));
    const float shadingModelId = 0.0;

    output.outSceneColor = float4(emissive, 1.0);
    output.outGBuffer1 = float4(encodeOctahedron(n), 0.0, 0.0);
    output.outGBuffer2 = float4(saturate(metallic), specular, saturate(roughness), shadingModelId);
    output.outGBuffer3 = float4(baseColor.rgb, ao);

    return output;
}
