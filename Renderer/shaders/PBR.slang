#pragma once

static const float kPbrPi = 3.141592653589793;

// glTF metallic-roughness: Lambert diffuse + GGX, correlated Smith visibility, Schlick Fresnel.
float3 pbrFresnel(float3 f0, float cosTheta)
{
    float w = pow(1.0 - saturate(cosTheta), 5.0);
    return f0 + (float3(1.0) - f0) * w;
}

float3 pbrF0(float3 baseColor, float metallic)
{
    return lerp(float3(0.04), baseColor, saturate(metallic));
}

// Returns BRDF * NdotL. Multiply by incident light radiance and visibility only once.
float3 pbrMetallicRoughness(float3 baseColor, float metallic, float roughness, float3 N, float3 V, float3 L)
{
    float NdotV = saturate(dot(N, V));
    float NdotL = saturate(dot(N, L));
    if (NdotV <= 0.0 || NdotL <= 0.0)
        return float3(0.0);

    float3 H = normalize(V + L);
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));
    float alpha = max(saturate(roughness) * saturate(roughness), 0.0014142);
    float a2 = alpha * alpha;
    // This form avoids cancellation for very smooth surfaces when NdotH approaches 1.
    float d = (1.0 - NdotH * NdotH) + NdotH * NdotH * a2;
    float D = a2 / (kPbrPi * d * d);
    float gv = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
    float gl = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
    float visibility = 0.5 / (gv + gl);
    float3 F = pbrFresnel(pbrF0(baseColor, metallic), VdotH);
    float3 diffuse = (float3(1.0) - F) * (1.0 - saturate(metallic)) * baseColor / kPbrPi;
    return (diffuse + F * D * visibility) * NdotL;
}

// Irradiance maps in this renderer store the diffuse convolution (E / pi).
float3 pbrIndirectDiffuse(float3 baseColor, float metallic, float3 F, float3 diffuseEnvironment, float ao)
{
    return (float3(1.0) - F) * (1.0 - saturate(metallic)) * baseColor * diffuseEnvironment * saturate(ao);
}
