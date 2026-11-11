#include "OmmSdk.h"

#ifdef COOKBOOK_WITH_RTX_OMM
#include "Shader.h"
#include <lvk/vulkan/VulkanClasses.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace FinalDemo
{

OmmBaker::OmmBaker(const std::unique_ptr<lvk::IContext> &ctx) : ctx_(ctx)
{
    if (!ctx_->supportsOpacityMicromap())
        return;

    VkPhysicalDeviceOpacityMicromapPropertiesKHR micromapProperties = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_PROPERTIES_KHR,
    };
    VkPhysicalDeviceProperties2 properties = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &micromapProperties,
    };
    const lvk::VulkanContext &vk = static_cast<const lvk::VulkanContext &>(*ctx_);
    vkGetPhysicalDeviceProperties2(vk.getVkPhysicalDevice(), &properties);
    maxSubdivisionLevel_ = static_cast<uint8_t>(std::min(5u, micromapProperties.maxOpacity4StateSubdivisionLevel));
    maxMicromapTriangles_ = micromapProperties.maxMicromapTriangles;

    ommBakerCreationDesc creation = ommBakerCreationDescDefault();
    creation.type = ommBakerType_CPU;
    if (ommCreateBaker(&creation, &baker_) != ommResult_SUCCESS)
        return;

    readAlphaShader_ = loadRendererShader(ctx_, "omm_read_alpha.comp.slang");
    if (readAlphaShader_.valid())
        readAlpha_ = ctx_->createComputePipeline({.smComp = readAlphaShader_, .debugName = "OMM: read texture alpha"});
}

OmmBaker::~OmmBaker()
{
    for (const auto &entry : textures_)
        if (entry.second)
            ommCpuDestroyTexture(baker_, entry.second);
    if (baker_)
        ommDestroyBaker(baker_);
}

ommCpuTexture OmmBaker::getTexture(const MeshData &meshData, uint32_t materialId)
{
    const auto [it, inserted] = textures_.try_emplace(materialId, nullptr);
    if (!inserted)
        return it->second;

    const Material &material = meshData.materials[materialId];
    if (material.baseColorTexture < 0 || size_t(material.baseColorTexture) >= meshData.textureFiles.size())
        return nullptr;
    // Read the same cached texture the renderer samples, including GPU decompression.
    const lvk::Holder<lvk::TextureHandle> texture = loadTexture(ctx_, meshData.textureFiles[material.baseColorTexture].c_str());
    if (!texture.valid())
        return nullptr;

    const lvk::Dimensions size = ctx_->getDimensions(texture);
    std::vector<float> alpha(size_t(size.width) * size.height);
    const size_t bytes = alpha.size() * sizeof(float);
    const lvk::Holder<lvk::BufferHandle> readback = ctx_->createBuffer({
        .usage = lvk::BufferUsageBits_Storage,
        .storage = lvk::StorageType_HostVisible,
        .size = bytes,
        .debugName = "OMM: alpha readback",
    });
    if (!readback.valid())
        return nullptr;

    const struct
    {
        uint64_t alpha;
        uint32_t textureId;
        uint32_t width;
        uint32_t height;
    } pc = {ctx_->gpuAddress(readback), texture.index(), size.width, size.height};

    lvk::ICommandBuffer &cmd = ctx_->acquireCommandBuffer();
    cmd.cmdBindComputePipeline(readAlpha_);
    cmd.cmdPushConstants(pc);
    cmd.cmdDispatch(size.divide2DRoundUp(8), {.sampledImages = {texture}, .buffers = {readback}});
    cmd.cmdBarrier({
        .srcStages = lvk::PipelineStageBits_Compute,
        .srcAccess = lvk::AccessBits_ShaderWrite,
        .dstStages = lvk::PipelineStageBits_Host,
        .dstAccess = lvk::AccessBits_HostRead,
    });
    ctx_->wait(ctx_->submit(cmd));
    if (!ctx_->download(readback, alpha.data(), bytes, 0).isOk())
        return nullptr;

    // SDK row pitch is measured in texels. Zero selects tightly packed rows.
    const ommCpuTextureMipDesc mip = {size.width, size.height, 0, alpha.data()};
    ommCpuTextureDesc desc = ommCpuTextureDescDefault();
    desc.format = ommCpuTextureFormat_FP32;
    desc.mips = &mip;
    desc.mipCount = 1;
    if (ommCpuCreateTexture(baker_, &desc, &it->second) != ommResult_SUCCESS)
        it->second = nullptr;
    return it->second;
}

