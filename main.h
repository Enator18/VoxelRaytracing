#pragma once

#include <glm/glm.hpp>

using namespace glm;

constexpr u32 WINDOW_WIDTH = 1920;
constexpr u32 WINDOW_HEIGHT = 1080;

constexpr u32 MAP_SIZE = 64;

extern std::array<u8, MAP_SIZE * MAP_SIZE * MAP_SIZE> map;
extern SDL_Window* window;

extern f32 fov;
extern vec3 playerPos;
extern vec2 playerRot;

inline bool InBounds(i32 val)
{
    return val >= 0 && val < MAP_SIZE;
}

inline bool InBounds(ivec3 val)
{
    return InBounds(val.x) && InBounds(val.y) && InBounds(val.z);
}

inline mat3 PlayerRotation()
{
    mat3 horizontal
    {
        {cos(playerRot.y), sin(playerRot.y), 0},
        {-sin(playerRot.y), cos(playerRot.y), 0},
        {0, 0, 1}
    };

    mat3 vertical
    {
        {cos(playerRot.x), 0, -sin(playerRot.x)},
        {0, 1, 0},
        {sin(playerRot.x), 0, cos(playerRot.x)}
    };
    return horizontal * vertical;
}
