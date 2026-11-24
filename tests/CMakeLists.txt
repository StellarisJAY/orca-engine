add_executable(orca-tests
    main.cpp
    core/math/test_vector2.cpp
    core/math/test_vector3.cpp
    core/math/test_vector4.cpp
    core/math/test_basis.cpp
    core/math/test_transform_2d.cpp
    core/math/test_transform_3d.cpp
    core/math/test_projection.cpp
    core/object/test_ref_counted.cpp
    core/object/test_rid.cpp
    core/os/test_memory.cpp
    core/os/test_time.cpp
    core/io/test_file_access_and_path.cpp

    "resource/test_obj_loader.cpp"
    "resource/test_image_texture_loader.cpp"
    "resource/test_mesh.cpp"
    "resource/test_scene.cpp"
    "resource/test_gltf_loader.cpp"
    "resource/test_skybox.cpp"

    scene/test_scene_tree_and_node.cpp
    scene/test_node3d_transform.cpp

    servers/test_skybox_cube.cpp
    servers/test_rhi_validation.cpp
)

# Vulkan 专属的纯逻辑测试（格式/用途映射）只在启用 Vulkan 后端时编译。
# 头文件会引入 vulkan.hpp，因此需要 Vulkan 的 include dir 与链接目标。
if(ORCA_RENDERER_VULKAN)
    target_sources(orca-tests PRIVATE servers/test_vk_format.cpp servers/test_vk_image_layout.cpp)
    target_link_libraries(orca-tests PRIVATE Vulkan::Vulkan)
endif()

add_executable(orca-application-test application/test_application.cpp)
add_executable(orca-app-test-sponza application/test_scene_sponza.cpp)

add_executable(orca-vulkan-init-test servers/test_vulkan_init.cpp)
target_link_libraries(orca-vulkan-init-test PRIVATE orca-core orca-math orca-scene orca-rendering orca-platform)

target_link_libraries(orca-tests PRIVATE orca-core orca-math orca-scene orca-rendering orca-resource doctest)
target_link_libraries(orca-application-test PUBLIC orca-application)
target_link_libraries(orca-app-test-sponza PUBLIC orca-application)
add_test(NAME orca-tests COMMAND orca-tests)
