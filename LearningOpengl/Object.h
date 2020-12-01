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
        m_Scale = glm::vec3(1.0f);
        m_Rotation = glm::vec3(0.0f);
    }

    virtual void Draw(const glm::mat4& projection){
        if (!m_Enabled) return;
        Phoenix::Renderer::Submit(m_Shader, m_Vertex_array, projection, m_Transform);
    }

    void SetPosition(const glm::vec3& new_pos) {
        glm::vec3 movement_vec = (new_pos - m_Position) / m_Scale;
        m_Position = new_pos;
        m_Transform = glm::translate(m_Transform, movement_vec);
    }

    void SetScale(const glm::vec3& scale) {
        glm::vec3 scale_vec = scale / m_Scale;
        m_Scale = scale;
        m_Transform = glm::scale(m_Transform, scale_vec);
    }

    void SetRotationX(const float& angle) {
        float rotation_angle = angle - m_Rotation.x;
        if (rotation_angle == 0.0f) { return; }
        m_Rotation.x = angle;
        m_Transform = glm::rotate(m_Transform, glm::radians(rotation_angle), glm::vec3(1.0f, 0.0f, 0.0f));
    }
    void SetRotationY(const float& angle) {
        float rotation_angle = angle - m_Rotation.y;
        if (rotation_angle == 0.0f) { return; }
        m_Rotation.y = angle;
        m_Transform = glm::rotate(m_Transform, glm::radians(rotation_angle), glm::vec3(0.0f, 1.0f, 0.0f));
    }
    void SetRotationZ(const float& angle) {
        float rotation_angle = angle - m_Rotation.z;
        if (rotation_angle == 0.0f) { return; }
        m_Rotation.z = angle;
        m_Transform = glm::rotate(m_Transform, glm::radians(rotation_angle), glm::vec3(0.0f, 0.0f, 1.0f));
    }
    void ResetRotation() {
        m_Rotation = glm::vec3(0.0f);
        m_Transform = glm::mat4(1.0f);
        m_Transform = glm::translate(m_Transform, m_Position);
        m_Transform = glm::scale(m_Transform, m_Scale);
    }

    
    glm::vec3& GetPosition() { return m_Position; }
    glm::vec3& GetScale() { return m_Scale; }
    glm::vec3& GetRotation() { return m_Rotation; }
    void Enable() { m_Enabled = true; }
    void Disable() { m_Enabled = false; }
    bool* GetEnablePtr() { return &m_Enabled; };

    operator std::string () const {return m_Name; }
protected:
    const std::string m_Name;
    glm::vec3 m_Position;
    glm::vec3 m_Scale;
    glm::vec3 m_Rotation;
    bool m_Enabled = true;
    Phoenix::Ref<Phoenix::VertexArray> m_Vertex_array;
    Phoenix::Ref<Phoenix::Shader> m_Shader;
    glm::mat4 m_Transform = glm::mat4(1.0);
};


