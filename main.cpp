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

std::array<u8, MAP_SIZE * MAP_SIZE * MAP_SIZE * MAP_SIZE> map;

std::unordered_map<ivec4, vec4> noiseDirs;

SDL_Window* window;

f32 fov = 90;
f32 mouseSensitivity = 0.25;
f32 moveSpeed = 2;
vec4 playerPos = {MAP_SIZE / 2 + 0.5, MAP_SIZE / 2 + 0.5, MAP_SIZE / 2 + 0.5, MAP_SIZE / 2 + 0.5};
mat4 playerRot{1};
f32 playerPitch;

std::unordered_set<SDL_Keycode> keysDown;
bool leftClick;

vec4 NoiseDir4D(u64 seed, ivec4 pos)
{
    if (noiseDirs.contains(pos))
    {
        return noiseDirs[pos];
    }
    std::mt19937_64 gen{seed};
    gen.seed(gen() + pos.x);
    gen.seed(gen() + pos.y);
    gen.seed(gen() + pos.z);
    gen.seed(gen() + pos.w);
    std::normal_distribution<f32> normal;
    vec4 dir = normalize(vec4{normal(gen), normal(gen), normal(gen), normal(gen)});
    noiseDirs[pos] = dir;
    return dir;
}

f32 NoiseGrad4D(u64 seed, vec4 pos, ivec4 corner)
{
    vec4 dir = NoiseDir4D(seed, corner);
    vec4 offset = pos - vec4(corner);
    return dot(dir, offset);
}

f32 Fade(f32 t)
{
    return t * t * t * (t * (t * 6 - 15) + 10);
}

