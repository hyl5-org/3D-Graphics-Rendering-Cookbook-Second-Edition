#include "Bindless.slang"

struct PerFrameData
{
    float4x4 mvp;
    uint texSkybox;
};
[[vk::push_constant]] ConstantBuffer<PerFrameData> pc;

static const float3 pos[8] = {
    float3(-1.0, -1.0, 1.0),  float3(1.0, -1.0, 1.0),  float3(1.0, 1.0, 1.0),  float3(-1.0, 1.0, 1.0),

    float3(-1.0, -1.0, -1.0), float3(1.0, -1.0, -1.0), float3(1.0, 1.0, -1.0), float3(-1.0, 1.0, -1.0)};

static const int indices[36] = {0, 1, 2, 2, 3, 0, 1, 5, 6, 6, 2, 1, 7, 6, 5, 5, 4, 7,
                                4, 0, 3, 3, 7, 4, 4, 5, 1, 1, 0, 4, 3, 2, 6, 6, 7, 3};

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float3 dir : TEXCOORD0;
};
[shader("vertex")] VertexOutput main(uint vertexId : SV_VertexID) {
    VertexOutput output;

    int idx = indices[vertexId];
    output.position = mul(pc.mvp, float4(1.0 * pos[idx], 1.0));
    output.position.z = output.position.w;
    output.dir = pos[idx].xyz;

    return output;
}
