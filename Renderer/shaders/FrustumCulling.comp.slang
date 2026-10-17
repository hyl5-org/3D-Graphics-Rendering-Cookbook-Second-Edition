#define RENDERER_COMPUTE 1
#include "Bindless.slang"

struct AABB
{
    float pt[6];
};

struct DrawIndexedIndirectCommand
{
    uint count;
    uint instanceCount;
    uint firstIndex;
    int baseVertex;
    uint baseInstance;
};

struct DrawData
{
    uint transformId;
    uint materialId;
};

typealias BoundingBoxes = AABB *;

typealias DrawDataBuffer = DrawData *;

typealias DrawCommands = uint *;

struct CullingDataData
{
    float4 planes[6];
    float4 corners[8];
    uint numMeshesToCull;
    uint numVisibleMeshes;
    uint numVisibleTriangles;
};
typealias CullingData = CullingDataData *;

struct PushConstants
{
    DrawCommands commands;
    DrawDataBuffer drawData;
    BoundingBoxes AABBs;
    CullingData frustum;
    uint numMeshesToCull;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

#define Box_min_x box.pt[0]
#define Box_min_y box.pt[1]
#define Box_min_z box.pt[2]
#define Box_max_x box.pt[3]
#define Box_max_y box.pt[4]
#define Box_max_z box.pt[5]

bool isAABBinFrustum(AABB box)
{
    for (int i = 0; i < 6; i++)
    {
        int r = 0;
        r += (dot(pc.frustum->planes[i], float4(Box_min_x, Box_min_y, Box_min_z, 1.0f)) < 0) ? 1 : 0;
        r += (dot(pc.frustum->planes[i], float4(Box_max_x, Box_min_y, Box_min_z, 1.0f)) < 0) ? 1 : 0;
        r += (dot(pc.frustum->planes[i], float4(Box_min_x, Box_max_y, Box_min_z, 1.0f)) < 0) ? 1 : 0;
        r += (dot(pc.frustum->planes[i], float4(Box_max_x, Box_max_y, Box_min_z, 1.0f)) < 0) ? 1 : 0;
        r += (dot(pc.frustum->planes[i], float4(Box_min_x, Box_min_y, Box_max_z, 1.0f)) < 0) ? 1 : 0;
        r += (dot(pc.frustum->planes[i], float4(Box_max_x, Box_min_y, Box_max_z, 1.0f)) < 0) ? 1 : 0;
        r += (dot(pc.frustum->planes[i], float4(Box_min_x, Box_max_y, Box_max_z, 1.0f)) < 0) ? 1 : 0;
        r += (dot(pc.frustum->planes[i], float4(Box_max_x, Box_max_y, Box_max_z, 1.0f)) < 0) ? 1 : 0;
        if (r == 8)
        {
            return false;
        }
    }

    int r = 0;
    r = 0;
    for (int i = 0; i < 8; i++)
    {
        r += ((pc.frustum->corners[i].x > Box_max_x) ? 1 : 0);
    }
    if (r == 8)
    {
        return false;
    }
    r = 0;
    for (int i = 0; i < 8; i++)
    {
        r += ((pc.frustum->corners[i].x < Box_min_x) ? 1 : 0);
    }
    if (r == 8)
    {
        return false;
    }
    r = 0;
    for (int i = 0; i < 8; i++)
    {
        r += ((pc.frustum->corners[i].y > Box_max_y) ? 1 : 0);
    }
    if (r == 8)
    {
        return false;
    }
    r = 0;
    for (int i = 0; i < 8; i++)
    {
        r += ((pc.frustum->corners[i].y < Box_min_y) ? 1 : 0);
    }
    if (r == 8)
    {
        return false;
    }
    r = 0;
    for (int i = 0; i < 8; i++)
    {
        r += ((pc.frustum->corners[i].z > Box_max_z) ? 1 : 0);
    }
    if (r == 8)
    {
        return false;
    }
    r = 0;
    for (int i = 0; i < 8; i++)
    {
        r += ((pc.frustum->corners[i].z < Box_min_z) ? 1 : 0);
    }
    if (r == 8)
    {
        return false;
    }

    return true;
}

[shader("compute")][numthreads(64, 1, 1)] void main(uint3 dispatchId : SV_DispatchThreadID) {
    // Indirect buffers start with a 32-bit draw count, followed by 20-byte commands.
    DrawIndexedIndirectCommand *drawCommands = (DrawIndexedIndirectCommand *)(pc.commands + 1);
    const uint idx = dispatchId.x;

    if (idx < pc.numMeshesToCull)
    {
        uint baseInstance = drawCommands[idx].baseInstance;
        AABB box = pc.AABBs[pc.drawData[baseInstance].transformId];
        uint numInstances = isAABBinFrustum(box) ? 1 : 0;
        drawCommands[idx].instanceCount = numInstances;
        InterlockedAdd(pc.frustum->numVisibleMeshes, numInstances);
        InterlockedAdd(pc.frustum->numVisibleTriangles, numInstances * (drawCommands[idx].count / 3));
    }
}
