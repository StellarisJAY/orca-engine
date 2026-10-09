# 平台后端：ORCA_PLATFORM_GLFW 与 ORCA_PLATFORM_SDL3 二选一。
# 两个后端都实现 Window::create()，同时启用会导致运行库符号冲突。
if(ORCA_PLATFORM_GLFW AND ORCA_PLATFORM_SDL3)
    message(FATAL_ERROR "ORCA_PLATFORM_GLFW 与 ORCA_PLATFORM_SDL3 只能启用一个")
endif()

# 公共实现：所有后端共享
add_library(orca-platform STATIC
    window.cpp
)
target_include_directories(orca-platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(ORCA_PLATFORM_GLFW)
    message(STATUS "Using GLFW for window and input")
    target_sources(orca-platform PRIVATE glfw/glfw_window.cpp)
    target_link_libraries(orca-platform PRIVATE glfw)
    target_compile_definitions(orca-platform PUBLIC ORCA_PLATFORM_GLFW)
elseif(ORCA_PLATFORM_SDL3)
    message(STATUS "Using SDL3 for window and input")
    target_sources(orca-platform PRIVATE sdl3/sdl3_window.cpp)
    target_link_libraries(orca-platform PRIVATE sdl3)
    target_compile_definitions(orca-platform PUBLIC ORCA_PLATFORM_SDL3)
else()
    message(FATAL_ERROR "未选择平台后端：请开启 ORCA_PLATFORM_GLFW 或 ORCA_PLATFORM_SDL3")
endif()

# SDL3 为动态库，链接 orca-platform 的可执行文件需要把 SDL3.dll 复制到其旁边。
# 用法：orca_copy_platform_runtime(<可执行目标>)
function(orca_copy_platform_runtime target)
    if(ORCA_PLATFORM_SDL3)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:sdl3>" "$<TARGET_FILE_DIR:${target}>"
            COMMENT "Copying SDL3 runtime next to ${target}")
    endif()
endfunction()
