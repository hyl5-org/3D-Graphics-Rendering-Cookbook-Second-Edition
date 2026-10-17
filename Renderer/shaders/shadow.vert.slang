#include "Bindless.slang"

#include "ShadowMesh.slang"

struct VertexInput
{
    [[vk::location(0)]] float3 in_pos : TEXCOORD0;
};
struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] nointerpolation uint materialId : TEXCOORD0;
};
[shader("vertex")] VertexOutput main(VertexInput input, uint baseInstance : SV_StartInstanceLocation) {
    VertexOutput output;

    float4x4 model = pc.transforms[pc.drawData[baseInstance].transformId];
    output.position = mul(pc.viewProj, mul(model, float4(input.in_pos, 1.0)));
    output.materialId = pc.drawData[baseInstance].materialId;

    return output;
}
