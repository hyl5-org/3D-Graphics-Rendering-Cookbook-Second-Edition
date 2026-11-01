#pragma once

#include <lvk/LVK.h>
#include <string>

struct CooperativeDecoder
{
    lvk::Holder<lvk::BufferHandle> scalarWeights;
    lvk::Holder<lvk::BufferHandle> weights;
    uint32_t secondLayerOffset = 0;
    std::string status;
};

CooperativeDecoder createCooperativeDecoder(lvk::IContext *ctx, const float *firstWeights, const float *secondWeights);
