#pragma once
#include "Bindless.slang"
#include "Geometry.slang"
#include "Material.slang"

// Shared by RayQuery candidates and RT pipeline hit shaders; barycentrics are (u, v).
float sampleRayTracingAlpha(RayTracingMeshData mesh, uint primitiveIndex, float2 barycentrics,
                           Indices *indices, VerticesAttributes *verticesAttributes, Materials *materials)
{
    uint index = mesh.indexOffset + 3 * primitiveIndex;
    uint3 triangle = uint3(indices->idx[index], indices->idx[index + 1], indices->idx[index + 2])
                     + mesh.vertexOffset;
    uint packedUV0 = verticesAttributes->vtx[triangle.x].uv;
    uint packedUV1 = verticesAttributes->vtx[triangle.y].uv;
    uint packedUV2 = verticesAttributes->vtx[triangle.z].uv;
    float2 uv0 = float2(f16tof32(packedUV0 & 0xffffu), 1.0 - f16tof32(packedUV0 >> 16));
    float2 uv1 = float2(f16tof32(packedUV1 & 0xffffu), 1.0 - f16tof32(packedUV1 >> 16));
    float2 uv2 = float2(f16tof32(packedUV2 & 0xffffu), 1.0 - f16tof32(packedUV2 >> 16));
    float3 bary = float3(1.0 - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);
    float2 uv = uv0 * bary.x + uv1 * bary.y + uv2 * bary.z;
    MetallicRoughnessDataGPU mat = materials->mtl[mesh.materialId];
    float alpha = mat.baseColorFactor.a;
    if (mat.baseColorTexture != 0)
        alpha *= textureBindless2DLod(mat.baseColorTexture, mat.baseColorTextureSampler, uv, 0).a;
    return alpha;
}
