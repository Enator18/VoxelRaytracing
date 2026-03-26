#version 460
#extension GL_EXT_shader_8bit_storage : enable
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_nonuniform_qualifier : require

#define MAP_SIZE 128

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

void TraceRay(vec3 origin, vec3 dir, out vec2 uv, out uint cell, out vec3 hitPos, out vec3 normal)
{
    ivec3 step = ivec3(sign(dir));
    ivec3 gridPos = ivec3(floor(origin));
    vec3 stepSize =
    {
        sqrt(1 + pow(dir.y / dir.x, 2) + pow(dir.z / dir.x, 2)),
        sqrt(1 + pow(dir.x / dir.y, 2) + pow(dir.z / dir.y, 2)),
        sqrt(1 + pow(dir.x / dir.z, 2) + pow(dir.y / dir.z, 2))
    };

    if (isnan(stepSize.x))
    {
        stepSize.x = 1 / 0;
    }
    if (isnan(stepSize.y))
    {
        stepSize.y = 1 / 0;
    }
    if (isnan(stepSize.z))
    {
        stepSize.z = 1 / 0;
    }

    vec3 rayLength;
    vec3 frac = origin - vec3(gridPos);

    if (dir.x < 0)
    {
        rayLength.x = frac.x * stepSize.x;
    }
    else
    {
        rayLength.x = (1 - frac.x) * stepSize.x;
    }
    if (dir.y < 0)
    {
        rayLength.y = frac.y * stepSize.y;
    }
    else
    {
        rayLength.y = (1 - frac.y) * stepSize.y;
    }
    if (dir.z < 0)
    {
        rayLength.z = frac.z * stepSize.z;
    }
    else
    {
        rayLength.z = (1 - frac.z) * stepSize.z;
    }

    float distance;
    vec2 hitCoord;

    while (true)
    {
        if (rayLength.x < rayLength.y && rayLength.x < rayLength.z)
        {
            gridPos.x += step.x;
            distance = rayLength.x;
            hitCoord = vec2(origin.y + distance * dir.y, origin.z + distance * dir.z);
            rayLength.x += stepSize.x;
            normal = vec3(-step.x, 0, 0);
        }
        else if (rayLength.y < rayLength.z)
        {
            gridPos.y += step.y;
            distance = rayLength.y;
            hitCoord = vec2(origin.x + distance * dir.x, origin.z + distance * dir.z);
            rayLength.y += stepSize.y;
            normal = vec3(0, -step.y, 0);
        }
        else
        {
            gridPos.z += step.z;
            distance = rayLength.z;
            hitCoord = vec2(origin.x + distance * dir.x, origin.y + distance * dir.y);
            rayLength.z += stepSize.z;
            normal = vec3(0, 0, -step.z);
        }

        if (!InBounds(gridPos))
        {
            cell = 0;
            return;
        }

        cell = uint(pcs.mapBuffer.cells[gridPos.z * MAP_SIZE * MAP_SIZE + gridPos.y * MAP_SIZE + gridPos.x]);
        if (cell != 0)
        {
            uv = fract(hitCoord);
            hitPos = origin + distance * dir;
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

void main()
{
    vec2 uv;
    uint cell;
    vec3 hitPos;
    vec3 normal;
    vec3 rayDirNorm = normalize(rayDir);
    vec3 shadowDir = normalize(vec3(2, 3, 4));
    TraceRay(pcs.playerPos, rayDirNorm, uv, cell, hitPos, normal);
    if (cell != 0)
    {
        vec4 color = texture(sampler2D(textures[nonuniformEXT(cell)], textureSampler), uv);
        vec3 shadowOrigin = hitPos;
        float light = 0.2;
        vec3 unused;
        TraceRay(shadowOrigin, shadowDir, uv, cell, hitPos, unused);
        if (cell == 0)
        {
            light += 0.8 * max(0, dot(normal, shadowDir));
        }
        outFragColor = color * light;
    }
    else
    {
        outFragColor = vec4(0, 0, 0, 1);
    }
}
