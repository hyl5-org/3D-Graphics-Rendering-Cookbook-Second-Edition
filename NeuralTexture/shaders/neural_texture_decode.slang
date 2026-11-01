static const int kFeatureCount = 8;
static const int kHiddenCount = 32;
static const int kChannelCount = 9;

struct Vertex
{
    float3 position;
    float3 normal;
    float4 tangent;
    float2 uv;
};

struct Frame
{
    float4x4 viewProj;
    float4 camera;
    uint grid;
    uint materials;
    uint size;
    uint source;
    uint channel;
    float width;
    uint manualGamma;
    uint secondLayerOffset;
};

struct PushConstants
{
    Frame *frame;
    Vertex *vertices;
    float *model;
    uint *reference;
    uint *weights;
    uint material;
    float cutoff;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

struct BilinearFootprint
{
    uint indices[4];
    float weights[4];
};

int wrap(int value, int size)
{
    return (value % size + size) % size;
}

// Both paths use periodic bilinear interpolation at texel centers, without mip filtering.
BilinearFootprint getBilinearFootprint(float2 uv, int size)
{
    float2 position = frac(uv) * size - 0.5;
    int2 cell = int2(floor(position));
    float2 fraction = frac(position);
    BilinearFootprint footprint;
    for (int corner = 0; corner < 4; ++corner)
    {
        int2 texel = cell + int2(corner & 1, corner >> 1);
        footprint.indices[corner] = (pc.material * size + wrap(texel.y, size)) * size + wrap(texel.x, size);
        footprint.weights[corner] =
            ((corner & 1) != 0 ? fraction.x : 1.0 - fraction.x) * ((corner >> 1) != 0 ? fraction.y : 1.0 - fraction.y);
    }
    return footprint;
}

void sampleReference(float2 uv, out float channels[kChannelCount])
{
    BilinearFootprint footprint = getBilinearFootprint(uv, int(pc.frame->size));
    for (int channel = 0; channel < kChannelCount; ++channel)
    {
        channels[channel] = 0.0;
        for (int corner = 0; corner < 4; ++corner)
        {
            uint address = footprint.indices[corner] * kChannelCount + channel;
            uint value = (pc.reference[address / 4] >> ((address % 4) * 8)) & 255;
            channels[channel] += footprint.weights[corner] * float(value) / 255.0;
        }
    }
}

void sampleLatent(float2 uv, out float features[kFeatureCount])
{
    BilinearFootprint footprint = getBilinearFootprint(uv, int(pc.frame->grid));
    for (int feature = 0; feature < kFeatureCount; ++feature)
    {
        features[feature] = 0.0;
        for (int corner = 0; corner < 4; ++corner)
        {
            features[feature] +=
                footprint.weights[corner] * pc.model[footprint.indices[corner] * kFeatureCount + feature];
        }
    }
}

float sigmoid(float value)
{
    return 1.0 / (1.0 + exp(-clamp(value, -20.0, 20.0)));
}

void decodeNeural(float2 uv, out float channels[kChannelCount])
{
    float features[kFeatureCount];
    sampleLatent(uv, features);

    uint weight1Offset = pc.frame->materials * pc.frame->grid * pc.frame->grid * kFeatureCount;
    uint bias1Offset = weight1Offset + kFeatureCount * kHiddenCount;
    uint weight2Offset = bias1Offset + kHiddenCount;
    uint bias2Offset = weight2Offset + kHiddenCount * kChannelCount;

#ifdef NTC_COOP_VECTOR
    CoopVec<half, kFeatureCount> input;
    for (int feature = 0; feature < kFeatureCount; ++feature)
    {
        input[feature] = half(features[feature]);
    }

    CoopVec<half, kHiddenCount> hidden = coopVecMatMul<half, kHiddenCount, kFeatureCount>(
        input, CoopVecComponentType.Float16, (void *)pc.weights, CoopVecComponentType.Float16,
        CoopVecMatrixLayout.InferencingOptimal, false, 0);
    for (int neuron = 0; neuron < kHiddenCount; ++neuron)
    {
        hidden[neuron] = half(max(float(hidden[neuron]) + pc.model[bias1Offset + neuron], 0.0));
    }

    CoopVec<half, kChannelCount> output = coopVecMatMul<half, kChannelCount, kHiddenCount>(
        hidden, CoopVecComponentType.Float16, (void *)(pc.weights + pc.frame->secondLayerOffset / 4),
        CoopVecComponentType.Float16, CoopVecMatrixLayout.InferencingOptimal, false, 0);
    for (int channel = 0; channel < kChannelCount; ++channel)
    {
        channels[channel] = sigmoid(float(output[channel]) + pc.model[bias2Offset + channel]);
    }
#elif defined(NTC_SCALAR_FP16)
    half *weights = (half *)pc.weights;
    half input[kFeatureCount];
    for (int feature = 0; feature < kFeatureCount; ++feature)
    {
        input[feature] = half(features[feature]);
    }
    half hidden[kHiddenCount];
    for (int neuron = 0; neuron < kHiddenCount; ++neuron)
    {
        half value = half(0);
        for (int feature = 0; feature < kFeatureCount; ++feature)
        {
            value += input[feature] * weights[feature * kHiddenCount + neuron];
        }
        hidden[neuron] = half(max(float(value) + pc.model[bias1Offset + neuron], 0.0));
    }
    for (int channel = 0; channel < kChannelCount; ++channel)
    {
        half value = half(0);
        for (int neuron = 0; neuron < kHiddenCount; ++neuron)
        {
            value += hidden[neuron] * weights[kFeatureCount * kHiddenCount + neuron * kChannelCount + channel];
        }
        channels[channel] = sigmoid(float(value) + pc.model[bias2Offset + channel]);
    }
#else
    float hidden[kHiddenCount];
    for (int neuron = 0; neuron < kHiddenCount; ++neuron)
    {
        float value = pc.model[bias1Offset + neuron];
        for (int feature = 0; feature < kFeatureCount; ++feature)
        {
            value += features[feature] * pc.model[weight1Offset + feature * kHiddenCount + neuron];
        }
        hidden[neuron] = max(value, 0.0);
    }
    for (int channel = 0; channel < kChannelCount; ++channel)
    {
        float value = pc.model[bias2Offset + channel];
        for (int neuron = 0; neuron < kHiddenCount; ++neuron)
        {
            value += hidden[neuron] * pc.model[weight2Offset + neuron * kChannelCount + channel];
        }
        channels[channel] = sigmoid(value);
    }
#endif
}
