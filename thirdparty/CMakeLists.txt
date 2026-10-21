add_library(doctest INTERFACE)
target_include_directories(doctest INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

add_library(stb_image INTERFACE)
target_include_directories(stb_image INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/stbimage")

# ---- GLFW (预编译, Windows/MinGW-w64) ----
# 以 IMPORTED 目标方式引入，下游只需 target_link_libraries(<target> glfw)。
# 目标名与 GLFW 官方 CMake / FetchContent 产出的一致，将来若要换成源码构建，
# 删除本段即可，消费方无需改动。
set(ORCA_GLFW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/glfw")

if(NOT EXISTS "${ORCA_GLFW_DIR}/include/GLFW/glfw3.h")
    message(FATAL_ERROR "未找到预编译 GLFW 头文件: ${ORCA_GLFW_DIR}/include/GLFW/glfw3.h")
endif()
if(NOT EXISTS "${ORCA_GLFW_DIR}/lib-mingw-w64/libglfw3.a")
    message(FATAL_ERROR "未找到预编译 GLFW 静态库: ${ORCA_GLFW_DIR}/lib-mingw-w64/libglfw3.a")
endif()

add_library(glfw STATIC IMPORTED GLOBAL)
set_target_properties(glfw PROPERTIES
    IMPORTED_LOCATION "${ORCA_GLFW_DIR}/lib-mingw-w64/libglfw3.a"
    INTERFACE_INCLUDE_DIRECTORIES "${ORCA_GLFW_DIR}/include"
)

if(WIN32)
    # 静态 libglfw3.a 不携带依赖信息，需手动补齐其使用的系统库。
    target_link_libraries(glfw INTERFACE gdi32 opengl32 user32 shell32)
else()
    message(FATAL_ERROR "当前仅提供 Windows/MinGW-w64 的预编译 GLFW，请为其它平台补充实现。")
endif()

# ---- SDL3 (预编译, Windows/MinGW-w64, 动态库) ----
# 以 IMPORTED 目标方式引入，下游只需 target_link_libraries(<target> sdl3)。
# 注意：SDL3 是动态库，运行时需要把 bin/SDL3.dll 复制到可执行文件旁边，
# 由 platform/CMakeLists.txt 中的 orca_copy_platform_runtime() 负责。
set(ORCA_SDL3_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sdl3")

if(NOT EXISTS "${ORCA_SDL3_DIR}/include/SDL3/SDL.h")
    message(FATAL_ERROR "未找到预编译 SDL3 头文件: ${ORCA_SDL3_DIR}/include/SDL3/SDL.h")
endif()
if(NOT EXISTS "${ORCA_SDL3_DIR}/lib/libSDL3.dll.a")
    message(FATAL_ERROR "未找到预编译 SDL3 导入库: ${ORCA_SDL3_DIR}/lib/libSDL3.dll.a")
endif()
if(NOT EXISTS "${ORCA_SDL3_DIR}/bin/SDL3.dll")
    message(FATAL_ERROR "未找到预编译 SDL3 运行时: ${ORCA_SDL3_DIR}/bin/SDL3.dll")
endif()

add_library(sdl3 SHARED IMPORTED GLOBAL)
set_target_properties(sdl3 PROPERTIES
    # Windows/MinGW 下动态库需要同时给出导入库(.dll.a)与运行时(.dll)
    IMPORTED_IMPLIB "${ORCA_SDL3_DIR}/lib/libSDL3.dll.a"
    IMPORTED_LOCATION "${ORCA_SDL3_DIR}/bin/SDL3.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${ORCA_SDL3_DIR}/include"
)
# 引擎自行提供入口点，禁用 SDL 对 main 的重映射。
target_compile_definitions(sdl3 INTERFACE SDL_MAIN_HANDLED)
