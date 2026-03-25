#version 460
#extension GL_EXT_scalar_block_layout : require

vec4 vertices[3] =
{
    vec4(-1, -1, 0, 1),
    vec4(-1, 3, 0, 1),
    vec4(3, -1, 0, 1),
};

vec2 screenPos[3] =
{
    vec2(-1, 1),
    vec2(-1, -3),
    vec2(3, 1)
};

layout(location = 0) out vec4 rayDir;

layout (push_constant, scalar) uniform PushConstants
{
    mat4 playerRot;
    vec2 planeSize;
} pcs;

void main()
{
    gl_Position = vertices[gl_VertexIndex];
    vec2 pos = screenPos[gl_VertexIndex];
    rayDir = pcs.playerRot * vec4(1, pcs.planeSize * pos, 0);
}
