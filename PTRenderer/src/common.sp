//

#include <data/shaders/gltf/common_material.sp>

struct DrawData {
  uint transformId;
  uint materialId;
};

layout(std430, buffer_reference) readonly buffer TransformBuffer {
  mat4 model[];
};

layout(std430, buffer_reference) readonly buffer DrawDataBuffer {
  DrawData dd[];
};

layout(std430, buffer_reference) readonly buffer MaterialBuffer {
  MetallicRoughnessDataGPU material[];
};

layout(std430, buffer_reference) buffer AtomicCounter {
  uint numFragments;
};

layout(std430, buffer_reference) readonly buffer LightBuffer {
  mat4 viewProjBias;
  vec4 lightDir;
  uint frameIndex;
};


struct PunctualLight
{
    vec3 color;
    float intensity;
    vec3 position;
    float radius;
};

layout(std430, buffer_reference) readonly buffer PunctualLightBuffer {
  uint count;
  uint padding0;
  uint padding1;
  uint padding2;
  PunctualLight lights[];
};

#ifndef PTR_PRIMARY_RAY_COMPUTE
layout(push_constant) uniform PerFrameData {
  mat4 viewProj;
  vec4 cameraPos;
  TransformBuffer transforms;
  DrawDataBuffer drawData;
  MaterialBuffer materials;
  LightBuffer light; // one directional light
  uint texSkybox;
  uint texSkyboxIrradiance;
} pc;
#endif

const float kPi = 3.14159265358979323846;

float rand(uvec3 seed)
{
  uint value = seed.x * 1973u + seed.y * 9277u + seed.z * 26699u + 0x68bc21ebu;
  value = (value ^ 61u) ^ (value >> 16u);
  value *= 9u;
  value ^= value >> 4u;
  value *= 0x27d4eb2du;
  value ^= value >> 15u;
  return float(value >> 8u) * (1.0 / 16777216.0);
}

vec3 decodeOctahedron(vec2 encoded)
{
    vec2 f = encoded * 2.0 - 1.0;
    vec3 n = vec3(f, 1.0 - abs(f.x) - abs(f.y));
    if (n.z < 0.0)
    {
        vec2 signNotZero = vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
        n.xy = (1.0 - abs(n.yx)) * signNotZero;
    }
    return normalize(n);
}

vec3 clipToWorld(mat4 transform, vec2 uv, float depth)
{
    vec2 ndc = uv * vec2(2.0, -2.0) + vec2(-1.0, 1.0);
    vec4 world = transform * vec4(ndc, depth, 1.0);
    return world.xyz / world.w;
}

vec3 fresnelSchlick(float vDotH, vec3 f0)
{
    float factor = pow(1.0 - clamp(vDotH, 0.0, 1.0), 5.0);
    return f0 + (1.0 - f0) * factor;
}

float distributionGGX(float nDotH, float roughness)
{
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float denominator = nDotH * nDotH * (alpha2 - 1.0) + 1.0;
    return alpha2 / max(kPi * denominator * denominator, 1e-7);
}

float geometrySmith(float nDotV, float nDotL, float roughness)
{
    float r = roughness + 1.0;
    float k = r * r * 0.125;
    float gV = nDotV / mix(nDotV, 1.0, k);
    float gL = nDotL / mix(nDotL, 1.0, k);
    return gV * gL;
}

vec3 evaluateBRDF(vec3 n, vec3 v, vec3 l, vec3 baseColor, float metallic, float roughness, vec3 f0)
{
    vec3 h = normalize(v + l);
    float nDotV = max(dot(n, v), 0.0);
    float nDotL = max(dot(n, l), 0.0);
    float nDotH = max(dot(n, h), 0.0);
    float vDotH = max(dot(v, h), 0.0);

    vec3 f = fresnelSchlick(vDotH, f0);
    float d = distributionGGX(nDotH, roughness);
    float g = geometrySmith(nDotV, nDotL, roughness);
    vec3 specular = d * g * f / max(4.0 * nDotV * nDotL, 1e-6);
    vec3 diffuse = (1.0 - metallic) * (1.0 - f) * baseColor / kPi;
    return diffuse + specular;
}
