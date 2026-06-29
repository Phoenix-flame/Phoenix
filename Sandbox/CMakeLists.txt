project(Sandbox)

# Add sub directories
include_directories(
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/Panels
)
# Create the executable
add_executable(${PROJECT_NAME} 
    main.cpp
    MainLayer.cpp
    Panels/SceneEditor.cpp
)

# Link the static library from subproject1 using it's alias sub::lib1
# Link the header only library from subproject2 using it's alias sub::lib2
# This will cause the include directories for that target to be added to this project
target_link_libraries(${PROJECT_NAME}
    PRIVATE
    phoenix
)

# Copy assets next to the executable so they resolve relative to the binary
# (main() chdir's to the executable's directory at startup).
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets   DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/backpack DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
