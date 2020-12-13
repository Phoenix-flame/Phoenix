#include <Phoenix/renderer/renderer.h>
#include <Phoenix/renderer/renderer_command.h>
#include <Phoenix/core/base.h>

namespace Phoenix{
    Scope<Renderer::SceneData> Renderer::s_SceneData = CreateScope<Renderer::SceneData>();
    Ref<Shader> Renderer::s_Shader;
	void Renderer::Init(){
		RenderCommand::Init();
        s_Shader = Shader::Create("/home/alireza/Programming/C++/MyGameEngineProject/Sandbox/assets/shaders/basic.glsl");
	}

	void Renderer::Shutdown(){
	}

	void Renderer::OnWindowResize(uint32_t width, uint32_t height){
		RenderCommand::SetViewport(0, 0, width, height);
	}

    void Renderer::Submit(const Ref<VertexArray>& vertexArray, const glm::mat4& transform){
        s_Shader->SetMat4("model", transform);
        s_Shader->SetMat4("view", s_SceneData->ViewMatrix);
        s_Shader->SetMat4("projection", s_SceneData->ProjectionMatrix);
        vertexArray->Bind();
        RenderCommand::DrawIndexed(vertexArray);
    }

    void Renderer::BeginScene(const glm::mat4& projection, const glm::mat4& view){
        s_SceneData->ProjectionMatrix = projection;
        s_SceneData->ViewMatrix = view;
        s_Shader->Bind();
    }

	void Renderer::EndScene(){
        s_Shader->Unbind();
	}

}