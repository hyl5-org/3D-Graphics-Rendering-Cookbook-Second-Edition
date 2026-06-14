//

#include <Renderer/shaders/shadow_common.sp>

layout(location = 0) in vec3 in_pos;
layout(location = 0) out flat uint materialId;

void main()
{
    mat4 model = pc.transforms.model[pc.drawData.dd[gl_BaseInstance].transformId];
    gl_Position = pc.viewProj * model * vec4(in_pos, 1.0);
    materialId = pc.drawData.dd[gl_BaseInstance].materialId;
}
