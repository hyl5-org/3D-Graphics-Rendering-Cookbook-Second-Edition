#pragma once

struct VertexPosition
{
    float3 position;
};

// Matches PackedVertexAttribs in shared/Scene/VtxData.h (12 bytes).
struct VertexAttribute
{
    uint normal;  // Packed signed 10:10:10 normal.
    uint uv;      // Two packed 16-bit floats.
    uint tangent; // Packed signed 10:10:10 tangent with 2-bit handedness.
};

struct VerticesPositions
{
    VertexPosition vtx[];
};

struct VerticesAttributes
{
    VertexAttribute vtx[];
};

struct Indices
{
    uint idx[];
};

// Matches RayTracingMeshData; indexed by the TLAS instance's custom index.
struct RayTracingMeshData
{
    uint indexOffset;
    uint vertexOffset;
    uint materialId;
    uint padding;
};
