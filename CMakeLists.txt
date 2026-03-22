cmake_minimum_required(VERSION 3.31)
project(VoxelRaytracing)

set(CMAKE_CXX_STANDARD 20)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CPM.cmake)
set(CPM_SOURCE_CACHE "${CMAKE_CURRENT_SOURCE_DIR}/packages")

set(BUILD_SHARED_LIBS OFF)
set(SDL_SHARED OFF)

CPMAddPackage("gh:libsdl-org/SDL#release-3.4.2")
CPMAddPackage("gh:g-truc/glm#1.0.1")

add_executable(VoxelRaytracing main.cpp render_soft.cpp)

target_link_libraries(VoxelRaytracing PRIVATE SDL3::SDL3 glm::glm)
