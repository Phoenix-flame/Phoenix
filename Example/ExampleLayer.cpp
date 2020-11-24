#include "ExampleLayer.h"
#include <Phoenix/renderer/renderer_command.h>
#include <Phoenix/core/base.h>
#include <Phoenix/event/event.h>
ExampleLayer::ExampleLayer(const std::string& name): Layer(name){  
    this->shader = Shader::Create("/home/alireza/Programming/C++/MyGameEngineProject/Example/assets/shaders/basic.glsl");
   
}

void ExampleLayer::OnAttach() {
    PHX_INFO("{0} attached.", this->layer_name);
}
void ExampleLayer::OnDetach() {
    PHX_INFO("{0} detached.", this->layer_name);
}

void ExampleLayer::OnUpdate(Phoenix::Timestep ts) {
    Phoenix::RenderCommand::SetClearColor(glm::vec4(0.9, 0.5, 0.4, 1.0));
    Phoenix::RenderCommand::Clear();
    shader->Bind();
   
    shader->Unbind();
}
void ExampleLayer::OnEvent(Phoenix::Event& e) {
    PHX_INFO("{0}", e);
    EventDispatcher dispacher(e);
    dispacher.Dispatch<KeyPressedEvent>([](KeyPressedEvent& e){
        if (e.GetKeyCode() == Phoenix::Key::Escape){
            Phoenix::Application::Get().Close();
            return true;
        }
        return false;
    });

}