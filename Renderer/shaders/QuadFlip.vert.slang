#include "Bindless.slang"

// Keep the PSO benchmark's vertex specialization independent of fragment gain.
[[vk::constant_id(1)]] const float psoScale = 1.0;

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};
[shader("vertex")] VertexOutput main(uint vertexId : SV_VertexID) {
    VertexOutput output;

    output.uv = float2((vertexId << 1) & 2, vertexId & 2);

    output.position = float4(output.uv * float2(2, -2) + float2(-1, 1), 0.0, 1.0);
    output.position.xy *= psoScale;

    return output;
}
