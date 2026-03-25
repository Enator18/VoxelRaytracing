#version 460
#extension GL_EXT_shader_8bit_storage : enable
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_nonuniform_qualifier : require

#define MAP_SIZE 64

layout(location = 0) in vec4 rayDir;
layout(location = 0) out vec4 outFragColor;

layout (buffer_reference, scalar) readonly buffer MapBuffer
{
    uint8_t cells[];
};

layout (push_constant, scalar) uniform PushConstants
{
    layout (offset = 72) vec4 playerPos;
    MapBuffer mapBuffer;
} pcs;

layout(set = 0, binding = 0) uniform texture3D textures[];

layout(set = 0, binding = 1) uniform sampler textureSampler;

bool InBounds(int val)
{
    return val >= 0 && val < MAP_SIZE;
}

bool InBounds(ivec4 val)
{
    return InBounds(val.x) && InBounds(val.y) && InBounds(val.z) && InBounds(val.w);
}

void TraceRay(vec4 origin, vec4 dir, out vec3 uvt, out uint cell, out vec4 hitPos, out vec4 normal)
{
    ivec4 step = ivec4(sign(dir));
    ivec4 gridPos = ivec4(floor(origin));
    vec4 stepSize =
    {
        sqrt(1 + pow(dir.y / dir.x, 2) + pow(dir.z / dir.x, 2) + pow(dir.w / dir.x, 2)),
        sqrt(1 + pow(dir.x / dir.y, 2) + pow(dir.z / dir.y, 2) + pow(dir.w / dir.y, 2)),
        sqrt(1 + pow(dir.x / dir.z, 2) + pow(dir.y / dir.z, 2) + pow(dir.w / dir.z, 2)),
        sqrt(1 + pow(dir.x / dir.w, 2) + pow(dir.y / dir.w, 2) + pow(dir.z / dir.w, 2))
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
    if (isnan(stepSize.w))
    {
        stepSize.w = 1 / 0;
    }

    vec4 rayLength;
    vec4 frac = origin - vec4(gridPos);

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
    if (dir.w < 0)
    {
        rayLength.w = frac.w * stepSize.w;
    }
    else
    {
        rayLength.w = (1 - frac.w) * stepSize.w;
    }

    float distance;
    vec3 hitCoord;

    while (true)
    {
        if (rayLength.x < rayLength.y && rayLength.x < rayLength.z && rayLength.x < rayLength.w)
        {
            gridPos.x += step.x;
            distance = rayLength.x;
            hitCoord = vec3(origin.y + distance * dir.y, origin.z + distance * dir.z, origin.w + distance * dir.w);
            rayLength.x += stepSize.x;
            normal = vec4(-step.x, 0, 0, 0);
        }
        else if (rayLength.y < rayLength.z && rayLength.y < rayLength.w)
        {
            gridPos.y += step.y;
            distance = rayLength.y;
            hitCoord = vec3(origin.x + distance * dir.x, origin.z + distance * dir.z, origin.w + distance * dir.w);
            rayLength.y += stepSize.y;
            normal = vec4(0, -step.y, 0, 0);
        }
        else if (rayLength.z < rayLength.w)
        {
            gridPos.z += step.z;
            distance = rayLength.z;
            hitCoord = vec3(origin.x + distance * dir.x, origin.y + distance * dir.y, origin.w + distance * dir.w);
            rayLength.z += stepSize.z;
            normal = vec4(0, 0, -step.z, 0);
        }
        else
        {
            gridPos.w += step.w;
            distance = rayLength.w;
            hitCoord = vec3(origin.x + distance * dir.x, origin.y + distance * dir.y, origin.z + distance * dir.z);
            rayLength.w += stepSize.w;
            normal = vec4(0, 0, 0, -step.w);
        }

        if (!InBounds(gridPos))
        {
            cell = 0;
            return;
        }

        cell = uint(pcs.mapBuffer.cells[gridPos.w * MAP_SIZE * MAP_SIZE * MAP_SIZE +
            gridPos.z * MAP_SIZE * MAP_SIZE + gridPos.y * MAP_SIZE + gridPos.x]);
        if (cell != 0)
        {
            uvt = fract(hitCoord);
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

vec4 RandomDirection(inout uint seed)
{
    float x = RandomNormalValue(seed);
    float y = RandomNormalValue(seed);
    float z = RandomNormalValue(seed);
    float w = RandomNormalValue(seed);
    return normalize(vec4(x, y, z, w));
}

void main()
{
    vec3 uvt;
    uint cell;
    vec4 hitPos;
    vec4 normal;
    vec4 rayDirNorm = normalize(rayDir);
    vec4 shadowDir = normalize(vec4(1, 2, 4, 3));
    TraceRay(pcs.playerPos, rayDirNorm, uvt, cell, hitPos, normal);
    if (cell != 0)
    {
        vec4 color = texture(sampler3D(textures[nonuniformEXT(cell)], textureSampler), uvt);
        vec4 shadowOrigin = hitPos;
        float light = 0.2;
        vec4 unused;
        TraceRay(shadowOrigin, shadowDir, uvt, cell, hitPos, unused);
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
