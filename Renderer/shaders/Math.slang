#pragma once
static const float M_PI = 3.141592653589793;

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

float2 blueNoiseVec2(uint texBlueNoise, uint smpl, float2 screenCoord, uint frameIndex)
{
    uint2 wrappedXY = uint2(screenCoord) & uint2(127);
    uint wrappedZ = frameIndex & 63u;
    float2 uv =
        (float2(float(wrappedXY.x), float(wrappedZ * 128u + wrappedXY.y)) + float2(0.5)) / float2(128.0, 8192.0);
    return textureBindless2D(texBlueNoise, smpl, uv).rg;
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
