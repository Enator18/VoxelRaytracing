cmake_minimum_required(VERSION 3.31)
project(VoxelRaytracing)

set(CMAKE_CXX_STANDARD 20)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CPM.cmake)
set(CPM_SOURCE_CACHE "${CMAKE_CURRENT_SOURCE_DIR}/packages")

set(BUILD_SHARED_LIBS OFF)
set(SDL_SHARED OFF)

CPMAddPackage("gh:libsdl-org/SDL#release-3.4.2")
CPMAddPackage("gh:g-truc/glm#1.0.1")
CPMAddPackage("gh:zeux/volk#vulkan-sdk-1.4.341.0")
CPMAddPackage("gh:GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator#v3.3.0")
CPMAddPackage("gh:charles-lunarg/vk-bootstrap#v1.4.341")

find_package(Vulkan REQUIRED)

add_executable(VoxelRaytracing main.cpp render_soft.cpp render_vk.cpp)

target_link_libraries(VoxelRaytracing PRIVATE SDL3::SDL3 glm::glm Vulkan::Headers volk::volk GPUOpen::VulkanMemoryAllocator vk-bootstrap::vk-bootstrap)
target_compile_definitions(VoxelRaytracing PUBLIC VK_NO_PROTOTYPES)

set(GLSLANG "glslangValidator")

macro(add_shader inName outName)
    set(SHADER_IN_PATH "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${inName}.glsl")
    set(SHADER_OUT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${outName}.spv")
    list(APPEND SHADER_OUT_NAMES ${SHADER_OUT_PATH})
    add_custom_command(
            MAIN_DEPENDENCY ${SHADER_IN_PATH}
            OUTPUT ${SHADER_OUT_PATH}
            COMMAND ${GLSLANG} ${SHADER_IN_PATH} -o ${SHADER_OUT_PATH} --target-env vulkan1.3 ${ARGN}
            VERBATIM)
endmacro()

add_shader("render.vert" "render.vert")
add_shader("render.frag" "render.frag")
add_custom_target(build_shaders DEPENDS ${SHADER_OUT_NAMES})
add_dependencies(VoxelRaytracing build_shaders)
