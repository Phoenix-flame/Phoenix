#pragma once
#include "Object.h"
#include <Phoenix/renderer/Texture.h>
class Box: public Object{
public:
    Box(std::string name, const glm::vec3& pos = glm::vec3(0.0f, 0.0f, 0.0f)): Object(pos, name){
        m_Shader = Phoenix::Shader::Create("/home/alireza/Programming/C++/MyGameEngineProject/Example/assets/shaders/basic.glsl");
        m_Vertex_array = Phoenix::CreateRef<Phoenix::VertexArray>();
        m_Vertex_array->Bind();
        Phoenix::Ref<Phoenix::VertexBuffer> vertexBuffer = Phoenix::CreateRef<Phoenix::VertexBuffer>(this->vertices, sizeof(this->vertices));
        Phoenix::BufferLayout layout = {
            { Phoenix::ShaderDataType::Float3, "a_Position" },
            { Phoenix::ShaderDataType::Float3, "a_Color" }
        };
        vertexBuffer->SetLayout(layout);
        m_Vertex_array->AddVertexBuffer(vertexBuffer);
        Phoenix::Ref<Phoenix::IndexBuffer> indexBuffer = Phoenix::CreateRef<Phoenix::IndexBuffer>(indices, sizeof(indices) / sizeof(unsigned int));
        m_Vertex_array->SetIndexBuffer(indexBuffer);
    }


private:
    float vertices[218] = {
        -0.5f, -0.5f, -0.5f,  0.4f,  0.4f,  1.0f,
         0.5f, -0.5f, -0.5f,  0.4f,  0.4f,  1.0f,
         0.5f,  0.5f, -0.5f,  0.4f,  0.4f,  1.0f,
         0.5f,  0.5f, -0.5f,  0.4f,  0.4f,  1.0f,
        -0.5f,  0.5f, -0.5f,  0.4f,  0.4f,  1.0f,
        -0.5f, -0.5f, -0.5f,  0.4f,  0.4f,  1.0f,

        -0.5f, -0.5f,  0.5f,  0.4f,  0.4f,  1.0f,
         0.5f, -0.5f,  0.5f,  0.4f,  0.4f,  1.0f,
         0.5f,  0.5f,  0.5f,  0.4f,  0.4f,  1.0f,
         0.5f,  0.5f,  0.5f,  0.4f,  0.4f,  1.0f,
        -0.5f,  0.5f,  0.5f,  0.4f,  0.4f,  1.0f,
        -0.5f, -0.5f,  0.5f,  0.4f,  0.4f,  1.0f,

        -0.5f,  0.5f,  0.5f,  1.0f,  0.4f,  0.4f,
        -0.5f,  0.5f, -0.5f,  1.0f,  0.4f,  0.4f,
        -0.5f, -0.5f, -0.5f,  1.0f,  0.4f,  0.4f,
        -0.5f, -0.5f, -0.5f,  1.0f,  0.4f,  0.4f,
        -0.5f, -0.5f,  0.5f,  1.0f,  0.4f,  0.4f,
        -0.5f,  0.5f,  0.5f,  1.0f,  0.4f,  0.4f,

         0.5f,  0.5f,  0.5f,  1.0f,  0.4f,  0.4f,
         0.5f,  0.5f, -0.5f,  1.0f,  0.4f,  0.4f,
         0.5f, -0.5f, -0.5f,  1.0f,  0.4f,  0.4f,
         0.5f, -0.5f, -0.5f,  1.0f,  0.4f,  0.4f,
         0.5f, -0.5f,  0.5f,  1.0f,  0.4f,  0.4f,
         0.5f,  0.5f,  0.5f,  1.0f,  0.4f,  0.4f,

        -0.5f, -0.5f, -0.5f,  0.4f,  1.0f,  0.4f,
         0.5f, -0.5f, -0.5f,  0.4f,  1.0f,  0.4f,
         0.5f, -0.5f,  0.5f,  0.4f,  1.0f,  0.4f,
         0.5f, -0.5f,  0.5f,  0.4f,  1.0f,  0.4f,
        -0.5f, -0.5f,  0.5f,  0.4f,  1.0f,  0.4f,
        -0.5f, -0.5f, -0.5f,  0.4f,  1.0f,  0.4f,

        -0.5f,  0.5f, -0.5f,  0.4f,  1.0f,  0.4f,
         0.5f,  0.5f, -0.5f,  0.4f,  1.0f,  0.4f,
         0.5f,  0.5f,  0.5f,  0.4f,  1.0f,  0.4f,
         0.5f,  0.5f,  0.5f,  0.4f,  1.0f,  0.4f,
        -0.5f,  0.5f,  0.5f,  0.4f,  1.0f,  0.4f,
        -0.5f,  0.5f, -0.5f,  0.4f,  1.0f,  0.4f
    };
    unsigned int indices[36] = {
        0, 1, 2,   // first Box
        3, 4, 5,
        6, 7, 8,
        9, 10, 11,
        12, 13, 14, 
        15, 16, 17, 
        18, 19, 20,
        21, 22, 23,
        24, 25, 26, 
        27, 28, 29,
        30, 31, 32,
        33, 34, 35
    };  
};

