struct PushConstants
{
    float4x4 projection;
    float2* vertices;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

[shader("vertex")]
float4 main(uint vertexId : SV_VertexID) : SV_Position
{
    // OpenXR supplies mask vertices in the z=-1 plane of view space.
    return mul(pc.projection, float4(pc.vertices[vertexId], -1.0, 1.0));
}
