//

layout(push_constant) uniform PerFrameData
{
    mat4 mvp[2];
    uint texSkybox;
}
pc;

layout(location = 0) out vec3 dir;

const vec3 pos[8] = vec3[8](vec3(-1.0, -1.0, 1.0), vec3(1.0, -1.0, 1.0), vec3(1.0, 1.0, 1.0), vec3(-1.0, 1.0, 1.0),

                            vec3(-1.0, -1.0, -1.0), vec3(1.0, -1.0, -1.0), vec3(1.0, 1.0, -1.0), vec3(-1.0, 1.0, -1.0));

const int indices[36] = int[36](0, 1, 2, 2, 3, 0, // front
                                1, 5, 6, 6, 2, 1, // right
                                7, 6, 5, 5, 4, 7, // back
                                4, 0, 3, 3, 7, 4, // left
                                4, 5, 1, 1, 0, 4, // bottom
                                3, 2, 6, 6, 7, 3  // top
);

void main()
{
    int idx = indices[gl_VertexIndex];
    gl_Position = pc.mvp[gl_ViewIndex] * vec4(1.0 * pos[idx], 1.0);
    gl_Position.z = gl_Position.w;
    dir = pos[idx].xyz;
}
