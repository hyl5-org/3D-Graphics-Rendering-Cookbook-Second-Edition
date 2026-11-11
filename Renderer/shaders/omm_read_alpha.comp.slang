#include "Bindless.slang"

struct PushConstants
{
    float *alpha;
    uint textureId;
    uint width;
    uint height;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x < pc.width && id.y < pc.height)
        pc.alpha[id.y * pc.width + id.x] = kTextures2D[pc.textureId].Load(int3(id.xy, 0)).a;
}
