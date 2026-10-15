add_library(orca-application STATIC
    application.cpp
)

target_link_libraries(orca-application PUBLIC 
    orca-core 
    orca-math 
    orca-object 
    orca-scene
    orca-rendering
    orca-platform
)

target_include_directories(orca-application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})