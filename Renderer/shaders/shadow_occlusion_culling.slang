#include "shadow_occlusion_culling_config.h"
#define RENDERER_COMPUTE 1
[[vk::binding(2, 0)]][format("r32ui")] RWTexture2D<uint> kRWTextures2DUint[];

struct Box { float min[3]; float max[3]; };
struct DrawData { uint transformId; uint materialId; };
struct Command { uint count; uint instanceCount; uint firstIndex; int baseVertex; uint baseInstance; };
struct PushConstants
{
    float4x4 lightViewProj;
    uint* commands;
    DrawData* drawData;
    Box* boxes;
    uint count;
    uint hzb[RECEIVER_MIP_LEVELS];
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

[shader("compute")][numthreads(OCCLUSION_GROUP_SIZE, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= pc.count) return;
    Command* commands = (Command*)(pc.commands + 1);
    Box box = pc.boxes[pc.drawData[commands[id.x].baseInstance].transformId];
    float3 rectMin = float3(1e30);
    float3 rectMax = float3(-1e30);
    // Project the bounding box into light NDC.
    for (uint i = 0; i < 8; ++i)
    {
        float3 p = float3((i & 1) != 0 ? box.max[0] : box.min[0],
                          (i & 2) != 0 ? box.max[1] : box.min[1],
                          (i & 4) != 0 ? box.max[2] : box.min[2]);
        float4 clip = mul(pc.lightViewProj, float4(p, 1));
        float3 q = clip.xyz / clip.w;
        rectMin = min(rectMin, q);
        rectMax = max(rectMax, q);
    }
    // Expand bounds to account for projection error.
    rectMin -= CASTER_BOUNDS_EPSILON;
    rectMax += CASTER_BOUNDS_EPSILON;

    if (any(rectMin > float3(1, 1, 1)) || any(rectMax < float3(-1, -1, 0)))
    {
        commands[id.x].instanceCount = 0;
        return;
    }

    uint4 hzbTexels = min(uint4(saturate(float4(rectMin.xy, rectMax.xy) * 0.5 + 0.5) * RECEIVER_TILE_SIZE),
                         uint4(RECEIVER_TILE_SIZE - 1));

    // Select the finest mip covering the rectangle with at most 2x2 texels.
    int2 mipLevelXY = firstbithigh(int2(hzbTexels.zw - hzbTexels.xy));
    uint mipLevel = uint(max(max(mipLevelXY.x, mipLevelXY.y), 0));
    // Power-of-two alignment can require one coarser level.
    mipLevel += any((hzbTexels.zw >> mipLevel) - (hzbTexels.xy >> mipLevel) > 1) ? 1u : 0u;
    hzbTexels >>= mipLevel;

    uint maxReceiverDepthEncoded = 0;
    for (uint y = hzbTexels.y; y <= hzbTexels.w; ++y)
        for (uint x = hzbTexels.x; x <= hzbTexels.z; ++x)
            maxReceiverDepthEncoded = max(maxReceiverDepthEncoded,
                kRWTextures2DUint[NonUniformResourceIndex(pc.hzb[mipLevel])][uint2(x, y)]);
    bool visible = maxReceiverDepthEncoded != 0 &&
        rectMin.z <= asfloat(maxReceiverDepthEncoded - 1) + RECEIVER_DEPTH_EPSILON;
    commands[id.x].instanceCount = visible ? 1 : 0;
}
