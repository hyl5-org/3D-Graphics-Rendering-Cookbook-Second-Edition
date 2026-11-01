#include "shadow_occlusion_culling_config.h"
#define RENDERER_COMPUTE 1
#include "Bindless.slang"
[[vk::binding(2, 0)]][format("r32ui")] RWTexture2D<uint> kRWTextures2DUint[];

struct PushConstants
{
    float4x4 cameraClipToLightClip;
    uint outputImage;
    uint sceneDepth;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

static const uint CACHE_SIZE = RECEIVER_GROUP_SIZE * RECEIVER_GROUP_SIZE;
groupshared uint cachedTiles[CACHE_SIZE];
groupshared uint cachedDepths[CACHE_SIZE];

uint2 reprojectLightTileDepth(float3 cameraNDC)
{
    uint2 depthLS = {RECEIVER_EMPTY_CELL, 0};
    // Reproject camera NDC into light clip space.
    float4 lightClip = mul(pc.cameraClipToLightClip, float4(cameraNDC, 1));
    float3 lightNDC = lightClip.xyz / lightClip.w;
    if (any(lightNDC < float3(-1, -1, 0)) || any(lightNDC > float3(1, 1, 1)))
    {
        return depthLS;
    }
    int2 tile = min(int2((lightNDC.xy * 0.5 + 0.5) * RECEIVER_TILE_SIZE), int2(RECEIVER_TILE_SIZE - 1));
    // Positive float bits preserve order. Reserve zero for empty cells, including at z=0.
    depthLS.y = asuint(min(lightNDC.z + RECEIVER_DEPTH_EPSILON, 1.0)) + 1;
    depthLS.x = tile.y * RECEIVER_TILE_SIZE + tile.x;
    return depthLS;
}
// Merge equal tile keys within a wave, then across the group, before global writes.
void writeLightTileDepth(uint2 depthLS, uint groupIndex)
{
    cachedTiles[groupIndex] = RECEIVER_EMPTY_CELL;
    cachedDepths[groupIndex] = 0;
    GroupMemoryBarrierWithGroupSync();

    uint pendingTile = depthLS.x;
    for (;;)
    {
        uint tileIndex = WaveActiveMin(pendingTile);
        if (tileIndex == RECEIVER_EMPTY_CELL) break;
        uint maxDepth = WaveActiveMax(pendingTile == tileIndex ? depthLS.y : 0u);
        if (WaveIsFirstLane())
        {
            uint slot = (tileIndex ^ (tileIndex / RECEIVER_TILE_SIZE)) % CACHE_SIZE;
            for (;;)
            {
                uint previousTile;
                InterlockedCompareExchange(cachedTiles[slot], RECEIVER_EMPTY_CELL, tileIndex, previousTile);
                if (previousTile == RECEIVER_EMPTY_CELL || previousTile == tileIndex)
                {
                    InterlockedMax(cachedDepths[slot], maxDepth);
                    break;
                }
                slot = (slot + 1) % CACHE_SIZE;
            }
        }
        if (pendingTile == tileIndex) pendingTile = RECEIVER_EMPTY_CELL;
    }

    GroupMemoryBarrierWithGroupSync();
    uint tileIndex = cachedTiles[groupIndex];
    if (tileIndex != RECEIVER_EMPTY_CELL)
    {
        uint2 tile = uint2(tileIndex % RECEIVER_TILE_SIZE, tileIndex / RECEIVER_TILE_SIZE);
        InterlockedMax(kRWTextures2DUint[pc.outputImage][tile], cachedDepths[groupIndex]);
    }
}

[shader("compute")][numthreads(RECEIVER_GROUP_SIZE, RECEIVER_GROUP_SIZE, 1)]
void main(uint3 id : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
    uint2 size = uint2(textureBindlessSize2D(pc.sceneDepth));
    uint2 pixel = id.xy;
    uint2 depthLS = {RECEIVER_EMPTY_CELL, 0};
    // Inactive pixels must still participate in the group barriers.
    if (all(pixel < size))
    {
        float depth = kTextures2D[pc.sceneDepth].Load(int3(pixel, 0)).r;
        if (depth < 1.0)
        {
            float2 uv = (float2(pixel) + 0.5) / float2(size);
            depthLS = reprojectLightTileDepth(float3(uv * float2(2, -2) + float2(-1, 1), depth));
        }
    }
    writeLightTileDepth(depthLS, groupIndex);
}
