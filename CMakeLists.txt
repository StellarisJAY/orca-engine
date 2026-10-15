cmake_minimum_required(VERSION 3.16)

project(orca-engine
    VERSION 0.1.0
    DESCRIPTION "A 3D rendering engine"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(ORCA_BUILD_TESTS "Build Orca unit tests" ON)
option(ORCA_PLATFORM_GLFW "Use GLFW for window and input" ON)
option(ORCA_PLATFORM_SDL3 "Use SDL3 for window and input" OFF)

# ---- 图形后端选择 ----
# 可以同时编译多个后端（便于对比/切换），运行时用 RenderingDevice::create_singleton(type) 选择。
option(ORCA_RENDERER_VULKAN "Enable the Vulkan rendering backend" ON)
option(ORCA_RENDERER_OPENGL "Enable the OpenGL rendering backend" ON)
# 默认后端（RenderingDevice::create_singleton() 无参版本使用）。取值：vulkan 或 opengl。
set(ORCA_RENDERER_DEFAULT "opengl" CACHE STRING "Default rendering backend (vulkan|opengl)")

if(ORCA_RENDERER_VULKAN)
    find_package(Vulkan REQUIRED)
    message(STATUS "Vulkan backend enabled, found Vulkan ${Vulkan_VERSION}")
endif()

if(ORCA_RENDERER_OPENGL)
    # 只用来定位 opengl32；GL 1.1 头文件随编译器提供，现代入口点在运行时动态加载。
    find_package(OpenGL REQUIRED)
    message(STATUS "OpenGL backend enabled")
endif()

if(NOT ORCA_RENDERER_VULKAN AND NOT ORCA_RENDERER_OPENGL)
    message(FATAL_ERROR "未选择图形后端：请至少开启 ORCA_RENDERER_VULKAN 或 ORCA_RENDERER_OPENGL")
endif()

add_subdirectory(thirdparty)
add_subdirectory(core)
add_subdirectory(servers/rendering)
add_subdirectory(platform)
add_subdirectory(scene)
add_subdirectory("resource")
add_subdirectory(application)

if(ORCA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
