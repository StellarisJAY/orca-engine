add_library(orca-scene STATIC 
    node.cpp
    scene_tree.cpp
)

target_link_libraries(orca-scene PUBLIC orca-object)
target_include_directories(orca-scene PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

