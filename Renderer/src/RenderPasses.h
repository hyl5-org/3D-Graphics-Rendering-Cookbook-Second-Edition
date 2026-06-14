#pragma once

#include "RayTracing.h"
#include "SceneResources.h"

#include "scene/Skybox.h"

#include <array>

namespace FinalDemo
{

struct LightData
{
    mat4 viewProjBias;
    vec4 lightDir;
    uint32_t shadowTexture;
    uint32_t shadowSampler;
    uint32_t rtShadowEnabled;
    uint32_t rtAOEnabled;
    uint32_t rtAOSamples;
    float rtAORadius;
    float rtAOPower;
    float rtShadowStrength;
    float rtShadowRadius;
    uint32_t frameIndex;
};

struct MeshPushConstants
{
    mat4 viewProj;
    vec4 cameraPos;
    uint64_t bufferTransforms;
    uint64_t bufferDrawData;
    uint64_t bufferMaterials;
    uint64_t bufferOIT;
    uint64_t bufferLight;
    uint32_t texSkybox;
    uint32_t texSkyboxIrradiance;
};

struct SSAOPushConstants
{
    uint32_t texDepth;
    uint32_t texRotation;
    uint32_t texOut;
    uint32_t sampler;
    float zNear;
    float zFar;
    float radius;
    float attScale;
    float distScale;
};

struct CombineSSAOPushConstants
{
    uint32_t texColor;
    uint32_t texSSAO;
    uint32_t sampler;
    float scale;
    float bias;
};

struct LightingPassPushConstants
{
    mat4 invViewProj;
    uint32_t sceneColor;
    uint32_t gbuffer1;
    uint32_t gbuffer2;
    uint32_t gbuffer3;
    uint32_t depth;
    uint32_t texSkyboxIrradiance;
    uint32_t texRTShadowAO;
    uint32_t sampler;
    uint64_t bufferLight;
};

struct HDRPushConstants
{
    uint32_t texColor;
    uint32_t texLuminance;
    uint32_t texBloom;
    uint32_t sampler;
    int drawMode = ToneMapping_Uchimura;

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

struct RTShadowAOPushConstants
{
    mat4 invViewProj;
    uint32_t gbuffer1;
    uint32_t depth;
    uint32_t smpl;
    uint32_t texBlueNoise;
    uint64_t bufferLight;
};

struct RTShadowAOPass
{
    RTShadowAOPass(const std::unique_ptr<lvk::IContext> &ctx, const FrameTargets &targets, lvk::Format swapchainFormat);
    void execute(const std::unique_ptr<lvk::IContext> &ctx, lvk::ICommandBuffer &buf, const FrameTargets &targets,
                 const ShadowPass &shadows, const RayTracingScene &rt, const mat4 &view, const mat4 &proj,
                 lvk::SamplerHandle samplerClamp);
    lvk::TextureHandle denoise(lvk::ICommandBuffer &buf, const FrameTargets &targets, const mat4 &view,
                               const mat4 &proj, lvk::SamplerHandle samplerClamp);

    lvk::Holder<lvk::ShaderModuleHandle> vert;
    lvk::Holder<lvk::ShaderModuleHandle> frag;
    lvk::Holder<lvk::RenderPipelineHandle> pipeline;
    lvk::Holder<lvk::TextureHandle> rtResult;
    lvk::Holder<lvk::TextureHandle> blueNoise;

    // Denoise
    std::array<lvk::Holder<lvk::TextureHandle>, 2> rtHistory;
    lvk::Holder<lvk::TextureHandle> rtDenoised;
    lvk::Holder<lvk::TextureHandle> prevDepth;

    lvk::Holder<lvk::ShaderModuleHandle> compTemporal;
    lvk::Holder<lvk::ComputePipelineHandle> pipelineTemporal;

