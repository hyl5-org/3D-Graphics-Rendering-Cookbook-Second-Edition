#pragma once

#include "SceneResources.h"

#include "scene/Skybox.h"

#include <array>

class VulkanApp;

namespace FinalDemo
{

struct LightData
{
    mat4 viewProjBias;
    vec4 lightDir;
    vec4 lightColorIntensity;
    uint32_t shadowTexture;
    uint32_t shadowSampler;
    uint32_t frameIndex;
    float iblIntensity;
};

struct MeshPushConstants
{
    mat4 viewProjLt;
    mat4 viewProjRt;
    vec4 cameraPos[2];
    uint64_t bufferTransforms;
    uint64_t bufferDrawData;
    uint64_t bufferMaterials;
    uint64_t bufferOITAtomicCounter;
    uint64_t bufferOITLists;
    uint32_t texHeadsOIT;
    uint32_t maxOITFragments;
    uint64_t bufferLight;
    uint32_t texSkybox;
    uint32_t texSkyboxIrradiance;
};

struct LightingPassPushConstants
{
    mat4 invViewProj[2];
    uint32_t sceneColor;
    uint32_t gbuffer1;
    uint32_t gbuffer2;
    uint32_t gbuffer3;
    uint32_t depth;
    uint32_t texSkyboxIrradiance;
    uint32_t sampler;
    uint64_t bufferLight;
};

struct HDRPushConstants
{
    uint32_t texColor;
    uint32_t texLuminance;
    uint32_t texBloom;
    uint32_t sampler;
    int drawMode = ToneMapping_None;

    float exposure = 0.95f;
    float bloomStrength = 0.0f;

    float maxWhite = 1.0f;

    float P = 1.0f;
    float a = 1.05f;
    float m = 0.1f;
    float l = 0.8f;
    float c = 3.0f;
    float b = 0.0f;

    float startCompression = 0.8f;
    float desaturation = 0.15f;
};

struct BlurPass
{
    lvk::TextureHandle texIn;
    lvk::TextureHandle texOut;
};

struct RenderPipelines
{
    VkPipelineDeferred opaque;
    VkPipelineDeferred masked;
    VKPipeline transparent;
    VKPipeline shadow;

    RenderPipelines(const std::unique_ptr<lvk::IContext> &ctx, const MeshData &meshData, lvk::Format depthFormat,
                    lvk::Format shadowMapFormat);
};

 struct ShadowPass
 {
     lvk::Holder<lvk::TextureHandle> map;
     lvk::Holder<lvk::SamplerHandle> sampler;
     lvk::Holder<lvk::BufferHandle> lightBuffer;
     LightParams previousLight = {.depthBiasConst = 0.0f};
     uint32_t frameIndex = 0;

     explicit ShadowPass(const std::unique_ptr<lvk::IContext> &ctx);

     void updateIfNeeded(lvk::ICommandBuffer &buf, const VKMesh11 &mesh, const RenderPipelines &pipelines,
                         const LightFrame &lightFrame);
 };

struct LightingPass
{
    LightingPass(const std::unique_ptr<lvk::IContext> &ctx, const FrameTargets &targets,
                 lvk::SamplerHandle samplerClamp, lvk::Format swapchainFormat);
    lvk::TextureHandle execute(const std::unique_ptr<lvk::IContext> &ctx, lvk::ICommandBuffer &buf,
                               const FrameTargets &targets, const Skybox &skyBox, const ShadowPass &shadows,
                               lvk::SamplerHandle samplerClamp);

    lvk::Holder<lvk::ShaderModuleHandle> vert;
    lvk::Holder<lvk::ShaderModuleHandle> frag;
    lvk::Holder<lvk::RenderPipelineHandle> pipeline;
    LightingPassPushConstants pc;
};

struct OITPass
{
    lvk::Holder<lvk::ShaderModuleHandle> vert;
    lvk::Holder<lvk::ShaderModuleHandle> frag;
    lvk::Holder<lvk::RenderPipelineHandle> pipeline;
    lvk::Holder<lvk::BufferHandle> atomicCounter;
    lvk::Holder<lvk::BufferHandle> fragmentLists;
    lvk::Holder<lvk::TextureHandle> heads;
    lvk::Holder<lvk::BufferHandle> passBuffer;
    uint32_t maxFragments = 0;

