#include "CooperativeDecoder.h"

#include <cstring>
#include <glm/gtc/packing.hpp>
#include <lvk/vulkan/VulkanUtils.h>
#include <stdexcept>
#include <vector>

namespace
{
void checkVk(VkResult result)
{
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("Cooperative Vector setup failed: " + std::to_string(result));
    }
}

std::vector<char> convertWeights(VkDevice device, PFN_vkConvertCooperativeVectorMatrixNV convert, const float *source,
                                 uint32_t rows, uint32_t columns)
{
    size_t size = 0;
    VkConvertCooperativeVectorMatrixInfoNV info = {
        .sType = VK_STRUCTURE_TYPE_CONVERT_COOPERATIVE_VECTOR_MATRIX_INFO_NV,
        .srcSize = size_t(rows) * columns * sizeof(float),
        .srcData = {.hostAddress = source},
        .pDstSize = &size,
        .srcComponentType = VK_COMPONENT_TYPE_FLOAT32_KHR,
        .dstComponentType = VK_COMPONENT_TYPE_FLOAT16_KHR,
        .numRows = rows,
        .numColumns = columns,
        .srcLayout = VK_COOPERATIVE_VECTOR_MATRIX_LAYOUT_COLUMN_MAJOR_NV,
        .srcStride = rows * sizeof(float),
        .dstLayout = VK_COOPERATIVE_VECTOR_MATRIX_LAYOUT_INFERENCING_OPTIMAL_NV,
    };
    checkVk(convert(device, &info));
    std::vector<char> result(size);
    info.dstData.hostAddress = result.data();
    checkVk(convert(device, &info));
    return result;
}
} // namespace

CooperativeDecoder createCooperativeDecoder(lvk::IContext *ctx, const float *firstWeights, const float *secondWeights)
{
    CooperativeDecoder decoder;
    std::vector<uint16_t> scalarWeights(8 * 32 + 32 * 9);
    for (size_t i = 0; i < 8 * 32; ++i)
    {
        scalarWeights[i] = glm::packHalf1x16(firstWeights[i]);
    }
    for (size_t i = 0; i < 32 * 9; ++i)
    {
        scalarWeights[8 * 32 + i] = glm::packHalf1x16(secondWeights[i]);
    }
    decoder.scalarWeights = ctx->createBuffer({.usage = lvk::BufferUsageBits_Storage,
                                               .storage = lvk::StorageType_Device,
                                               .size = scalarWeights.size() * sizeof(uint16_t),
                                               .data = scalarWeights.data(),
                                               .debugName = "NTC scalar FP16 weights"});
    if (!decoder.scalarWeights.valid())
    {
        throw std::runtime_error("Cannot create scalar FP16 weights");
    }
    const VkDevice device = lvk::getVkDevice(ctx);
    const PFN_vkConvertCooperativeVectorMatrixNV convert = reinterpret_cast<PFN_vkConvertCooperativeVectorMatrixNV>(
        vkGetDeviceProcAddr(device, "vkConvertCooperativeVectorMatrixNV"));
    if (!convert || !vkGetPhysicalDeviceCooperativeVectorPropertiesNV)
    {
        decoder.status = "VK_NV_cooperative_vector is unavailable; using scalar FP16 decoding.";
        return decoder;
    }

    const VkPhysicalDevice physicalDevice = lvk::getVkPhysicalDevice(ctx);
    // Slang uses replicated composites when constructing cooperative vectors.
    VkPhysicalDeviceShaderReplicatedCompositesFeaturesEXT replicated = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_REPLICATED_COMPOSITES_FEATURES_EXT};
    VkPhysicalDeviceFeatures2 features = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &replicated};
    vkGetPhysicalDeviceFeatures2(physicalDevice, &features);
    if (!replicated.shaderReplicatedComposites)
    {
        decoder.status = "CoopVec shader requires replicated composites; using scalar FP16 decoding.";
        return decoder;
    }
    VkPhysicalDeviceCooperativeVectorPropertiesNV properties = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_VECTOR_PROPERTIES_NV};
    VkPhysicalDeviceProperties2 properties2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
                                               .pNext = &properties};
    vkGetPhysicalDeviceProperties2(physicalDevice, &properties2);
    if (!(properties.cooperativeVectorSupportedStages & VK_SHADER_STAGE_FRAGMENT_BIT) ||
        properties.maxCooperativeVectorComponents < 32)
    {
        decoder.status = "Fragment-stage CoopVec with 32 components is unavailable; using scalar FP16 decoding.";
        return decoder;
    }

    uint32_t count = 0;
    checkVk(vkGetPhysicalDeviceCooperativeVectorPropertiesNV(physicalDevice, &count, nullptr));
    std::vector<VkCooperativeVectorPropertiesNV> types(count,
                                                       {.sType = VK_STRUCTURE_TYPE_COOPERATIVE_VECTOR_PROPERTIES_NV});
    checkVk(vkGetPhysicalDeviceCooperativeVectorPropertiesNV(physicalDevice, &count, types.data()));
    bool supportsFP16 = false;
    for (const VkCooperativeVectorPropertiesNV &type : types)
    {
        supportsFP16 |= type.inputType == VK_COMPONENT_TYPE_FLOAT16_KHR &&
                        type.inputInterpretation == VK_COMPONENT_TYPE_FLOAT16_KHR &&
                        type.matrixInterpretation == VK_COMPONENT_TYPE_FLOAT16_KHR &&
                        type.resultType == VK_COMPONENT_TYPE_FLOAT16_KHR;
    }
    if (!supportsFP16)
    {
        decoder.status = "FP16 CoopVec multiplication is unavailable; using scalar FP16 decoding.";
        return decoder;
    }

    const std::vector<char> first = convertWeights(device, convert, firstWeights, 32, 8);
    const std::vector<char> second = convertWeights(device, convert, secondWeights, 9, 32);
    decoder.secondLayerOffset = uint32_t((first.size() + 63) & ~size_t(63));
    std::vector<char> packed(decoder.secondLayerOffset + second.size());
    std::memcpy(packed.data(), first.data(), first.size());
    std::memcpy(packed.data() + decoder.secondLayerOffset, second.data(), second.size());
    decoder.weights = ctx->createBuffer({.usage = lvk::BufferUsageBits_Storage,
                                         .storage = lvk::StorageType_Device,
                                         .size = packed.size(),
                                         .data = packed.data(),
                                         .debugName = "NTC FP16 inference-optimal weights"});
    if (!decoder.weights.valid() || ctx->gpuAddress(decoder.weights) % 64 != 0)
    {
        throw std::runtime_error("CoopVec weights require a 64-byte aligned buffer address");
    }
    decoder.status = "FP16 Cooperative Vector available (inference-optimal weights).";
    return decoder;
}
