#include "RenderPasses.h"

#include "shared/VulkanApp.h"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace FinalDemo
{

FixedFoveatedRendering::FixedFoveatedRendering(
    const std::unique_ptr<lvk::IContext> &ctx, lvk::Dimensions renderSize, bool requested)
{
    if (!requested)
    {
        return;
    }

    const lvk::IContext::FragmentShadingRateCapabilities caps =
        ctx->getFragmentShadingRateCapabilities();
    if (!caps.attachmentSupported || !caps.supportsFragmentSize(1, 1))
    {
        LLOGW("Vulkan fragment shading rate attachment is unavailable; fixed foveated rendering is disabled.\n");
        return;
    }

    const uint32_t texelWidth = caps.minAttachmentTexelSize.width;
    const uint32_t texelHeight = caps.minAttachmentTexelSize.height;
    LVK_ASSERT(texelWidth && texelHeight);

    const uint32_t rateWidth = (renderSize.width + texelWidth - 1) / texelWidth;
    const uint32_t rateHeight = (renderSize.height + texelHeight - 1) / texelHeight;
    const uint32_t middleRate = caps.supportsFragmentSize(2, 2) ? 2u : 1u;
    const uint32_t outerRate = caps.supportsFragmentSize(4, 4) ? 4u : middleRate;
    const auto encodeRate = [](uint32_t rate) -> uint8_t
    {
        // VK_FORMAT_R8_UINT encodes log2(width) in bits 0..1 and
        // log2(height) in bits 2..3. Rates here are square.
        return static_cast<uint8_t>((rate >> 1) | (rate << 1));
    };

    std::vector<uint8_t> rates(rateWidth * rateHeight * kMultiViewLayerCount);
    for (uint32_t eye = 0; eye != kMultiViewLayerCount; ++eye)
    {
        for (uint32_t y = 0; y != rateHeight; ++y)
        {
            const float ny = (static_cast<float>(y) + 0.5f) / static_cast<float>(rateHeight) * 2.0f - 1.0f;
            for (uint32_t x = 0; x != rateWidth; ++x)
            {
                const float nx =
                    (static_cast<float>(x) + 0.5f) / static_cast<float>(rateWidth) * 2.0f - 1.0f;
                const float radiusSquared = nx * nx + ny * ny;
                const uint32_t rate = radiusSquared < 0.30f * 0.30f
                                          ? 1u
                                          : (radiusSquared < 0.65f * 0.65f ? middleRate : outerRate);
                rates[eye * rateWidth * rateHeight + y * rateWidth + x] = encodeRate(rate);
            }
        }
    }

    rateImage = ctx->createTexture({
        .format = lvk::Format_R_UI8,
        .dimensions = {rateWidth, rateHeight},
        .numLayers = kMultiViewLayerCount,
        .usage = lvk::TextureUsageBits_FragmentShadingRateAttachment,
        .data = rates.data(),
        .debugName = "Fixed foveated fragment shading rate",
    });
    if (!rateImage.valid())
    {
        LLOGW("Failed to create the fragment shading rate attachment; fixed foveated rendering is disabled.\n");
        return;
    }

    attachment = {
        .texture = rateImage,
        .texelWidth = texelWidth,
        .texelHeight = texelHeight,
    };
    enabled = true;
    LLOGL("Fixed foveated rendering: %ux%u rate image, %ux%u pixels/texel, outer rate %ux%u.\n",
          rateWidth, rateHeight, texelWidth, texelHeight, outerRate, outerRate);
}

RenderPipelines::RenderPipelines(const std::unique_ptr<lvk::IContext> &ctx, const MeshData &meshData,
                                 lvk::Format depthFormat, lvk::Format shadowMapFormat,
                                 bool visibilityMaskEnabled, bool fragmentShadingRateEnabled)
    : opaque(ctx, meshData.streams, kOffscreenFormat, depthFormat, kNumSamples,
             loadShaderModule(ctx, "Renderer/src/gbuffer_opaque.vert"),
             loadShaderModule(ctx, "Renderer/src/gbuffer_opaque.frag"), false, visibilityMaskEnabled,
             fragmentShadingRateEnabled),
      masked(ctx, meshData.streams, kOffscreenFormat, depthFormat, kNumSamples,
             loadShaderModule(ctx, "Renderer/src/gbuffer_opaque.vert"),
             loadShaderModule(ctx, "Renderer/src/gbuffer_masked.frag"), false, visibilityMaskEnabled,
             fragmentShadingRateEnabled),
      transparent(ctx, meshData.streams, kOffscreenFormat, depthFormat, kNumSamples,
                  loadShaderModule(ctx, "Renderer/src/main.vert"),
                  loadShaderModule(ctx, "Renderer/src/transparent.frag"), false, visibilityMaskEnabled),
      shadow(ctx, meshData.positionOnlyStreams, lvk::Format_Invalid, shadowMapFormat, 1,
            loadShaderModule(ctx, "Renderer/shaders/shadow.vert"),
            loadShaderModule(ctx, "Renderer/shaders/shadow.frag"), true)
{
}

VisibilityMaskPass::VisibilityMaskPass(const std::unique_ptr<lvk::IContext> &ctx, const VulkanApp &app,
                                       lvk::Format depthStencilFormat)
    : enabled(app.isOpenXR() && app.isXrVisibilityMaskSupported() &&
              isDepthStencilFormat(depthStencilFormat))
{
    if (!enabled)
    {
        return;
    }

    bool hasGeometry = false;
#if defined(LVK_WITH_OPENXR) && LVK_WITH_OPENXR
    for (uint32_t eye = 0; eye != kMultiViewLayerCount; ++eye)
    {
        std::vector<XrVector2f> xrVertices;
        std::vector<uint32_t> xrIndices;
        app.getXrVisibilityMask(eye, xrVertices, xrIndices);

        std::vector<vec2> vertices;
        vertices.reserve(xrVertices.size());
        for (const XrVector2f &vertex : xrVertices)
        {
            vertices.emplace_back(vertex.x, vertex.y);
        }

        EyeMesh &mesh = eyes[eye];
        mesh.indexCount = static_cast<uint32_t>(xrIndices.size());
        if (vertices.empty() || xrIndices.empty())
        {
            continue;
        }

        mesh.vertices = ctx->createBuffer({
            .usage = lvk::BufferUsageBits_Storage,
            .storage = lvk::StorageType_Device,
            .size = vertices.size() * sizeof(vertices[0]),
            .data = vertices.data(),
            .debugName = eye == 0 ? "Visibility mask vertices: left" : "Visibility mask vertices: right",
        });
        mesh.indices = ctx->createBuffer({
            .usage = lvk::BufferUsageBits_Index,
            .storage = lvk::StorageType_Device,
            .size = xrIndices.size() * sizeof(xrIndices[0]),
            .data = xrIndices.data(),
            .debugName = eye == 0 ? "Visibility mask indices: left" : "Visibility mask indices: right",
        });
        hasGeometry = true;
    }
#endif

    if (!hasGeometry)
    {
        return;
    }

    vert = loadShaderModule(ctx, "Renderer/shaders/VisibilityMask.vert");
    frag = loadShaderModule(ctx, "Renderer/shaders/VisibilityMask.frag");
    pipeline = ctx->createRenderPipeline({
        .smVert = vert,
        .smFrag = frag,
        .depthFormat = depthStencilFormat,
        .stencilFormat = depthStencilFormat,
        .cullMode = lvk::CullMode_None,
        .backFaceStencil = visibilityMaskWriteState(),
        .frontFaceStencil = visibilityMaskWriteState(),
        .debugName = "Pipeline: OpenXR visibility mask",
    });
    LVK_ASSERT(pipeline.valid());
}

bool VisibilityMaskPass::render(const std::unique_ptr<lvk::IContext> &ctx, lvk::ICommandBuffer &buf,
                                lvk::TextureHandle depthStencil,
                                const std::array<mat4, kMultiViewLayerCount> &projection) const
{
    if (!enabled)
    {
        return false;
    }

    buf.cmdPushDebugGroupLabel("OpenXR visibility mask", 0xff00a0ff);
    for (uint32_t eye = 0; eye != kMultiViewLayerCount; ++eye)
    {
        buf.cmdBeginRendering(
            {
                .depth = {
                    // LightweightVK currently mirrors depth load/store/clear to
                    // the stencil aspect, so this clears both aspects per eye.
                    .loadOp = lvk::LoadOp_Clear,
                    .storeOp = lvk::StoreOp_Store,
                    .layer = static_cast<uint8_t>(eye),
                    .clearDepth = 1.0f,
                    .clearStencil = 0,
                },
                .stencil = {
                    .loadOp = lvk::LoadOp_Clear,
                    .storeOp = lvk::StoreOp_Store,
                    .layer = static_cast<uint8_t>(eye),
                    .clearStencil = 0,
                },
            },
            {.depthStencil = {.texture = depthStencil}});

        const EyeMesh &mesh = eyes[eye];
        if (pipeline.valid() && mesh.indexCount)
        {
            const struct
            {
                mat4 projection;
                uint64_t vertices;
            } pc = {
                .projection = projection[eye],
                .vertices = ctx->gpuAddress(mesh.vertices),
            };

            buf.cmdBindRenderPipeline(pipeline);
            buf.cmdBindDepthState({});
            buf.cmdBindIndexBuffer(mesh.indices, lvk::IndexFormat_UI32);
            buf.cmdPushConstants(pc);
            buf.cmdDrawIndexed(mesh.indexCount);
        }
        buf.cmdEndRendering();
    }
    buf.cmdPopDebugGroupLabel();
    return true;
}

ShadowPass::ShadowPass(const std::unique_ptr<lvk::IContext> &ctx)
{
    map = ctx->createTexture({
        .type = lvk::TextureType_2D,
        .format = lvk::Format_Z_UN16,
        .dimensions = {512, 512},
        .usage = lvk::TextureUsageBits_Attachment | lvk::TextureUsageBits_Sampled,
        .components = {.r = lvk::Swizzle_R, .g = lvk::Swizzle_R, .b = lvk::Swizzle_R, .a = lvk::Swizzle_1},
        .debugName = "Shadow map",
    });

    sampler = ctx->createSampler({
        .wrapU = lvk::SamplerWrap_Clamp,
        .wrapV = lvk::SamplerWrap_Clamp,
        .depthCompareOp = lvk::CompareOp_LessEqual,
        .depthCompareEnabled = true,
        .debugName = "Sampler: shadow",
    });

    lightBuffer = ctx->createBuffer({
        .usage = lvk::BufferUsageBits_Storage,
        .storage = lvk::StorageType_Device,
        .size = sizeof(LightData),
        .debugName = "Buffer: light",
    });
}
//
void ShadowPass::updateIfNeeded(lvk::ICommandBuffer &buf, const VKMesh11 &mesh, const RenderPipelines &pipelines,
                                const LightFrame &lightFrame)
{
    if (previousLight != gSettings.light)
    {
       previousLight = gSettings.light;
       buf.cmdBeginRendering(lvk::RenderPass{.depth = {.loadOp = lvk::LoadOp_Clear, .clearDepth = 1.0f}},
                             lvk::Framebuffer{.depthStencil = {.texture = map}});
       buf.cmdPushDebugGroupLabel("Shadow map", 0xff0000ff);
       buf.cmdSetDepthBias(gSettings.light.depthBiasConst, gSettings.light.depthBiasSlope);
       buf.cmdSetDepthBiasEnable(true);
       mesh.draw(buf, pipelines.shadow, lightFrame.view, lightFrame.proj);
       buf.cmdSetDepthBiasEnable(false);
       buf.cmdPopDebugGroupLabel();
       buf.cmdEndRendering();
    }

    // clang-format off
  const mat4 scaleBias = mat4(0.5, 0.0, 0.0, 0.0,
                              0.0, 0.5, 0.0, 0.0,
                              0.0, 0.0, 1.0, 0.0,
                              0.5, 0.5, 0.0, 1.0);
    // clang-format on
    buf.cmdUpdateBuffer(lightBuffer,
                        LightData{
                            .viewProjBias = scaleBias * lightFrame.proj * lightFrame.view,
                            .lightDir = vec4(lightFrame.dir, 0.0f),
                            .lightColorIntensity = vec4(gSettings.light.color, gSettings.light.intensity),
                            .shadowTexture = map.index(),
                            .shadowSampler = sampler.index(),
                            .frameIndex = frameIndex++,
                            .iblIntensity = gSettings.light.iblIntensity,
                        });
}

OITPass::OITPass(const std::unique_ptr<lvk::IContext> &ctx, const lvk::Dimensions &sizeFb,
                 lvk::Format depthStencilFormat, bool visibilityMaskEnabled)
    : visibilityMaskEnabled(visibilityMaskEnabled)
{
   vert = loadShaderModule(ctx, "data/shaders/QuadFlip.vert");
   frag = loadShaderModule(ctx, "Renderer/shaders/oit.frag");
   pipeline = ctx->createRenderPipeline({
       .smVert = vert,
       .smFrag = frag,
       .color = {{.format = kOffscreenFormat}},
       .depthFormat = depthStencilFormat,
       .stencilFormat = stencilFormat(depthStencilFormat, visibilityMaskEnabled),
       .backFaceStencil = visibilityMaskTestState(depthStencilFormat, visibilityMaskEnabled),
       .frontFaceStencil = visibilityMaskTestState(depthStencilFormat, visibilityMaskEnabled),
       .debugName = "Pipeline: OIT Combine",
   });

   maxFragments = sizeFb.width * sizeFb.height * kNumSamples * kMultiViewLayerCount;
   atomicCounter = ctx->createBuffer({
       .usage = lvk::BufferUsageBits_Storage,
       .storage = lvk::StorageType_Device,
       .size = sizeof(uint32_t),
       .debugName = "Buffer: atomic counter",
   });
   fragmentLists = ctx->createBuffer({
       .usage = lvk::BufferUsageBits_Storage,
       .storage = lvk::StorageType_Device,
       .size = sizeof(TransparentFragment) * maxFragments,
       .debugName = "Buffer: transparency lists",
   });
   heads = ctx->createTexture({
       .format = lvk::Format_R_UI32,
       .dimensions = sizeFb,
       .numLayers = kMultiViewLayerCount,
       .usage = lvk::TextureUsageBits_Storage,
       .debugName = "oitHeads",
   });

   const struct OITBuffer
   {
       uint64_t bufferAtomicCounter;
       uint64_t bufferTransparencyLists;
       uint32_t texHeadsOIT;
       uint32_t maxOITFragments;
   } data = {
       .bufferAtomicCounter = ctx->gpuAddress(atomicCounter),
       .bufferTransparencyLists = ctx->gpuAddress(fragmentLists),
       .texHeadsOIT = heads.index(),
       .maxOITFragments = maxFragments,
   };

   passBuffer = ctx->createBuffer({
       .usage = lvk::BufferUsageBits_Storage,
       .storage = lvk::StorageType_Device,
       .size = sizeof(data),
       .data = &data,
       .debugName = "Buffer: OIT",
   });
}

void OITPass::clear(lvk::ICommandBuffer &buf)
{
   buf.cmdPushDebugGroupLabel("OIT Clear", 0xffff80ff);
   buf.cmdClearColorImage(heads, {.uint32 = {0xffffffff}}, {.numLayers = kMultiViewLayerCount});
   buf.cmdFillBuffer(atomicCounter, 0, sizeof(uint32_t), 0);
   buf.cmdPopDebugGroupLabel();
}

lvk::TextureHandle OITPass::combine(const std::unique_ptr<lvk::IContext> &ctx, lvk::ICommandBuffer &buf,
                                   const FrameTargets &targets, lvk::TextureHandle texColor)
{
   buf.cmdPushDebugGroupLabel("OIT Combine", 0xffff80ff);
   buf.cmdBeginRendering(lvk::RenderPass{
       .color = {{.loadOp = lvk::LoadOp_Clear,
                  .storeOp = lvk::StoreOp_Store,
                  .clearColor = {0.0f, 0.0f, 0.0f, 1.0f}}},
       .depth = {.loadOp = lvk::LoadOp_Load, .storeOp = lvk::StoreOp_Store},
       .stencil = visibilityMaskEnabled
                      ? lvk::RenderPass::AttachmentDesc{.loadOp = lvk::LoadOp_Load,
                                                        .storeOp = lvk::StoreOp_Store}
                      : lvk::RenderPass::AttachmentDesc{.loadOp = lvk::LoadOp_DontCare,
                                                        .storeOp = lvk::StoreOp_DontCare},
       .viewMask = kMultiViewViewMask},
       lvk::Framebuffer{
           .color = {{.texture = targets.sceneColor}},
           .depthStencil = {.texture = targets.opaqueDepth},
       },

       {.textures = {lvk::TextureHandle(heads), texColor}, .buffers = {lvk::BufferHandle(fragmentLists)}});

   const struct OITPushConstants
   {
       uint64_t bufferTransparencyLists;
       uint32_t texColor;
       uint32_t texHeadsOIT;
       float time;
       float opacityBoost;
       uint32_t showHeatmap;
   } pc = {
       .bufferTransparencyLists = ctx->gpuAddress(fragmentLists),
       .texColor = texColor.index(),
       .texHeadsOIT = heads.index(),
       .time = static_cast<float>(glfwGetTime()),
       .opacityBoost = gSettings.oit.opacityBoost,
       .showHeatmap = gSettings.oit.showHeatmap ? 1u : 0u,
   };
   buf.cmdBindRenderPipeline(pipeline);
   buf.cmdPushConstants(pc);
   buf.cmdBindDepthState({});
   buf.cmdDraw(3);
   buf.cmdEndRendering();
   buf.cmdPopDebugGroupLabel();
   return lvk::TextureHandle(targets.sceneColor);
}

LightingPass::LightingPass(const std::unique_ptr<lvk::IContext> &ctx, const FrameTargets &targets,
                           lvk::SamplerHandle samplerClamp, lvk::Format swapchainFormat,
                           const FixedFoveatedRendering &foveation)
    : fragmentShadingRate(foveation.attachment)
{
    vert = loadShaderModule(ctx, "data/shaders/QuadFlip.vert");
    frag = loadShaderModule(ctx, "Renderer/shaders/lighting.frag");
    pipeline = ctx->createRenderPipeline({
        .smVert = vert,
        .smFrag = frag,
        .color = {{.format = kOffscreenFormat}},
        .fragmentShadingRateAttachment = foveation.enabled,
        .debugName = "Pipeline: Lighting",
    });
}
lvk::TextureHandle LightingPass::execute(const std::unique_ptr<lvk::IContext> &ctx, lvk::ICommandBuffer &buf,
                                         const FrameTargets &targets, const Skybox &skyBox, const ShadowPass &shadows,
                                         lvk::SamplerHandle samplerClamp)
{
    const lvk::Framebuffer framebufferMain = {
        .color = {{.texture = targets.lightingColor}},
        .fragmentShadingRate = fragmentShadingRate,
    };
    buf.cmdPushDebugGroupLabel("Lighting", 0xffff00ff);
    pc.invViewProj[0] = gSettings.view.inverseViewProjection[0];
    pc.invViewProj[1] = gSettings.view.inverseViewProjection[1];
    pc.sceneColor = targets.sceneColor.index();
    pc.gbuffer1 = targets.gbufferRT1.index();
    pc.gbuffer2 = targets.gbufferRT2.index();
    pc.gbuffer3 = targets.gbufferRT3.index();
    pc.depth = targets.opaqueDepth.index();
    pc.texSkyboxIrradiance = skyBox.texSkyboxIrradiance.index();
    pc.sampler = samplerClamp.index();
    pc.bufferLight = ctx->gpuAddress(shadows.lightBuffer);

    buf.cmdBeginRendering({.color = {{.loadOp = lvk::LoadOp_Clear,
                                      .storeOp = lvk::StoreOp_Store,
                                      .clearColor = {0.0f, 0.0f, 0.0f, 1.0f}}},
                           //.layerCount = kMultiViewLayerCount,
                           .viewMask  = kMultiViewViewMask},
                          framebufferMain,
                          {.textures =
                               {
                                   lvk::TextureHandle(targets.sceneColor),
                                   lvk::TextureHandle(targets.gbufferRT1),
                                   lvk::TextureHandle(targets.gbufferRT2),
                                   lvk::TextureHandle(targets.gbufferRT3),
                                   lvk::TextureHandle(targets.opaqueDepth),
                                   lvk::TextureHandle(skyBox.texSkyboxIrradiance),
                                   lvk::TextureHandle(shadows.map),
                               },
                           .buffers = {lvk::BufferHandle(shadows.lightBuffer)}});
    buf.cmdBindRenderPipeline(pipeline);
    buf.cmdPushConstants(pc);
    buf.cmdBindDepthState({});
    buf.cmdDraw(3);
    buf.cmdEndRendering();
    buf.cmdPopDebugGroupLabel();
    return lvk::TextureHandle(targets.lightingColor);
}

HDRPass::HDRPass(const std::unique_ptr<lvk::IContext> &ctx, const FrameTargets &targets,
                 lvk::SamplerHandle samplerClamp, lvk::Format swapchainFormat,
                 lvk::Format depthStencilFormat, bool visibilityMaskEnabled,
                 const FixedFoveatedRendering &foveation)
    : visibilityMaskEnabled(visibilityMaskEnabled),
      fragmentShadingRate(foveation.attachment)
{
    brightPass = ctx->createTexture({
        .format = kHDRBloomFormat,
        .dimensions = kBloomSize,
        .numLayers = kMultiViewLayerCount,
        .usage = lvk::TextureUsageBits_Sampled | lvk::TextureUsageBits_Storage,
        .debugName = "texBrightPass",
    });
    bloomPass = ctx->createTexture({
        .format = kHDRBloomFormat,
        .dimensions = kBloomSize,
        .numLayers = kMultiViewLayerCount,
        .usage = lvk::TextureUsageBits_Sampled | lvk::TextureUsageBits_Storage,
        .debugName = "texBloomPass",
    });
    bloom[0] = ctx->createTexture({
        .format = kHDRBloomFormat,
        .dimensions = kBloomSize,
        .numLayers = kMultiViewLayerCount,
        .usage = lvk::TextureUsageBits_Sampled | lvk::TextureUsageBits_Storage,
        .debugName = "texBloom0",
    });
    bloom[1] = ctx->createTexture({
        .format = kHDRBloomFormat,
        .dimensions = kBloomSize,
        .numLayers = kMultiViewLayerCount,
        .usage = lvk::TextureUsageBits_Sampled | lvk::TextureUsageBits_Storage,
        .debugName = "texBloom1",
    });

    const lvk::ComponentMapping swizzle = {
        .r = lvk::Swizzle_R, .g = lvk::Swizzle_R, .b = lvk::Swizzle_R, .a = lvk::Swizzle_1};
    luminanceViews[0] = ctx->createTexture({
        .format = lvk::Format_R_F16,
        .dimensions = kBloomSize,
        .numLayers = kMultiViewLayerCount,
        .usage = lvk::TextureUsageBits_Sampled | lvk::TextureUsageBits_Storage,
        .numMipLevels = lvk::calcNumMipLevels(kBloomSize.width, kBloomSize.height),
        .components = swizzle,
        .debugName = "texLuminance",
    });
    for (uint32_t v = 1; v != luminanceViews.size(); v++)
    {
        luminanceViews[v] = ctx->createTextureView(
            luminanceViews[0], {.numLayers = kMultiViewLayerCount, .mipLevel = v, .components = swizzle},
            "texLumViews[]");
    }

    const lvk::TextureDesc luminanceTextureDesc{
        .format = lvk::Format_R_F16,
        .dimensions = {1, 1},
        .numLayers = kMultiViewLayerCount,
        .usage = lvk::TextureUsageBits_Sampled | lvk::TextureUsageBits_Storage,
        .components = swizzle,
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
        .depthFormat = depthStencilFormat,
        .stencilFormat = stencilFormat(depthStencilFormat, visibilityMaskEnabled),
        .backFaceStencil = visibilityMaskTestState(depthStencilFormat, visibilityMaskEnabled),
        .frontFaceStencil = visibilityMaskTestState(depthStencilFormat, visibilityMaskEnabled),
        .fragmentShadingRateAttachment = foveation.enabled,
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
   if (!adaptedLuminanceInitialized)
   {
       buf.cmdClearColorImage(adaptedLuminance[0], {.float32 = {50.0f, 0.0f, 0.0f, 0.0f}},
                              {.numLayers = kMultiViewLayerCount});
       buf.cmdClearColorImage(adaptedLuminance[1], {.float32 = {50.0f, 0.0f, 0.0f, 0.0f}},
                              {.numLayers = kMultiViewLayerCount});
       adaptedLuminanceInitialized = true;
   }
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
   lvk::Dimensions dim = kBloomSize.divide2D(16);
   dim.depth = kMultiViewLayerCount;
   buf.cmdDispatchThreadGroups(dim,
                               {.textures = {texColor},
                                .storageImages = {lvk::TextureHandle(brightPass),
                                                  lvk::TextureHandle(luminanceViews[0])}});
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

    if (gSettings.hdr.enableBloom)
    {
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
            lvk::Dimensions dim = kBloomSize.divide2D(16);
            dim.depth = kMultiViewLayerCount;
            buf.cmdDispatchThreadGroups(dim, {.textures = {pass.texIn}, .storageImages = {pass.texOut}});
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
        uint32_t enableAdaptation;
    } pcAdaptation = {
        .texCurrSceneLuminance = luminanceViews.back().index(),
        .texPrevAdaptedLuminance = adaptedLuminance[0].index(),
        .texNewAdaptedLuminance = adaptedLuminance[1].index(),
        .adaptationSpeed = deltaSeconds * gSettings.hdr.adaptationSpeed,
        .enableAdaptation = gSettings.hdr.enableAdaptation ? 1u : 0u,
    };
    buf.cmdBindComputePipeline(pipelineAdaptationPass);
    buf.cmdPushConstants(pcAdaptation);
    buf.cmdDispatchThreadGroups({1, 1, kMultiViewLayerCount},
                                {.storageImages = {
                                     lvk::TextureHandle(luminanceViews[0]),
                                     lvk::TextureHandle(adaptedLuminance[0]),
                                     lvk::TextureHandle(adaptedLuminance[1]),
                                 }});
    buf.cmdPopDebugGroupLabel();
}

void HDRPass::toneMap(lvk::ICommandBuffer &buf, const lvk::Framebuffer &framebufferMain,
                      lvk::TextureHandle depthStencil, lvk::TextureHandle texColor)
{
    buf.cmdPushDebugGroupLabel("ToneMap", 0xffff00ff);
    pc.texColor = texColor.index();
    pc.texLuminance = adaptedLuminance[1].index();
    lvk::Framebuffer framebuffer = framebufferMain;
    framebuffer.depthStencil = {.texture = depthStencil};
    framebuffer.fragmentShadingRate = fragmentShadingRate;
    buf.cmdBeginRendering({.color = {{.loadOp = lvk::LoadOp_Clear,
                                     .storeOp = lvk::StoreOp_Store,
                                     .clearColor = {0.0f, 0.0f, 0.0f, 1.0f}}},
                           .depth = {.loadOp = lvk::LoadOp_Load, .storeOp = lvk::StoreOp_Store},
                           .stencil = visibilityMaskEnabled
                                          ? lvk::RenderPass::AttachmentDesc{
                                                .loadOp = lvk::LoadOp_Load,
                                                .storeOp = lvk::StoreOp_Store}
                                          : lvk::RenderPass::AttachmentDesc{
                                                .loadOp = lvk::LoadOp_DontCare,
                                                .storeOp = lvk::StoreOp_DontCare},
                           //.layerCount = kMultiViewLayerCount,
                           .viewMask  = kMultiViewViewMask},
                          framebuffer,
                          {.textures = {texColor,
                                        lvk::TextureHandle(bloomPass),
                                        lvk::TextureHandle(adaptedLuminance[1])}});
    buf.cmdBindRenderPipeline(pipelineToneMap);
    buf.cmdPushConstants(pc);
    buf.cmdBindDepthState({});
    buf.cmdDraw(3);
    buf.cmdPopDebugGroupLabel();
    buf.cmdEndRendering();
}

void HDRPass::swapAdaptedLuminance()
{
    std::swap(adaptedLuminance[0], adaptedLuminance[1]);
}

void renderGbufferPass(const std::unique_ptr<lvk::IContext> &ctx, lvk::ICommandBuffer &buf,
                       const FrameTargets &targets, const LoadedScene &loadedScene, const Skybox &skyBox,
                       const VKMesh11 &mesh, const RenderPipelines &pipelines, SceneDrawLists &drawLists,
                       const ShadowPass &shadows, LineCanvas3D &canvas3d, const LightFrame &lightFrame,
                       bool visibilityMaskEnabled, const FixedFoveatedRendering &foveation)

{
    buf.cmdPushDebugGroupLabel("GBuffer", 0xff40ff40);
    const lvk::Framebuffer framebufferOpaque = {
        .color = {{.texture = targets.sceneColor},
                  {.texture = targets.gbufferRT1},
                  {.texture = targets.gbufferRT2},
                  {.texture = targets.gbufferRT3}},
        .depthStencil = {.texture = targets.opaqueDepth},
        .fragmentShadingRate = foveation.attachment,
    };
    buf.cmdBeginRendering(
        lvk::RenderPass{
            .color =
                {{.loadOp = lvk::LoadOp_Clear, .storeOp = lvk::StoreOp_Store, .clearColor = {0.0f, 0.0f, 0.0f, 1.0f}},
                 {.loadOp = lvk::LoadOp_Clear, .storeOp = lvk::StoreOp_Store, .clearColor = {0.5f, 0.5f, 1.0f, 1.0f}},
                 {.loadOp = lvk::LoadOp_Clear, .storeOp = lvk::StoreOp_Store, .clearColor = {0.0f, 0.5f, 1.0f, 0.0f}},
                 {.loadOp = lvk::LoadOp_Clear, .storeOp = lvk::StoreOp_Store, .clearColor = {0.0f, 0.0f, 0.0f, 1.0f}}},
            .depth = {.loadOp = visibilityMaskEnabled ? lvk::LoadOp_Load : lvk::LoadOp_Clear,
                      .storeOp = lvk::StoreOp_Store,
                      .clearDepth = 1.0f},
            .stencil = visibilityMaskEnabled
                           ? lvk::RenderPass::AttachmentDesc{.loadOp = lvk::LoadOp_Load,
                                                             .storeOp = lvk::StoreOp_Store}
                           : lvk::RenderPass::AttachmentDesc{.loadOp = lvk::LoadOp_DontCare,
                                                             .storeOp = lvk::StoreOp_DontCare},
            //.layerCount = kMultiViewLayerCount,
            .viewMask  = kMultiViewViewMask},
        framebufferOpaque,
        {.buffers = {
             lvk::BufferHandle(drawLists.opaque.bufferIndirect_),
             lvk::BufferHandle(drawLists.opaque.bufferDrawData_),
             lvk::BufferHandle(drawLists.masked.bufferIndirect_),
             lvk::BufferHandle(drawLists.masked.bufferDrawData_),
             lvk::BufferHandle(drawLists.transparent.bufferDrawData_),
         }});

    const MeshPushConstants pc = {
        .viewProjLt = gSettings.view.viewProjection[0],
        .viewProjRt = gSettings.view.viewProjection[1],
        .cameraPos = {gSettings.view.cameraPosition[0], gSettings.view.cameraPosition[1]},
        .bufferTransforms = ctx->gpuAddress(mesh.bufferTransforms_),
        .bufferDrawData = ctx->gpuAddress(mesh.bufferDrawData_),
        .bufferMaterials = ctx->gpuAddress(mesh.bufferMaterials_),
        .bufferOITAtomicCounter = 0,
        .bufferOITLists = 0,
        .texHeadsOIT = 0,
        .maxOITFragments = 0,
        .bufferLight = ctx->gpuAddress(shadows.lightBuffer),
        .texSkybox = skyBox.texSkybox.index(),
        .texSkyboxIrradiance = skyBox.texSkyboxIrradiance.index(),
    };
    if (gSettings.draw.meshesOpaque)
    {
        constexpr size_t kGBufferPushConstantsSize = offsetof(MeshPushConstants, bufferLight);

        buf.cmdPushDebugGroupLabel("Mesh opaque", 0xff0000ff);
        MeshPushConstants pcOpaque = pc;
        pcOpaque.bufferDrawData = ctx->gpuAddress(drawLists.opaque.bufferDrawData_);
        mesh.draw(buf, pipelines.opaque, &pcOpaque, kGBufferPushConstantsSize,
                  {.compareOp = lvk::CompareOp_Less, .isDepthWriteEnabled = true}, &drawLists.opaque);
        buf.cmdPopDebugGroupLabel();

        buf.cmdPushDebugGroupLabel("Mesh alpha masked", 0xff0000ff);
        MeshPushConstants pcMasked = pc;
        pcMasked.bufferDrawData = ctx->gpuAddress(drawLists.masked.bufferDrawData_);
        mesh.draw(buf, pipelines.masked, &pcMasked, kGBufferPushConstantsSize,
                  {.compareOp = lvk::CompareOp_Less, .isDepthWriteEnabled = true}, &drawLists.masked);
        buf.cmdPopDebugGroupLabel();
    }
    buf.cmdEndRendering();
    buf.cmdPopDebugGroupLabel();
}

void renderSkyboxPass(lvk::ICommandBuffer &buf, const FrameTargets &targets, const Skybox &skyBox,
                      bool visibilityMaskEnabled, const FixedFoveatedRendering &foveation)
{
    const lvk::Framebuffer framebufferSkybox = {
        .color = {{.texture = targets.lightingColor}},
        .depthStencil = {.texture = targets.opaqueDepth},
        .fragmentShadingRate = foveation.attachment,
    };
    buf.cmdBeginRendering(
        lvk::RenderPass{.color = {{.loadOp = lvk::LoadOp_Load, .storeOp = lvk::StoreOp_Store}},
                        .depth = {.loadOp = lvk::LoadOp_Load, .storeOp = lvk::StoreOp_Store},
                        .stencil = visibilityMaskEnabled
                                       ? lvk::RenderPass::AttachmentDesc{
                                             .loadOp = lvk::LoadOp_Load,
                                             .storeOp = lvk::StoreOp_Store}
                                       : lvk::RenderPass::AttachmentDesc{
                                             .loadOp = lvk::LoadOp_DontCare,
                                             .storeOp = lvk::StoreOp_DontCare},
                        //.layerCount = kMultiViewLayerCount,
                        .viewMask  = kMultiViewViewMask},
        framebufferSkybox,
        {.textures = {lvk::TextureHandle(skyBox.texSkybox)}});
    skyBox.draw(buf);
    buf.cmdEndRendering();
}

void renderTransparentPass(const std::unique_ptr<lvk::IContext> &ctx, lvk::ICommandBuffer &buf,
                           const FrameTargets &targets, const Skybox &skyBox, const VKMesh11 &mesh,
                           const RenderPipelines &pipelines, SceneDrawLists &drawLists, const OITPass &oit,
                           const ShadowPass &shadows, bool visibilityMaskEnabled)
{
    if (!gSettings.draw.meshesTransparent)
    {
        return;
    }

    buf.cmdPushDebugGroupLabel("Transparent OIT", 0xffff80ff);
    const lvk::Framebuffer framebufferTransparent = {
        .color = {{.texture = targets.lightingColor}},
        .depthStencil = {.texture = targets.opaqueDepth},
    };
    buf.cmdBeginRendering(lvk::RenderPass{.color = {{.loadOp = lvk::LoadOp_Load, .storeOp = lvk::StoreOp_Store}},
                                          .depth = {.loadOp = lvk::LoadOp_Load, .storeOp = lvk::StoreOp_Store},
                                          .stencil = visibilityMaskEnabled
                                                         ? lvk::RenderPass::AttachmentDesc{
                                                               .loadOp = lvk::LoadOp_Load,
                                                               .storeOp = lvk::StoreOp_Store}
                                                         : lvk::RenderPass::AttachmentDesc{
                                                               .loadOp = lvk::LoadOp_DontCare,
                                                               .storeOp = lvk::StoreOp_DontCare},
                                          .viewMask = kMultiViewViewMask},
                          framebufferTransparent,
                          {.textures =
                               {
                                   lvk::TextureHandle(oit.heads),
                                   lvk::TextureHandle(targets.lightingColor),
                                   lvk::TextureHandle(skyBox.texSkybox),
                                   lvk::TextureHandle(skyBox.texSkyboxIrradiance),
                                   lvk::TextureHandle(shadows.map),
                               },
                           .buffers = {
                               lvk::BufferHandle(drawLists.transparent.bufferIndirect_),
                               lvk::BufferHandle(drawLists.transparent.bufferDrawData_),
                               lvk::BufferHandle(oit.atomicCounter),
                               lvk::BufferHandle(oit.fragmentLists),
                               lvk::BufferHandle(shadows.lightBuffer),
                           }});

    const MeshPushConstants pc = {
        .viewProjLt = gSettings.view.viewProjection[0],
        .viewProjRt = gSettings.view.viewProjection[1],
        .cameraPos = {gSettings.view.cameraPosition[0], gSettings.view.cameraPosition[1]},
        .bufferTransforms = ctx->gpuAddress(mesh.bufferTransforms_),
        .bufferDrawData = ctx->gpuAddress(drawLists.transparent.bufferDrawData_),
        .bufferMaterials = ctx->gpuAddress(mesh.bufferMaterials_),
        .bufferOITAtomicCounter = ctx->gpuAddress(oit.atomicCounter),
        .bufferOITLists = ctx->gpuAddress(oit.fragmentLists),
        .texHeadsOIT = oit.heads.index(),
        .maxOITFragments = oit.maxFragments,
        .bufferLight = ctx->gpuAddress(shadows.lightBuffer),
        .texSkybox = skyBox.texSkybox.index(),
        .texSkyboxIrradiance = skyBox.texSkyboxIrradiance.index(),
    };
    mesh.draw(buf, pipelines.transparent, &pc, sizeof(pc),
              {.compareOp = lvk::CompareOp_Less, .isDepthWriteEnabled = false}, &drawLists.transparent);
    buf.cmdEndRendering();
    buf.cmdPopDebugGroupLabel();
}
} // namespace FinalDemo
