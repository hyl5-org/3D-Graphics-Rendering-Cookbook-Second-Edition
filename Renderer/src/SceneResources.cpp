#include "SceneResources.h"

#include "scene/Bistro.h"

namespace FinalDemo
{

SceneDrawLists::SceneDrawLists(const std::unique_ptr<lvk::IContext> &ctx, const MeshData &meshData,
                               const VKMesh11 &mesh)
    : opaque(ctx, mesh.numMeshes_, lvk::StorageType_HostVisible),
      masked(ctx, mesh.numMeshes_, lvk::StorageType_HostVisible),
      transparent(ctx, mesh.numMeshes_, lvk::StorageType_HostVisible)
{
    auto isTransparent = [&meshData, &mesh](const DrawIndexedIndirectCommand &c) -> bool
    {
        const uint32_t mtlIndex = mesh.drawData_[c.baseInstance].materialId;
        const Material &mtl = meshData.materials[mtlIndex];
        return (mtl.flags & sMaterialFlags_Transparent) > 0;
    };
    auto isMasked = [&meshData, &mesh, &isTransparent](const DrawIndexedIndirectCommand &c) -> bool
    {
        const uint32_t mtlIndex = mesh.drawData_[c.baseInstance].materialId;
        const Material &mtl = meshData.materials[mtlIndex];
        return !isTransparent(c) && mtl.alphaTest > 0.0f && mtl.opacityTexture != -1 && mtl.baseColorTexture != -1;
    };

    mesh.indirectBuffer_.selectTo(opaque, mesh.drawData_,
                                  [&isTransparent, &isMasked](const DrawIndexedIndirectCommand &c) -> bool
                                  {
                                      return !isTransparent(c) && !isMasked(c);
                                  });
    mesh.indirectBuffer_.selectTo(masked, mesh.drawData_,
                                  [&isMasked](const DrawIndexedIndirectCommand &c) -> bool
                                  {
                                      return isMasked(c);
                                  });
    mesh.indirectBuffer_.selectTo(transparent, mesh.drawData_,
                                  [&isTransparent](const DrawIndexedIndirectCommand &c) -> bool
                                  {
                                      return isTransparent(c);
                                  });

    opaqueTriangles = countTriangles(opaque.drawCommands_);
    maskedTriangles = countTriangles(masked.drawCommands_);
    transparentTriangles = countTriangles(transparent.drawCommands_);
    sceneTriangles = opaqueTriangles + maskedTriangles + transparentTriangles;
}

uint64_t SceneDrawLists::countTriangles(const std::vector<DrawIndexedIndirectCommand> &commands)
{
    uint64_t triangles = 0;
    for (const DrawIndexedIndirectCommand &c : commands)
    {
        triangles += uint64_t(c.count / 3) * c.instanceCount;
    }
    return triangles;
}

CullingPipeline::CullingPipeline(const std::unique_ptr<lvk::IContext> &ctx)
{
    comp = loadShaderModule(ctx, "Renderer/shaders/FrustumCulling.comp");
    pipeline = ctx->createComputePipeline({.smComp = comp});
}

SceneCulling::SceneCulling(const std::unique_ptr<lvk::IContext> &ctx, const LoadedScene &loadedScene,
                           const VKMesh11 &mesh)
{
    aabbs = ctx->createBuffer({
        .usage = lvk::BufferUsageBits_Storage,
        .storage = lvk::StorageType_Device,
        .size = loadedScene.worldBoxes.size() * sizeof(BoundingBox),
        .data = loadedScene.worldBoxes.data(),
        .debugName = "Buffer: AABBs",
    });

    CullingData emptyCullingData;
    const lvk::BufferDesc cullingDataDesc = {
        .usage = lvk::BufferUsageBits_Storage,
        .storage = lvk::StorageType_HostVisible,
        .size = sizeof(CullingData),
        .data = &emptyCullingData,
        .debugName = "Buffer: CullingData 0",
    };
    dataBuffers[0] = ctx->createBuffer(cullingDataDesc, "Buffer: CullingData 0");
    dataBuffers[1] = ctx->createBuffer(cullingDataDesc, "Buffer: CullingData 1");

    pc = {
        .commands = 0,
        .drawData = ctx->gpuAddress(mesh.bufferDrawData_),
        .AABBs = ctx->gpuAddress(aabbs),
    };
}

CullingData SceneCulling::prepare(const mat4 &proj, const mat4 &cameraView, const SceneDrawLists &drawLists)
{
    if (!gSettings.culling.freezeView)
    {
        gSettings.culling.view = cameraView;
    }

    CullingData cullingData = {
        .numMeshesToCull = static_cast<uint32_t>(drawLists.opaque.drawCommands_.size()),
    };
    getFrustumPlanes(proj * gSettings.culling.view, cullingData.frustumPlanes);
    getFrustumCorners(proj * gSettings.culling.view, cullingData.frustumCorners);
    return cullingData;
}

namespace
{
uint32_t cullDrawListCPU(const std::unique_ptr<lvk::IContext> &ctx, const LoadedScene &loadedScene,
                         const VKIndirectBuffer11 &drawList, CullingData &cullingData, uint64_t &visibleTriangles)
{
    LVK_ASSERT(ctx->getMappedPtr(drawList.bufferIndirect_));
    uint32_t *drawCount = (uint32_t *)ctx->getMappedPtr(drawList.bufferIndirect_);
    DrawIndexedIndirectCommand *dst = drawList.getDrawIndexedIndirectCommandPtr();
    uint32_t visibleCommands = 0;
    for (size_t i = 0; i != drawList.drawCommands_.size(); i++)
    {
        DrawIndexedIndirectCommand c = drawList.drawCommands_[i];
        const BoundingBox box = loadedScene.worldBoxes[drawList.drawData_[c.baseInstance].transformId];
        if (isBoxInFrustum(cullingData.frustumPlanes, cullingData.frustumCorners, box))
        {
            c.instanceCount = 1;
            visibleCommands++;
            visibleTriangles += uint64_t(c.count / 3);
        }
        else
        {
            c.instanceCount = 0;
        }
        dst[i] = c;
    }
    *drawCount = static_cast<uint32_t>(drawList.drawCommands_.size());
    drawList.flushActiveCommands(static_cast<uint32_t>(drawList.drawCommands_.size()));
    return visibleCommands;
}

void dispatchCullDrawListGPU(const std::unique_ptr<lvk::IContext> &ctx, lvk::ICommandBuffer &buf,
                             const VKIndirectBuffer11 &drawList, CullingPushConstants &pc,
                             lvk::BufferHandle cullingDataBuffer)
{
    const uint32_t numCommands = static_cast<uint32_t>(drawList.drawCommands_.size());
    if (numCommands == 0)
    {
        return;
    }

    pc.commands = ctx->gpuAddress(drawList.bufferIndirect_);
    pc.drawData = ctx->gpuAddress(drawList.bufferDrawData_);
    pc.numMeshesToCull = numCommands;
    buf.cmdPushConstants(pc);
    buf.cmdUpdateBuffer(drawList.bufferIndirect_, numCommands);
    buf.cmdDispatchThreadGroups({1 + numCommands / 64},
                                {.buffers = {lvk::BufferHandle(drawList.bufferIndirect_),
                                             lvk::BufferHandle(drawList.bufferDrawData_), cullingDataBuffer}});
}
} // namespace

void SceneCulling::execute(const std::unique_ptr<lvk::IContext> &ctx, lvk::ICommandBuffer &buf,
                           const LoadedScene &loadedScene, const VKMesh11 &mesh, SceneDrawLists &drawLists,
                           lvk::ComputePipelineHandle pipeline, CullingData cullingData)
{
    if (gSettings.culling.mode == CullingMode_None)
    {
        numVisibleMeshes = static_cast<uint32_t>(loadedScene.scene.meshForNode.size());
        numVisibleTriangles = drawLists.sceneTriangles;
        drawLists.opaque.restoreAllCommands();
        drawLists.masked.restoreAllCommands();
    }
    else if (gSettings.culling.mode == CullingMode_CPU)
    {
        numVisibleMeshes = static_cast<uint32_t>(drawLists.transparent.drawCommands_.size());
        numVisibleTriangles = drawLists.transparentTriangles;
        numVisibleMeshes += cullDrawListCPU(ctx, loadedScene, drawLists.opaque, cullingData, numVisibleTriangles);
        numVisibleMeshes += cullDrawListCPU(ctx, loadedScene, drawLists.masked, cullingData, numVisibleTriangles);
    }
    else if (gSettings.culling.mode == CullingMode_GPU)
    {
        cullingData.numVisibleMeshes = static_cast<uint32_t>(drawLists.transparent.drawCommands_.size());
        cullingData.numVisibleTriangles = static_cast<uint32_t>(drawLists.transparentTriangles);

        buf.cmdBindComputePipeline(pipeline);
        pc.meshes = ctx->gpuAddress(dataBuffers[currentBufferId]);
        buf.cmdUpdateBuffer(dataBuffers[currentBufferId], cullingData);
        dispatchCullDrawListGPU(ctx, buf, drawLists.opaque, pc, dataBuffers[currentBufferId]);
        dispatchCullDrawListGPU(ctx, buf, drawLists.masked, pc, dataBuffers[currentBufferId]);
    }
}

void SceneCulling::storeSubmitHandle(lvk::SubmitHandle handle)
{
    submitHandles[currentBufferId] = handle;
}

void SceneCulling::retrieveGpuStats(const std::unique_ptr<lvk::IContext> &ctx, uint32_t numFrames)
{
    currentBufferId = (currentBufferId + 1) % dataBuffers.size();
    if (gSettings.culling.mode == CullingMode_GPU && numFrames > 1)
    {
        ctx->wait(submitHandles[currentBufferId]);
        ctx->download(dataBuffers[currentBufferId], &numVisibleMeshes, sizeof(uint32_t),
                      offsetof(CullingData, numVisibleMeshes));
        uint32_t visibleTriangles = 0;
        ctx->download(dataBuffers[currentBufferId], &visibleTriangles, sizeof(uint32_t),
                      offsetof(CullingData, numVisibleTriangles));
        numVisibleTriangles = visibleTriangles;
    }
}

LoadedScene loadDemoScene()
{
    LoadedScene loadedScene;
    loadBistro(loadedScene.meshData, loadedScene.scene);

    loadedScene.worldBoxes.resize(loadedScene.scene.globalTransform.size());
    for (auto &p : loadedScene.scene.meshForNode)
    {
        loadedScene.worldBoxes[p.first] =
            loadedScene.meshData.boxes[p.second].getTransformed(loadedScene.scene.globalTransform[p.first]);
    }

    loadedScene.worldBounds = loadedScene.worldBoxes.front();
    for (const auto &box : loadedScene.worldBoxes)
    {
        loadedScene.worldBounds.combinePoint(box.min_);
        loadedScene.worldBounds.combinePoint(box.max_);
    }
    return loadedScene;
}

FrameTargets createGBufferTargets(const std::unique_ptr<lvk::IContext> &ctx, lvk::Format depthFormat)
{
    const lvk::Dimensions sizeFb = ctx->getDimensions(ctx->getCurrentSwapchainTexture());
    return {
        .sizeFb = sizeFb,
        .sceneColor = ctx->createTexture({
            .format = lvk::Format::Format_R11G11B10_F,
            .dimensions = sizeFb,
            .usage = lvk::TextureUsageBits_Attachment | lvk::TextureUsageBits_Sampled,
            .debugName = "gbuffer0",
        }),
        .lightingColor = ctx->createTexture({
            .format = lvk::Format::Format_R11G11B10_F,
            .dimensions = sizeFb,
            .usage = lvk::TextureUsageBits_Attachment | lvk::TextureUsageBits_Sampled,
            .debugName = "lightingColor",
        }),
        .gbufferRT1 = ctx->createTexture({
            .format = lvk::Format::Format_A2B10G10R10_UN,
            .dimensions = sizeFb,
            .usage = lvk::TextureUsageBits_Attachment | lvk::TextureUsageBits_Sampled,
            .debugName = "gbuffer1",
        }),
        .gbufferRT2 = ctx->createTexture({
            .format = lvk::Format::Format_RGBA_UN8,
            .dimensions = sizeFb,
            .usage = lvk::TextureUsageBits_Attachment | lvk::TextureUsageBits_Sampled,
            .debugName = "gbuffer2",
        }),
        .gbufferRT3 = ctx->createTexture({
            .format = lvk::Format::Format_RGBA_UN8,
            .dimensions = sizeFb,
            .usage = lvk::TextureUsageBits_Attachment | lvk::TextureUsageBits_Sampled,
            .debugName = "gbuffer3",
        }),
        .opaqueDepth = ctx->createTexture({
            .format = depthFormat,
            .dimensions = sizeFb,
            .usage = lvk::TextureUsageBits_Attachment | lvk::TextureUsageBits_Sampled,
            .debugName = "opaqueDepth",
        }),
    };
}

LightFrame buildLightFrame(const LightParams &light, const BoundingBox &sceneBounds)
{
    const glm::mat4 rot1 = glm::rotate(mat4(1.f), glm::radians(light.theta), glm::vec3(0, 1, 0));
    const glm::mat4 rot2 = glm::rotate(rot1, glm::radians(light.phi), glm::vec3(1, 0, 0));
    const vec3 lightDir = glm::normalize(vec3(rot2 * vec4(0.0f, -1.0f, 0.0f, 1.0f)));
    const mat4 lightView = glm::lookAt(glm::vec3(0.0f), lightDir, vec3(0, 0, 1));
    const BoundingBox boxLS = sceneBounds.getTransformed(lightView);

    return {
        .dir = lightDir,
        .view = lightView,
        .proj = glm::orthoLH_ZO(boxLS.min_.x, boxLS.max_.x, boxLS.min_.y, boxLS.max_.y, boxLS.max_.z, boxLS.min_.z),
    };
}

} // namespace FinalDemo