class TexturedBox: public Object{
public:
    TexturedBox(std::string name, const glm::vec3& pos = glm::vec3(0.0f, 0.0f, 0.0f)): Object(pos, name){
        m_Textrue = Phoenix::Texture2D::Create("/home/alireza/Programming/C++/MyGameEngineProject/LearningOpengl/assets/textures/container.jpg");
        m_Shader = Phoenix::Shader::Create("/home/alireza/Programming/C++/MyGameEngineProject/LearningOpengl/assets/shaders/texture.glsl");
        m_Vertex_array = Phoenix::CreateRef<Phoenix::VertexArray>();
        m_Vertex_array->Bind();
        Phoenix::Ref<Phoenix::VertexBuffer> vertexBuffer = Phoenix::CreateRef<Phoenix::VertexBuffer>(this->vertices, sizeof(this->vertices));
        Phoenix::BufferLayout layout = {
            { Phoenix::ShaderDataType::Float3, "a_Position" },
            { Phoenix::ShaderDataType::Float2, "a_TexCoord" }
        };
        vertexBuffer->SetLayout(layout);
        m_Vertex_array->AddVertexBuffer(vertexBuffer);
        Phoenix::Ref<Phoenix::IndexBuffer> indexBuffer = Phoenix::CreateRef<Phoenix::IndexBuffer>(indices, sizeof(indices) / sizeof(unsigned int));
        m_Vertex_array->SetIndexBuffer(indexBuffer);
    }

    virtual void Draw(const glm::mat4& projection) override{
        if (!m_Enabled) return;
        m_Textrue->Bind();
        Phoenix::Renderer::Submit(m_Shader, m_Vertex_array, projection, m_Transform);
    }

private:
    float vertices[180] = {
        -0.5f, -0.5f, -0.5f,  0.0f, 0.0f,
         0.5f, -0.5f, -0.5f,  1.0f, 0.0f,
         0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
         0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
        -0.5f,  0.5f, -0.5f,  0.0f, 1.0f,
        -0.5f, -0.5f, -0.5f,  0.0f, 0.0f,

        -0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
         0.5f, -0.5f,  0.5f,  1.0f, 0.0f,
         0.5f,  0.5f,  0.5f,  1.0f, 1.0f,
         0.5f,  0.5f,  0.5f,  1.0f, 1.0f,
        -0.5f,  0.5f,  0.5f,  0.0f, 1.0f,
        -0.5f, -0.5f,  0.5f,  0.0f, 0.0f,

        -0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
        -0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
        -0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
        -0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
        -0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
        -0.5f,  0.5f,  0.5f,  1.0f, 0.0f,

         0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
         0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
         0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
         0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
         0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
         0.5f,  0.5f,  0.5f,  1.0f, 0.0f,

        -0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
         0.5f, -0.5f, -0.5f,  1.0f, 1.0f,
         0.5f, -0.5f,  0.5f,  1.0f, 0.0f,
         0.5f, -0.5f,  0.5f,  1.0f, 0.0f,
        -0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
        -0.5f, -0.5f, -0.5f,  0.0f, 1.0f,

        -0.5f,  0.5f, -0.5f,  0.0f, 1.0f,
         0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
         0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
         0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
        -0.5f,  0.5f,  0.5f,  0.0f, 0.0f,
        -0.5f,  0.5f, -0.5f,  0.0f, 1.0f
    };
    unsigned int indices[36] = {
        0, 1, 2,   // first triangle
        3, 4, 5,
        6, 7, 8,
        9, 10, 11,
        12, 13, 14, 
        15, 16, 17, 
        18, 19, 20,
        21, 22, 23,
        24, 25, 26, 
        27, 28, 29,
        30, 31, 32,
        33, 34, 35
    };  
    Phoenix::Ref<Phoenix::Texture2D> m_Textrue;
};
