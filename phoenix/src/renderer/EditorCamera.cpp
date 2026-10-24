#include <Phoenix/renderer/Camera.h>
#include <Phoenix/core/timestep.h>
#include <Phoenix/event/applicationEvent.h>
#include <Phoenix/event/mouseEvent.h>
#include <Phoenix/event/event.h>
#include <Phoenix/core/Input.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace Phoenix{
    void EditorCamera::OnUpdate(Timestep ts){
        if (m_IsActivated)
        {
            float xoffset = Input::GetMouseDelta().x;
            float yoffset = -Input::GetMouseDelta().y; 
            
            xoffset *= 0.2f;
            yoffset *= 0.2f;

            if (m_FlyMode){
                if (m_MouseCaptured){
                    // Raw cursor motion: unbounded while the cursor is locked (ImGui's
                    // delta is off then, the app disables its mouse input).
                    glm::vec2 mouse = Input::GetMousePosition();
                    if (m_HaveLastMouse){
                        glm::vec2 d = (mouse - m_LastMouse) * 0.12f;
                        LookAround(d.x, -d.y);
                    }
                    m_LastMouse = mouse;
                    m_HaveLastMouse = true;
                }
                else if (Input::IsMouseButtonPressed(Mouse::Button1)){
                    LookAround(xoffset, yoffset);
                }
                Fly(ts);
                return;
            }

            // Right mouse: orbit. Middle mouse: pan. (Left mouse is reserved for
            // selection / the transform gizmo.)
            if (Input::IsMouseButtonPressed(Mouse::Button1)){
                Rotate(xoffset, yoffset);
            }
            else if (Input::IsMouseButtonPressed(Mouse::Button2)){
                Pan(xoffset, yoffset);
            }
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

    static constexpr float kMinRadius = 1.0f, kMaxRadius = 150.0f;

    void EditorCamera::SetRadius(float radius){
        // Zoom proportionally so the wheel feels the same close up and far away.
        m_Radius -= radius * std::max(1.0f, m_Radius * 0.2f);
        m_Radius = std::clamp(m_Radius, kMinRadius, kMaxRadius);
        RecalculateViewMatrix();
    }

    void EditorCamera::Focus(const glm::vec3& target, float radius){
        Focus(target, radius, m_Yaw, m_Pitch);
    }

    void EditorCamera::Focus(const glm::vec3& target, float radius, float yawDegrees, float pitchDegrees){
        m_Target = target;
        m_Radius = std::clamp(radius, kMinRadius, kMaxRadius);
        m_Yaw = yawDegrees;
        m_Pitch = std::clamp(pitchDegrees, -89.0f, 89.0f);
        RecalculateViewMatrix();
    }

    void EditorCamera::Rotate(float xoffset, float yoffset){
        m_Yaw   -= xoffset;
        m_Pitch += yoffset;
        if (m_Pitch > 89.0f) { m_Pitch = 89.0f; }
        if (m_Pitch < -89.0f) { m_Pitch = -89.0f; }
        RecalculateViewMatrix();
    }
    
    // Turn the view about the eye: same angles as Rotate, but the target moves instead of
    // the camera.
    void EditorCamera::LookAround(float xoffset, float yoffset){
        glm::vec3 eye = m_Position;
        m_Yaw   -= xoffset;
        m_Pitch = std::clamp(m_Pitch + yoffset, -89.0f, 89.0f);
        float yaw = glm::radians(m_Yaw), pitch = glm::radians(m_Pitch);
        glm::vec3 forward(sinf(yaw) * cosf(pitch), sinf(pitch), cosf(yaw) * cosf(pitch));
        m_Target = eye + forward * m_Radius;
        RecalculateViewMatrix();
    }

    void EditorCamera::Fly(Timestep ts){
        glm::vec3 forward = glm::normalize(m_Target - m_Position);
        glm::vec3 right = glm::normalize(glm::cross(forward, m_WorldUp));
        glm::vec3 move(0.0f);
        if (Input::IsKeyPressed(Key::W)) { move += forward; }
        if (Input::IsKeyPressed(Key::S)) { move -= forward; }
        if (Input::IsKeyPressed(Key::D)) { move += right; }
        if (Input::IsKeyPressed(Key::A)) { move -= right; }
        if (Input::IsKeyPressed(Key::E) || Input::IsKeyPressed(Key::Space))       { move += m_WorldUp; }
        if (Input::IsKeyPressed(Key::Q))                                          { move -= m_WorldUp; }
        if (glm::dot(move, move) < 1e-6f) { return; }
        float speed = m_FlySpeed * (Input::IsKeyPressed(Key::LeftShift) ? 4.0f : 1.0f);
        // Cap the step so a long frame (a scene load, a hitch) can't throw the camera far.
        float dt = std::min((float)ts, 0.1f);
        m_Target += glm::normalize(move) * speed * dt;
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
        m_ViewProjectionMatrix = m_Projection * m_View;
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
        if (m_FlyMode){
            m_FlySpeed = std::clamp(m_FlySpeed * std::pow(1.2f, e.GetYOffset()), 0.25f, 200.0f);
            return false;
        }
        SetRadius(e.GetYOffset() * 0.1);
        return false;
    }
    bool EditorCamera::OnWindowResized(WindowResizeEvent& e){
        OnResize((float)e.GetWidth(), (float)e.GetHeight());
        return false;
    }
}