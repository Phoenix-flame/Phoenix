#include <vector>
#include <string>
#include <Phoenix/core/layer.h>
#include <Phoenix/renderer/shader.h>
#include <Phoenix/event/keyEvent.h>
#include <Phoenix/core/application.h>
#include <Phoenix/imGui/imgui.h>
#include <vendor/imFileDialog/ImGuiFileBrowser.h>
#include <Phoenix/renderer/CameraController.h>
#include <Phoenix/renderer/Framebuffer.h>
#include <Phoenix/renderer/Bloom.h>
#include <Phoenix/renderer/renderer_command.h>
#include <Phoenix/Scene/Scene.h>
#include <Phoenix/event/keyEvent.h>
#include "Panels/SceneEditor.h"


using namespace Phoenix;
namespace Phoenix { struct TerrainComponent; }

class MainLayer: public Layer{
public:
    MainLayer(const std::string& name = "Layer");
    ~MainLayer() = default;

    virtual void OnAttach() override;
    virtual void OnDetach() override;

    virtual void OnUpdate(Phoenix::Timestep ts) override;
    virtual void OnEvent(Phoenix::Event& e) override;
    virtual void OnImGuiRender() override;

private:
    bool OnResize(WindowResizeEvent& e){
        m_ViewportSize.x = (float)e.GetWidth();
        m_ViewportSize.y = (float)e.GetHeight();
        return false;
    }
    bool OnKeyPressed(KeyPressedEvent& e);

    // Snapshot-based undo/redo. CommitHistory records a snapshot once an edit
    // settles (no active gizmo/widget); Undo/Redo restore snapshots.
    void CommitHistory();
    void Undo();
    void Redo();
    void RestoreScene(const std::string& snapshot);

    // Dock the editor panels around the viewport (first run, or View > Reset Layout).
    void BuildDefaultLayout(ImGuiID dockspaceID);

    // Make scene the edited scene: stops any running simulation on the old one,
    // retargets the editor panels and starts a fresh undo history.
    void SetActiveScene(const Ref<Scene>& scene);
    // SetActiveScene(new empty scene), then populate it with one of the Build* showcases.
    void LoadShowcase(void (MainLayer::*build)());

    // World-space ray through the mouse cursor; false if the cursor is outside the
    // viewport image whose top-left screen position is imageMin.
    bool MouseRay(const ImVec2& imageMin, glm::vec3& outOrigin, glm::vec3& outDir) const;

    // Run/Stop: Stop restores the edit-time scene captured when Run was pressed.
    void StartRuntime();
    void StopRuntime();

    // Populate m_Scene with the bloom/glow/shadow showcase.
    void BuildShowcaseScene();
    // Populate m_Scene with the terrain-lake + water showcase.
    void BuildWaterShowcase();
    // Populate m_Scene with the skeletal-animation (walking robot) showcase.
    void BuildRobotShowcase();
    // Populate m_Scene with the multi-light shadow showcase (3 coloured suns).
    void BuildMultiShadowShowcase();
    // Populate m_Scene with the physics playground (joints, materials, triggers, cannon).
    void BuildPhysicsShowcase();

    // Terrain sculpting: raycast to the terrain plane and raise/lower/smooth heights.
    void SculptTerrain(TerrainComponent& terrain, const glm::vec3& terrainPos,
                       const glm::vec3& terrainScale, const glm::vec3& rayOrigin,
                       const glm::vec3& rayDir, float dt);

private:
    bool vsync = true;
private:
    Ref<Framebuffer> m_Framebuffer;
    bool m_ViewportFocused = false, m_ViewportHovered = false;
    glm::vec2 m_ViewportSize = { 0.0f, 0.0f };
    
    glm::vec3 m_BackgroundColor = { 0.0, 0.0, 0.0 };
    EditorCamera m_MainCamera;
    Ref<Phoenix::SceneEditor> m_SceneEditor;
    Ref<Phoenix::Scene> m_Scene;

    Scope<Phoenix::ShaderLibrary> m_ShaderLibrary;

    bool m_OverlayEnabled = true;

    bool m_LayoutChecked = false; // first-frame check for a saved dock layout
    bool m_ResetLayout = false;   // rebuild the default layout before the next DockSpace()
    int m_SelectDefaultTabsFrames = 0; // frames left to select the default tabs after a rebuild

    // Active ImGuizmo operation: 0=translate, 1=rotate, 2=scale, -1=none.
    int m_GizmoType = 0;

    // Undo/redo history (YAML scene snapshots).
    std::vector<std::string> m_UndoStack;
    std::vector<std::string> m_RedoStack;
    std::string m_LastSnapshot;
    bool m_WasEditing = false; // a widget/gizmo/sculpt edit was in progress last frame

    // Edit-time scene captured on Run, restored on Stop.
    std::string m_RuntimeSnapshot;

    // Bloom post-processing.
    Bloom m_Bloom;
    bool m_BloomEnabled = true;
    float m_BloomIntensity = 1.0f;
    float m_BloomThreshold = 1.0f;
    float m_Exposure = 1.8f; // pre-tonemap exposure (compensates for ACES darkening)

    // Terrain sculpting tool.
    bool m_TerrainSculpt = false;
    int m_BrushMode = 0; // 0 raise, 1 lower, 2 smooth
    float m_BrushRadius = 5.0f;
    float m_BrushStrength = 6.0f;
    bool m_Sculpting = false; // actively dragging this frame (gates undo capture)

private:
    imgui_addons::ImGuiFileBrowser file_dialog;

};