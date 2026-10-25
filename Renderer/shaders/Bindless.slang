#pragma once

[[vk::binding(0, 0)]] Texture2D<float4> kTextures2D[];
[[vk::binding(0, 0)]] TextureCube<float4> kTexturesCube[];
[[vk::binding(1, 0)]] SamplerState kSamplers[];
[[vk::binding(1, 0)]] SamplerComparisonState kShadowSamplers[];

float4 textureBindless2DLod(uint textureId, uint samplerId, float2 uv, float lod)
{
    return kTextures2D[NonUniformResourceIndex(textureId)].SampleLevel(
        kSamplers[NonUniformResourceIndex(samplerId)], uv, lod);
}

float4 textureBindlessCubeLod(uint textureId, uint samplerId, float3 direction, float lod)
{
    return kTexturesCube[NonUniformResourceIndex(textureId)].SampleLevel(
        kSamplers[NonUniformResourceIndex(samplerId)], direction, lod);
}

float textureBindlessCubeMaxLod(uint textureId)
{
    uint width, height, levels;
    kTexturesCube[NonUniformResourceIndex(textureId)].GetDimensions(0, width, height, levels);
    return float(levels - 1);
}

float4 textureBindless2D(uint textureId, uint samplerId, float2 uv)
{
#ifdef RENDERER_COMPUTE
    return kTextures2D[NonUniformResourceIndex(textureId)].SampleLevel(kSamplers[NonUniformResourceIndex(samplerId)],
                                                                       uv, 0);
#else
    return kTextures2D[NonUniformResourceIndex(textureId)].Sample(kSamplers[NonUniformResourceIndex(samplerId)], uv);
#endif
}

float4 textureBindlessCube(uint textureId, uint samplerId, float3 direction)
{
    return kTexturesCube[NonUniformResourceIndex(textureId)].Sample(kSamplers[NonUniformResourceIndex(samplerId)],
                                                                    direction);
}

int2 textureBindlessSize2D(uint textureId)
{
    uint width, height;
    kTextures2D[NonUniformResourceIndex(textureId)].GetDimensions(width, height);
    return int2(width, height);
}

float textureBindless2DShadow(uint textureId, uint samplerId, float3 uvw)
{
    return kTextures2D[NonUniformResourceIndex(textureId)].SampleCmpLevelZero(
        kShadowSamplers[NonUniformResourceIndex(samplerId)], uvw.xy, uvw.z);
}
