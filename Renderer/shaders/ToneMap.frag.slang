#include "Bindless.slang"
#include "ColorSpace.slang"

// Distinct, observable PSO specializations; the normal renderer keeps gain = 1.
[[vk::constant_id(0)]] const float psoGain = 1.0;

static const int ToneMappingMode_None = 0;
static const int ToneMappingMode_Reinhard = 1;
static const int ToneMappingMode_Uchimura = 2;
static const int ToneMappingMode_KhronosPBR = 3;

struct PushConstants
{
    uint texColor;
    uint texLuminance;
    uint texBloom;
    uint smpl;
    int drawMode;

    float exposure;
    float bloomStrength;

    float maxWhite;

    float P;
    float a;
    float m;
    float l;
    float c;
    float b;

    float startCompression;
    float desaturation;
    uint encodeSRGB;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

float3 uchimura(float3 x, float P, float a, float m, float l, float c, float b)
{
    float l0 = ((P - m) * l) / a;
    float L0 = m - m / a;
    float L1 = m + (1.0 - m) / a;
    float S0 = m + l0;
    float S1 = m + a * l0;
    float C2 = (a * P) / (P - S1);
    float CP = -C2 / P;

    float3 w0 = float3(1.0 - smoothstep(0.0, m, x));
    float3 w2 = float3(step(m + l0, x));
    float3 w1 = float3(1.0 - w0 - w2);

    float3 T = float3(m * pow(x / m, float3(c)) + b);
    float3 S = float3(P - (P - S1) * exp(CP * (x - S0)));
    float3 L = float3(m + a * (x - m));

    return T * w0 + L * w1 + S * w2;
}

float luminance(float3 v)
{
    return dot(v, float3(0.2126, 0.7152, 0.0722));
}

float3 reinhard2(float3 v, float maxWhite)
{
    float l_old = luminance(v);
    float l_new = l_old * (1.0 + (l_old / (maxWhite * maxWhite))) / (1.0 + l_old);
    return v * (l_new / l_old);
}

float3 PBRNeutralToneMapping(float3 color, float startCompression, float desaturation)
{
    startCompression -= 0.04;

    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;

    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression)
    {
        return color;
    }

    const float d = 1. - startCompression;
    float newPeak = 1. - d * d / (peak + d - startCompression);
    color *= newPeak / peak;

    float g = 1. - 1. / (desaturation * (peak - newPeak) + 1.);
    return lerp(color, newPeak * float3(1, 1, 1), g);
}

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

    float3 color = textureBindless2D(pc.texColor, pc.smpl, input.uv).rgb;
    float3 bloom = textureBindless2D(pc.texBloom, pc.smpl, input.uv).rgb;
    float avgLuminance = textureBindless2D(pc.texLuminance, pc.smpl, float2(0.5)).r;

    if (pc.drawMode != ToneMappingMode_None)
    {
        float midGray = 0.5;
        color *= pc.exposure * midGray / (avgLuminance + 0.001);
    }

    if (pc.drawMode == ToneMappingMode_Reinhard)
    {
        color = reinhard2(pc.exposure * color, pc.maxWhite);
    }
    if (pc.drawMode == ToneMappingMode_Uchimura)
    {
        color = uchimura(pc.exposure * color, pc.P, pc.a, pc.m, pc.l, pc.c, pc.b);
    }
    if (pc.drawMode == ToneMappingMode_KhronosPBR)
    {
        color = PBRNeutralToneMapping(pc.exposure * color, pc.startCompression, pc.desaturation);
    }

    color = (color + pc.bloomStrength * bloom) * psoGain;
    // sRGB attachments encode automatically; SDR UNORM attachments require explicit encoding.
    if (pc.encodeSRGB != 0)
        color = linearToSRGB(color);
    output.out_FragColor = float4(color, 1.0);

    return output;
}
