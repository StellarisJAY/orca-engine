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
)

add_executable(orca-platform-window-test platform/test_window.cpp)

target_link_libraries(orca-tests PRIVATE orca-math doctest)
# 该测试通过平台抽象（window.h）使用当前后端，无需直接链接具体后端库。
target_link_libraries(orca-platform-window-test PRIVATE orca-platform orca-rendering)
orca_copy_platform_runtime(orca-platform-window-test)
add_test(NAME orca-tests COMMAND orca-tests)
