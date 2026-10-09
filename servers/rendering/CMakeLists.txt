# 渲染服务器（RenderingServer / RHI）。
# 后端实现按需逐步加入，当前提供 Vulkan 后端骨架。

# target_include_directories(orca-rendering PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# target_link_libraries(orca-rendering PUBLIC orca-math)

# if(ORCA_RENDERER_VULKAN)
    # Vulkan 仅作为实现细节，不需要向消费者暴露头文件，使用 PRIVATE 链接。
    # target_link_libraries(orca-rendering PRIVATE Vulkan::Vulkan)
    # target_compile_definitions(orca-rendering PUBLIC ORCA_RENDERER_VULKAN)
# endif()
