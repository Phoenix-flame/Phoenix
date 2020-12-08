#include <Phoenix/renderer/Camera.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
namespace Phoenix{
    PerspectiveCamera::PerspectiveCamera(float fov, float aspect, float near, float far, const glm::vec3& pos){
        _position = pos;
        _target = glm::vec3(0.0, 0.0, 0.0);
        this->SetProjection(fov, aspect, near, far);
        this->RecalculateViewMatrix();
    }

    void PerspectiveCamera::SetProjection(float fov, float aspect, float near, float far){
        _projectionMatrix = glm::perspective(fov, aspect, near, far);
        _viewProjectionMatrix = _projectionMatrix * _viewMatrix;
        this->RecalculateViewMatrix();
    }

    void PerspectiveCamera::SetTarget(const glm::vec3& target){
        _target = target;
        this->RecalculateViewMatrix();
    }

    void PerspectiveCamera::SetRadius(float offset){
        if (_radius >= 1.0f && _radius <= 45.0f){ _radius -= offset; }
        if (_radius <= 1.0f) { _radius = 1.0f; }
        else if (_radius >= 45.0f) { _radius = 45.0f; }
        this->RecalculateViewMatrix();
    }

    void PerspectiveCamera::ChangeDirection(float xoffset, float yoffset){
        _yaw   -= xoffset;
        _pitch += yoffset;
        if (_pitch > 89.0f) { _pitch = 89.0f; }
        if (_pitch < -89.0f) { _pitch = -89.0f; }
        this->RecalculateViewMatrix();
    }

    void PerspectiveCamera::RecalculateViewMatrix(){
        float camX = -_radius * sinf(_yaw*(M_PI/180)) * cosf((_pitch)*(M_PI/180));
        float camY = -_radius * sinf((_pitch)*(M_PI/180));
        float camZ = -_radius * cosf((_yaw)*(M_PI/180)) * cosf((_pitch)*(M_PI/180));

        _position = glm::vec3(camX, camY, camZ);
	    glm::vec3 cameraDirection = glm::normalize(_position - _target);
        _right = glm::normalize(glm::cross(_worldUp, cameraDirection));
        _up = glm::cross(cameraDirection, _right);

        _viewMatrix = glm::lookAt(_position, _target, _up);
        _viewProjectionMatrix = _projectionMatrix * _viewMatrix;
    }
}