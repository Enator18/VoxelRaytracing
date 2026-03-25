#include <limits>
#include <vector>
#include <thread>
#include <barrier>
#include <atomic>
#include <array>

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#include "main.h"
#include "render_soft.h"

using namespace glm;

SDL_Surface* surface;

std::array<SDL_Surface*, 4> textures;

u32 numThreads = std::thread::hardware_concurrency();
u32 tileSize = 32;

struct RenderTile
{
    u32 xStart;
    u32 yStart;
    u32 xEnd;
    u32 yEnd;
};

std::vector<RenderTile> tiles;
u32 tileCount;
std::atomic<u32> nextTile;
std::barrier<> startBarrier{numThreads};
std::barrier<> endBarrier{numThreads};

struct RayHit
{
    f32 distance;
    vec3 uvt;
    u8 cell;
};

RayHit TraceRay(vec4 origin, vec4 dir)
{
    ivec4 step = sign(dir);
    ivec4 gridPos = floor(origin);
    vec4 stepSize =
    {
        sqrt(1 + pow(dir.y / dir.x, 2) + pow(dir.z / dir.x, 2) + pow(dir.w / dir.x, 2)),
        sqrt(1 + pow(dir.x / dir.y, 2) + pow(dir.z / dir.y, 2) + pow(dir.w / dir.y, 2)),
        sqrt(1 + pow(dir.x / dir.z, 2) + pow(dir.y / dir.z, 2) + pow(dir.w / dir.z, 2)),
        sqrt(1 + pow(dir.x / dir.w, 2) + pow(dir.y / dir.w, 2) + pow(dir.z / dir.w, 2))
    };

    constexpr f32 INF = std::numeric_limits<f32>::infinity();

    if (isnan(stepSize.x))
    {
        stepSize.x = INF;
    }
    if (isnan(stepSize.y))
    {
        stepSize.y = INF;
    }
    if (isnan(stepSize.z))
    {
        stepSize.z = INF;
    }
    if (isnan(stepSize.w))
    {
        stepSize.w = INF;
    }

    vec4 rayLength;
    vec4 frac = origin - vec4{gridPos};

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

    f32 distance;
    vec3 hitCoord;

    while (true)
    {
        if (rayLength.x < rayLength.y && rayLength.x < rayLength.z && rayLength.x < rayLength.w)
        {
            gridPos.x += step.x;
            distance = rayLength.x;
            hitCoord = {origin.y + distance * dir.y, origin.z + distance * dir.z, origin.w + distance * dir.w};
            rayLength.x += stepSize.x;
        }
        else if (rayLength.y < rayLength.z && rayLength.y < rayLength.w)
        {
            gridPos.y += step.y;
            distance = rayLength.y;
            hitCoord = {origin.x + distance * dir.x, origin.z + distance * dir.z, origin.w + distance * dir.w};
            rayLength.y += stepSize.y;
        }
        else if (rayLength.z < rayLength.w)
        {
            gridPos.z += step.z;
            distance = rayLength.z;
            hitCoord = {origin.x + distance * dir.x, origin.y + distance * dir.y, origin.w + distance * dir.w};
            rayLength.z += stepSize.z;
        }
        else
        {
            gridPos.w += step.w;
            distance = rayLength.w;
            hitCoord = {origin.x + distance * dir.x, origin.y + distance * dir.y, origin.z + distance * dir.z};
            rayLength.w += stepSize.w;
        }

        if (!InBounds(gridPos))
        {
            return {};
        }

        u8 cell = map[gridPos.w * MAP_SIZE * MAP_SIZE * MAP_SIZE +
            gridPos.z * MAP_SIZE * MAP_SIZE + gridPos.y * MAP_SIZE + gridPos.x];
        if (cell != 0)
        {
            vec3 uvt = fract(hitCoord);
            return {distance, uvt, cell};
        }
    }
}

void DrawTile(RenderTile tile)
{
    f32 planeHeight = 2 * tan(radians(fov / 2));
    f32 planeWidth = planeHeight * WINDOW_WIDTH / WINDOW_HEIGHT;
    mat4 playerRotMat = PlayerRotation();
    for (u32 x = tile.xStart; x < tile.xEnd; x++)
    {
        for (u32 y = tile.yStart; y < tile.yEnd; y++)
        {
            vec4 dir = playerRotMat * normalize(vec4{1, planeWidth * ((f32)x / WINDOW_WIDTH - 0.5f), -planeHeight * ((f32)y / WINDOW_HEIGHT - 0.5f), 0});
            RayHit hit = TraceRay(playerPos, dir);

            u32* pixel = (u32*)((u8*)surface->pixels + x * 4 + y * surface->pitch);

            if (hit.cell != 0)
            {
                SDL_Surface* texture = textures[hit.cell];
                u32 u = hit.uvt.x * 16;
                u32 v = hit.uvt.y * 16;
                u32 t = hit.uvt.z * 16;

                u32* texel = (u32*)((u8*)texture->pixels + u * 4 + v * texture->pitch + t * 16 * texture->pitch);

                *pixel = *texel;
            }
            else
            {
                *pixel = 0;
            }
        }
    }
}

void DrawTiles(u32 startTile)
{
    u32 tile = startTile;
    while (tile < tileCount)
    {
        DrawTile(tiles[tile]);
        tile = nextTile++;
    }
}

[[noreturn]] void DrawThread(u32 startTile)
{
    while (true)
    {
        startBarrier.arrive_and_wait();
        DrawTiles(startTile);
        endBarrier.arrive_and_wait();
    }
}

void RenderInitSoft()
{
    surface = SDL_GetWindowSurface(window);

    u32 hTiles = (WINDOW_WIDTH + tileSize - 1) / tileSize;
    u32 vTiles = (WINDOW_HEIGHT + tileSize - 1) / tileSize;
    for (u32 x = 0; x < hTiles; x++)
    {
        for (u32 y = 0; y < vTiles; y++)
        {
            tiles.push_back({
                x * tileSize, y * tileSize,
                min((x + 1) * tileSize, WINDOW_WIDTH),
                min((y + 1) * tileSize, WINDOW_HEIGHT)});
        }
    }
    tileCount = hTiles * vTiles;

    for (u32 i = 1; i < numThreads; i++)
    {
        std::thread thread{DrawThread, i};
        thread.detach();
    }
}

void LoadTextureSoft(const char* path, u32 index)
{
    SDL_Surface* raw = SDL_LoadPNG(path);
    textures[index] = SDL_ConvertSurface(raw, surface->format);
    SDL_DestroySurface(raw);
}

void DrawViewSoft()
{
    SDL_LockSurface(surface);

    nextTile = numThreads;

    startBarrier.arrive_and_wait();
    DrawTiles(0);
    endBarrier.arrive_and_wait();

    SDL_UnlockSurface(surface);
    SDL_UpdateWindowSurface(window);
}