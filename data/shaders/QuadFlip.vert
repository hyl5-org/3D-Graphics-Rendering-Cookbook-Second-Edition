//
#version 460

layout(location = 0) out vec2 uv;

// Default preserves normal rendering; PSO benchmark specializes this independently of PS gain.
layout(constant_id = 1) const float psoScale = 1.0;

// https://www.saschawillems.de/blog/2016/08/13/vulkan-tutorial-on-rendering-a-fullscreen-quad-without-buffers/
void main()
{
    uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);

    gl_Position = vec4(uv * vec2(2, -2) + vec2(-1, 1), 0.0, 1.0);
    gl_Position.xy *= psoScale;
}
