#include "shadow_occlusion_culling_config.h"
[[vk::binding(2, 0)]][format("r32ui")] RWTexture2D<uint> kRWTextures2DUint[];

struct PushConstants
{
    uint hzb[RECEIVER_MIP_LEVELS];
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

// One group covers the entire receiver grid. Ping-pong LDS avoids overwriting
// values that another lane still needs; no cross-workgroup counter is needed.
groupshared uint reduced[2][RECEIVER_REDUCE_THREADS];

[shader("compute")][numthreads(RECEIVER_REDUCE_THREADS, 1, 1)]
void main(uint lane : SV_GroupIndex)
{
    uint2 tile = uint2(lane % RECEIVER_REDUCE_SIZE, lane / RECEIVER_REDUCE_SIZE);
    uint value = 0;
    // Each lane produces four mip-1 texels and one mip-2 texel.
    for (uint y = 0; y < 2; ++y)
        for (uint x = 0; x < 2; ++x)
        {
            uint2 dst = tile * 2 + uint2(x, y);
            uint2 p = dst * 2;
            uint maxDepth = max(
                max(kRWTextures2DUint[pc.hzb[0]][p], kRWTextures2DUint[pc.hzb[0]][p + uint2(1, 0)]),
                max(kRWTextures2DUint[pc.hzb[0]][p + uint2(0, 1)], kRWTextures2DUint[pc.hzb[0]][p + uint2(1, 1)]));
            kRWTextures2DUint[pc.hzb[1]][dst] = maxDepth;
            value = max(value, maxDepth);
        }
    kRWTextures2DUint[pc.hzb[2]][tile] = value;
    reduced[0][lane] = value;
    GroupMemoryBarrierWithGroupSync();

    for (uint mip = 3; mip < RECEIVER_MIP_LEVELS; ++mip)
    {
        uint size = RECEIVER_TILE_SIZE >> mip;
        uint sourceBuffer = (mip - 3) & 1;
        if (lane < size * size)
        {
            uint2 dst = uint2(lane % size, lane / size);
            uint i = dst.y * 2 * (size * 2) + dst.x * 2;
            value = max(max(reduced[sourceBuffer][i], reduced[sourceBuffer][i + 1]),
                        max(reduced[sourceBuffer][i + size * 2], reduced[sourceBuffer][i + size * 2 + 1]));
            reduced[sourceBuffer ^ 1][lane] = value;
            kRWTextures2DUint[pc.hzb[mip]][dst] = value;
        }
        // All lanes participate, including those inactive at coarser levels.
        GroupMemoryBarrierWithGroupSync();
    }
}
