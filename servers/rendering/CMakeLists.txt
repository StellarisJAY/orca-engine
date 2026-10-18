# 渲染服务器（RenderingServer / RHI）。
#
# 公共部分（rendering_device.*）与具体图形 API 无关，只依赖抽象的 RenderingDevice 接口。
# 每个后端各自编译成一个实现文件，并通过 ORCA_RENDERER_<API> 宏接入工厂。
add_library(orca-rendering STATIC
    rendering_device.cpp
    rendering_server.cpp
    rendering_storage.cpp
)

target_include_directories(orca-rendering PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(orca-rendering PUBLIC orca-math orca-core)

# 默认后端：把 ORCA_RENDERER_DEFAULT 转成编译期宏，供 RenderingDevice::get_default_driver() 使用。
string(TOUPPER "${ORCA_RENDERER_DEFAULT}" ORCA_RENDERER_DEFAULT_UPPER)
if(ORCA_RENDERER_DEFAULT_UPPER STREQUAL "VULKAN")
    target_compile_definitions(orca-rendering PUBLIC ORCA_RENDERER_DEFAULT_VULKAN)
elseif(ORCA_RENDERER_DEFAULT_UPPER STREQUAL "OPENGL")
    target_compile_definitions(orca-rendering PUBLIC ORCA_RENDERER_DEFAULT_OPENGL)
else()
    message(FATAL_ERROR "ORCA_RENDERER_DEFAULT 必须是 vulkan 或 opengl，当前为 '${ORCA_RENDERER_DEFAULT}'")
endif()

if(ORCA_RENDERER_VULKAN)
    # Vulkan 仅作为实现细节，头文件不对外暴露，使用 PRIVATE 链接。
    target_sources(orca-rendering PRIVATE vulkan/vk_rendering_device.cpp)
    target_link_libraries(orca-rendering PRIVATE Vulkan::Vulkan)
    target_compile_definitions(orca-rendering PUBLIC ORCA_RENDERER_VULKAN)
endif()

if(ORCA_RENDERER_OPENGL)
    target_sources(orca-rendering PRIVATE opengl/gl_rendering_device.cpp)
    target_include_directories(orca-rendering PRIVATE ${PROJECT_SOURCE_DIR}/thirdparty/glad/include)
    # 静态库的 PRIVATE 依赖会以 LINK_ONLY 形式传递给最终可执行文件，因此消费方无需显式链接 GL。
    target_link_libraries(orca-rendering PRIVATE OpenGL::GL)
    target_compile_definitions(orca-rendering PUBLIC ORCA_RENDERER_OPENGL)
endif()
