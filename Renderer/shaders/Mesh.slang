#pragma once

#include "Material.slang"
#include "OIT.slang"

struct DrawData
{
    uint transformId;
    uint materialId;
};

typealias TransformBuffer = float4x4 *;

typealias DrawDataBuffer = DrawData *;

typealias MaterialBuffer = MetallicRoughnessDataGPU *;

struct AtomicCounterData
{
    uint numFragments;
};
typealias AtomicCounter = AtomicCounterData *;

struct LightBufferData
{
    float4x4 viewProjBias;
    float4 lightDir;
    uint shadowTexture;
    uint shadowSampler;
    uint rtShadowEnabled;
    uint rtAOEnabled;
    uint rtAOSamples;
    float rtAORadius;
    float rtAOPower;
    float rtShadowStrength;
    float rtShadowRadius;
    uint frameIndex;
    float environmentIntensity;
};
typealias LightBuffer = LightBufferData *;

struct OITData
{
    AtomicCounter atomicCounter;
    TransparencyListsBuffer oitLists;
    uint texHeadsOIT;
    uint maxOITFragments;
};
typealias OIT = OITData *;

struct PerFrameData
{
    float4x4 viewProj;
    float4 cameraPos;
    TransformBuffer transforms;
    DrawDataBuffer drawData;
    MaterialBuffer materials;
    OIT oit;
    LightBuffer light;
    uint texSkybox;
    uint texSkyboxIrradiance;
};
[[vk::push_constant]] ConstantBuffer<PerFrameData> pc;
