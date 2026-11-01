#pragma once

#include "../shaders/shadow_occlusion_culling_config.h"
#include "SceneResources.h"

namespace FinalDemo
{
// Camera-depth receiver reprojection and GPU max-depth hierarchy, inspired by
// Haar/Aaltonen (2015). Receiver generation is per pixel; caster tests are per mesh.
class ShadowOcclusionCulling
{
  public:
    VKIndirectBuffer11 draws;
    lvk::Holder<lvk::BufferHandle> boxes;
    lvk::Holder<lvk::TextureHandle> lightTileDepth;
    std::array<lvk::Holder<lvk::TextureHandle>, RECEIVER_MIP_LEVELS> lightTileDepthMipViews;
    std::array<uint32_t, RECEIVER_MIP_LEVELS> hzb;

    lvk::Holder<lvk::ShaderModuleHandle> lightTileDepthShader;
    lvk::Holder<lvk::ComputePipelineHandle> lightTileDepthPipeline;

    lvk::Holder<lvk::ShaderModuleHandle> lightTileDepthHierarchyShader;
    lvk::Holder<lvk::ComputePipelineHandle> lightTileDepthHierarchyPipeline;

    lvk::Holder<lvk::ShaderModuleHandle> shadowOcclusionCullingShader;
    lvk::Holder<lvk::ComputePipelineHandle> shadowOcclusionCullingPipeline;

    ShadowOcclusionCulling(const std::unique_ptr<lvk::IContext>& ctx, const LoadedScene& scene, const VKMesh11& mesh)
        : draws(ctx, mesh.indirectBuffer_.drawCommands_.size())
    {
        draws.drawCommands_ = mesh.indirectBuffer_.drawCommands_;
        draws.drawData_ = mesh.drawData_;
        draws.uploadIndirectBuffer();

        boxes = ctx->createBuffer({.usage = lvk::BufferUsageBits_Storage,
                                   .storage = lvk::StorageType_Device,
                                   .size = scene.worldBoxes.size() * sizeof(BoundingBox),
                                   .data = scene.worldBoxes.data(),
                                   .debugName = "Shadow caster bounds"});

        lightTileDepth = ctx->createTexture({.format = lvk::Format_R_UI32,
                                             .dimensions = {RECEIVER_TILE_SIZE, RECEIVER_TILE_SIZE},
                                             .usage = lvk::TextureUsageBits_Storage,
                                             .numMipLevels = RECEIVER_MIP_LEVELS,
                                             .debugName = "light tile depth"});
        for (uint32_t mip = 0; mip < lightTileDepthMipViews.size(); ++mip)
        {
            lightTileDepthMipViews[mip] =
                ctx->createTextureView(lightTileDepth, {.mipLevel = mip}, "light tile depth hierarchy");
            hzb[mip] = lightTileDepthMipViews[mip].index();
        }

        lightTileDepthShader = loadRendererShader(ctx, "reproject_light_tile_depth.slang");
        lightTileDepthPipeline =
            ctx->createComputePipeline({.smComp = lightTileDepthShader, .debugName = "reproject_light_tile_depth"});

        lightTileDepthHierarchyShader = loadRendererShader(ctx, "build_light_tile_depth_hierarchy.slang");
        lightTileDepthHierarchyPipeline = ctx->createComputePipeline(
            {.smComp = lightTileDepthHierarchyShader, .debugName = "build_light_tile_depth_hierarchy"});

        shadowOcclusionCullingShader = loadRendererShader(ctx, "shadow_occlusion_culling.slang");
        shadowOcclusionCullingPipeline = ctx->createComputePipeline(
            {.smComp = shadowOcclusionCullingShader, .debugName = "shadow_occlusion_culling"});
    }

    void execute(const std::unique_ptr<lvk::IContext>& ctx, lvk::ICommandBuffer& buf, lvk::TextureHandle depth,
                 const VKMesh11& mesh, const mat4& cameraViewProj, const mat4& lightViewProj)
    {
        LVK_PROFILER_FUNCTION();
        const uint32_t count = static_cast<uint32_t>(draws.drawCommands_.size());
        if (!count) return;
        buf.cmdPushDebugGroupLabel("Build shadow receiver HZB", 0xffb080ff);

        // Reproject camera depth into light tiles.
        buf.cmdClearColorImage(lightTileDepth, {.uint32 = {0}});
        const struct
        {
            mat4 cameraClipToLightClip;
            uint32_t outputImage;
            uint32_t sceneDepth;
        } lightTileDepthPC = {.cameraClipToLightClip = lightViewProj * glm::inverse(cameraViewProj),
                              .outputImage = hzb[0],
                              .sceneDepth = depth.index()};

        const lvk::Dimensions dimensions = ctx->getDimensions(depth);
        buf.cmdBindComputePipeline(lightTileDepthPipeline);
        buf.cmdPushConstants(lightTileDepthPC);
        buf.cmdDispatch(dimensions.divide2DRoundUp(RECEIVER_GROUP_SIZE),
                        {.sampledImages = {depth}, .storageImages = {lvk::TextureHandle(lightTileDepth)}});

        // Build the max-depth hierarchy.
        buf.cmdBarrier({.srcStages = lvk::PipelineStageBits_Compute,
                        .srcAccess = lvk::AccessBits_ShaderWrite,
                        .dstStages = lvk::PipelineStageBits_Compute,
                        .dstAccess = lvk::AccessBits_ShaderRead | lvk::AccessBits_ShaderWrite});

        buf.cmdBindComputePipeline(lightTileDepthHierarchyPipeline);
        buf.cmdPushConstants(hzb);
        buf.cmdDispatch({1, 1, 1}, {.storageImages = {lvk::TextureHandle(lightTileDepth)}});
        buf.cmdPopDebugGroupLabel();

        // Cull shadow casters against receiver depth.
        const struct
        {
            mat4 lightViewProj;
            uint64_t commands, drawData, boxes;
            uint32_t count;
            std::array<uint32_t, RECEIVER_MIP_LEVELS> hzb;
        } pc = {.lightViewProj = lightViewProj,
                .commands = ctx->gpuAddress(draws.bufferIndirect_),
                .drawData = ctx->gpuAddress(mesh.bufferDrawData_),
                .boxes = ctx->gpuAddress(boxes),
                .count = count,
                .hzb = hzb};
        buf.cmdPushDebugGroupLabel("Shadow occlusion culling", 0xffb080ff);
        buf.cmdBarrier(
            {.srcStages = lvk::PipelineStageBits_AllCommands,
             .srcAccess = lvk::AccessBits_TransferWrite | lvk::AccessBits_IndirectRead | lvk::AccessBits_ShaderWrite,
             .dstStages = lvk::PipelineStageBits_Compute,
             .dstAccess = lvk::AccessBits_ShaderRead | lvk::AccessBits_ShaderWrite});
        buf.cmdBindComputePipeline(shadowOcclusionCullingPipeline);
        buf.cmdPushConstants(pc);
        buf.cmdDispatch({(count + OCCLUSION_GROUP_SIZE - 1) / OCCLUSION_GROUP_SIZE},
                        {.storageImages = {lvk::TextureHandle(lightTileDepth)},
                         .buffers = {draws.bufferIndirect_, mesh.bufferDrawData_, boxes}});
        buf.cmdBarrier({.srcStages = lvk::PipelineStageBits_Compute,
                        .srcAccess = lvk::AccessBits_ShaderWrite,
                        .dstStages = lvk::PipelineStageBits_DrawIndirect,
                        .dstAccess = lvk::AccessBits_IndirectRead});
        buf.cmdPopDebugGroupLabel();
    }
};
} // namespace FinalDemo
