add_library(orca-resource STATIC 
    "resource.cpp"
    resource_cache.cpp
    resource_loader.cpp
    loaders/obj_loader.cpp
    loaders/mtl_loader.cpp
    mesh/mesh.cpp
    material/material.cpp
    material/material_library.cpp
    environment/environment_3d.cpp
    loaders/image_texture_loader.cpp
    loaders/gltf_loader.cpp
    scene/scene.cpp
    environment/skybox.cpp
)

target_link_libraries(orca-resource PRIVATE orca-object orca-math orca-core stb_image cgltf)
target_include_directories(orca-resource PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})