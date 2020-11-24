project(Example)

# Add sub directories
include_directories(${PROJECT_SOURCE_DIR})
# Create the executable
add_executable(${PROJECT_NAME} 
    main.cpp
    ExampleLayer.cpp
)

# Link the static library from subproject1 using it's alias sub::lib1
# Link the header only library from subproject2 using it's alias sub::lib2
# This will cause the include directories for that target to be added to this project
target_link_libraries(${PROJECT_NAME}
    PRIVATE
    phoenix
)
