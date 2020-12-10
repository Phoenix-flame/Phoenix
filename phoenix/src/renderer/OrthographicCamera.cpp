#include <Phoenix/renderer/Camera.h>
#include <glm/gtc/matrix_transform.hpp>
#include <Phoenix/core/log.h>
namespace Phoenix{
    OrthographicCamera::OrthographicCamera(float left, float right, float bottom, float top)
		: _projectionMatrix(glm::ortho(left, right, bottom, top, -1.0f, 1.0f)), _viewMatrix(1.0f){
		_viewProjectionMatrix = _projectionMatrix * _viewMatrix;
	}

	void OrthographicCamera::SetProjection(float left, float right, float bottom, float top){
		_projectionMatrix = glm::ortho(left, right, bottom, top, -1.0f, 1.0f);
		_viewProjectionMatrix = _projectionMatrix * _viewMatrix;
	}

	void OrthographicCamera::RecalculateViewMatrix(){
		glm::mat4 transform = glm::translate(glm::mat4(1.0f), _position) *
			glm::rotate(glm::mat4(1.0f), glm::radians(_rotation), glm::vec3(0, 0, 1));

		_viewMatrix = glm::inverse(transform);
		_viewProjectionMatrix = _projectionMatrix * _viewMatrix;
	}

    void OrthographicCamera::SetViewportSize(uint32_t width, uint32_t height){
        
        m_AspectRatio = (float)width / (float)height;
        
        SetProjection(-m_AspectRatio, m_AspectRatio, -1.0, 1.0);
    }
}