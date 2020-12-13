#include <Phoenix/renderer/renderer.h>
#include <Phoenix/renderer/renderer_command.h>
#include <Phoenix/core/base.h>

namespace Phoenix{
    Scope<Renderer::SceneData> Renderer::s_SceneData = CreateScope<Renderer::SceneData>();
    Scope<RenderCube> Renderer::s_RenderCube;
	void Renderer::Init(){
		RenderCommand::Init();
        s_RenderCube = CreateScope<RenderCube>();
	}

	void Renderer::Shutdown(){
	}

	void Renderer::OnWindowResize(uint32_t width, uint32_t height){
		RenderCommand::SetViewport(0, 0, width, height);
	}

    void Renderer::Submit(const glm::mat4& transform){
        if ((int)(*s_RenderCube) >= 1000) { Flush(); }
        s_RenderCube->m_Transformations.push_back(transform);
        
        // RenderCommand::DrawIndexed(s_RenderCube->m_Vertex_array);
    }

    void Renderer::BeginScene(const glm::mat4& projection, const glm::mat4& view){
        s_RenderCube->Reset();
        s_SceneData->ProjectionMatrix = projection;
        s_SceneData->ViewMatrix = view;
        s_RenderCube->m_Shader->Bind();
        s_RenderCube->m_Shader->SetMat4("view", s_SceneData->ViewMatrix);
        s_RenderCube->m_Shader->SetMat4("projection", s_SceneData->ProjectionMatrix);
    }

    void Renderer::Flush(){
        s_RenderCube->vertexBufferTransformation->SetData(s_RenderCube->m_Transformations.data(), 
            s_RenderCube->m_Transformations.size() * sizeof(glm::mat4));
        s_RenderCube->m_Vertex_array->Bind();
        glDrawElementsInstanced(GL_TRIANGLES, s_RenderCube->m_Vertex_array->GetIndexBuffer()->GetCount(),
            GL_UNSIGNED_INT, 0, s_RenderCube->m_Transformations.size());
    }


	void Renderer::EndScene(){
        Flush();

        s_RenderCube->m_Shader->Unbind();
	}

}