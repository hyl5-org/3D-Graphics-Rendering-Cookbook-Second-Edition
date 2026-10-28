#pragma once

static const float M_PI = 3.141592653589793;

// Ray Tracing Gems, chapter 6. n is the unit geometric normal facing the outgoing ray.
float3 offsetRay(float3 p, float3 n)
{
    const float origin = 1.0 / 32.0;
    const float floatScale = 1.0 / 65536.0;
    const float intScale = 256.0;
    int3 offset = int3(intScale * n);
    float3 shifted = float3(
        asfloat(asint(p.x) + (p.x < 0.0 ? -offset.x : offset.x)),
        asfloat(asint(p.y) + (p.y < 0.0 ? -offset.y : offset.y)),
        asfloat(asint(p.z) + (p.z < 0.0 ? -offset.z : offset.z)));
    return float3(
        abs(p.x) < origin ? p.x + floatScale * n.x : shifted.x,
        abs(p.y) < origin ? p.y + floatScale * n.y : shifted.y,
        abs(p.z) < origin ? p.z + floatScale * n.z : shifted.z);
}

float2 signNotZero(float2 v)
{
    return float2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
}

half2 signNotZero16(half2 v)
{
    return half2(v.x >= half(0.0) ? half(1.0) : half(-1.0), v.y >= half(0.0) ? half(1.0) : half(-1.0));
}

float3 unpackSnorm10x3(uint packed)
{
    int3 v = int3(packed, packed >> 10, packed >> 20);
    v = (v << 22) >> 22;
    return max(float3(v) / 511.0, float3(-1.0));
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

half3 decodeOctahedron16(half2 e)
{
    half2 f = e * half(2.0) - half(1.0);
    half3 n = half3(f.x, f.y, half(1.0) - abs(f.x) - abs(f.y));
    if (n.z < half(0.0))
    {
        n.xy = (half(1.0) - abs(n.yx)) * signNotZero16(n.xy);
    }
    return normalize(n);
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

float3 reconstructWorldPosition(float2 uv, float depth, float4x4 invViewProj)
{
    float2 ndc = uv * float2(2.0, -2.0) + float2(-1.0, 1.0);
    float4 world = mul(invViewProj, float4(ndc, depth, 1.0));
    return world.xyz / world.w;
}

float2 reprojectToPrevUV(float3 worldPos, float4x4 prevViewProj)
{
    float4 prevClip = mul(prevViewProj, float4(worldPos, 1.0));
    float2 prevNDC = prevClip.xy / prevClip.w;
    return prevNDC * float2(0.5, -0.5) + float2(0.5, 0.5);
}

float3 fresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    return F0 + (max(float3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float pow2(float x)
{
    return x * x;
}

float3x3 getTangentBasis(float3 tangentZ)
{
    const float sign_ = tangentZ.z >= 0 ? 1 : -1;
    const float a = -1.0 / (sign_ + tangentZ.z);
    const float b = tangentZ.x * tangentZ.y * a;

    float3 tangentX = float3(1 + sign_ * a * pow2(tangentZ.x), sign_ * b, -sign_ * tangentZ.x);
    float3 tangentY = float3(b, sign_ + a * pow2(tangentZ.y), -tangentZ.y);

    return transpose(float3x3(tangentX, tangentY, tangentZ));
}

float4 cosineSampleHemisphere(float2 e)
{
    float phi = 2.0 * M_PI * e.x;
    float cosTheta = sqrt(e.y);
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    float3 h;
    h.x = sinTheta * cos(phi);
    h.y = sinTheta * sin(phi);
    h.z = cosTheta;

    float pdf = cosTheta * (1.0 / M_PI);

    return float4(h, pdf);
}

float4 uniformSampleConeRobust(float2 e, float sinThetaMax2)
{
    float phi = 2.0 * M_PI * e.x;
    float oneMinusCosThetaMax =
        sinThetaMax2 < 0.01 ? sinThetaMax2 * (0.5 + 0.125 * sinThetaMax2) : 1.0 - sqrt(1.0 - sinThetaMax2);

    float cosTheta = 1.0 - oneMinusCosThetaMax * e.y;
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    float3 l;
    l.x = sinTheta * cos(phi);
    l.y = sinTheta * sin(phi);
    l.z = cosTheta;
    float pdf = 1.0 / (2.0 * M_PI * oneMinusCosThetaMax);

    return float4(l, pdf);
}

float3 generateSoftShadowRayDir(float3 lightDir, float lightRadius, float2 randSample)
{
    float sinThetaMax2 = lightRadius * lightRadius;
    float4 coneSample = uniformSampleConeRobust(randSample, sinThetaMax2);
    float3x3 basis = getTangentBasis(lightDir);
    return normalize(mul(basis, coneSample.xyz));
}
