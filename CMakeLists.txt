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
option(ORCA_RENDERER_VULKAN "Enable the Vulkan rendering backend" ON)
option(ORCA_PLATFORM_GLFW "Use GLFW for window and input" ON)
option(ORCA_PLATFORM_SDL3 "Use SDL3 for window and input" OFF)

if(ORCA_RENDERER_VULKAN)
    find_package(Vulkan REQUIRED)
    message(STATUS "Vulkan backend enabled, found Vulkan ${Vulkan_VERSION}")
endif()

add_subdirectory(thirdparty)
add_subdirectory(core)
add_subdirectory(servers/rendering)
add_subdirectory(platform)

if(ORCA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
