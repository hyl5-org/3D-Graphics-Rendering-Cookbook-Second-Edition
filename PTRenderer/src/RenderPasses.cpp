#include "RenderPasses.h"

#include <algorithm>

namespace FinalDemo
{

RenderPipelines::RenderPipelines(const std::unique_ptr<lvk::IContext> &ctx, const MeshData &meshData,
                                 lvk::Format depthFormat)
    : opaque(ctx, meshData.streams, kOffscreenFormat, depthFormat, kNumSamples,
             loadShaderModule(ctx, "Renderer/shaders/gbuffer_opaque.vert"),
             loadShaderModule(ctx, "Renderer/shaders/gbuffer_opaque.frag")),
      masked(ctx, meshData.streams, kOffscreenFormat, depthFormat, kNumSamples,
             loadShaderModule(ctx, "Renderer/shaders/gbuffer_opaque.vert"),
             loadShaderModule(ctx, "Renderer/shaders/gbuffer_masked.frag"))
{
}

// RTShadowAOPass::RTShadowAOPass(const std::unique_ptr<lvk::IContext> &ctx, const FrameTargets &targets,
//                                lvk::Format swapchainFormat)
// {
//     uint32_t rtDownsampleScale = 2;
//     vert = loadShaderModule(ctx, "data/shaders/QuadFlip.vert");
//     frag = loadShaderModule(ctx, "Renderer/shaders/rt_shadow_ao.frag");
//     pipeline = ctx->createRenderPipeline({
//         .smVert = vert,
//         .smFrag = frag,
//         .color = {{.format = lvk::Format_RG_UN8}},
//         .debugName = "Pipeline: RT Shadow/AO",
//     });
//     const lvk::Dimensions Res = {.width = targets.sizeFb.width / rtDownsampleScale,
//                                  .height = targets.sizeFb.height / rtDownsampleScale};
//     rtResult = ctx->createTexture({
//         .format = lvk::Format_RG_UN8,
//         .dimensions = Res,
//         .usage = lvk::TextureUsageBits_Attachment | lvk::TextureUsageBits_Sampled | lvk::TextureUsageBits_Storage,
//         .debugName = "RT Shadow/AO",
//     });
//     blueNoise = loadTexture(ctx, "data/blue_noise_128x128x64.png");

//     // Denoise textures
//     halfRes = Res;
//     const lvk::TextureDesc historyDesc = {
//         .format = lvk::Format_RG_UN8,
//         .dimensions = Res,
//         .usage = lvk::TextureUsageBits_Sampled | lvk::TextureUsageBits_Storage,
//         .debugName = "RT History",
//     };
//     rtHistory[0] = ctx->createTexture(historyDesc, "RT History 0");
//     rtHistory[1] = ctx->createTexture(historyDesc, "RT History 1");
//     rtDenoised = ctx->createTexture({
//         .format = lvk::Format_RG_UN8,
//         .dimensions = Res,
//         .usage = lvk::TextureUsageBits_Sampled | lvk::TextureUsageBits_Storage,
//         .debugName = "RT Denoised",
//     });
//     prevDepth = ctx->createTexture({
//         .format = lvk::Format::Format_Z_F32,
//         .dimensions = targets.sizeFb,
//         .usage = lvk::TextureUsageBits_Sampled | lvk::TextureUsageBits_Attachment,
//         .debugName = "RT Prev Depth",
//     });

//     // Denoise pipelines
//     compTemporal = loadShaderModule(ctx, "Renderer/shaders/rt_denoise_temporal.comp");
//     pipelineTemporal =
//         ctx->createComputePipeline({.smComp = compTemporal, .debugName = "Pipeline: RT Denoise Temporal"});

//     compSpatial = loadShaderModule(ctx, "Renderer/shaders/rt_denoise_spatial.comp");
//     pipelineSpatialX = ctx->createComputePipeline({
//         .smComp = compSpatial,
//         .specInfo = {.entries = {{.constantId = 0, .size = sizeof(uint32_t)}},
//                      .data = &kHorizontal,
//                      .dataSize = sizeof(uint32_t)},
//         .debugName = "Pipeline: RT Denoise Spatial X",
//     });
//     pipelineSpatialY = ctx->createComputePipeline({
//         .smComp = compSpatial,
//         .specInfo = {.entries = {{.constantId = 0, .size = sizeof(uint32_t)}},
//                      .data = &kVertical,
//                      .dataSize = sizeof(uint32_t)},
//         .debugName = "Pipeline: RT Denoise Spatial Y",
//     });

//     compAtrous = loadShaderModule(ctx, "Renderer/shaders/rt_denoise_atrous.comp");
//     pipelineAtrousStep1 = ctx->createComputePipeline({
//         .smComp = compAtrous,
//         .specInfo = {.entries = {{.constantId = 0, .size = sizeof(uint32_t)}},
//                      .data = &kAtrousStep1,
//                      .dataSize = sizeof(uint32_t)},
//         .debugName = "Pipeline: RT Denoise A-trous Step 1",
//     });
//     pipelineAtrousStep2 = ctx->createComputePipeline({
//         .smComp = compAtrous,
//         .specInfo = {.entries = {{.constantId = 0, .size = sizeof(uint32_t)}},
//                      .data = &kAtrousStep2,
//                      .dataSize = sizeof(uint32_t)},
//         .debugName = "Pipeline: RT Denoise A-trous Step 2",
//     });
// }

// void RTShadowAOPass::execute(const std::unique_ptr<lvk::IContext> &ctx, lvk::ICommandBuffer &buf,
//                              const FrameTargets &targets, const ShadowPass &shadows, const RayTracingScene &rt,
//                              const mat4 &view, const mat4 &proj, lvk::SamplerHandle samplerClamp)
// {
//     if (!rt.valid())
//     {
//         return;
//     }

//     buf.cmdPushDebugGroupLabel("RT Shadow/AO", 0xff4040ff);
//     const lvk::Framebuffer fb = {
//         .color = {{.texture = rtResult}},
//     };
//     buf.cmdBeginRendering({.color = {{.loadOp = lvk::LoadOp_DontCare, .storeOp = lvk::StoreOp_Store}}}, fb,
//                           {.textures =
//                                {
//                                    lvk::TextureHandle(targets.gbufferRT1),
//                                    lvk::TextureHandle(targets.opaqueDepth),
//                                    lvk::TextureHandle(blueNoise),
//                                },
//                            .buffers = {lvk::BufferHandle(shadows.lightBuffer)}});

//     const RTShadowAOPushConstants pc = {
//         .invViewProj = glm::inverse(proj * view),
//         .gbuffer1 = targets.gbufferRT1.index(),
//         .depth = targets.opaqueDepth.index(),
//         .smpl = samplerClamp.index(),
//         .texBlueNoise = blueNoise.index(),
//         .bufferLight = ctx->gpuAddress(shadows.lightBuffer),
//     };
//     buf.cmdBindRenderPipeline(pipeline);
//     buf.cmdPushConstants(pc);
//     buf.cmdBindDepthState({});
//     buf.cmdDraw(3);
//     buf.cmdEndRendering();
//     buf.cmdPopDebugGroupLabel();
// }


PrimaryRayPass::PrimaryRayPass(const std::unique_ptr<lvk::IContext> &ctx, const FrameTargets &targets,
                           lvk::SamplerHandle samplerClamp, lvk::Format swapchainFormat)
{
    //vert = loadShaderModule(ctx, "data/shaders/QuadFlip.vert");
    //frag = loadShaderModule(ctx, "Renderer/shaders/lighting.frag");
    shader = loadShaderModule(ctx, "PTRenderer/shaders/primary.comp");
    pipeline = ctx->createComputePipeline({
        .smComp = shader,
        .debugName = "Pipeline: Primary",
    });
}
void PrimaryRayPass::execute(const std::unique_ptr<lvk::IContext> &ctx, lvk::ICommandBuffer &buf,
                                           const FrameTargets &targets, const RayTracingScene &rt, const Skybox &skyBox,
                                           const mat4 &view, const mat4 &proj, lvk::SamplerHandle samplerClamp)
{
    //const lvk::Framebuffer framebufferMain = {
    //    .color = {{.texture = targets.lightingColor}},
    //};
    buf.cmdPushDebugGroupLabel("Lighting", 0xffff00ff);
    pc.invViewProj = glm::inverse(proj * view);
    pc.sceneColor = targets.sceneColor.index();
    pc.gbuffer1 = targets.gbufferRT1.index();
    pc.gbuffer2 = targets.gbufferRT2.index();
    pc.gbuffer3 = targets.gbufferRT3.index();
    pc.gbuffer4 = targets.gbufferRT4.index();
    pc.depth = targets.opaqueDepth.index();
    pc.texSkyboxIrradiance = skyBox.texSkyboxIrradiance.index();
    pc.sampler = samplerClamp.index();
    pc.bufferLight = ctx->gpuAddress(lightBuffer);
    buf.cmdBindComputePipeline(pipeline);
    buf.cmdPushConstants(pc);
    // buf.cmdBindDepthState({});
    buf.cmdDispatchThreadGroups(targets.sizeFb.divide2D(16), {.textures =
                                                                  {
                                                                      lvk::TextureHandle(targets.sceneColor),
                                                                      lvk::TextureHandle(targets.gbufferRT1),
                                                                      lvk::TextureHandle(targets.gbufferRT2),
                                                                      lvk::TextureHandle(targets.gbufferRT3),
                                                                      lvk::TextureHandle(targets.gbufferRT4),
                                                                      lvk::TextureHandle(targets.opaqueDepth),
                                                                      lvk::TextureHandle(skyBox.texSkyboxIrradiance),
                                                                  },
                                                              .buffers = {lvk::BufferHandle(lightBuffer)}});
    buf.cmdPopDebugGroupLabel();
}

HDRPass::HDRPass(const std::unique_ptr<lvk::IContext> &ctx, const FrameTargets &targets,
                 lvk::SamplerHandle samplerClamp, lvk::Format swapchainFormat)
{
    brightPass = ctx->createTexture({
        .format = kHDRBloomFormat,
        .dimensions = kBloomSize,
        .usage = lvk::TextureUsageBits_Sampled | lvk::TextureUsageBits_Storage,
        .debugName = "texBrightPass",
    });
    bloomPass = ctx->createTexture({
        .format = kHDRBloomFormat,
        .dimensions = kBloomSize,
        .usage = lvk::TextureUsageBits_Sampled | lvk::TextureUsageBits_Storage,
        .debugName = "texBloomPass",
    });
    bloom[0] = ctx->createTexture({
        .format = kHDRBloomFormat,
        .dimensions = kBloomSize,
        .usage = lvk::TextureUsageBits_Sampled | lvk::TextureUsageBits_Storage,
        .debugName = "texBloom0",
    });
    bloom[1] = ctx->createTexture({
        .format = kHDRBloomFormat,
        .dimensions = kBloomSize,
        .usage = lvk::TextureUsageBits_Sampled | lvk::TextureUsageBits_Storage,
        .debugName = "texBloom1",
    });

    const lvk::ComponentMapping swizzle = {
        .r = lvk::Swizzle_R, .g = lvk::Swizzle_R, .b = lvk::Swizzle_R, .a = lvk::Swizzle_1};
    luminanceViews[0] = ctx->createTexture({
        .format = lvk::Format_R_F16,
        .dimensions = kBloomSize,
        .usage = lvk::TextureUsageBits_Sampled | lvk::TextureUsageBits_Storage,
        .numMipLevels = lvk::calcNumMipLevels(kBloomSize.width, kBloomSize.height),
        .components = swizzle,
        .debugName = "texLuminance",
    });
    for (uint32_t v = 1; v != luminanceViews.size(); v++)
    {
        luminanceViews[v] =
            ctx->createTextureView(luminanceViews[0], {.mipLevel = v, .components = swizzle}, "texLumViews[]");
    }

    const uint16_t brightPixel = glm::packHalf1x16(50.0f);
    const lvk::TextureDesc luminanceTextureDesc{
        .format = lvk::Format_R_F16,
        .dimensions = {1, 1},
        .usage = lvk::TextureUsageBits_Sampled | lvk::TextureUsageBits_Storage,
        .components = swizzle,
        .data = &brightPixel,
    };
    adaptedLuminance[0] = ctx->createTexture(luminanceTextureDesc, "texAdaptedLuminance0");
    adaptedLuminance[1] = ctx->createTexture(luminanceTextureDesc, "texAdaptedLuminance1");

    compBrightPass = loadShaderModule(ctx, "Renderer/shaders/BrightPass.comp");
    pipelineBrightPass =
        ctx->createComputePipeline({.smComp = compBrightPass, .debugName = "Pipeline: HDR BrightPass"});
    compAdaptationPass = loadShaderModule(ctx, "Renderer/shaders/Adaptation.comp");
    pipelineAdaptationPass =
        ctx->createComputePipeline({.smComp = compAdaptationPass, .debugName = "Pipeline: HDR Adaptation"});
    compBloomPass = loadShaderModule(ctx, "Renderer/shaders/Bloom.comp");
    pipelineBloomX = ctx->createComputePipeline({
        .smComp = compBloomPass,
        .specInfo = {.entries = {{.constantId = 0, .size = sizeof(uint32_t)}},
                     .data = &kHorizontal,
                     .dataSize = sizeof(uint32_t)},
        .debugName = "Pipeline: HDR Bloom X",
    });
    pipelineBloomY = ctx->createComputePipeline({
        .smComp = compBloomPass,
        .specInfo = {.entries = {{.constantId = 0, .size = sizeof(uint32_t)}},
                     .data = &kVertical,
                     .dataSize = sizeof(uint32_t)},
        .debugName = "Pipeline: HDR Bloom Y",
    });
    vertToneMap = loadShaderModule(ctx, "data/shaders/QuadFlip.vert");
    fragToneMap = loadShaderModule(ctx, "Renderer/shaders/ToneMap.frag");
    pipelineToneMap = ctx->createRenderPipeline({
        .smVert = vertToneMap,
        .smFrag = fragToneMap,
        .color = {{.format = swapchainFormat}},
        .debugName = "Pipeline: ToneMap",
    });

    pc = {
        .texColor = targets.sceneColor.index(),
        .texLuminance = adaptedLuminance[0].index(),
        .texBloom = bloomPass.index(),
        .sampler = samplerClamp.index(),
    };
}

void HDRPass::execute(lvk::ICommandBuffer &buf, lvk::TextureHandle texColor, float deltaSeconds,
                      lvk::SamplerHandle samplerClamp)
{
    buf.cmdPushDebugGroupLabel("HDR", 0xffffff00);
    pc.texColor = texColor.index();
    const struct BrightPassPC
    {
        uint32_t texColor;
        uint32_t texOut;
        uint32_t texLuminance;
        uint32_t sampler;
        float exposure;
        uint32_t enableBloom;
    } pcBright = {
        .texColor = texColor.index(),
        .texOut = brightPass.index(),
        .texLuminance = luminanceViews[0].index(),
        .sampler = samplerClamp.index(),
        .exposure = pc.exposure,
        .enableBloom = gSettings.hdr.enableBloom ? 1u : 0u,
    };
    buf.cmdPushDebugGroupLabel("HDR BrightPass", 0xffffff00);
    buf.cmdBindComputePipeline(pipelineBrightPass);
    buf.cmdPushConstants(pcBright);
    buf.cmdDispatchThreadGroups(kBloomSize.divide2D(16),
                                {.textures = {texColor, lvk::TextureHandle(luminanceViews[0])}});
    buf.cmdGenerateMipmap(luminanceViews[0]);
    buf.cmdPopDebugGroupLabel();

    runBloom(buf, samplerClamp);
    runAdaptation(buf, deltaSeconds);
    buf.cmdPopDebugGroupLabel();
}

void HDRPass::runBloom(lvk::ICommandBuffer &buf, lvk::SamplerHandle samplerClamp)
{
    buf.cmdPushDebugGroupLabel("HDR Bloom", 0xffffff00);
    struct BlurPC
    {
        uint32_t texIn;
        uint32_t texOut;
        uint32_t sampler;
    };

    std::vector<BlurPass> passes;
    passes.reserve(2 * gSettings.hdr.numBloomPasses);
    passes.push_back({brightPass, bloom[0]});
    for (int i = 0; i != gSettings.hdr.numBloomPasses - 1; i++)
    {
        passes.push_back({bloom[0], bloom[1]});
        passes.push_back({bloom[1], bloom[0]});
    }
    passes.push_back({bloom[0], bloomPass});

    for (uint32_t i = 0; i != passes.size(); i++)
    {
        const BlurPass pass = passes[i];
        buf.cmdBindComputePipeline(i & 1 ? pipelineBloomX : pipelineBloomY);
        buf.cmdPushConstants(BlurPC{
            .texIn = pass.texIn.index(),
            .texOut = pass.texOut.index(),
            .sampler = samplerClamp.index(),
        });
        if (gSettings.hdr.enableBloom)
        {
            buf.cmdDispatchThreadGroups(kBloomSize.divide2D(16),
                                        {.textures = {pass.texIn, pass.texOut, lvk::TextureHandle(brightPass)}});
        }
    }
    buf.cmdPopDebugGroupLabel();
}

void HDRPass::runAdaptation(lvk::ICommandBuffer &buf, float deltaSeconds)
{
    buf.cmdPushDebugGroupLabel("HDR Adaptation", 0xffffff00);
    const struct AdaptationPassPC
    {
        uint32_t texCurrSceneLuminance;
        uint32_t texPrevAdaptedLuminance;
        uint32_t texNewAdaptedLuminance;
        float adaptationSpeed;
    } pcAdaptation = {
        .texCurrSceneLuminance = luminanceViews.back().index(),
        .texPrevAdaptedLuminance = adaptedLuminance[0].index(),
        .texNewAdaptedLuminance = adaptedLuminance[1].index(),
        .adaptationSpeed = deltaSeconds * gSettings.hdr.adaptationSpeed,
    };
    buf.cmdBindComputePipeline(pipelineAdaptationPass);
    buf.cmdPushConstants(pcAdaptation);
    buf.cmdDispatchThreadGroups({1, 1, 1}, {.textures = {
                                                lvk::TextureHandle(luminanceViews[0]),
                                                lvk::TextureHandle(adaptedLuminance[0]),
                                                lvk::TextureHandle(adaptedLuminance[1]),
                                            }});
    buf.cmdPopDebugGroupLabel();
}

void HDRPass::toneMap(lvk::ICommandBuffer &buf, const lvk::Framebuffer &framebufferMain)
{
    buf.cmdPushDebugGroupLabel("ToneMap", 0xffff00ff);
    buf.cmdBeginRendering({.color = {{.loadOp = lvk::LoadOp_DontCare, .clearColor = {1.0f, 1.0f, 1.0f, 1.0f}}}},
                          framebufferMain, {.textures = {lvk::TextureHandle(adaptedLuminance[1])}});
    buf.cmdBindRenderPipeline(pipelineToneMap);
    buf.cmdPushConstants(pc);
    buf.cmdBindDepthState({});
    buf.cmdDraw(3);
    buf.cmdPopDebugGroupLabel();
}

void HDRPass::swapAdaptedLuminance()
{
    std::swap(adaptedLuminance[0], adaptedLuminance[1]);
}

void renderGbufferPass(const std::unique_ptr<lvk::IContext> &ctx, VulkanApp &app, lvk::ICommandBuffer &buf,
                       const FrameTargets &targets, const LoadedScene &loadedScene, const Skybox &skyBox,
                       const VKMesh11 &mesh, const RenderPipelines &pipelines, SceneDrawLists &drawLists,
                       LineCanvas3D &canvas3d, const mat4 &view,
                       const mat4 &proj, const LightFrame &lightFrame)

{
    buf.cmdPushDebugGroupLabel("GBuffer", 0xff40ff40);
    const lvk::Framebuffer framebufferOpaque = {
        .color = {{.texture = targets.sceneColor},
                  {.texture = targets.gbufferRT1},
                  {.texture = targets.gbufferRT2},
                  {.texture = targets.gbufferRT3},
                  {.texture = targets.gbufferRT4}},
        .depthStencil = {.texture = targets.opaqueDepth},
    };
    buf.cmdBeginRendering(
        lvk::RenderPass{
            .color =
                {{.loadOp = lvk::LoadOp_Clear, .storeOp = lvk::StoreOp_Store, .clearColor = {0.0f, 0.0f, 0.0f, 1.0f}},
                 {.loadOp = lvk::LoadOp_Clear, .storeOp = lvk::StoreOp_Store, .clearColor = {0.5f, 0.5f, 1.0f, 1.0f}},
                 {.loadOp = lvk::LoadOp_Clear, .storeOp = lvk::StoreOp_Store, .clearColor = {0.0f, 0.5f, 1.0f, 0.0f}},
                 {.loadOp = lvk::LoadOp_Clear, .storeOp = lvk::StoreOp_Store, .clearColor = {0.0f, 0.0f, 0.0f, 1.0f}}},
            .depth = {.loadOp = lvk::LoadOp_Clear, .storeOp = lvk::StoreOp_Store, .clearDepth = 1.0f}},
        framebufferOpaque,
        {.buffers = {
             lvk::BufferHandle(drawLists.opaque.bufferIndirect_),
             lvk::BufferHandle(drawLists.opaque.bufferDrawData_),
             lvk::BufferHandle(drawLists.masked.bufferIndirect_),
             lvk::BufferHandle(drawLists.masked.bufferDrawData_),
         }});

    const MeshPushConstants pc = {
        .viewProj = proj * view,
        .cameraPos = vec4(app.camera_.getPosition(), 1.0f),
        .bufferTransforms = ctx->gpuAddress(mesh.bufferTransforms_),
        .bufferDrawData = ctx->gpuAddress(mesh.bufferDrawData_),
        .bufferMaterials = ctx->gpuAddress(mesh.bufferMaterials_),
        //.bufferLight = ctx->gpuAddress(shadows.lightBuffer),
        .texSkybox = skyBox.texSkybox.index(),
        .texSkyboxIrradiance = skyBox.texSkyboxIrradiance.index(),
    };
    if (gSettings.draw.meshesOpaque)
    {
        buf.cmdPushDebugGroupLabel("Mesh opaque", 0xff0000ff);
        MeshPushConstants pcOpaque = pc;
        pcOpaque.bufferDrawData = ctx->gpuAddress(drawLists.opaque.bufferDrawData_);
        mesh.draw(buf, pipelines.opaque, &pcOpaque, sizeof(pcOpaque),
                  {.compareOp = lvk::CompareOp_Less, .isDepthWriteEnabled = true}, &drawLists.opaque);
        buf.cmdPopDebugGroupLabel();

        buf.cmdPushDebugGroupLabel("Mesh alpha masked", 0xff0000ff);
        MeshPushConstants pcMasked = pc;
        pcMasked.bufferDrawData = ctx->gpuAddress(drawLists.masked.bufferDrawData_);
        mesh.draw(buf, pipelines.masked, &pcMasked, sizeof(pcMasked),
                  {.compareOp = lvk::CompareOp_Less, .isDepthWriteEnabled = true}, &drawLists.masked);
        buf.cmdPopDebugGroupLabel();
    }
    buf.cmdEndRendering();
    buf.cmdPopDebugGroupLabel();
}

void renderSkyboxPass(lvk::ICommandBuffer &buf, const FrameTargets &targets, const Skybox &skyBox, const mat4 &view,
                      const mat4 &proj)
{
    const lvk::Framebuffer framebufferSkybox = {
        .color = {{.texture = targets.lightingColor}},
        .depthStencil = {.texture = targets.opaqueDepth},
    };
    buf.cmdBeginRendering(
        lvk::RenderPass{.color = {{.loadOp = lvk::LoadOp_Load, .storeOp = lvk::StoreOp_Store}},
                        .depth = {.loadOp = lvk::LoadOp_Load, .storeOp = lvk::StoreOp_Store}},
        framebufferSkybox,
        {.textures = {lvk::TextureHandle(skyBox.texSkybox), lvk::TextureHandle(targets.opaqueDepth)}});
    skyBox.draw(buf, view, proj);
    buf.cmdEndRendering();
}

} // namespace FinalDemo
