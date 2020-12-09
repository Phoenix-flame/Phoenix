#include <Phoenix/renderer/CameraController.h>
#include <Phoenix/renderer/Camera.h>
#include <Phoenix/core/Input.h>
#include <Phoenix/imGui/imgui.h>
namespace Phoenix{
    PerspectiveCameraController::PerspectiveCameraController(float aspectRatio, float fov, float near, float far):
        _aspectRatio(aspectRatio), _fov(fov), _near(near), _far(far), _camera(fov, aspectRatio, near, far){}
    void PerspectiveCameraController::OnUpdate(Timestep ts){
        
    }
    void PerspectiveCameraController::OnEvent(Event& e){
        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<MouseMovedEvent>(BIND_EVENT_FN(PerspectiveCameraController::OnMouseMoved));
		dispatcher.Dispatch<MouseScrolledEvent>(BIND_EVENT_FN(PerspectiveCameraController::OnMouseScrolled));
		dispatcher.Dispatch<WindowResizeEvent>(BIND_EVENT_FN(PerspectiveCameraController::OnWindowResized));
    }
    void PerspectiveCameraController::OnResize(float width, float height){
        _aspectRatio = width / height;
		_camera.SetProjection(_fov, _aspectRatio, _near, _far);
    }

    void PerspectiveCameraController::SetFOV(float fov){
        _fov = glm::radians(fov);
        _camera.SetProjection(_fov, _aspectRatio, _near, _far);
    }   

    bool PerspectiveCameraController::OnMouseMoved(MouseMovedEvent& e){
        float xoffset = Input::GetMouseDelta().x;
        float yoffset = -Input::GetMouseDelta().y; 
        
        xoffset *= 0.2f;
        yoffset *= 0.2f;
        if (Input::IsMouseButtonPressed(Mouse::Button0)){
            _camera.ChangeDirection(xoffset, yoffset);
        }
        return false;
    }
    bool PerspectiveCameraController::OnMouseScrolled(MouseScrolledEvent& e){
        _camera.SetRadius(e.GetYOffset() * 0.1);
        return false;
    }
    bool PerspectiveCameraController::OnWindowResized(WindowResizeEvent& e){
        this->OnResize((float)e.GetWidth(), (float)e.GetHeight());
        return false;
    }
}