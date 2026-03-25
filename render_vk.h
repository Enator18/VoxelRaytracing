#pragma once

#include <glm/glm.hpp>

using namespace glm;

void RenderInitVk();

void LoadTextureVk(const char* path, u32 index);

void DrawViewVk();
