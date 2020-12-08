#pragma once
#include <Phoenix/core/timestep.h>
#include <Phoenix/event/event.h>
#include <Phoenix/event/mouseEvent.h>
#include <Phoenix/event/applicationEvent.h>
#include <Phoenix/renderer/Camera.h>
namespace Phoenix{
    class PerspectiveCameraController{
    public:
        PerspectiveCameraController(float aspectRatio, float fov, float near, float far);

        void OnUpdate(Timestep ts);
        void OnEvent(Event& e);
        void OnResize(float width, float height);
        void SetFOV(float fov);
        PerspectiveCamera& GetCamera() { return _camera; }
        const PerspectiveCamera& GetCamera() const { return _camera; }

    private:
        bool OnMouseMoved(MouseMovedEvent& e);
        bool OnMouseScrolled(MouseScrolledEvent& e);
		bool OnWindowResized(WindowResizeEvent& e);

    private:
        float _aspectRatio;
        float _fov;
        float _near;
        float _far;
        float _lastX = 1280 / 2.0f;
        float _lastY = 720 / 2.0f;
        bool _firstMouse = true;
        PerspectiveCamera _camera;
    };
    class OrthographicCameraController{
    public:
        OrthographicCameraController(float aspectRatio, bool rotation = false);
        void OnUpdate(Timestep ts);
        void OnEvent(Event& e);
        void OnResize(float width, float height);
        OrthographicCamera& GetCamera() { return m_Camera; }
		const OrthographicCamera& GetCamera() const { return m_Camera; }

		float GetZoomLevel() const { return m_ZoomLevel; }
		void SetZoomLevel(float level) { m_ZoomLevel = level; }
	private:
		bool OnMouseScrolled(MouseScrolledEvent& e);
		bool OnWindowResized(WindowResizeEvent& e);
    private:
        float m_AspectRatio;
		float m_ZoomLevel = 1.0f;
		OrthographicCamera m_Camera;

		bool m_Rotation;

		glm::vec3 m_CameraPosition = { 0.0f, 0.0f, 0.0f };
		float m_CameraRotation = 0.0f; //In degrees, in the anti-clockwise direction
		float m_CameraTranslationSpeed = 5.0f, m_CameraRotationSpeed = 180.0f;
    };
}