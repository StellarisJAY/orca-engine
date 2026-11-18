# 渲染服务器（RenderingServer / RHI）。
#
# 公共部分（rendering_device.*）与具体图形 API 无关，只依赖抽象的 RenderingDevice 接口。
# 每个后端各自编译成一个实现文件，并通过 ORCA_RENDERER_<API> 宏接入工厂。
add_library(orca-rendering STATIC
    rendering_device.cpp
    rendering_device_refactor.cpp
    rendering_server.cpp
    rendering_storage.cpp
    rhi_validation.cpp
    shader_loader.cpp
    skybox_cube.cpp
)

target_include_directories(orca-rendering PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(orca-rendering PUBLIC orca-math orca-core)

# shader 以 glsl 文件为唯一来源：
# 1. 开发期回退路径直接指向源码树，改动后无需拷贝即可生效；
# 2. 同时按资源目录约定拷贝到构建目录，供 res://shaders/... 加载。
# 用 ALL 自定义目标而不是静态库的 POST_BUILD，保证只改 glsl 时也能同步拷贝。
target_compile_definitions(orca-rendering PRIVATE
    ORCA_SHADER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
)
file(GLOB_RECURSE ORCA_SHADER_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.glsl")
add_custom_target(orca-shader-resources ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        "${CMAKE_BINARY_DIR}/resources/shaders"
    DEPENDS ${ORCA_SHADER_FILES}
    COMMENT "Copying shader sources to runtime resources")
add_dependencies(orca-rendering orca-shader-resources)

# 后端默认驱动由各接口自己的工厂决定：旧 RenderingDevice 在编译了 GL 时默认 OpenGL，
# 新 RHI（RenderingDeviceRefactor）固定为 Vulkan，因此不再需要全局默认后端宏。

if(ORCA_RENDERER_VULKAN)
    # Vulkan 仅作为实现细节，头文件不对外暴露，使用 PRIVATE 链接。
    target_sources(orca-rendering PRIVATE 
        vulkan/vk_rendering_device.cpp 
        vulkan/vk_rendering_device_init.cpp
        vulkan/vk_resource.cpp
        vulkan/vk_buffer.cpp
        vulkan/vk_format.cpp
        vulkan/vk_memory.cpp
        vulkan/vk_image_layout.cpp
        vulkan/vk_pipeline.cpp
        vulkan/vk_command.cpp
        vulkan/vk_swapchain.cpp
    )
    target_link_libraries(orca-rendering PRIVATE Vulkan::Vulkan)
    target_compile_definitions(orca-rendering PUBLIC ORCA_RENDERER_VULKAN)
endif()

if(ORCA_RENDERER_OPENGL)
    # 旧 RenderingDevice 的 OpenGL 实现（RenderingServer 仍走这条路径）。
    # 新 RHI（RenderingDeviceRefactor）的 OpenGL 适配层已移除，只保留 Vulkan 实现。
    # gl_loader.cpp 是 glad 实现（GLAD_GL_IMPLEMENTATION）的唯一翻译单元，
    # 其它 OpenGL 源文件只 include 头文件获取声明，避免符号重复定义。
    target_sources(orca-rendering PRIVATE
        opengl/gl_loader.cpp
        opengl/gl_rendering_device.cpp)
    target_include_directories(orca-rendering PRIVATE ${PROJECT_SOURCE_DIR}/thirdparty/glad/include)
    # 静态库的 PRIVATE 依赖会以 LINK_ONLY 形式传递给最终可执行文件，因此消费方无需显式链接 GL。
    target_link_libraries(orca-rendering PRIVATE OpenGL::GL)
    target_compile_definitions(orca-rendering PUBLIC ORCA_RENDERER_OPENGL)
endif()
