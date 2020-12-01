#include "MainLayer.h"

#include <Phoenix/core/base.h>
#include <Phoenix/event/event.h>

MainLayer::MainLayer(const std::string& name): Layer(name), 
        m_MainCamera(1280.0f / 720.0f, glm::radians(45.0), 0.1, 100.0)
    {  

    for (int i = -5 ; i <= 5 ; i++){
        m_Boxes.push_back(CreateRef<Box>("Box " + std::to_string(i + 5) ,glm::vec3(2*i, 0, 0)));
    }
}

void MainLayer::OnAttach() {
    PHX_INFO("{0} attached.", this->layer_name);
}
void MainLayer::OnDetach() {
    PHX_INFO("{0} detached.", this->layer_name);
}

void MainLayer::OnUpdate(Phoenix::Timestep ts) {
    m_MainCamera.OnUpdate(ts);
    
    Phoenix::RenderCommand::SetClearColor(glm::vec4(m_BackgroundColor, 1.0));
    Phoenix::RenderCommand::Clear();

    glm::mat4 projection = m_MainCamera.GetCamera().GetViewProjectionMatrix();
    for (auto b:m_Boxes){
        b->Draw(projection);
    }

}
void MainLayer::OnEvent(Phoenix::Event& e) {
    m_MainCamera.OnEvent(e);
    EventDispatcher dispacher(e);
    dispacher.Dispatch<KeyPressedEvent>([](KeyPressedEvent& e){
        if (e.GetKeyCode() == Phoenix::Key::Escape){
            Phoenix::Application::Get().Close();
            return true;
        }
        return false;
    });
}


void MainLayer::OnImGuiRender(){
    ImGui::ShowMetricsWindow();
    ImGui::Begin("Settings", nullptr, (ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize) & ImGuiWindowFlags_None);
	ImGui::Text("Hello");
    ImGui::ColorEdit3("Background Color", glm::value_ptr(m_BackgroundColor));
    if (ImGui::Checkbox("VSync", &vsync)){
        Application::Get().GetWindow().SetVSync(vsync);
    }
	ImGui::End();
    ImGui::Begin("Objects", nullptr, (ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize) & ImGuiWindowFlags_None);
    for (auto& o:m_Boxes){
        ImGui::Checkbox(static_cast<std::string>(*o).c_str(), o->GetEnablePtr());
    }
    ImGui::End();

}