f32 PerlinNoise4D(u64 seed, vec4 pos)
{
    ivec4 base = floor(pos);
    f32 MMMM = NoiseGrad4D(seed, pos, base);
    f32 PMMM = NoiseGrad4D(seed, pos, base + ivec4{1, 0, 0, 0});
    f32 MPMM = NoiseGrad4D(seed, pos, base + ivec4{0, 1, 0, 0});
    f32 PPMM = NoiseGrad4D(seed, pos, base + ivec4{1, 1, 0, 0});
    f32 MMPM = NoiseGrad4D(seed, pos, base + ivec4{0, 0, 1, 0});
    f32 PMPM = NoiseGrad4D(seed, pos, base + ivec4{1, 0, 1, 0});
    f32 MPPM = NoiseGrad4D(seed, pos, base + ivec4{0, 1, 1, 0});
    f32 PPPM = NoiseGrad4D(seed, pos, base + ivec4{1, 1, 1, 0});
    f32 MMMP = NoiseGrad4D(seed, pos, base + ivec4{0, 0, 0, 1});
    f32 PMMP = NoiseGrad4D(seed, pos, base + ivec4{1, 0, 0, 1});
    f32 MPMP = NoiseGrad4D(seed, pos, base + ivec4{0, 1, 0, 1});
    f32 PPMP = NoiseGrad4D(seed, pos, base + ivec4{1, 1, 0, 1});
    f32 MMPP = NoiseGrad4D(seed, pos, base + ivec4{0, 0, 1, 1});
    f32 PMPP = NoiseGrad4D(seed, pos, base + ivec4{1, 0, 1, 1});
    f32 MPPP = NoiseGrad4D(seed, pos, base + ivec4{0, 1, 1, 1});
    f32 PPPP = NoiseGrad4D(seed, pos, base + ivec4{1, 1, 1, 1});

    vec4 frac = fract(pos);
    f32 xInterp = Fade(frac.x);
    f32 yInterp = Fade(frac.y);
    f32 zInterp = Fade(frac.z);
    f32 wInterp = Fade(frac.w);

    f32 MMM = mix(MMMM, MMMP, wInterp);
    f32 PMM = mix(PMMM, PMMP, wInterp);
    f32 MPM = mix(MPMM, MPMP, wInterp);
    f32 PPM = mix(PPMM, PPMP, wInterp);
    f32 MMP = mix(MMPM, MMPP, wInterp);
    f32 PMP = mix(PMPM, PMPP, wInterp);
    f32 MPP = mix(MPPM, MPPP, wInterp);
    f32 PPP = mix(PPPM, PPPP, wInterp);

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
                for (u32 w = 0; w < MAP_SIZE; w++)
                {
                    vec4 pos = {x, y, z, w};
                    f32 noise = PerlinNoise4D(seed, pos * 0.05f);
                    if (noise < 0)
                    {
                        u8 val = random(gen);
                        map[w * MAP_SIZE * MAP_SIZE * MAP_SIZE + z * MAP_SIZE * MAP_SIZE + y * MAP_SIZE + x] = val;
                    }
                    else
                    {
                        map[w * MAP_SIZE * MAP_SIZE * MAP_SIZE + z * MAP_SIZE * MAP_SIZE + y * MAP_SIZE + x] = 0;
                    }
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

    GenerateMap(4);

    u64 then = SDL_GetPerformanceCounter();

    while (true)
    {
        u64 now = SDL_GetPerformanceCounter();
        f32 delta = (f64)(now - then) / SDL_GetPerformanceFrequency();
        then = now;

        // std::cout << (int)(1 / delta) << std::endl;

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
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    if (event.button.button == 1)
                    {
                        leftClick = true;
                    }
                    break;
                case SDL_EVENT_MOUSE_BUTTON_UP:
                    if (event.button.button == 1)
                    {
                        leftClick = false;
                    }
                    break;
                default:
                    break;
            }
        }

        f32 mouseX;
        f32 mouseY;
        SDL_GetRelativeMouseState(&mouseX, &mouseY);

        if (leftClick)
        {
            vec2 rotDir = {mouseX, mouseY};
            f32 rotAngle = radians(length(rotDir) * mouseSensitivity);
            if (rotDir.x != 0 || rotDir.y != 0)
            {
                rotDir = normalize(rotDir);
                mat4 rotMat
                {
                    {rotDir.x * rotDir.x * (1 - cos(rotAngle)) + cos(rotAngle), rotDir.x * rotDir.y * (cos(rotAngle) - 1), 0, rotDir.y * -sin(rotAngle)},
                    {rotDir.x * rotDir.y * (cos(rotAngle) - 1), rotDir.y * rotDir.y * (1 - cos(rotAngle)) + cos(rotAngle), 0, rotDir.x * -sin(rotAngle)},
                    {0, 0, 1, 0},
                    {rotDir.y * sin(rotAngle), rotDir.x * sin(rotAngle), 0, cos(rotAngle)}
                };
                playerRot = playerRot * rotMat;
            }
        }
        else
        {
            f32 yawRot = radians(mouseX * mouseSensitivity);
            mat4 yawMat
            {
                {cos(yawRot), sin(yawRot), 0, 0},
                {-sin(yawRot), cos(yawRot), 0, 0},
                {0, 0, 1, 0},
                {0, 0, 0, 1}
            };
            playerRot = playerRot * yawMat;
            playerPitch += radians(mouseY * mouseSensitivity);
            playerPitch = clamp(playerPitch, radians(-90.0f), radians(90.0f));
        }

        vec4 moveDir{};

        if (keysDown.contains(SDLK_W))
        {
            moveDir += vec4{1, 0, 0, 0};
        }
        if (keysDown.contains(SDLK_S))
        {
            moveDir += vec4{-1, 0, 0, 0};
        }
        if (keysDown.contains(SDLK_D))
        {
            moveDir += vec4{0, 1, 0, 0};
        }
        if (keysDown.contains(SDLK_A))
        {
            moveDir += vec4{0, -1, 0, 0};
        }
        if (keysDown.contains(SDLK_E))
        {
            moveDir += vec4{0, 0, 0, 1};
        }
        if (keysDown.contains(SDLK_Q))
        {
            moveDir += vec4{0, 0, 0, -1};
        }

        if (moveDir.x != 0 && moveDir.y != 0)
        {
            moveDir = normalize(moveDir);
        }

        playerPos += (PlayerRotation() * moveDir) * moveSpeed * delta;

        DrawViewSoft();
    }
}