#pragma once

// TODO(luhanyang): Enable when OMM bake output can be attached to BLAS geometry.
// #include "OmmSdk.h"
#include "shared/Scene/Scene.h"
#include "shared/Scene/VtxData.h"
#include "shared/UtilsMath.h"

#include <algorithm>
#include <inttypes.h>
#include <memory>
#include <vector>

namespace FinalDemo
{

struct RayTracingScene
{
    lvk::Holder<lvk::BufferHandle> blasTransformBuffer;
    lvk::Holder<lvk::BufferHandle> instancesBuffer;
    std::vector<lvk::Holder<lvk::AccelStructHandle>> blas;
    lvk::Holder<lvk::AccelStructHandle> tlas;

    bool valid() const
    {
        return tlas.valid();
    }
};

inline lvk::mat3x4 toAccelTransform(const mat4 &m)
{
    lvk::mat3x4 result = {};
    result.matrix[0][0] = m[0][0];
    result.matrix[0][1] = m[1][0];
    result.matrix[0][2] = m[2][0];
    result.matrix[0][3] = m[3][0];
    result.matrix[1][0] = m[0][1];
    result.matrix[1][1] = m[1][1];
    result.matrix[1][2] = m[2][1];
    result.matrix[1][3] = m[3][1];
    result.matrix[2][0] = m[0][2];
    result.matrix[2][1] = m[1][2];
    result.matrix[2][2] = m[2][2];
    result.matrix[2][3] = m[3][2];
    return result;
}

inline uint64_t bytesToMiB100(uint64_t bytes)
{
    return (bytes * 100) / (1024 * 1024);
}

inline constexpr uint32_t kMaxRayTracingBLAS = 5000;

inline RayTracingScene createRayTracingScene(const std::unique_ptr<lvk::IContext> &ctx, const MeshData &meshData,
                                             const Scene &scene, lvk::BufferHandle positionBuffer,
                                             lvk::BufferHandle indexBuffer)
{
    RayTracingScene rt;
    // sizeof(RGBA32F)
    const uint32_t vertexStride = meshData.positionOnlyStreams.inputBindings[0].stride;
    // RGBA32F
    const lvk::VertexFormat vertexFormat = meshData.positionOnlyStreams.attributes[0].format;
    const uint32_t totalVertices = static_cast<uint32_t>(meshData.positionData.size() / vertexStride);
    const bool allowCompaction = true;
    // const OmmSdkStatus ommStatus = probeOmmSdk();

    LLOGL(
        "RT AS: build BLAS from shared mesh buffers: meshes=%zu, maxBLAS=%u, vertices=%u, indexBytes=%zu, compact=%s\n",
        meshData.meshes.size(), kMaxRayTracingBLAS, totalVertices, meshData.indexData.size() * sizeof(uint32_t),
        allowCompaction ? "on" : "off");
    // if (ommStatus.compiled)
    // {
    //     LLOGL("RT OMM: NVIDIA SDK %u.%u.%u, status=%s\n", ommStatus.versionMajor, ommStatus.versionMinor,
    //           ommStatus.versionBuild, ommSdkStatusText(ommStatus));
    // }

    // const lvk::mat3x4 identity = toAccelTransform(mat4(1.0f));
    mat4 identity = mat4(1.0f);
    rt.blasTransformBuffer = ctx->createBuffer({
        .usage = lvk::BufferUsageBits_AccelStructBuildInputReadOnly,
        .storage = lvk::StorageType_HostVisible,
        .size = sizeof(identity),
        .data = &identity,
        .debugName = "Buffer: BLAS identity transform",
    });

    rt.blas.resize(meshData.meshes.size());
    std::vector<lvk::AccelStructHandle> blasHandles(meshData.meshes.size());
    std::vector<lvk::AccelStructDesc> blasDescs;
    std::vector<uint32_t> blasMeshIndices;
    uint32_t blasCount = 0;
    uint64_t blasPrimitiveCount = 0;
    uint64_t blasBuildBytes = 0;
    uint64_t blasFinalBytes = 0;
    uint64_t blasBuildScratchBytes = 0;

    for (uint32_t meshIndex = 0; meshIndex != meshData.meshes.size(); ++meshIndex)
    {
        if (blasCount >= kMaxRayTracingBLAS)
        {
            break;
        }

        const Mesh &mesh = meshData.meshes[meshIndex];
        const uint32_t lod = std::min(0u, mesh.lodCount - 1);
        const uint32_t indexCount = mesh.getLODIndicesCount(lod);
        if (indexCount < 3 || (indexCount % 3) != 0 || mesh.vertexCount == 0)
        {
            continue;
        }
        const uint64_t indexBufferOffset = uint64_t(mesh.indexOffset) * sizeof(uint32_t);

        lvk::AccelStructDesc blasDesc = {
            .type = lvk::AccelStructType_BLAS,
            .geometryType = lvk::AccelStructGeomType_Triangles,
            .vertexFormat = vertexFormat,
            .vertexBuffer = positionBuffer,
            .vertexStride = vertexStride,
            .numVertices = totalVertices,
            .indexFormat = lvk::IndexFormat_UI32,
            .indexBuffer = indexBuffer,
            .transformBuffer = rt.blasTransformBuffer,
            .buildRange =
                {
                    .primitiveCount = indexCount / 3,
                    .primitiveOffset = static_cast<uint32_t>(indexBufferOffset),
                    .firstVertex = mesh.vertexOffset,
                },
            .buildFlags = lvk::AccelStructBuildFlagBits_PreferFastTrace |
                          (allowCompaction ? lvk::AccelStructBuildFlagBits_AllowCompaction : 0),
            .debugName = "BLAS: Renderer mesh",
        };

        lvk::Result sizeResult;
        const lvk::AccelStructSizes sizes = ctx->getAccelStructSizes(blasDesc, &sizeResult);
        assert(sizeResult.isOk());

        blasDescs.push_back(blasDesc);
        blasMeshIndices.push_back(meshIndex);
        ++blasCount;
        blasPrimitiveCount += indexCount / 3;
        blasBuildBytes += sizes.accelerationStructureSize;
        blasBuildScratchBytes += sizes.buildScratchSize;
    }

    {
        std::vector<lvk::AccelStructHandle> batchHandles(blasDescs.size());
        lvk::Result batchResult;
        ctx->createBLASBatch(blasDescs.data(), batchHandles.data(), static_cast<uint32_t>(blasDescs.size()),
                             &batchResult);
        assert(batchResult.isOk());
        for (uint32_t i = 0; i != blasDescs.size(); ++i)
        {
            const uint32_t meshIndex = blasMeshIndices[i];
            rt.blas[meshIndex] = lvk::Holder<lvk::AccelStructHandle>(ctx.get(), batchHandles[i]);
            blasHandles[meshIndex] = batchHandles[i];
            blasFinalBytes += ctx->getAccelStructMemorySize(batchHandles[i]);
        }
    }

    std::vector<lvk::AccelStructInstance> instances;
    instances.reserve(scene.meshForNode.size());
    uint32_t instanceIndex = 0;
    for (const auto &nodeMesh : scene.meshForNode)
    {
        const uint32_t nodeIndex = nodeMesh.first;
        const uint32_t meshIndex = nodeMesh.second;
        if (!blasHandles[meshIndex].valid())
        {
            continue;
        }

        instances.push_back({
            .transform = toAccelTransform(scene.globalTransform[nodeIndex]),
            .instanceCustomIndex = instanceIndex++,
            .mask = 0xff,
            .instanceShaderBindingTableRecordOffset = 0,
            .flags = lvk::AccelStructInstanceFlagBits_TriangleFacingCullDisable,
            .accelerationStructureReference = ctx->gpuAddress(blasHandles[meshIndex]),
        });
    }

    if (instances.empty())
    {
        return rt;
    }

    rt.instancesBuffer = ctx->createBuffer({
        .usage = lvk::BufferUsageBits_AccelStructBuildInputReadOnly,
        .storage = lvk::StorageType_HostVisible,
        .size = sizeof(lvk::AccelStructInstance) * instances.size(),
        .data = instances.data(),
        .debugName = "Buffer: TLAS instances",
    });

    lvk::Result result;
    const lvk::AccelStructDesc tlasDesc = {
        .type = lvk::AccelStructType_TLAS,
        .geometryType = lvk::AccelStructGeomType_Instances,
        .instancesBuffer = rt.instancesBuffer,
        .buildRange = {.primitiveCount = static_cast<uint32_t>(instances.size())},
        .buildFlags = lvk::AccelStructBuildFlagBits_PreferFastTrace,
        .debugName = "TLAS: Renderer scene",
    };
    const lvk::AccelStructSizes tlasSizes = ctx->getAccelStructSizes(tlasDesc, &result);
    if (!result.isOk())
    {
        LLOGW("RT AS: failed to query TLAS size: %s\n", result.message);
        rt.blas.clear();
        return rt;
    }
    // LLOGL("RT AS: TLAS build instances=%zu as=%" PRIu64 " bytes scratch=%" PRIu64 " bytes\n",
    //       instances.size(),
    //       tlasSizes.accelerationStructureSize,
    //       tlasSizes.buildScratchSize);

    lvk::Result createResult;
    rt.tlas = ctx->createAccelerationStructure(tlasDesc, &createResult);
    if (!createResult.isOk() || !rt.tlas.valid())
    {
        LLOGW("Failed to create TLAS: %s\n", createResult.message);
        rt.blas.clear();
        return rt;
    }

    const uint64_t tlasFinalBytes = ctx->getAccelStructMemorySize(rt.tlas);
    const uint64_t blasBuildMiB100 = bytesToMiB100(blasBuildBytes);
    const uint64_t blasFinalMiB100 = bytesToMiB100(blasFinalBytes);
    const uint64_t blasScratchMiB100 = bytesToMiB100(blasBuildScratchBytes);
    const uint64_t totalFinalBytes = blasFinalBytes + tlasFinalBytes;
    const uint64_t totalFinalMiB100 = bytesToMiB100(totalFinalBytes);
    const uint64_t tlasFinalMiB100 = bytesToMiB100(tlasFinalBytes);

    LLOGL("RT AS: BLAS summary count=%u triangles=%llu preCompactBuildMemory=%llu bytes (%llu.%02llu MiB) "
          "finalMemory=%llu bytes (%llu.%02llu MiB) buildScratch=%llu bytes (%llu.%02llu MiB) compact=%s\n",
          blasCount, (unsigned long long)blasPrimitiveCount, (unsigned long long)blasBuildBytes,
          (unsigned long long)(blasBuildMiB100 / 100), (unsigned long long)(blasBuildMiB100 % 100),
          (unsigned long long)blasFinalBytes, (unsigned long long)(blasFinalMiB100 / 100),
          (unsigned long long)(blasFinalMiB100 % 100), (unsigned long long)blasBuildScratchBytes,
          (unsigned long long)(blasScratchMiB100 / 100), (unsigned long long)(blasScratchMiB100 % 100),
          allowCompaction ? "requested" : "disabled");
    LLOGL("RT AS: total AS memory=%llu bytes (%llu.%02llu MiB), BLAS=%llu bytes, TLAS=%llu bytes (%llu.%02llu MiB), "
          "TLAS instances=%zu\n",
          (unsigned long long)totalFinalBytes, (unsigned long long)(totalFinalMiB100 / 100),
          (unsigned long long)(totalFinalMiB100 % 100), (unsigned long long)blasFinalBytes,
          (unsigned long long)tlasFinalBytes, (unsigned long long)(tlasFinalMiB100 / 100),
          (unsigned long long)(tlasFinalMiB100 % 100), instances.size());

    return rt;
}

} // namespace FinalDemo
