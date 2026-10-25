#pragma once

#include "Math.slang"

float4 SRGBtoLINEAR(float4 srgbIn)
{
    float3 linOut = pow(srgbIn.xyz, float3(2.2));

    return float4(linOut, srgbIn.a);
}

float3x3 cotangentFrame(float3 N, float3 p, float2 uv)
{
    float3 dp1 = ddx(p);
    float3 dp2 = ddy(p);
    float2 duv1 = ddx(uv);
    float2 duv2 = ddy(uv);

    float3 dp2perp = cross(dp2, N);
    float3 dp1perp = cross(N, dp1);
    float3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    float3 B = dp2perp * duv1.y + dp1perp * duv2.y;

    float invmax = rsqrt(max(dot(T, T), dot(B, B)));

    float w = (dot(cross(N, T), B) < 0.0) ? -1.0 : 1.0;

    T = T * w;

    return transpose(float3x3(T * invmax, B * invmax, N));
}

float3 perturbNormal(float3 n, float3 v, float3 normalSample, float2 uv)
{
    float3 map = normalize(2.0 * normalSample - float3(1.0));
    float3x3 TBN = cotangentFrame(n, v, uv);
    return normalize(mul(TBN, map));
}

void runAlphaTest(float alpha, float alphaThreshold, float2 pixel)
{
    if (alphaThreshold > 0.0)
    {
        float4x4 thresholdMatrix = float4x4(1.0 / 17.0, 9.0 / 17.0, 3.0 / 17.0, 11.0 / 17.0, 13.0 / 17.0, 5.0 / 17.0,
                                            15.0 / 17.0, 7.0 / 17.0, 4.0 / 17.0, 12.0 / 17.0, 2.0 / 17.0, 10.0 / 17.0,
                                            16.0 / 17.0, 8.0 / 17.0, 14.0 / 17.0, 6.0 / 17.0);

        alpha = clamp(alpha - 0.5 * thresholdMatrix[int(fmod(pixel.x, 4.0))][int(fmod(pixel.y, 4.0))], 0.0, 1.0);

        if (alpha < alphaThreshold)
            discard;
    }
}
