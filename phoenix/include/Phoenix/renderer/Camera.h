#pragma once

#include <glm/glm.hpp>


namespace Phoenix{
    class CenteredCamera{
    public:
        CenteredCamera(float fov, float aspect, float near, float far, const glm::vec3& pos = glm::vec3(0, 0, -5));
        CenteredCamera(const glm::vec3& pos = glm::vec3(0, 0, -5));

        void SetProjection(float fov, float aspect, float near, float far);
        void SetTarget(const glm::vec3& target);

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

		const glm::mat4& GetProjectionMatrix() const { return _projectionMatrix; }
		const glm::mat4& GetViewMatrix() const { return _viewMatrix; }
		const glm::mat4& GetViewProjectionMatrix() const { return _viewProjectionMatrix; }

    private:
		void RecalculateViewMatrix();
	private:
		glm::mat4 _projectionMatrix;
		glm::mat4 _viewMatrix;
		glm::mat4 _viewProjectionMatrix;

		glm::vec3 _position = { 0.0f, 0.0f, 0.0f };
		float _rotation = 0.0f;
    };
}