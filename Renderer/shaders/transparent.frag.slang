#include "Math.slang"
#include "Bindless.slang"

#include "Mesh.slang"
#include "Surface.slang"

[[vk::binding(2, 0)]][format("r32ui")] RWTexture2D<uint> kTextures2DInOut[];

struct FragmentInput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float3 normal : TEXCOORD1;
    [[vk::location(2)]] float3 worldPos : TEXCOORD2;
    [[vk::location(3)]] nointerpolation uint materialId : TEXCOORD3;
    uint sampleId : SV_SampleIndex;
    uint coverage : SV_Coverage;
};
[shader("fragment")][earlydepthstencil] void main(FragmentInput input) {
    MetallicRoughnessDataGPU mat = pc.materials[input.materialId];

    float4 emissiveColor =
        float4(mat.emissiveFactorAlphaCutoff.rgb, 0) * textureBindless2D(mat.emissiveTexture, 0, input.uv);
    float4 baseColor = mat.baseColorFactor *
                       (mat.baseColorTexture > 0 ? textureBindless2D(mat.baseColorTexture, 0, input.uv) : float4(1.0));

    float3 n = normalize(input.normal);

    float3 normalSample = textureBindless2D(mat.normalTexture, 0, input.uv).xyz;
    if (length(normalSample) > 0.5)
    {
        n = perturbNormal(n, input.worldPos, normalSample, input.uv);
    }

    float NdotL = clamp(dot(n, -normalize(pc.light->lightDir.xyz)), 0.1, 1.0);

    const float4 f0 = float4(0.04);
    float3 sky = float3(-n.x, n.y, -n.z);
    float4 diffuse =
        (textureBindlessCube(pc.texSkyboxIrradiance, 0, sky) + float4(NdotL)) * baseColor * (float4(1.0) - f0);

    float3 v = normalize(pc.cameraPos.xyz - input.worldPos);
    float3 reflection = reflect(v, n);
    reflection = float3(reflection.x, -reflection.y, reflection.z);
    float3 colorRefl = textureBindlessCube(pc.texSkybox, 0, reflection).rgb;
    float3 kS = fresnelSchlickRoughness(clamp(dot(n, v), 0.0, 1.0), f0.rgb, 0.1);
    float3 color = emissiveColor.rgb + diffuse.rgb + colorRefl * kS;

    float alpha = clamp(baseColor.a * mat.clearcoatTransmissionThickness.z, 0.0, 1.0);
    bool isTransparent = (alpha > 0.01) && (alpha < 0.99);
    uint mask = 1 << input.sampleId;
    if (isTransparent && !IsHelperLane() && ((input.coverage & mask) == mask))
    {
        uint index;
        InterlockedAdd(pc.oit->atomicCounter->numFragments, 1, index);
        if (index < pc.oit->maxOITFragments)
        {
            uint prevIndex;
            InterlockedExchange(kTextures2DInOut[pc.oit->texHeadsOIT][int2(input.position.xy)], index, prevIndex);
            TransparentFragment frag;
            frag.color = half4(half3(color), half(alpha));
            frag.depth = input.position.z;
            frag.next = prevIndex;
            pc.oit->oitLists[index] = frag;
        }
    }
}
