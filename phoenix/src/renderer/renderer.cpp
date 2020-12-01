#include <Phoenix/renderer/renderer.h>
#include <Phoenix/renderer/renderer_command.h>

namespace Phoenix{
    Scope<Renderer::SceneData> Renderer::s_SceneData = CreateScope<Renderer::SceneData>();

	void Renderer::Init(){
		RenderCommand::Init();
	}

	void Renderer::Shutdown(){
	}

	void Renderer::OnWindowResize(uint32_t width, uint32_t height){
		RenderCommand::SetViewport(0, 0, width, height);
	}

    void Renderer::Submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray,  const glm::mat4& projection, const glm::mat4& transform){
        shader->Bind();
        shader->SetMat4("model", transform);
        shader->SetMat4("projection", projection);
        vertexArray->Bind();
        RenderCommand::DrawIndexed(vertexArray);
        shader->Unbind();
    }

	void Renderer::EndScene(){
	}

}