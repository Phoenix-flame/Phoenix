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
#include "Panels/AIPanels.h"
#include <Phoenix/AI/Bridge.h>
#include <Phoenix/AI/Environment.h>


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
    // Editor keyboard shortcuts (undo/redo, file, play, gizmo tools).
    void HandleShortcuts();

    // Snapshot-based undo/redo. CommitHistory records a snapshot once an edit
    // settles (no active gizmo/widget); Undo/Redo restore snapshots.
    void CommitHistory();
    void Undo();
    void Redo();
    void RestoreScene(const std::string& snapshot);

    // Dock the editor panels around the viewport (first run, or View > Reset Layout).
    void BuildDefaultLayout(ImGuiID dockspaceID);

    // Editor chrome. The title bar holds the logo, menus, Play/Stop and (for the
    // undecorated window) the window controls; it also drags the OS window.
    void DrawTitleBar();
    void DrawMenus();
    void DrawStatusBar();
    // Gizmo tools and play-state overlays drawn on top of the viewport image.
    void DrawViewportOverlay(const ImVec2& imageMin, const ImVec2& imageSize);
    void DrawSettingsPanel();
    void DrawProfilerPanel();
    void DrawShaderLibraryPanel();
    void DrawHelpPopups();
    // Resize the undecorated window from its edges (the OS draws no frame for it).
    void UpdateWindowResize();

    // Make scene the edited scene: stops any running simulation on the old one,
    // retargets the editor panels and starts a fresh undo history.
    void SetActiveScene(const Ref<Scene>& scene);
    // SetActiveScene(new empty scene), then populate it with one of the Build* showcases.
    void LoadShowcase(void (MainLayer::*build)(), const char* name);

    // World-space ray through the mouse cursor; false if the cursor is outside the
    // viewport image whose top-left screen position is imageMin.
    bool MouseRay(const ImVec2& imageMin, glm::vec3& outOrigin, glm::vec3& outDir) const;

    // Run/Stop: Stop restores the edit-time scene captured when Run was pressed.
    void StartRuntime();
    void StopRuntime();
    void TogglePlay();

    // File menu actions (scenes are read/written at m_ScenePath).
    void NewScene();
    void ImportScene();
    void ExportScene();

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
    // Populate m_Scene with a maze a capsule solves with A* (all logic in Lua scripts).
    void BuildMazeShowcase();
    // Populate m_Scene with the robot platformer: platforms, shooting, collectibles.
    void BuildRobotArenaShowcase();
    // Script HUD (ShowText / ShowBanner) over the viewport image while playing.
    void DrawScriptHud(const ImVec2& imageMin);

    // RL: four parallel arenas where walker agents learn to find a randomized goal.
    void BuildAIGoalShowcase();
    // RL: rolling balls learn to push themselves onto a target without falling off.
    void BuildAIRollerShowcase();

    // Runs the RL environment (fixed steps, time scale, Python bridge) for this frame.
    void UpdateSimulation(float frameSeconds);
    AIPanelContext MakeAIContext();

    // Populate m_Scene with the shadow showcase: soft sun shadows with contact hardening,
    // shadows falling on other objects, and two orbiting point lights with cube shadows.
    void BuildShadowShowcase();

    // Populate m_Scene with the vehicle test track (car, ramp, cones, crates, ragdoll dummies).
    void BuildVehicleShowcase();

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
    bool m_GizmoWorld = false; // world- instead of local-space handles
    bool m_GizmoSnap = false;  // snap to increments (Ctrl inverts while dragging)

    // Shown in the title bar: the loaded showcase / file name.
    std::string m_SceneName = "Untitled";
    std::string m_ScenePath = "scene.phx";

    // Panels owned by the main layer (the scene panels' flags live in SceneEditor).
    bool m_ShowSettings = true;
    bool m_ShowProfiler = true;
    bool m_ShowShaderLibrary = true;
    bool m_OpenImportShader = false;
    bool m_OpenShortcuts = false;
    bool m_OpenAbout = false;

    // Custom title bar: dragging the window by the bar, resizing it by its edges.
    bool m_TitleDragging = false;
    bool m_TitleDragMoved = false;
    glm::vec2 m_DragStartCursor = { 0.0f, 0.0f };
    glm::vec2 m_DragGrab = { 0.0f, 0.0f }; // cursor in window coords; x as a fraction of the width
    int m_HoverEdges = 0;  // window edges under the cursor (bitmask, see UpdateWindowResize)
    int m_ResizeEdges = 0; // edges being dragged
    glm::vec2 m_ResizeStartCursor = { 0.0f, 0.0f };
    glm::ivec2 m_ResizeStartPos = { 0, 0 };
    glm::ivec2 m_ResizeStartSize = { 0, 0 };

    // Undo/redo history (YAML scene snapshots).
    std::vector<std::string> m_UndoStack;
    std::vector<std::string> m_RedoStack;
    std::string m_LastSnapshot;
    bool m_WasEditing = false; // a widget/gizmo/sculpt edit was in progress last frame

    // Edit-time scene captured on Run, restored on Stop.
    std::string m_RuntimeSnapshot;

    // RL environment API: the environment exists while playing; the episode log and
    // the bridge persist across Play sessions. Learning happens in the client.
    Scope<Environment> m_Environment;
    EpisodeLog m_EpisodeLog;
    Bridge m_Bridge;
    AIPanels m_AIPanels;
    bool m_BridgeEnabled = false;
    int m_BridgePort = 5555;
    int m_BridgeFailedPort = 0; // don't retry a port that failed to bind every frame
    float m_TimeScale = 1.0f;
    bool m_MaxSpeed = false;
    bool m_RenderAtMaxSpeed = true;
    bool m_Paused = false;
    bool m_OpenGuide = false;

    // Bloom post-processing.
    Bloom m_Bloom;
    int m_MSAASamples = 4; // 1 = anti-aliasing off
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