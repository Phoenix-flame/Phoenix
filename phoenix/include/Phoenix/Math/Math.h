#pragma once 

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
namespace Phoenix::Math{
    
	bool DecomposeTransform(const glm::mat4& transform, glm::vec3& outTranslation, glm::vec3& outRotation, glm::vec3& outScale);

	// Rotations in the engine's XYZ Euler convention (TransformComponent::GetTransform
	// applies Rx * Ry * Rz). glm::quat(euler) / glm::eulerAngles use ZYX instead.
	glm::quat EulerXYZToQuat(const glm::vec3& euler);

	// XYZ Euler angles for q, choosing among the equivalent solutions (and 2*pi
	// wraps) the one closest to `reference`, so angles stay continuous frame to frame.
	glm::vec3 QuatToEulerXYZ(const glm::quat& q, const glm::vec3& reference);
}