    lvk::Holder<lvk::ShaderModuleHandle> compSpatial;
    lvk::Holder<lvk::ComputePipelineHandle> pipelineSpatialX;
    lvk::Holder<lvk::ComputePipelineHandle> pipelineSpatialY;
    lvk::Holder<lvk::ShaderModuleHandle> compAtrous;
    lvk::Holder<lvk::ComputePipelineHandle> pipelineAtrousStep1;
    lvk::Holder<lvk::ComputePipelineHandle> pipelineAtrousStep2;
    mat4 prevViewProj = mat4(1.0f);
    uint32_t historyIndex = 0;
    bool historyValid = false;
    lvk::Dimensions halfRes = {};
};

struct LightingPass
{
    LightingPass(const std::unique_ptr<lvk::IContext> &ctx, const FrameTargets &targets,
                 lvk::SamplerHandle samplerClamp, lvk::Format swapchainFormat);
    lvk::TextureHandle execute(const std::unique_ptr<lvk::IContext> &ctx, lvk::ICommandBuffer &buf,
                               const FrameTargets &targets, const Skybox &skyBox, const ShadowPass &shadows,
                               const mat4 &view, const mat4 &proj, lvk::SamplerHandle samplerClamp,
                               lvk::TextureHandle rtShadowAO);

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

struct SSAOPass
{
    lvk::Holder<lvk::TextureHandle> ssao;
    std::array<lvk::Holder<lvk::TextureHandle>, 2> blur;
    lvk::Holder<lvk::TextureHandle> rotations;
    lvk::Holder<lvk::ShaderModuleHandle> compSSAO;
    lvk::Holder<lvk::ComputePipelineHandle> pipelineSSAO;
    lvk::Holder<lvk::ShaderModuleHandle> compBlur;
    lvk::Holder<lvk::ComputePipelineHandle> pipelineBlurX;
    lvk::Holder<lvk::ComputePipelineHandle> pipelineBlurY;
    lvk::Holder<lvk::ShaderModuleHandle> vertCombine;
    lvk::Holder<lvk::ShaderModuleHandle> fragCombine;
    lvk::Holder<lvk::RenderPipelineHandle> pipelineCombine;
    SSAOPushConstants pc;
    CombineSSAOPushConstants pcCombine;

    SSAOPass(const std::unique_ptr<lvk::IContext> &ctx, const FrameTargets &targets, lvk::SamplerHandle samplerClamp,
             lvk::Format swapchainFormat);
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

    HDRPass(const std::unique_ptr<lvk::IContext> &ctx, const FrameTargets &targets, lvk::SamplerHandle samplerClamp,
            lvk::Format swapchainFormat);

    void execute(lvk::ICommandBuffer &buf, lvk::TextureHandle texColor, float deltaSeconds,
                 lvk::SamplerHandle samplerClamp);
    void runBloom(lvk::ICommandBuffer &buf, lvk::SamplerHandle samplerClamp);
    void runAdaptation(lvk::ICommandBuffer &buf, float deltaSeconds);
    void toneMap(lvk::ICommandBuffer &buf, const lvk::Framebuffer &framebufferMain);
    void swapAdaptedLuminance();
};

void renderGbufferPass(const std::unique_ptr<lvk::IContext> &ctx, VulkanApp &app, lvk::ICommandBuffer &buf,
                       const FrameTargets &targets, const LoadedScene &loadedScene, const Skybox &skyBox,
                       const VKMesh11 &mesh, const RenderPipelines &pipelines, SceneDrawLists &drawLists,
                       const OITPass &oit, const ShadowPass &shadows, LineCanvas3D &canvas3d, const mat4 &view,
                       const mat4 &proj, const LightFrame &lightFrame);

void renderSkyboxPass(lvk::ICommandBuffer &buf, const FrameTargets &targets, const Skybox &skyBox, const mat4 &view,
                      const mat4 &proj);

void renderTransparentPass(const std::unique_ptr<lvk::IContext> &ctx, VulkanApp &app, lvk::ICommandBuffer &buf,
                           const FrameTargets &targets, const Skybox &skyBox, const VKMesh11 &mesh,
                           const RenderPipelines &pipelines, SceneDrawLists &drawLists, const OITPass &oit,
                           const ShadowPass &shadows, const mat4 &view, const mat4 &proj);
} // namespace FinalDemo