OmmGeometry OmmBaker::bake(const MeshData &meshData, const Mesh &mesh, uint32_t indexCount)
{
    if (!baker_ || !readAlpha_.valid() || size_t(mesh.indexOffset) + indexCount > meshData.indexData.size())
        return {};
    const Material &material = meshData.materials[mesh.materialID];
    const float factor = material.baseColorFactor.a;
    if (!std::isfinite(factor) || !std::isfinite(material.alphaTest) || factor <= 0.0f)
        return {};

    const uint32_t *indices = meshData.indexData.data() + mesh.indexOffset;
    const size_t vertexCount = size_t(*std::max_element(indices, indices + indexCount)) + 1;
    if (size_t(mesh.vertexOffset) + vertexCount > meshData.attributeData.size() / sizeof(PackedVertexAttribs))
        return {};

    const ommCpuTexture texture = getTexture(meshData, mesh.materialID);
    if (!texture)
        return {};

    std::vector<vec2> uvs(vertexCount);
    for (size_t i = 0; i < vertexCount; ++i)
    {
        uint32_t packedUV;
        const size_t offset = (size_t(mesh.vertexOffset) + i) * sizeof(PackedVertexAttribs);
        std::memcpy(&packedUV, meshData.attributeData.data() + offset + offsetof(PackedVertexAttribs, uv), sizeof(packedUV));
        uvs[i] = glm::unpackHalf2x16(packedUV);
        uvs[i].y = 1.0f - uvs[i].y;
    }

    ommCpuBakeInputDesc input = ommCpuBakeInputDescDefault();
    input.bakeFlags = static_cast<ommCpuBakeFlags>(ommCpuBakeFlags_Force32BitIndices | ommCpuBakeFlags_EnableInternalThreads);
    input.texture = texture;
    input.runtimeSamplerDesc.addressingMode = ommTextureAddressMode_Wrap;
    input.runtimeSamplerDesc.filter = ommTextureFilterMode_Linear;
    input.alphaMode = ommAlphaMode_Test;
    input.texCoordFormat = ommTexCoordFormat_UV32_FLOAT;
    input.texCoords = uvs.data();
    input.texCoordStrideInBytes = sizeof(vec2);
    input.indexFormat = ommIndexFormat_UINT_32;
    input.indexBuffer = indices;
    input.indexCount = indexCount;
    // SDK uses > cutoff, while the hit shader accepts equality.
    input.alphaCutoff = std::min(1.0f, std::nextafter(material.alphaTest / factor, 0.0f));
    input.maxSubdivisionLevel = maxSubdivisionLevel_;
    input.maxArrayDataSize = 16u << 20;
    input.maxWorkloadSize = 1ull << 28;

    ommCpuBakeResult result = nullptr;
    if (ommCpuBake(baker_, &input, &result) != ommResult_SUCCESS)
        return {};
    std::unique_ptr<_ommCpuBakeResult, decltype(&ommCpuDestroyBakeResult)> ownedResult(result, ommCpuDestroyBakeResult);
    const ommCpuBakeResultDesc *baked = nullptr;
    if (ommCpuGetBakeResultDesc(result, &baked) != ommResult_SUCCESS || !baked)
        return {};
    if (!baked->descArrayCount)
    {
        // LVK needs an actual micromap object even when the bake uses only whole-triangle special indices.
        input.bakeFlags = static_cast<ommCpuBakeFlags>(input.bakeFlags | ommCpuBakeFlags_DisableSpecialIndices);
        result = nullptr;
        if (ommCpuBake(baker_, &input, &result) != ommResult_SUCCESS)
            return {};
        ownedResult.reset(result);
        if (ommCpuGetBakeResultDesc(result, &baked) != ommResult_SUCCESS || !baked)
            return {};
    }
    if (!baked->descArrayCount || baked->descArrayCount > maxMicromapTriangles_ ||
        baked->indexFormat != ommIndexFormat_UINT_32 || baked->indexCount != indexCount / 3)
        return {};

    std::vector<lvk::OpacityMicromapTriangle> triangles;
    triangles.reserve(baked->descArrayCount);
    for (uint32_t i = 0; i < baked->descArrayCount; ++i)
    {
        const ommCpuOpacityMicromapDesc &desc = baked->descArray[i];
        triangles.push_back({desc.offset, desc.subdivisionLevel, desc.format});
    }
    std::vector<lvk::OpacityMicromapUsage> usage;
    usage.reserve(baked->descArrayHistogramCount);
    for (uint32_t i = 0; i < baked->descArrayHistogramCount; ++i)
    {
        const ommCpuOpacityMicromapUsageCount &entry = baked->descArrayHistogram[i];
        usage.push_back({entry.count, entry.subdivisionLevel, static_cast<lvk::OpacityMicromapFormat>(entry.format)});
    }
    const auto upload = [this](size_t bytes, const void *data, const char *name)
    {
        return ctx_->createBuffer({
            .usage = lvk::BufferUsageBits_AccelStructBuildInputReadOnly,
            .storage = lvk::StorageType_Device,
            .size = bytes,
            .data = data,
            .debugName = name,
        });
    };
    const lvk::Holder<lvk::BufferHandle> data = upload(baked->arrayDataSize, baked->arrayData, "OMM: states");
    const lvk::Holder<lvk::BufferHandle> descriptors =
        upload(triangles.size() * sizeof(triangles[0]), triangles.data(), "OMM: triangles");
    OmmGeometry geometry;
    geometry.indices = upload(baked->indexCount * sizeof(uint32_t), baked->indexBuffer, "OMM: indices");
    if (!data.valid() || !descriptors.valid() || !geometry.indices.valid())
        return {};

    geometry.micromap = ctx_->createAccelerationStructure({
        .type = lvk::AccelStructType_OpacityMicromap,
        .micromapData = data,
        .micromapTriangles = descriptors,
        .micromapUsageCounts = usage.data(),
        .micromapNumUsageCounts = static_cast<uint32_t>(usage.size()),
        .buildFlags = lvk::AccelStructBuildFlagBits_PreferFastTrace,
        .debugName = "OMM: masked mesh",
    });
    return geometry;
}

} // namespace FinalDemo
#endif
