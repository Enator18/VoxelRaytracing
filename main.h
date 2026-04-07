#pragma once

#include <glm/glm.hpp>

using namespace glm;

constexpr u32 WINDOW_WIDTH = 1920;
constexpr u32 WINDOW_HEIGHT = 1080;

constexpr u32 MAP_SIZE = 32;

extern std::array<u8, MAP_SIZE * MAP_SIZE * MAP_SIZE * MAP_SIZE> map;
extern SDL_Window* window;

extern f32 fov;
extern vec4 playerPos;
extern mat4 playerRot;
extern f32 playerPitch;

inline bool InBounds(i32 val)
{
    return val >= 0 && val < MAP_SIZE;
}

inline bool InBounds(ivec4 val)
{
    return InBounds(val.x) && InBounds(val.y) && InBounds(val.z) && InBounds(val.w);
}

inline mat4 PlayerRotation()
{
    mat4 vertical
    {
        {cos(playerPitch), 0, -sin(playerPitch), 0},
        {0, 1, 0, 0},
        {sin(playerPitch), 0, cos(playerPitch), 0},
        {0, 0, 0, 1}
    };
    return playerRot * vertical;
}
