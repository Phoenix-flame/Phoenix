# The editor.
add_executable(Sandbox
    main.cpp
    MainLayer.cpp
    Panels/SceneEditor.cpp
    Panels/ScriptEditorPanel.cpp
    Panels/CodeEditor.cpp
)
target_include_directories(Sandbox PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Panels
)
target_link_libraries(Sandbox PRIVATE phoenix)

# Engine assets, then the editor's own (showcase models/textures, extra shaders) on top.
# main() chdir's to the executable's directory at startup.
phoenix_copy_assets(Sandbox)
add_custom_command(TARGET Sandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:Sandbox>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/backpack $<TARGET_FILE_DIR:Sandbox>/backpack
    VERBATIM
)
