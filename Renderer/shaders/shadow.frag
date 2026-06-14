//

#include <Renderer/shaders/shadow_common.sp>
#include <data/shaders/AlphaTest.sp>
#include <data/shaders/UtilsPBR.sp>

layout(location = 0) in flat uint materialId;

void main()
{
    MetallicRoughnessDataGPU mat = pc.materials.material[materialId];

    if (mat.emissiveFactorAlphaCutoff.w > 0.5)
    {
        discard;
    }
}
