#include <Phoenix/core/layer.h>
#include <Phoenix/renderer/shader.h>
#include <Phoenix/event/keyEvent.h>
#include <Phoenix/core/application.h>
#include <Phoenix/imGui/imgui.h>
#include <Phoenix/renderer/CameraController.h>
#include <Phoenix/renderer/Buffers.h>
#include <Phoenix/renderer/VertexArray.h>
#include <Phoenix/renderer/renderer_command.h>
#include <Phoenix/renderer/Framebuffer.h>
#include "Box.h"
#include "Origin.h"


using namespace Phoenix;
class MainLayer: public Layer{
public:
    MainLayer(const std::string& name = "Layer");
    ~MainLayer() = default;

    virtual void OnAttach() override;
    virtual void OnDetach() override;

    virtual void OnUpdate(Timestep ts) override;
    virtual void OnEvent(Event& e) override;
    virtual void OnImGuiRender() override;

private:
    bool vsync = false;
private:
    Ref<Framebuffer> m_Framebuffer;
    bool m_ViewportFocused = false, m_ViewportHovered = false;
    glm::vec2 m_ViewportSize = { 0.0f, 0.0f };
    

    Ref<Origin> m_Origin;
    std::vector<Ref<Object>> m_Boxes;
    glm::vec3 m_BackgroundColor = { 0.28, 0.65, 0.87 };
    PerspectiveCameraController m_MainCamera;
private:
    void ImGuiOverlay();
    void ShowObject(const char* prefix, int uid, Ref<Object> obj);
    void DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue = 0.0f, float columnWidth = 100.0f);
};