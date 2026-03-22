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

SDL_Surface* LoadTexture(const char* path)
{
    SDL_Surface* raw = SDL_LoadPNG(path);
    SDL_Surface* converted = SDL_ConvertSurface(raw, surface->format);
    SDL_DestroySurface(raw);
    return converted;
}

struct RayHit
{
    f32 distance;
    vec2 uv;
    u8 cell;
};

RayHit TraceRay(vec3 origin, vec3 dir)
{
    ivec3 step = sign(dir);
    ivec3 gridPos = floor(origin);
    vec3 stepSize =
    {
        sqrt(1 + pow(dir.y / dir.x, 2) + pow(dir.z / dir.x, 2)),
        sqrt(1 + pow(dir.x / dir.y, 2) + pow(dir.z / dir.y, 2)),
        sqrt(1 + pow(dir.x / dir.z, 2) + pow(dir.y / dir.z, 2))
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

    vec3 rayLength;
    vec3 frac = origin - vec3{gridPos};

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

    f32 distance;
    vec2 hitCoord;

    bool inBlock = !InBounds(gridPos) || map[gridPos.z * MAP_SIZE * MAP_SIZE + gridPos.y * MAP_SIZE + gridPos.x] != 0;

    while (true)
    {
        if (rayLength.x < rayLength.y && rayLength.x < rayLength.z)
        {
            gridPos.x += step.x;
            distance = rayLength.x;
            hitCoord = {origin.y + distance * dir.y, origin.z + distance * dir.z};
            rayLength.x += stepSize.x;
        }
        else if (rayLength.y < rayLength.z)
        {
            gridPos.y += step.y;
            distance = rayLength.y;
            hitCoord = {origin.x + distance * dir.x, origin.z + distance * dir.z};
            rayLength.y += stepSize.y;
        }
        else
        {
            gridPos.z += step.z;
            distance = rayLength.z;
            hitCoord = {origin.x + distance * dir.x, origin.y + distance * dir.y};
            rayLength.z += stepSize.z;
        }

        if (!InBounds(gridPos))
        {
            return {};
        }

        u8 cell = map[gridPos.z * MAP_SIZE * MAP_SIZE + gridPos.y * MAP_SIZE + gridPos.x];
        if (cell != 0 && !inBlock)
        {
            vec2 uv = fract(hitCoord);
            return {distance, uv, cell};
        }
        inBlock = cell != 0;
    }
}

void DrawTile(RenderTile tile)
{
    f32 planeHeight = 2 * tan(radians(fov / 2));
    f32 planeWidth = planeHeight * WINDOW_WIDTH / WINDOW_HEIGHT;
    vec3 playerForward = PlayerForwardDir();
    vec3 playerRight = PlayerRightDir();
    vec3 playerUp = PlayerUpDir();
    for (u32 x = tile.xStart; x < tile.xEnd; x++)
    {
        for (u32 y = tile.yStart; y < tile.yEnd; y++)
        {
            vec3 dir = normalize(playerForward + playerRight * (planeWidth * ((f32)x / WINDOW_WIDTH - 0.5f)) + playerUp * (-planeHeight * ((f32)y / WINDOW_HEIGHT - 0.5f)));
            RayHit hit = TraceRay(playerPos, dir);

            u32* pixel = (u32*)((u8*)surface->pixels + x * 4 + y * surface->pitch);

            if (hit.cell != 0)
            {
                SDL_Surface* texture = textures[hit.cell];
                u32 u = hit.uv.x * texture->w;
                u32 v = hit.uv.y * texture->h;

                u32* texel = (u32*)((u8*)texture->pixels + u * 4 + v * texture->pitch);

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
    textures[1] = LoadTexture("../textures/stone.png");
    textures[2] = LoadTexture("../textures/cobblestone.png");
    textures[3] = LoadTexture("../textures/dirt.png");

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