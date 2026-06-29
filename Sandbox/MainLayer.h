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
#include <Phoenix/renderer/renderer_command.h>
#include <Phoenix/Scene/Scene.h>
#include <Phoenix/event/keyEvent.h>
#include "Panels/SceneEditor.h"


using namespace Phoenix;
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

    // Active ImGuizmo operation: 0=translate, 1=rotate, 2=scale, -1=none.
    int m_GizmoType = 0;

    // Undo/redo history (YAML scene snapshots).
    std::vector<std::string> m_UndoStack;
    std::vector<std::string> m_RedoStack;
    std::string m_LastSnapshot;

private:
    imgui_addons::ImGuiFileBrowser file_dialog;

};