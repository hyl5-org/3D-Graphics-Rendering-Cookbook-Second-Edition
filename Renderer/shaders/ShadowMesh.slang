#pragma once

#include "Material.slang"

struct DrawData
{
    uint transformId;
    uint materialId;
};

typealias TransformBuffer = float4x4 *;

typealias DrawDataBuffer = DrawData *;

typealias MaterialBuffer = MetallicRoughnessDataGPU *;

struct LightBufferData
{
    float4x4 viewProjBias;
    float4 lightDir;
    uint shadowTexture;
    uint shadowSampler;
};
typealias LightBuffer = LightBufferData *;

struct PerFrameData
{
    float4x4 viewProj;
    TransformBuffer transforms;
    DrawDataBuffer drawData;
    MaterialBuffer materials;

    uint texSkyboxIrradiance;
};
[[vk::push_constant]] ConstantBuffer<PerFrameData> pc;
