#include "neural_texture_decode.slang"

// Write decoded channels into the reference buffer for an offline scalar/CoopVec comparison.
[shader("compute")][numthreads(64, 1, 1)] void main(uint3 id : SV_DispatchThreadID)
{
    uint size = pc.frame->size;
    if (id.x >= size * size)
    {
        return;
    }
    float2 uv = (float2(id.x % size, id.x / size) + 0.5) / float(size);
    float channels[kChannelCount];
    decodeNeural(uv, channels);
    for (int channel = 0; channel < kChannelCount; ++channel)
    {
        pc.reference[id.x * kChannelCount + channel] = asuint(channels[channel]);
    }
}
