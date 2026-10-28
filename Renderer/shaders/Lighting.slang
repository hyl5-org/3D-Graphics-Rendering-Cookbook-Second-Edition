#pragma once

#include "Bindless.slang"
#include "PBR.slang"

struct PBRLighting
{
    float3 direct;
    float3 indirectDiffuse;
    float3 indirectSpecular;
    float3 fresnel;

    float3 total()
    {
        return direct + indirectDiffuse + indirectSpecular;
    }
};

// N, V and L are normalized world-space directions; L points toward the light.
// White directional light: perpendicular illuminance in lux.
// BRDF * illuminance produces outgoing luminance in cd/m^2; auto exposure handles display scaling.
// Environment maps are uncalibrated HDR assets, controlled separately by a relative multiplier.
// Explicit LOD sampling keeps the same function usable in fragment and closest-hit stages.
PBRLighting evaluatePBRLighting(float3 baseColor, float metallic, float roughness,
                                float3 N, float3 V, float3 L, float shadowVisibility, float ao,
                                uint texIrradiance, uint texEnvironment, uint texBRDF, uint samplerId,
                                bool computeSpecularIBL = true, float illuminanceLux = 0.0,
                                float environmentIntensity = 1.0)
{
    metallic = saturate(metallic);
    roughness = saturate(roughness);
    float NdotV = saturate(dot(N, V));
    float3 f0 = pbrF0(baseColor, metallic);
    PBRLighting result;
    result.fresnel = pbrFresnel(f0, NdotV);
    result.direct = pbrMetallicRoughness(baseColor, metallic, roughness, N, V, L)
                    * shadowVisibility * max(illuminanceLux, 0.0);

    float3 sky = float3(-N.x, N.y, -N.z);
    float3 irradiance = textureBindlessCubeLod(texIrradiance, samplerId, sky, 0).rgb;
    result.indirectDiffuse = pbrIndirectDiffuse(baseColor, metallic, result.fresnel, irradiance, ao) * max(environmentIntensity, 0.0);

    result.indirectSpecular = float3(0.0);
    if (computeSpecularIBL)
    {
        float3 r = reflect(-V, N);
        float lod = roughness * textureBindlessCubeMaxLod(texEnvironment);
        float3 radiance = textureBindlessCubeLod(texEnvironment, samplerId, float3(-r.x, r.y, -r.z), lod).rgb;
        float2 brdf = textureBindless2DLod(texBRDF, samplerId, float2(NdotV, roughness), 0).rg;
        result.indirectSpecular = pbrIndirectSpecular(f0, brdf, radiance) * max(environmentIntensity, 0.0);
    }
    return result;
}
