# The editor.
add_executable(Sandbox
    main.cpp
    MainLayer.cpp
    Panels/SceneEditor.cpp
    Panels/ScriptEditorPanel.cpp
    Panels/CodeEditor.cpp
    Panels/AIPanels.cpp
    AIPrefabs.cpp
    Panels/PhysicsPanel.cpp
)
target_include_directories(Sandbox PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Panels
)
target_link_libraries(Sandbox PRIVATE phoenix)

# Engine assets, then the editor's own (showcase models/textures, extra shaders) on top.
# main() chdir's to the executable's directory at startup.
phoenix_copy_assets(Sandbox
    ${CMAKE_CURRENT_SOURCE_DIR}/assets
    ${CMAKE_CURRENT_SOURCE_DIR}/backpack
)
