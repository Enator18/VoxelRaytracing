#version 460
#extension GL_EXT_shader_8bit_storage : enable
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_nonuniform_qualifier : require

#define MAP_SIZE 64
#define PI 3.1415926538

layout(location = 0) in vec3 rayDir;
layout(location = 0) out vec4 outFragColor;

layout (buffer_reference, scalar) readonly buffer MapBuffer
{
    uint8_t cells[];
};

layout (push_constant, scalar) uniform PushConstants
{
    layout (offset = 44) vec3 playerPos;
    MapBuffer mapBuffer;
    ivec2 screenSize;
    uint frameNum;
} pcs;

layout(set = 0, binding = 0) uniform texture2D textures[];

layout(set = 0, binding = 1) uniform sampler textureSampler;

bool InBounds(int val)
{
    return val >= 0 && val < MAP_SIZE;
}

bool InBounds(ivec3 val)
{
    return InBounds(val.x) && InBounds(val.y) && InBounds(val.z);
}

void TraceRay(inout vec3 pos, vec3 dir, out vec2 uv, out uint cell, out vec3 normal)
{
    ivec3 step = ivec3(sign(dir));
    ivec3 gridPos = ivec3(floor(pos));

    vec3 stepSize = 1 / abs(dir);

    vec3 rayLength;
    vec3 frac = pos - vec3(gridPos);

    rayLength = mix((1 - frac) * stepSize, frac * stepSize, lessThan(dir, vec3(0)));

    float distance;

    while (true)
    {
        ivec3 axis =
        ivec3(
            rayLength.x <= rayLength.y && rayLength.x <= rayLength.z,
            rayLength.y <= rayLength.x && rayLength.y <= rayLength.z,
            rayLength.z <= rayLength.x && rayLength.z <= rayLength.y
        );

        gridPos += step * axis;
        distance = length(rayLength * axis);
        rayLength += stepSize * axis;

        if (!InBounds(gridPos))
        {
            cell = 0;
            return;
        }

        cell = uint(pcs.mapBuffer.cells[gridPos.z * MAP_SIZE * MAP_SIZE + gridPos.y * MAP_SIZE + gridPos.x]);
        if (cell != 0)
        {
            normal = -step * axis;
            pos += distance * dir;
            mat3x2 uvMat =
            {
                {pos.y, pos.z},
                {pos.x, pos.z},
                {pos.x, pos.y},
            };
            uv = fract(uvMat * normal);
            return;
        }
    }
}

float RandomValue(inout uint seed)
{
    seed = seed * 747796405 + 2891336453;
    uint result = ((seed >> ((seed >> 28) + 4)) ^ seed) * 277803737;
    result = (result >> 22) ^ result;
    return result / 4294967295.0;
}

float RandomNormalValue(inout uint seed)
{
    float theta = 2 * 3.1415926 * RandomValue(seed);
    float rho = sqrt(-2 * log(RandomValue(seed)));
    return rho * cos(theta);
}

vec3 RandomDirection(inout uint seed)
{
    float x = RandomNormalValue(seed);
    float y = RandomNormalValue(seed);
    float z = RandomNormalValue(seed);
    return normalize(vec3(x, y, z));
}

void SampleRay(vec3 pos, vec3 dir, out vec3 color, inout uint seed)
{
    vec2 uv;
    uint cell;
    vec3 normal;
    color = vec3(1);

    for (uint i = 0; i < 3; i++)
    {
        TraceRay(pos, dir, uv, cell, normal);
        if (cell != 0)
        {
            color *= texture(sampler2D(textures[nonuniformEXT(cell)], textureSampler), uv).xyz;
            if (cell == 4)
            {
                return;
            }
            vec3 randDir = RandomDirection(seed);

            dir = normalize(normal + randDir);
        }
        else
        {
            break;
        }
    }
    color = vec3(0);
}

void main()
{
    uvec2 uCoord = uvec2(gl_FragCoord.xy);
    uint seed = uCoord.x + (uCoord.y + pcs.frameNum * pcs.screenSize.y) * pcs.screenSize.x;
    vec3 pos = pcs.playerPos;
    vec3 rayDirNorm = normalize(rayDir);
    vec2 uv;
    uint cell;
    vec3 normal;
    TraceRay(pos, rayDirNorm, uv, cell, normal);
    if (cell != 0)
    {
        vec3 texColor = texture(sampler2D(textures[nonuniformEXT(cell)], textureSampler), uv).xyz;
        if (cell == 4)
        {
            vec3 hdr = texColor;
            vec3 mapped = 1 - exp(-hdr * 2);
            outFragColor = vec4(mapped, 1);
            return;
        }
        vec3 totalBounceColor = vec3(0);
        for (uint i = 0; i < 64; i++)
        {
            vec3 dir = RandomDirection(seed);
            vec3 cosDir = normalize(normal + dir);
            vec3 color;
            SampleRay(pos, cosDir, color, seed);
            totalBounceColor += color;
        }
        vec3 hdr = texColor * totalBounceColor * PI / 64;
        vec3 mapped = 1 - exp(-hdr * 2);
        outFragColor = vec4(mapped, 1);
    }
    else
    {
        outFragColor = vec4(0, 0, 0, 1);
    }
}
