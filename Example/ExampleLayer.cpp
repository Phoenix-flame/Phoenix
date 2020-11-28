#include "ExampleLayer.h"
#include <Phoenix/renderer/renderer_command.h>
#include <Phoenix/core/base.h>
#include <Phoenix/event/event.h>
ExampleLayer::ExampleLayer(const std::string& name): Layer(name), camera_controller(1280.0f / 720.0f){  
    this->shader = Shader::Create("/home/alireza/Programming/C++/MyGameEngineProject/Example/assets/shaders/basic.glsl");
    float vertices[] = {
        // first triangle
        -0.5f, -0.5f, 0.0f,  // left 
        -0.0f,  0.5f, 0.0f,  // right
         0.5f, -0.5f, 0.0f,  // top 
    };
    float colors[] = {
        // first
        1.0f, 0.0f,  0.0f,
        0.0f, 1.0f,  0.0f,
        0.0f, 0.0f,  1.0f,
    };
    // All triangles in one VAO
    t = CreateRef<Triangle>(vertices, colors);
    t->Init(9);
}

void ExampleLayer::OnAttach() {
    PHX_INFO("{0} attached.", this->layer_name);
}
void ExampleLayer::OnDetach() {
    PHX_INFO("{0} detached.", this->layer_name);
}

void ExampleLayer::OnUpdate(Phoenix::Timestep ts) {
    camera_controller.OnUpdate(ts);
    Phoenix::RenderCommand::SetClearColor(glm::vec4(_backgroundColor, 1.0));
    Phoenix::RenderCommand::Clear();
    shader->Bind();
    shader->SetFloat4("my_color", glm::vec4(0.4f, 0.4f, 0.4f, 0.0f));
    shader->SetMat4("projection", camera_controller.GetCamera().GetViewProjectionMatrix());
    t->Draw();
    shader->Unbind();
}
void ExampleLayer::OnEvent(Phoenix::Event& e) {
    // PHX_INFO("{0}", e);
    camera_controller.OnEvent(e);
    EventDispatcher dispacher(e);
    dispacher.Dispatch<KeyPressedEvent>([](KeyPressedEvent& e){
        if (e.GetKeyCode() == Phoenix::Key::Escape){
            Phoenix::Application::Get().Close();
            return true;
        }
        return false;
    });
}


void ExampleLayer::OnImGuiRender(){
    ImGui::ShowMetricsWindow();
    ImGui::ShowDemoWindow();
    ImGui::Begin("Settings", nullptr, (ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize) & ImGuiWindowFlags_None);
	ImGui::Text("Hello");
    ImGui::ColorEdit3("Background Color", glm::value_ptr(_backgroundColor));
    if (ImGui::Checkbox("VSync", &vsync)){
        Application::Get().GetWindow().SetVSync(vsync);
    }
	ImGui::End();
}
