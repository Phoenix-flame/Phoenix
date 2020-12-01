#pragma once 
#include <Phoenix/renderer/shader.h>
#include <Phoenix/renderer/CameraController.h>
#include <Phoenix/renderer/Buffers.h>
#include <Phoenix/renderer/VertexArray.h>
#include <Phoenix/renderer/renderer.h>
#include "Object.h"

class Origin: public Object{
public:
    Origin(const glm::vec3& pos = glm::vec3(0.0f, 0.0f, 0.0f)): Object(pos, "Origin"){
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
    virtual void Draw(const glm::mat4& projection) override{
        if (!m_Enabled) return;
        glDisable(GL_DEPTH_TEST);
        m_Shader->Bind();
        m_Shader->SetMat4("model", m_Transform);
        m_Shader->SetMat4("projection", projection);
        m_Vertex_array->Bind();
        glLineWidth(2.5);
        uint32_t count = m_Vertex_array->GetIndexBuffer()->GetCount();
		glDrawElements(GL_LINES, count, GL_UNSIGNED_INT, 0);
		glBindTexture(GL_TEXTURE_2D, 0);
        m_Shader->Unbind();
        glEnable(GL_DEPTH_TEST);
    }

private:
    float vertices[218] = {
         0.0f,  0.0f,  0.0f,  1.0f,  0.0f,  0.0f,
         0.3f,  0.0f,  0.0f,  1.0f,  0.0f,  0.0f,
         0.0f,  0.0f,  0.0f,  0.0f,  1.0f,  0.0f,
         0.0f,  0.3f,  0.0f,  0.0f,  1.0f,  0.0f,
         0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  1.0f,
         0.0f,  0.0f,  0.3f,  0.0f,  0.0f,  1.0f,
    };
    unsigned int indices[36] = {
        0, 1, 2,   // first Box
        3, 4, 5,
    };  
};