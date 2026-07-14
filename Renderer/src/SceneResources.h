#pragma once

#include "DemoConfig.h"

#include "shared/Scene/Scene.h"
#include "shared/Scene/VtxData.h"
#include "shared/UtilsMath.h"

#include "scene/VKMesh11.h"

#include <array>
#include <vector>

namespace FinalDemo
{

struct LoadedScene
{
    MeshData meshData;
    Scene scene;
    std::vector<BoundingBox> worldBoxes;
    BoundingBox worldBounds;
};

struct CullingData
{
    vec4 frustumPlanes[6];
    vec4 frustumCorners[8];
    uint32_t numMeshesToCull = 0;
    uint32_t numVisibleMeshes = 0;
    uint32_t numVisibleTriangles = 0;
};
struct CullingDataXr
{
    CullingData cullingData[2];
};
struct CullingPushConstants
{
    uint64_t commands;
    uint64_t drawData;
    uint64_t AABBs;
    uint64_t meshes;
    uint32_t numMeshesToCull;
};

struct SceneDrawLists
{
    VKIndirectBuffer11 opaque;
    VKIndirectBuffer11 masked;
    VKIndirectBuffer11 transparent;
    uint64_t opaqueTriangles = 0;
    uint64_t maskedTriangles = 0;
    uint64_t transparentTriangles = 0;
    uint64_t sceneTriangles = 0;

    SceneDrawLists(const std::unique_ptr<lvk::IContext> &ctx, const MeshData &meshData, const VKMesh11 &mesh);

    static uint64_t countTriangles(const std::vector<DrawIndexedIndirectCommand> &commands);
};

struct CullingPipeline
{
    lvk::Holder<lvk::ShaderModuleHandle> comp;
    lvk::Holder<lvk::ComputePipelineHandle> pipeline;

    explicit CullingPipeline(const std::unique_ptr<lvk::IContext> &ctx);
};

struct SceneCulling
{
    lvk::Holder<lvk::BufferHandle> aabbs;
    std::array<lvk::Holder<lvk::BufferHandle>, 2> dataBuffers;
    std::array<lvk::SubmitHandle, 2> submitHandles = {};
    CullingPushConstants pc = {};
    uint32_t currentBufferId = 0;
    int numVisibleMeshes = 0;
    uint64_t numVisibleTriangles = 0;

    SceneCulling(const std::unique_ptr<lvk::IContext> &ctx, const LoadedScene &loadedScene, const VKMesh11 &mesh);

    //CullingData prepare(const mat4 &proj, const mat4 &cameraView, const SceneDrawLists &drawLists);
    CullingDataXr prepareXr(const mat4 &projLe, const mat4 &cameraViewLe,const mat4 &projRe, const mat4 &cameraViewRe, const SceneDrawLists &drawLists);

    // generate a conservative draw list for stereo rendering
    void executeXr(const std::unique_ptr<lvk::IContext> &ctx, lvk::ICommandBuffer &buf, const LoadedScene &loadedScene,
                 const VKMesh11 &mesh, SceneDrawLists &drawLists, lvk::ComputePipelineHandle pipeline,
                 CullingDataXr cullingData);

    void storeSubmitHandle(lvk::SubmitHandle handle);
    void retrieveGpuStats(const std::unique_ptr<lvk::IContext> &ctx, uint32_t numFrames);
};

LoadedScene loadDemoScene();
FrameTargets createGBufferTargets(const std::unique_ptr<lvk::IContext> &ctx, lvk::Format depthFormat,
                                  lvk::Dimensions sizeFb);
LightFrame buildLightFrame(const LightParams &light, const BoundingBox &sceneBounds);

} // namespace FinalDemo
