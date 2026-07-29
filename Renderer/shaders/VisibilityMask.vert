#version 460

#extension GL_EXT_buffer_reference : require

layout(buffer_reference, std430) readonly buffer VisibilityMaskVertices
{
    vec2 positions[];
};

layout(push_constant) uniform PushConstants
{
    mat4 projection;
    VisibilityMaskVertices vertices;
} pc;

void main()
{
    // OpenXR supplies mask vertices in the z=-1 plane of view space.
    gl_Position = pc.projection * vec4(pc.vertices.positions[gl_VertexIndex], -1.0, 1.0);
}