    struct TransparentFragment
    {
        uint64_t rgba;
        float depth;
        uint32_t next;
    };

    OITPass(const std::unique_ptr<lvk::IContext> &ctx, const lvk::Dimensions &sizeFb);

    void clear(lvk::ICommandBuffer &buf);
    lvk::TextureHandle combine(const std::unique_ptr<lvk::IContext> &ctx, lvk::ICommandBuffer &buf,
                               const FrameTargets &targets, lvk::TextureHandle texColor);
};

struct HDRPass
{
    lvk::Holder<lvk::TextureHandle> brightPass;
    lvk::Holder<lvk::TextureHandle> bloomPass;
    std::array<lvk::Holder<lvk::TextureHandle>, 2> bloom;
    std::array<lvk::Holder<lvk::TextureHandle>, 10> luminanceViews;
    std::array<lvk::Holder<lvk::TextureHandle>, 2> adaptedLuminance;
    lvk::Holder<lvk::ShaderModuleHandle> compBrightPass;
    lvk::Holder<lvk::ComputePipelineHandle> pipelineBrightPass;
    lvk::Holder<lvk::ShaderModuleHandle> compAdaptationPass;
    lvk::Holder<lvk::ComputePipelineHandle> pipelineAdaptationPass;
    lvk::Holder<lvk::ShaderModuleHandle> compBloomPass;
    lvk::Holder<lvk::ComputePipelineHandle> pipelineBloomX;
    lvk::Holder<lvk::ComputePipelineHandle> pipelineBloomY;
    lvk::Holder<lvk::ShaderModuleHandle> vertToneMap;
    lvk::Holder<lvk::ShaderModuleHandle> fragToneMap;
    lvk::Holder<lvk::RenderPipelineHandle> pipelineToneMap;
    HDRPushConstants pc;
    bool adaptedLuminanceInitialized = false;

    HDRPass(const std::unique_ptr<lvk::IContext> &ctx, const FrameTargets &targets, lvk::SamplerHandle samplerClamp,
            lvk::Format swapchainFormat);

    void execute(lvk::ICommandBuffer &buf, lvk::TextureHandle texColor, float deltaSeconds,
                lvk::SamplerHandle samplerClamp);
    void runBloom(lvk::ICommandBuffer &buf, lvk::SamplerHandle samplerClamp);
    void runAdaptation(lvk::ICommandBuffer &buf, float deltaSeconds);
    void toneMap(lvk::ICommandBuffer &buf, const lvk::Framebuffer &framebufferMain, lvk::TextureHandle texColor);
    void swapAdaptedLuminance();
};

void renderGbufferPass(const std::unique_ptr<lvk::IContext> &ctx, lvk::ICommandBuffer &buf,
                       const FrameTargets &targets, const LoadedScene &loadedScene, const Skybox &skyBox,
                       const VKMesh11 &mesh, const RenderPipelines &pipelines, SceneDrawLists &drawLists,
                       const ShadowPass &shadows, LineCanvas3D &canvas3d, const LightFrame &lightFrame);

void renderSkyboxPass(lvk::ICommandBuffer &buf, const FrameTargets &targets, const Skybox &skyBox);

void renderTransparentPass(const std::unique_ptr<lvk::IContext> &ctx, lvk::ICommandBuffer &buf,
                           const FrameTargets &targets, const Skybox &skyBox, const VKMesh11 &mesh,
                           const RenderPipelines &pipelines, SceneDrawLists &drawLists,
                           const OITPass &oit, const ShadowPass &shadows);
} // namespace FinalDemo
