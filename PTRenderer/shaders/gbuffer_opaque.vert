//

#extension GL_ARB_shader_draw_parameters : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require

#include <PTRenderer/src/common.sp>

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec4 in_normal_packed;
layout(location = 2) in uint in_tc_packed;
// layout(location = 3) in vec4 in_tangent_packed;

layout(location = 0) out f16vec2 uv;
layout(location = 1) out f16vec3 normal;
layout(location = 2) out vec3 worldPos;
layout(location = 3) out flat uint materialId;

void main()
{
    DrawData draw = pc.drawData.dd[gl_DrawID];
    mat4 model = pc.transforms.model[draw.transformId];
    vec2 in_tc = unpackHalf2x16(in_tc_packed);
    vec3 in_normal = normalize(in_normal_packed.xyz);
    vec4 world = model * vec4(in_pos, 1.0);

    gl_Position = pc.viewProj * world;
    uv = f16vec2(in_tc.x, 1.0 - in_tc.y);
    normal = f16vec3(normalize(mat3(model) * in_normal));
    worldPos = world.xyz / world.w;
    materialId = draw.materialId;
}
