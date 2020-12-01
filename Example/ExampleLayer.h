#include <Phoenix/core/layer.h>
#include <Phoenix/renderer/shader.h>
#include <Phoenix/event/keyEvent.h>
#include <Phoenix/core/application.h>
#include <Phoenix/imGui/imgui.h>
#include <Phoenix/renderer/CameraController.h>
#include <Phoenix/renderer/Buffers.h>
#include <Phoenix/renderer/VertexArray.h>
#include <Phoenix/renderer/renderer_command.h>
#include "Box.h"

using namespace Phoenix;

class ExampleLayer: public Layer{
public:
    ExampleLayer(const std::string& name = "Layer");
    ~ExampleLayer() = default;

    virtual void OnAttach() override;
    virtual void OnDetach() override;

    virtual void OnUpdate(Timestep ts) override;
    virtual void OnEvent(Event& e) override;
    virtual void OnImGuiRender() override;

private:
    bool vsync = false;
private:
    Ref<Shader> shader;
    Ref<Box> box;
    glm::vec3 _backgroundColor = { 0.28, 0.65, 0.87 };
    OrthographicCameraController main_camera;
    PerspectiveCameraController second_camera;
    int selected_camera = 0;
    bool overlayEnabled = true;
    float fov = 45.0;
private:
    void ImGuiOverlay();
};