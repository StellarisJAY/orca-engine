add_library(orca-resource STATIC 
    "resource.cpp"
    mesh/mesh.cpp
    material/material.cpp
    environment/environment_3d.cpp
)

target_link_libraries(orca-resource PUBLIC orca-object orca-math)
target_include_directories(orca-resource PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})