#include <iostream>
#include <array>
#include <random>
#include <unordered_set>
#include <unordered_map>

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

#include "main.h"
#include "render_soft.h"

using namespace glm;

std::array<u8, MAP_SIZE * MAP_SIZE * MAP_SIZE> map;

std::unordered_map<ivec3, vec3> noiseDirs;

SDL_Window* window;

f32 fov = 90;
f32 mouseSensitivity = 0.25;
f32 moveSpeed = 5;
vec3 playerPos = {MAP_SIZE / 2 + 0.5, MAP_SIZE / 2 + 0.5, MAP_SIZE / 2 + 0.5};
vec2 playerRot;

std::unordered_set<SDL_Keycode> keysDown;

vec3 NoiseDir3D(u64 seed, ivec3 pos)
{
    if (noiseDirs.contains(pos))
    {
        return noiseDirs[pos];
    }
    std::mt19937_64 gen{seed};
    gen.seed(gen() + pos.x);
    gen.seed(gen() + pos.y);
    gen.seed(gen() + pos.z);
    std::normal_distribution<f32> normal;
    vec3 dir = normalize(vec3{normal(gen), normal(gen), normal(gen)});
    noiseDirs[pos] = dir;
    return dir;
}

f32 NoiseGrad3D(u64 seed, vec3 pos, ivec3 corner)
{
    vec3 dir = NoiseDir3D(seed, corner);
    vec3 offset = pos - vec3(corner);
    return dot(dir, offset);
}

f32 Fade(f32 t)
{
    return t * t * t * (t * (t * 6 - 15) + 10);
}

f32 PerlinNoise3D(u64 seed, vec3 pos)
{
    ivec3 base = floor(pos);
    f32 MMM = NoiseGrad3D(seed, pos, base);
    f32 PMM = NoiseGrad3D(seed, pos, base + ivec3{1, 0, 0});
    f32 MPM = NoiseGrad3D(seed, pos, base + ivec3{0, 1, 0});
    f32 PPM = NoiseGrad3D(seed, pos, base + ivec3{1, 1, 0});
    f32 MMP = NoiseGrad3D(seed, pos, base + ivec3{0, 0, 1});
    f32 PMP = NoiseGrad3D(seed, pos, base + ivec3{1, 0, 1});
    f32 MPP = NoiseGrad3D(seed, pos, base + ivec3{0, 1, 1});
    f32 PPP = NoiseGrad3D(seed, pos, base + ivec3{1, 1, 1});

    vec3 frac = fract(pos);
    f32 xInterp = Fade(frac.x);
    f32 yInterp = Fade(frac.y);
    f32 zInterp = Fade(frac.z);

    f32 MM = mix(MMM, MMP, zInterp);
    f32 PM = mix(PMM, PMP, zInterp);
    f32 MP = mix(MPM, MPP, zInterp);
    f32 PP = mix(PPM, PPP, zInterp);

    return mix(mix(MM, MP, yInterp), mix(PM, PP, yInterp), xInterp);
}

void GenerateMap(u64 seed)
{
    std::mt19937_64 gen{seed};
    std::uniform_int_distribution<u64> random{1, 3};
    noiseDirs.clear();
    for (u32 x = 0; x < MAP_SIZE; x++)
    {
        for (u32 y = 0; y < MAP_SIZE; y++)
        {
            for (u32 z = 0; z < MAP_SIZE; z++)
            {
                vec3 pos = {x, y, z};
                f32 noise = PerlinNoise3D(seed, pos * 0.05f);
                if (noise < 0)
                {
                    u8 val = random(gen);
                    map[z * MAP_SIZE * MAP_SIZE + y * MAP_SIZE + x] = val;
                }
                else
                {
                    map[z * MAP_SIZE * MAP_SIZE + y * MAP_SIZE + x] = 0;
                }
            }
        }
    }
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

    RenderInitSoft();

    GenerateMap(0);

    u64 then = SDL_GetPerformanceCounter();

    while (true)
    {
        u64 now = SDL_GetPerformanceCounter();
        f32 delta = (f64)(now - then) / SDL_GetPerformanceFrequency();
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
            moveDir += vec3{1, 0, 0};
        }
        if (keysDown.contains(SDLK_S))
        {
            moveDir += vec3{-1, 0, 0};
        }
        if (keysDown.contains(SDLK_D))
        {
            moveDir += vec3{0, 1, 0};
        }
        if (keysDown.contains(SDLK_A))
        {
            moveDir += vec3{0, -1, 0};
        }

        if (moveDir.x != 0 && moveDir.y != 0)
        {
            moveDir = normalize(moveDir);
        }

        playerPos += (PlayerRotation() * moveDir) * moveSpeed * delta;

        DrawViewSoft();
    }
}