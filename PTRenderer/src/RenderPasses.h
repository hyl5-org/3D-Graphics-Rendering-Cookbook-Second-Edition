#pragma once

#include "SceneResources.h"

#include "scene/Skybox.h"

#include <array>
#include <cstddef>

namespace FinalDemo
{

struct LightData
{
    mat4 viewProjBias;
    vec4 lightDir;
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
    //uint64_t bufferLight;
    uint32_t texSkybox;
    uint32_t texSkyboxIrradiance;
};

struct PrimaryRayPassPushConstants
{
    mat4 clipToWorld;
    vec4 lightDir;
    vec4 cameraPos;
    uint32_t sceneColor;
    uint32_t lightingColor;
    uint32_t gbuffer1;
    uint32_t gbuffer2;
    uint32_t gbuffer3;
    uint32_t depth;
    uint32_t sampler;
    uint32_t frameIndex;
    uint32_t texBlueNoise;
    uint64_t punctualLightBuffer;
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

    RenderPipelines(const std::unique_ptr<lvk::IContext> &ctx, const MeshData &meshData, lvk::Format depthFormat);
};

struct PunctualLightBufferStruct
{
    glm::vec3 color;
    float intensity;
    glm::vec3 position;
    float radius;
};

struct alignas(16) PunctualLightBufferHeader
{
    uint32_t count;
    uint32_t padding[3] = {};
};

static_assert(sizeof(PunctualLightBufferStruct) == 32);
static_assert(offsetof(PunctualLightBufferStruct, intensity) == 12);
static_assert(offsetof(PunctualLightBufferStruct, position) == 16);
static_assert(offsetof(PunctualLightBufferStruct, radius) == 28);
static_assert(sizeof(PunctualLightBufferHeader) == 16);

struct PrimaryRayPass
{
    PrimaryRayPass(const std::unique_ptr<lvk::IContext> &ctx, const BoundingBox &worldBounds);
    void execute(lvk::ICommandBuffer &buf, const FrameTargets &targets, const mat4 &view, const mat4 &proj,
                 const LightFrame &lightFrame, lvk::SamplerHandle samplerClamp);

    static constexpr uint32_t kPunctualLightCount = 64u;
    std::vector<PunctualLightBufferStruct> lights;
    lvk::Holder<lvk::BufferHandle> punctualLightBuffer;
    lvk::Holder<lvk::TextureHandle> blueNoise;

    lvk::Holder<lvk::ShaderModuleHandle> shader;
    lvk::Holder<lvk::ComputePipelineHandle> pipeline;
    PrimaryRayPassPushConstants pc;
    uint32_t frameIndex = 0;
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
                        LineCanvas3D &canvas3d, const mat4 &view,
                       const mat4 &proj, const LightFrame &lightFrame);

void renderSkyboxPass(lvk::ICommandBuffer &buf, const FrameTargets &targets, const Skybox &skyBox, const mat4 &view,
                      const mat4 &proj);

} // namespace FinalDemo
