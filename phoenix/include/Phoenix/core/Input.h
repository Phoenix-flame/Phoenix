#pragma once
#include <Phoenix/core/keyCodes.h>
#include <Phoenix/core/mouseCodes.h>
#include <glm/glm.hpp>
namespace Phoenix{
    class Input{
	public:
		static bool IsKeyPressed(KeyCode key);

		static bool IsMouseButtonPressed(MouseCode button);
		static glm::vec2 GetMousePosition();
		static float GetMouseX();
		static float GetMouseY();
        static glm::vec2 GetMouseDelta();
	};
}