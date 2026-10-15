add_library(orca-scene STATIC 
    node.cpp
    viewport.cpp
    scene_tree.cpp
    3d/node3d.cpp
    3d/camera_3d.cpp
)

target_link_libraries(orca-scene PRIVATE orca-object orca-rendering orca-resource orca-core)
target_include_directories(orca-scene PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

