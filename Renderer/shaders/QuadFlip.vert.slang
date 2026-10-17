#include "Bindless.slang"

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};
[shader("vertex")] VertexOutput main(uint vertexId : SV_VertexID) {
    VertexOutput output;

    output.uv = float2((vertexId << 1) & 2, vertexId & 2);

    output.position = float4(output.uv * float2(2, -2) + float2(-1, 1), 0.0, 1.0);

    return output;
}
