//

#extension GL_ARB_shader_draw_parameters : require

#include <Renderer/src/common.sp>

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec4 in_normal_packed;
layout(location = 2) in uint in_tc_packed;
// layout (location=3) in vec4 in_tangent_packed;

layout(location = 0) out vec2 uv;
layout(location = 1) out vec3 normal;
layout(location = 2) out vec3 worldPos;
layout(location = 3) out flat uint materialId;
layout(location = 4) out vec4 shadowCoords;

void main()
{
    DrawData draw = pc.drawData.dd[gl_DrawID];
    mat4 model = pc.transforms.model[draw.transformId];
    vec2 in_tc = unpackHalf2x16(in_tc_packed);
    vec3 in_normal = normalize(in_normal_packed.xyz);
    gl_Position = pc.viewProj[gl_ViewIndex] * model * vec4(in_pos, 1.0);
    uv = vec2(in_tc.x, 1.0 - in_tc.y);
    normal = normalize(mat3(model) * in_normal);
    vec4 posClip = model * vec4(in_pos, 1.0);
    worldPos = posClip.xyz / posClip.w;
    materialId = draw.materialId;

    shadowCoords = pc.light.viewProjBias * posClip;
}
