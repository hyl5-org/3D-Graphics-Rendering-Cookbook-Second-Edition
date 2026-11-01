#include "neural_texture_decode.slang"

static const float kPi = 3.14159265359;

struct VertexOutput
{
    float4 position : SV_Position;
    float3 worldPosition : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float4 tangent : TEXCOORD2;
    float2 uv : TEXCOORD3;
};

[shader("vertex")] VertexOutput vertexMain(uint vertexId : SV_VertexID)
{
    Vertex vertex = pc.vertices[vertexId];
    VertexOutput output;
    output.position = mul(pc.frame->viewProj, float4(vertex.position, 1.0));
    output.worldPosition = vertex.position;
    output.normal = vertex.normal;
    output.tangent = vertex.tangent;
    output.uv = vertex.uv;
    return output;
}

float linearChannel(float value)
{
    return value <= 0.04045 ? value / 12.92 : pow((value + 0.055) / 1.055, 2.4);
}

float srgbChannel(float value)
{
    return value <= 0.0031308 ? value * 12.92 : 1.055 * pow(value, 1.0 / 2.4) - 0.055;
}

float3 toLinear(float3 color)
{
    return float3(linearChannel(color.x), linearChannel(color.y), linearChannel(color.z));
}

float3 toSRGB(float3 color)
{
    return float3(srgbChannel(color.x), srgbChannel(color.y), srgbChannel(color.z));
}

float3 evaluateLighting(float3 baseColor, float roughness, float metallic, float ao, float3 normal, float3 position)
{
    float3 lightDirection = normalize(float3(0.4, 1.0, 0.3));
    float3 viewDirection = normalize(pc.frame->camera.xyz - position);
    float3 halfVector = normalize(viewDirection + lightDirection);
    float nDotL = max(dot(normal, lightDirection), 0.0);
    float nDotV = max(dot(normal, viewDirection), 0.0001);
    float nDotH = max(dot(normal, halfVector), 0.0);
    float vDotH = max(dot(viewDirection, halfVector), 0.0);

    float alpha = roughness * roughness;
    float alphaSquared = alpha * alpha;
    float denominator = nDotH * nDotH * (alphaSquared - 1.0) + 1.0;
    float distribution = alphaSquared / max(kPi * denominator * denominator, 0.000001);
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    float geometry = nDotL / (nDotL * (1.0 - k) + k) * nDotV / (nDotV * (1.0 - k) + k);
    float3 f0 = lerp(float3(0.04), baseColor, metallic);
    float3 fresnel = f0 + (1.0 - f0) * pow(1.0 - vDotH, 5.0);
    float3 diffuse = (1.0 - fresnel) * (1.0 - metallic) * baseColor / kPi;
    float3 specular = distribution * geometry * fresnel / max(4.0 * nDotL * nDotV, 0.0001);
    return (diffuse + specular) * nDotL * 4.0 + baseColor * 0.12 * ao;
}

[shader("fragment")] float4 fragmentMain(VertexOutput input, bool frontFacing : SV_IsFrontFace) : SV_Target
{
    bool neural = pc.frame->source == 1 || (pc.frame->source == 2 && input.position.x > pc.frame->width * 0.5);
    float channels[kChannelCount];
    if (neural)
    {
        decodeNeural(input.uv, channels);
    }
    else
    {
        sampleReference(input.uv, channels);
    }
    if (channels[3] < pc.cutoff)
    {
        discard;
    }

    float3 baseColor = toLinear(float3(channels[0], channels[1], channels[2]));
    float2 normalXY = float2(channels[4], channels[5]) * 2.0 - 1.0;
    float3 tangentNormal = normalize(float3(normalXY, sqrt(max(1.0 - dot(normalXY, normalXY), 0.0))));
    float3 normal = normalize(input.normal);
    float3 tangent = input.tangent.xyz - normal * dot(input.tangent.xyz, normal);
    tangent = dot(tangent, tangent) > 1e-8
                  ? normalize(tangent)
                  : normalize(cross(normal, abs(normal.x) < 0.9 ? float3(1, 0, 0) : float3(0, 1, 0)));
    normal = normalize(mul(tangentNormal, float3x3(tangent, cross(normal, tangent) * input.tangent.w, normal)));
    if (!frontFacing)
    {
        normal = -normal;
    }

    float roughness = clamp(channels[6], 0.045, 1.0);
    float metallic = channels[7];
    float ao = channels[8];
    float3 color;
    switch (pc.frame->channel)
    {
    case 1:
        color = baseColor;
        break;
    case 2:
        color = tangentNormal * 0.5 + 0.5;
        break;
    case 3:
        color = float3(roughness);
        break;
    case 4:
        color = float3(metallic);
        break;
    case 5:
        color = float3(ao);
        break;
    case 6:
        color = float3(channels[3]);
        break;
    default:
        color = evaluateLighting(baseColor, roughness, metallic, ao, normal, input.worldPosition);
        color = color / (1.0 + color);
        break;
    }
    return float4(pc.frame->manualGamma != 0 ? toSRGB(max(color, 0.0)) : color, 1.0);
}
