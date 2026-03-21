#include <iostream>
#include <array>
#include <random>
#include <limits>
#include <unordered_set>
#include <thread>
#include <barrier>

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

using namespace glm;

constexpr u32 WINDOW_WIDTH = 1920;
constexpr u32 WINDOW_HEIGHT = 1080;

constexpr u32 MAP_SIZE = 32;

std::array<u8, MAP_SIZE * MAP_SIZE * MAP_SIZE> map;

SDL_Window* window;
SDL_Surface* surface;

std::array<SDL_Surface*, 4> textures;

f32 fov = 90;
f32 mouseSensitivity = 0.25;
f32 moveSpeed = 5;
vec3 playerPos = {MAP_SIZE / 2 + 0.5, MAP_SIZE / 2 + 0.5, MAP_SIZE / 2 + 0.5};
vec2 playerRot = {0, 0};

u32 hThreads = 8;
u32 vThreads = 4;

std::unordered_set<SDL_Keycode> keysDown;

inline bool InBounds(i32 val)
{
    return val >= 0 && val < MAP_SIZE;
}

inline bool InBounds(ivec3 val)
{
    return InBounds(val.x) && InBounds(val.y) && InBounds(val.z);
}

inline vec3 PlayerForwardDir()
{
    return {cos(playerRot.y) * cos(playerRot.x), sin(playerRot.y) * cos(playerRot.x), -sin(playerRot.x)};
}

inline vec3 PlayerRightDir()
{
    return {-sin(playerRot.y), cos(playerRot.y), 0};
}

inline vec3 PlayerUpDir()
{
    return {cos(playerRot.y) * sin(playerRot.x), sin(playerRot.y) * sin(playerRot.x), cos(playerRot.x)};
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
        if (cell != 0)
        {
            vec2 uv = fract(hitCoord);
            return {distance, uv, cell};
        }
    }
}

void DrawView(u32 xStart, u32 yStart, u32 xEnd, u32 yEnd)
{
    f32 planeHeight = 2 * tan(radians(fov / 2));
    f32 planeWidth = planeHeight * WINDOW_WIDTH / WINDOW_HEIGHT;
    vec3 playerForward = PlayerForwardDir();
    vec3 playerRight = PlayerRightDir();
    vec3 playerUp = PlayerUpDir();
    for (u32 x = xStart; x < xEnd; x++)
    {
        for (u32 y = yStart; y < yEnd; y++)
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

void DrawThread(u32 xStart, u32 yStart, u32 xEnd, u32 yEnd, std::barrier<>& startBarrier, std::barrier<>& endBarrier)
{
    while (true)
    {
        startBarrier.arrive_and_wait();
        DrawView(xStart, yStart, xEnd, yEnd);
        endBarrier.arrive_and_wait();
    }
}

void GenerateMap(u64 seed)
{
    std::mt19937_64 gen{seed};
    std::uniform_int_distribution<u64> random{0, 47};
    for (u32 i = 0; i < map.size(); i++)
    {
        u8 val = random(gen);
        if (val > 3)
        {
            val = 0;
        }
        map[i] = val;
    }
}

SDL_Surface* LoadTexture(const char* path)
{
    SDL_Surface* raw = SDL_LoadPNG(path);
    SDL_Surface* converted = SDL_ConvertSurface(raw, surface->format);
    SDL_DestroySurface(raw);
    return converted;
}

i32 main()
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
    {
        std::cerr << "SDL could not initialize! SDL_Error: " << SDL_GetError() << std::endl;
        return 1;
    }

    window = SDL_CreateWindow("Voxel Raytracer", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    if (window == nullptr)
    {
        std::cerr << "Window could not be created! SDL_Error: " << SDL_GetError() << std::endl;
        return 1;
    }

    SDL_SetWindowRelativeMouseMode(window, true);

    surface = SDL_GetWindowSurface(window);
    textures[1] = LoadTexture("../textures/stone.png");
    textures[2] = LoadTexture("../textures/cobblestone.png");
    textures[3] = LoadTexture("../textures/dirt.png");

    GenerateMap(0);

    std::barrier<> startBarrier{hThreads * vThreads};
    std::barrier<> endBarrier{hThreads * vThreads};

    u32 threadWidth = (WINDOW_WIDTH + hThreads - 1) / hThreads;
    u32 threadHeight = (WINDOW_HEIGHT + vThreads - 1) / vThreads;
    for (u32 x = 0; x < hThreads; x++)
    {
        for (u32 y = 0; y < vThreads; y++)
        {
            if (x < hThreads - 1 || y < vThreads - 1)
            {
                std::thread thread{DrawThread,
                x * threadWidth, y * threadHeight,
                min((x + 1) * threadWidth, WINDOW_WIDTH), min((y + 1) * threadHeight, WINDOW_HEIGHT),
                std::ref(startBarrier), std::ref(endBarrier)};
                thread.detach();
            }
        }
    }

    u64 then = SDL_GetPerformanceCounter();

    while (true)
    {
        u64 now = SDL_GetPerformanceCounter();
        f32 delta = (f32)(now - then) / SDL_GetPerformanceFrequency();
        then = now;

        std::cout << (int)(1 / delta) << std::endl;

        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
                case SDL_EVENT_QUIT:
                    return 0;

                case SDL_EVENT_KEY_DOWN:
                    if (event.key.key == SDLK_ESCAPE)
                    {
                        return 0;
                    }
                    if (event.key.key == SDLK_G)
                    {
                        GenerateMap(SDL_GetPerformanceCounter());
                    }
                    keysDown.insert(event.key.key);
                    break;
                case SDL_EVENT_KEY_UP:
                    keysDown.erase(event.key.key);
                    break;
                default:
                    break;
            }
        }

        f32 mouseX;
        f32 mouseY;
        SDL_GetRelativeMouseState(&mouseX, &mouseY);

        playerRot.y += radians(mouseX * mouseSensitivity);
        playerRot.x += radians(mouseY * mouseSensitivity);

        playerRot.x = clamp(playerRot.x, radians(-90.0f), radians(90.0f));

        vec3 moveDir{};

        if (keysDown.contains(SDLK_W))
        {
            moveDir += PlayerForwardDir();
        }
        if (keysDown.contains(SDLK_S))
        {
            moveDir -= PlayerForwardDir();
        }
        if (keysDown.contains(SDLK_D))
        {
            moveDir += PlayerRightDir();
        }
        if (keysDown.contains(SDLK_A))
        {
            moveDir -= PlayerRightDir();
        }

        if (moveDir.x != 0 && moveDir.y != 0)
        {
            moveDir = normalize(moveDir);
        }

        playerPos += moveDir * moveSpeed * delta;

        SDL_LockSurface(surface);

        startBarrier.arrive_and_wait();
        DrawView(threadWidth * (hThreads - 1), threadHeight * (vThreads - 1), WINDOW_WIDTH, WINDOW_HEIGHT);
        endBarrier.arrive_and_wait();

        SDL_UnlockSurface(surface);
        SDL_UpdateWindowSurface(window);
    }
}