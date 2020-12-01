#pragma once
#include <Phoenix/renderer/shader.h>
#include <Phoenix/renderer/CameraController.h>
#include <Phoenix/renderer/Buffers.h>
#include <Phoenix/renderer/VertexArray.h>
#include <Phoenix/renderer/renderer.h>


class Object{
public:
    Object(const glm::vec3& pos, std::string name = "Object") : m_Name(name), m_Position(pos) {
        m_Transform = glm::translate(m_Transform, m_Position);
    }

    virtual void Draw(const glm::mat4& projection){
        if (!m_Enabled) return;
        Phoenix::Renderer::Submit(m_Shader, m_Vertex_array, projection, m_Transform);
    }

    void SetPosition(const glm::vec3 new_pos) {
        m_Position = new_pos;
        m_Transform = glm::translate(m_Transform, m_Position);
    }
    
    glm::vec3& GetPosition() { return m_Position; }

    void Enable() { m_Enabled = true; }
    void Disable() { m_Enabled = false; }
    bool* GetEnablePtr() { return &m_Enabled; };

    operator std::string () const {return m_Name; }
protected:
    const std::string m_Name;
    glm::vec3 m_Position;
    bool m_Enabled = true;
    Phoenix::Ref<Phoenix::VertexArray> m_Vertex_array;
    Phoenix::Ref<Phoenix::Shader> m_Shader;
    glm::mat4 m_Transform = glm::mat4(1.0);
    
};


