#include "Bindless.slang"

#include "Mesh.slang"

struct VertexInput
{
    [[vk::location(0)]] float3 in_pos : TEXCOORD0;
    [[vk::location(1)]] float4 in_normal_packed : TEXCOORD1;
    [[vk::location(2)]] uint in_tc_packed : TEXCOORD2;
};
struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float3 normal : TEXCOORD1;
    [[vk::location(2)]] float3 worldPos : TEXCOORD2;
    [[vk::location(3)]] nointerpolation uint materialId : TEXCOORD3;
    [[vk::location(4)]] float4 shadowCoords : TEXCOORD4;
};
[shader("vertex")] VertexOutput main(VertexInput input, uint drawId : SV_DrawIndex) {
    VertexOutput output;

    DrawData draw = pc.drawData[drawId];
    float4x4 model = pc.transforms[draw.transformId];
    float2 in_tc = float2(f16tof32(input.in_tc_packed & 0xffffu), f16tof32(input.in_tc_packed >> 16));
    float3 in_normal = normalize(input.in_normal_packed.xyz);
    output.position = mul(pc.viewProj, mul(model, float4(input.in_pos, 1.0)));
    output.uv = float2(in_tc.x, 1.0 - in_tc.y);
    output.normal = normalize(mul(float3x3(model), in_normal));
    float4 posClip = mul(model, float4(input.in_pos, 1.0));
    output.worldPos = posClip.xyz / posClip.w;
    output.materialId = draw.materialId;

    output.shadowCoords = mul(pc.light->viewProjBias, posClip);

    return output;
}
