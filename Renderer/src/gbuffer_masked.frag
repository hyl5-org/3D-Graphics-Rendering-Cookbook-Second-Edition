//

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require

#include <Renderer/src/common.sp>
#include <data/shaders/AlphaTest.sp>
#include <data/shaders/UtilsPBR.sp>

layout(location = 0) in f16vec2 uv;
layout(location = 1) in f16vec3 normal;
layout(location = 2) in vec3 worldPos;
layout(location = 3) in flat uint materialId;

layout(location = 0) out vec4 outSceneColor;
layout(location = 1) out vec4 outGBuffer1;
layout(location = 2) out vec4 outGBuffer2;
layout(location = 3) out vec4 outGBuffer3;

vec2 signNotZero(vec2 v)
{
    return vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
}

vec2 encodeOctahedron(vec3 n)
{
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    vec2 p = n.xy;
    if (n.z < 0.0)
    {
        p = (1.0 - abs(p.yx)) * signNotZero(p);
    }
    return p * 0.5 + 0.5;
}

void main()
{
    // FIXME(luhanyang)
    // Adreno 840 currently fails to link this GBuffer MRT pipeline when the
    // fragment shader reads MaterialBuffer through buffer_reference here.
    MetallicRoughnessDataGPU mat = pc.materials.material[materialId];

    vec4 baseColor = mat.baseColorFactor;
    if (mat.baseColorTexture > 0)
    {
        baseColor *= textureBindless2D(mat.baseColorTexture, mat.baseColorTextureSampler, vec2(uv));
    }

    runAlphaTest(baseColor.a, mat.emissiveFactorAlphaCutoff.w / max(32.0 * fwidth(float(uv.x)), 1.0));

    vec3 n = normalize(vec3(normal));
    if (mat.normalTexture > 0)
    {
        vec3 normalSample = textureBindless2D(mat.normalTexture, mat.normalTextureSampler, uv).xyz;
        if (length(normalSample) > 0.5)
        {
            n = perturbNormal(n, worldPos, normalSample, vec2(uv));
        }
    }

    vec3 emissive = vec3(0.0);
    if (mat.emissiveTexture > 0)
    {
        emissive = mat.emissiveFactorAlphaCutoff.rgb *
                   textureBindless2D(mat.emissiveTexture, mat.emissiveTextureSampler, vec2(uv)).rgb;
    }

    const float specular = 0.5;
    const float metallic = 0.0;
    const float roughness = 1.0;
    const float ao = 1.0;
    const float shadingModelId = 0.0;

    outSceneColor = vec4(emissive, 1.0);
    outGBuffer1 = vec4(encodeOctahedron(n), 0.0, 0.0);
    outGBuffer2 = vec4(metallic, specular, roughness, shadingModelId);
    outGBuffer3 = vec4(baseColor.rgb, ao);
}
