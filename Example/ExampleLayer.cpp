#include "ExampleLayer.h"

#include <Phoenix/core/application.h>
#include <Phoenix/event/keyEvent.h>
#include <Phoenix/imGui/imgui.h>
#include <Phoenix/renderer/renderer.h>
#include <Phoenix/renderer/renderer_command.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

ExampleLayer::ExampleLayer(const std::string& name): Layer(name){
    m_Material.diffuse = m_Material.ambient = glm::vec3(0.95f, 0.45f, 0.20f);
    m_Light.castsShadow = false;
}

void ExampleLayer::OnUpdate(Timestep ts){
    m_Angle += m_SpinSpeed * (float)ts;

    Window& window = Application::Get().GetWindow();
    float aspect = window.GetHeight() > 0 ? (float)window.GetWidth() / (float)window.GetHeight() : 1.0f;

    glm::vec3 cameraPos(2.5f, 2.0f, 3.5f);
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
    glm::mat4 view = glm::lookAt(cameraPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    RenderCommand::SetClearColor(glm::vec4(m_BackgroundColor, 1.0f));
    RenderCommand::Clear();

    // One directional light, no point lights. SetLights uploads them once per frame;
    // every Submit after it is lit by them.
    glm::vec3 lightDir = glm::normalize(m_LightDirection);
    Renderer::BeginScene(projection, view, cameraPos);
    Renderer::SetLights(m_Ambient, &m_Light, &lightDir, 1, nullptr, nullptr, 0);

    glm::mat4 transform = glm::rotate(glm::mat4(1.0f), glm::radians(m_Angle), glm::vec3(0.3f, 1.0f, 0.1f));
    Renderer::SubmitCube(m_Material, transform);

    Renderer::EndScene();
}

void ExampleLayer::OnEvent(Event& e){
    EventDispatcher dispatcher(e);
    dispatcher.Dispatch<KeyPressedEvent>([](KeyPressedEvent& e){
        if (e.GetKeyCode() == Key::Escape){
            Application::Get().Close();
            return true;
        }
        return false;
    });
}

void ExampleLayer::OnImGuiRender(){
    ImGui::SetNextWindowSize(ImVec2(340.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Example");
    ImGui::Text("%.1f FPS", ImGui::GetIO().Framerate);
    ImGui::ColorEdit3("Cube", glm::value_ptr(m_Material.diffuse));
    m_Material.ambient = m_Material.diffuse;
    ImGui::DragFloat("Spin (deg/s)", &m_SpinSpeed, 1.0f, -360.0f, 360.0f);
    ImGui::DragFloat3("Light direction", glm::value_ptr(m_LightDirection), 0.01f, -1.0f, 1.0f);
    ImGui::ColorEdit3("Ambient", glm::value_ptr(m_Ambient));
    ImGui::ColorEdit3("Background", glm::value_ptr(m_BackgroundColor));
    if (ImGui::Checkbox("VSync", &m_VSync))
        Application::Get().GetWindow().SetVSync(m_VSync);
    ImGui::TextDisabled("Esc quits");
    ImGui::End();
}
