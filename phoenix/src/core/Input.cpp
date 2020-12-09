#include <Phoenix/core/Input.h>
#include <Phoenix/core/application.h>
#include <GLFW/glfw3.h>
#include <Phoenix/imGui/imgui.h>
namespace Phoenix{
    bool Input::IsKeyPressed(const KeyCode key){
		auto* window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
		auto state = glfwGetKey(window, static_cast<int32_t>(key));
		return state == GLFW_PRESS || state == GLFW_REPEAT;
	}

	bool Input::IsMouseButtonPressed(const MouseCode button){
		auto* window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
		auto state = glfwGetMouseButton(window, static_cast<int32_t>(button));
		return state == GLFW_PRESS;
	}

	glm::vec2 Input::GetMousePosition(){
		auto* window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
		double xpos, ypos;
		glfwGetCursorPos(window, &xpos, &ypos);

		return { (float)xpos, (float)ypos };
	}

	float Input::GetMouseX(){
		return GetMousePosition().x;
	}

	float Input::GetMouseY(){
		return GetMousePosition().y;
	}

    glm::vec2 Input::GetMouseDelta(){
        ImGuiIO& io = ImGui::GetIO(); (void)io;

        return {io.MouseDelta.x, io.MouseDelta.y};
    }
}