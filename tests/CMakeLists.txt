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
    core/os/test_memory.cpp
    core/os/test_time.cpp

    scene/test_scene_tree_and_node.cpp
)

add_executable(orca-platform-window-test platform/test_window.cpp)
add_executable(orca-application-test application/test_application.cpp)

target_link_libraries(orca-tests PRIVATE orca-core orca-math orca-scene doctest)
target_link_libraries(orca-application-test PUBLIC orca-application)
# 该测试通过平台抽象（window.h）使用当前后端，无需直接链接具体后端库。
target_link_libraries(orca-platform-window-test PRIVATE orca-platform orca-rendering)
orca_copy_platform_runtime(orca-platform-window-test)
add_test(NAME orca-tests COMMAND orca-tests)
