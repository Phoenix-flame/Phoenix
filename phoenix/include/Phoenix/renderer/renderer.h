#pragma once 
#include <Phoenix/renderer/renderer_api.h>
#include <Phoenix/renderer/shader.h>
#include <Phoenix/renderer/Camera.h>

namespace Phoenix{
    class Renderer{
	public:
		static void Init();
		static void Shutdown();
		
		static void OnWindowResize(uint32_t width, uint32_t height);

		static void BeginScene(const glm::mat4& projection, const glm::mat4& view);
		static void EndScene();



		static void Submit(
            const Ref<VertexArray>& vertexArray, 
            const glm::mat4& transform = glm::mat4(1.0f)
        );

		static RendererAPI::API GetAPI() { return RendererAPI::GetAPI(); }
	private:
		struct SceneData{
			glm::mat4 ViewMatrix;
            glm::mat4 ProjectionMatrix;
		};
        static Ref<Shader> s_Shader;
		static Scope<SceneData> s_SceneData;
	};
}