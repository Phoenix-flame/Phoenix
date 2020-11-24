#include "ExampleLayer.h"

ExampleLayer::ExampleLayer(const std::string& name){  
    this->shader = Shader::Create("/home/alireza/Programming/C++/MyGameEngineProject/Example/assets/shaders/basic.glsl");
    
    float vertices[] = {
        // first triangle
        -0.9f, -0.5f, 0.0f,  // left 
        -0.0f, -0.5f, 0.0f,  // right
        -0.45f, 0.5f, 0.0f,  // top 
        // second triangle
         0.0f, -0.5f, 0.0f,  // left
         0.9f, -0.5f, 0.0f,  // right
         0.45f, 0.5f, 0.0f,   // top 
        // third triangle
        -0.45f, 0.5f, 0.0f,  // left
         0.45f, 0.5f, 0.0f,   // right
        -0.0f, -0.5f, 0.0f,  // bottom

    };
    float colors[] = {
        // first
        1.0f, 0.0f,  0.0f,
        0.0f, 1.0f,  0.0f,
        0.0f, 0.0f,  1.0f,
        // second
        0.0f, 1.0f,  0.0f,
        0.0f, 0.0f,  1.0f,
        1.0f, 0.0f,  0.0f,
        // third
        0.0f, 0.0f,  1.0f,
        1.0f, 0.0f,  0.0f,
        0.0f, 1.0f,  0.0f
    };
    
    // All triangles in one VAO
    this->three_triangle = CreateRef<Triangle>(vertices, colors);
    three_triangle->Init(27);
}

void ExampleLayer::OnAttach() {
    PHX_INFO("{0} attached.", this->layer_name);
}
void ExampleLayer::OnDetach() {
    PHX_INFO("{0} detached.", this->layer_name);
}

void ExampleLayer::OnUpdate(Phoenix::Timestep ts) {
    shader->Bind();
    three_triangle->Draw(9);
    shader->Unbind();
}
void ExampleLayer::OnEvent(Phoenix::Event& e) {

}