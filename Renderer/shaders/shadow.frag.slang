#include "Bindless.slang"

#include "ShadowMesh.slang"
#include "Surface.slang"

struct FragmentInput
{
    float4 position : SV_Position;
    [[vk::location(0)]] nointerpolation uint materialId : TEXCOORD0;
};
[shader("fragment")] void main(FragmentInput input) {
    MetallicRoughnessDataGPU mat = pc.materials[input.materialId];

    if (mat.emissiveFactorAlphaCutoff.w > 0.5)
    {
        discard;
    }
}
