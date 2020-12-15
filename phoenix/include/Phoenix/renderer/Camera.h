#pragma once

#include <glm/glm.hpp>



namespace Phoenix{
    class SceneEditor;
    class Timestep;
    class Event;
    class MouseScrolledEvent;
    class WindowResizeEvent;

    class Camera{
	public:
		Camera() = default;
		Camera(const glm::mat4& projection)
			: m_Projection(projection) {}

		virtual ~Camera() = default;

		const glm::mat4& GetProjection() const { return m_Projection; }
        const glm::mat4& GetView() const { return m_View; }
        
	protected:
		glm::mat4 m_Projection = glm::mat4(1.0f);
        glm::mat4 m_View = glm::mat4(1.0f);
	};


    class EditorCamera: public Camera{
    public:
        EditorCamera() = default;

        void OnUpdate(Timestep ts);
        void OnEvent(Event& e);
        void OnResize(float width, float height);

        void SetRadius(float radius);
        void Rotate(float xoffset, float yoffset);
        void Pan(float xoffset, float yoffset);
        const glm::mat4& GetViewProjectionMatrix() const { return m_ViewProjectionMatrix; }
        
        void SetFOV(float fov);
        const float GetFOV() const { return m_FOV; }

        const glm::vec3& GetPosition() const { return m_Position; }
    
    private:
        void UpdateProjection(float fov, float aspect, float near, float far);
        void RecalculateViewMatrix();
        bool OnMouseScrolled(MouseScrolledEvent& e);
		bool OnWindowResized(WindowResizeEvent& e);

    private:
        glm::vec3 m_Position = glm::vec3(0.0f);
        glm::vec3 m_Up;
        glm::vec3 m_Right;
        glm::vec3 m_Target;
		glm::mat4 m_ViewProjectionMatrix;
        const glm::vec3 m_WorldUp = glm::vec3(0.0, 1.0, 0.0);
        float m_Yaw = 0.0f;
        float m_Pitch = 0.0f;
        float m_Radius = 5.0f;
        float m_FOV = 45.0f;
        float m_AspectRatio = 1.0f;
        float m_NearClip = 0.001f;
        float m_FarClip = 1000.0f;
        
        float m_ViewportWidth, m_ViewportHeight;

        friend class SceneEditor;
    };


    class PerspectiveCamera{
    public:
        PerspectiveCamera(float fov, float aspect, float near, float far, const glm::vec3& pos = glm::vec3(0, 0, -5));

        void SetProjection(float fov, float aspect, float near, float far);
        void SetTarget(const glm::vec3& target);
        void SetRadius(float offset);
        void ChangeDirection(float xoffset, float yoffset);
        const glm::mat4& GetProjectionMatrix() const { return _projectionMatrix; }
		const glm::mat4& GetViewMatrix() const { return _viewMatrix; }
		const glm::mat4& GetViewProjectionMatrix() const { return _viewProjectionMatrix; }
    private:
        void RecalculateViewMatrix();
    private:
        glm::vec3 _position;
        glm::vec3 _up;
        glm::vec3 _right;
        glm::vec3 _target;
        glm::mat4 _projectionMatrix;
		glm::mat4 _viewMatrix;
		glm::mat4 _viewProjectionMatrix;
        const glm::vec3 _worldUp = glm::vec3(0.0, 1.0, 0.0);
        float _yaw = 0.0f;
        float _pitch = 0.0f;
        float _radius = 5.0f;
    };

    
    class OrthographicCamera{
    public:
        OrthographicCamera(float left, float right, float bottom, float top);

		void SetProjection(float left, float right, float bottom, float top);

		const glm::vec3& GetPosition() const { return _position; }
		void SetPosition(const glm::vec3& position) { _position = position; RecalculateViewMatrix(); }

		float GetRotation() const { return _rotation; }
		void SetRotation(float rotation) { _rotation = rotation; RecalculateViewMatrix(); }
        
        void SetViewportSize(uint32_t width, uint32_t height);

		const glm::mat4& GetProjectionMatrix() const { return _projectionMatrix; }
		const glm::mat4& GetViewMatrix() const { return _viewMatrix; }
		const glm::mat4& GetViewProjectionMatrix() const { return _viewProjectionMatrix; }

        OrthographicCamera& OnUpdate() { RecalculateViewMatrix(); return *this; }
    private:
		void RecalculateViewMatrix();
	private:
		glm::mat4 _projectionMatrix;
		glm::mat4 _viewMatrix;
		glm::mat4 _viewProjectionMatrix;
        float m_AspectRatio;
		glm::vec3 _position = { 0.0f, 0.0f, 0.0f };
		float _rotation = 0.0f;

        friend class SceneEditor;
    };
}