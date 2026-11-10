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
)

# gl_internal.h 里的格式映射断言直接对比 GL 常量，因此单元测试需要 glad 头文件。
# 只加 include 路径：实现（GLAD_GL_IMPLEMENTATION）仍然只存在于 orca-rendering 的 gl_loader.cpp。
if(ORCA_RENDERER_OPENGL)
    target_include_directories(orca-tests PRIVATE ${PROJECT_SOURCE_DIR}/thirdparty/glad/include)
endif()

add_executable(orca-platform-window-test platform/test_window.cpp)
add_executable(orca-application-test application/test_application.cpp)
add_executable(orca-app-test-sponza application/test_scene_sponza.cpp)

add_executable(orca-vulkan-init-test servers/test_vulkan_init.cpp)
target_link_libraries(orca-vulkan-init-test PRIVATE orca-core orca-math orca-scene orca-rendering orca-platform)

target_link_libraries(orca-tests PRIVATE orca-core orca-math orca-scene orca-rendering orca-resource doctest)
target_link_libraries(orca-application-test PUBLIC orca-application)
target_link_libraries(orca-app-test-sponza PUBLIC orca-application)
# 该测试通过平台抽象（window.h）使用当前后端，无需直接链接具体后端库。
target_link_libraries(orca-platform-window-test PRIVATE orca-platform orca-rendering)
orca_copy_platform_runtime(orca-platform-window-test)
add_test(NAME orca-tests COMMAND orca-tests)
