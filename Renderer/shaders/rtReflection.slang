[[vk::binding(4, 0)]] RaytracingAccelerationStructure kTLAS[128];
#include "Math.slang"
#include "Material.slang"
#include "Geometry.slang"
#include "Bindless.slang"
#include "Lighting.slang"

struct LightBufferData
{
    float4x4 viewProjBias;
    float4 lightDir;
    uint shadowTexture;
    uint shadowSampler;
    uint rtShadowEnabled;
    uint rtAOEnabled;
    uint rtAOSamples;
    float rtAORadius;
    float rtAOPower;
    float rtShadowStrength;
    float rtShadowRadius;
    uint frameIndex;
};
typealias LightBuffer = LightBufferData *;

struct PushConstants
{
    float4x4 invViewProj;
    float4 cameraPos;
    uint gbuffer1;
    uint depth;
    uint smpl;
    uint texBlueNoise;
    Materials *materials;
    Indices *indices;
    VerticesPositions *verticesPosition;
    VerticesAttributes *verticesAttributes;
    RayTracingMeshData *meshes;
    uint outTexture;
    uint envTex;
    uint texSkyboxIrradiance;
    uint texBRDF_LUT;
    uint tlas;
    uint enableShadows;
    uint maxDepth;
    LightBuffer light;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;
[[vk::binding(2, 0)]] RWTexture2D<float4> kTextures2DInOut[];

struct RayPayload
{
    float3 color;
    uint depth;
};
struct ShadowPayload
{
    bool isShadowed;
};

[shader("raygeneration")]
void rayGenMain()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 size = DispatchRaysDimensions().xy;
    float2 uv = (float2(pixel) + 0.5) / float2(size);
    // Load depth and normal from the same full-resolution pixel, without blending across silhouettes.
    int2 depthSize = textureBindlessSize2D(pc.depth);
    int2 sourcePixel = min(int2(uv * float2(depthSize)), depthSize - 1);
    float depth = kTextures2D[pc.depth].Load(int3(sourcePixel, 0)).r;
    if (depth >= 0.9999)
    {
        kTextures2DInOut[pc.outTexture][pixel] = float4(0.0);
        return;
    }
    float2 sourceUV = (float2(sourcePixel) + 0.5) / float2(depthSize);
    float3 n = decodeOctahedron(kTextures2D[pc.gbuffer1].Load(int3(sourcePixel, 0)).xy);
    float3 worldPos = reconstructWorldPosition(sourceUV, depth, pc.invViewProj);
    float3 viewDir = normalize(pc.cameraPos.xyz - worldPos);
    RayDesc ray;
    ray.Origin = worldPos + n * 0.001;
    ray.Direction = reflect(-viewDir, n);
    ray.TMin = 0.001;
    ray.TMax = 1e6;
    RayPayload payload;
    payload.color = float3(0.0);
    payload.depth = 0;
    float3 accumulatedColor = float3(0.0, 0.0, 0.0);
    //while(payload.depth < pc.maxDepth)
    {
        TraceRay(kTLAS[pc.tlas], RAY_FLAG_FORCE_OPAQUE, 0xff, 0, 0, 0, ray, payload);
        accumulatedColor += payload.color;
        payload.depth++;
    }
    // Unweighted incoming radiance. The receiving surface's Fresnel is applied in Lighting.
    kTextures2DInOut[pc.outTexture][pixel] = float4(accumulatedColor, 1.0);
}

[shader("miss")]
void missMain(inout RayPayload payload)
{
    // Match the environment coordinate convention used by the renderer's skybox.
    float3 dir = WorldRayDirection();
    payload.color = textureBindlessCubeLod(pc.envTex, 0, float3(-dir.x, dir.y, -dir.z), 0).rgb;
    payload.depth = 0xff;
}

[shader("miss")]
void missMainShadow(inout ShadowPayload payload)
{
    payload.isShadowed = false;
}

[shader("closesthit")]
void closestHitMain(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    float3 bary = float3(1.0 - attribs.barycentrics.x - attribs.barycentrics.y,
                         attribs.barycentrics.x, attribs.barycentrics.y);
    RayTracingMeshData mesh = pc.meshes[InstanceID()];
    uint index = mesh.indexOffset + 3 * PrimitiveIndex();
    uint3 triangle = uint3(pc.indices->idx[index], pc.indices->idx[index + 1], pc.indices->idx[index + 2])
                     + mesh.vertexOffset;
    VertexAttribute a0 = pc.verticesAttributes->vtx[triangle.x];
    VertexAttribute a1 = pc.verticesAttributes->vtx[triangle.y];
    VertexAttribute a2 = pc.verticesAttributes->vtx[triangle.z];
    float3 normal = unpackSnorm10x3(a0.normal) * bary.x + unpackSnorm10x3(a1.normal) * bary.y
                    + unpackSnorm10x3(a2.normal) * bary.z;
    float3 N = normalize(mul(normal, (float3x3)WorldToObject3x4()));
    float3 V = -normalize(WorldRayDirection());
    if (dot(N, V) < 0.0)
        N = -N;

    float2 uv0 = float2(f16tof32(a0.uv & 0xffffu), 1.0 - f16tof32(a0.uv >> 16));
    float2 uv1 = float2(f16tof32(a1.uv & 0xffffu), 1.0 - f16tof32(a1.uv >> 16));
    float2 uv2 = float2(f16tof32(a2.uv & 0xffffu), 1.0 - f16tof32(a2.uv >> 16));
    float2 uv = uv0 * bary.x + uv1 * bary.y + uv2 * bary.z;
    MetallicRoughnessDataGPU mat = pc.materials->mtl[mesh.materialId];
    float3 baseColor = mat.baseColorFactor.rgb;
    if (mat.baseColorTexture != 0)
        baseColor *= textureBindless2DLod(mat.baseColorTexture, mat.baseColorTextureSampler, uv, 0).rgb;

    float metallic = mat.metallicRoughnessNormalOcclusion.x;
    float roughness = mat.metallicRoughnessNormalOcclusion.y;
    if (mat.metallicRoughnessTexture != 0)
    {
        float4 mr = textureBindless2DLod(mat.metallicRoughnessTexture, mat.metallicRoughnessTextureSampler, uv, 0);
        metallic *= mr.b;
        roughness *= mr.g;
    }
    float3 emissive = mat.emissiveFactorAlphaCutoff.rgb;
    if (mat.emissiveTexture != 0)
        emissive *= textureBindless2DLod(mat.emissiveTexture, mat.emissiveTextureSampler, uv, 0).rgb;

    float3 L = -normalize(pc.light->lightDir.xyz);
    // Reflected surfaces are currently shaded without shadow tests.
    PBRLighting lighting = evaluatePBRLighting(baseColor, metallic, roughness, N, V, L,
                                               1.0, 1.0, pc.texSkyboxIrradiance,
                                               pc.envTex, pc.texBRDF_LUT, pc.smpl);
    payload.color = emissive + lighting.total();
}
