#include <Phoenix/renderer/Camera.h>
#include <Phoenix/core/timestep.h>
#include <Phoenix/event/applicationEvent.h>
#include <Phoenix/event/mouseEvent.h>
#include <Phoenix/event/event.h>
#include <Phoenix/core/Input.h>
#include <glm/gtc/matrix_transform.hpp>

namespace Phoenix{
    // EditorCamera::EditorCamera(){

    // }

    void EditorCamera::OnUpdate(Timestep ts){
        float xoffset = Input::GetMouseDelta().x;
        float yoffset = -Input::GetMouseDelta().y; 
        
        xoffset *= 0.2f;
        yoffset *= 0.2f;
        if (Input::IsMouseButtonPressed(Mouse::Button0) && Input::IsKeyPressed(Key::LeftControl)){
            Rotate(xoffset, yoffset);
        }
        else if (Input::IsMouseButtonPressed(Mouse::Button1) && Input::IsKeyPressed(Key::LeftControl)){
            Pan(xoffset, yoffset);
        }
    }

    void EditorCamera::OnEvent(Event& e){
        EventDispatcher dispatcher(e);
		dispatcher.Dispatch<MouseScrolledEvent>(BIND_EVENT_FN(EditorCamera::OnMouseScrolled));
		dispatcher.Dispatch<WindowResizeEvent>(BIND_EVENT_FN(EditorCamera::OnWindowResized));
    }

    void EditorCamera::OnResize(float width, float height){
        m_ViewportWidth = width;
        m_ViewportHeight = height;
        m_AspectRatio = width / height;
		UpdateProjection(m_FOV, m_AspectRatio, m_NearClip, m_FarClip);
    }

    void EditorCamera::SetRadius(float radius){
        if (m_Radius >= 1.0f && m_Radius <= 45.0f){ m_Radius -= radius; }
        if (m_Radius <= 1.0f) { m_Radius = 1.0f; }
        else if (m_Radius >= 45.0f) { m_Radius = 45.0f; }
        RecalculateViewMatrix();
    }

    void EditorCamera::Rotate(float xoffset, float yoffset){
        m_Yaw   -= xoffset;
        m_Pitch += yoffset;
        if (m_Pitch > 89.0f) { m_Pitch = 89.0f; }
        if (m_Pitch < -89.0f) { m_Pitch = -89.0f; }
        RecalculateViewMatrix();
    }
    
    void EditorCamera::Pan(float xoffset, float yoffset){
        float x = std::min(m_ViewportWidth / 1000.0f, 2.4f); // max = 2.4f
		float xFactor = 0.0366f * (x * x) - 0.1778f * x + 0.3021f;

		float y = std::min(m_ViewportHeight / 1000.0f, 2.4f); // max = 2.4f
		float yFactor = 0.0366f * (y * y) - 0.1778f * y + 0.3021f;
        m_Target.x -= m_Right.x * (xoffset * xFactor);
        m_Target.z -= m_Right.z * (xoffset * xFactor);
       
        m_Target.x -= m_Up.x * (yoffset * yFactor);
        m_Target.z -= m_Up.z * (yoffset * yFactor);
        RecalculateViewMatrix();
    }

    void EditorCamera::SetFOV(float fov){
        m_FOV = fov;
        m_Projection = glm::perspective(fov, m_AspectRatio, m_NearClip, m_FarClip);
        RecalculateViewMatrix();
    }

    void EditorCamera::RecalculateViewMatrix(){
        float camX = -m_Radius * sinf(m_Yaw*(M_PI/180)) * cosf((m_Pitch)*(M_PI/180));
        float camY = -m_Radius * sinf((m_Pitch)*(M_PI/180));
        float camZ = -m_Radius * cosf((m_Yaw)*(M_PI/180)) * cosf((m_Pitch)*(M_PI/180));

        m_Position = glm::vec3(camX, camY, camZ) + m_Target;
	    glm::vec3 cameraDirection = glm::normalize(m_Position - m_Target);
        m_Right = glm::normalize(glm::cross(m_WorldUp, cameraDirection));
        m_Up = glm::cross(cameraDirection, m_Right);

        m_View = glm::lookAt(m_Position, m_Target, m_Up);
        // m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix;
    }

    void EditorCamera::UpdateProjection(float fov, float aspect, float near, float far){
        m_FOV = fov;
        m_AspectRatio = aspect;
        m_FarClip = far;
        m_NearClip = near;

        m_Projection = glm::perspective(fov, aspect, near, far);
        RecalculateViewMatrix();
    }


    bool EditorCamera::OnMouseScrolled(MouseScrolledEvent& e){
        SetRadius(e.GetYOffset() * 0.1);
        return false;
    }
    bool EditorCamera::OnWindowResized(WindowResizeEvent& e){
        OnResize((float)e.GetWidth(), (float)e.GetHeight());
        return false;
    }
}