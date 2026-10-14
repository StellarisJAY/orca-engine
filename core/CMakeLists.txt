add_library(orca-object STATIC
    object/object.cpp
)

target_include_directories(orca-object PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(orca-math STATIC
    math/vector2.cpp
    math/vector3.cpp
    math/vector4.cpp
    math/basis.cpp
    math/transform_2d.cpp
    math/transform_3d.cpp
    math/projection.cpp
)

target_include_directories(orca-math PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})