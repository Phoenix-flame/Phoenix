#include <Phoenix/renderer/renderer.h>
#include <Phoenix/renderer/renderer_command.h>
#include <Phoenix/core/base.h>

namespace Phoenix{
    Scope<Renderer::SceneData> Renderer::s_SceneData = CreateScope<Renderer::SceneData>();
    Scope<RenderCube> Renderer::s_RenderCube;
    Scope<RenderLightCube> Renderer::s_RenderLightCube = CreateScope<RenderLightCube>();
	void Renderer::Init(){
		RenderCommand::Init();
        s_RenderCube = CreateScope<RenderCube>();
        s_RenderLightCube->Init();
	}

	void Renderer::Shutdown(){
	}

	void Renderer::OnWindowResize(uint32_t width, uint32_t height){
		RenderCommand::SetViewport(0, 0, width, height);
	}

    void Renderer::SubmitCube(const glm::mat4& transform){
        if ((int)(*s_RenderCube) >= 1000) { Flush(); }
        s_RenderCube->m_Transformations.push_back(transform);
    }

    void Renderer::SubmitLightCube(
            const glm::vec3& cameraPos,
            const glm::vec3& lightPos,
            const glm::vec3& materialAmbient,
            const glm::vec3& materialDiffuse,
            const glm::vec3& materialSpecular,
            float materialShininess,
            const glm::vec3& lightAmbient,
            const glm::vec3& lightDiffuse,
            const glm::vec3& lightSpecular,
            const glm::mat4& transform ){
        s_RenderLightCube->m_Shader->SetFloat3("light.ambient", lightAmbient);
        s_RenderLightCube->m_Shader->SetFloat3("light.diffuse", lightDiffuse);
        s_RenderLightCube->m_Shader->SetFloat3("light.specular", lightSpecular);

        s_RenderLightCube->m_Shader->SetFloat3("material.ambient", materialAmbient);
        s_RenderLightCube->m_Shader->SetFloat3("material.diffuse", materialDiffuse);
        s_RenderLightCube->m_Shader->SetFloat3("material.specular", materialSpecular);
        s_RenderLightCube->m_Shader->SetFloat("material.shininess", materialShininess);

        s_RenderLightCube->m_Shader->SetFloat3("cameraPos", cameraPos);
        s_RenderLightCube->m_Shader->SetFloat3("light.position", lightPos);
        s_RenderLightCube->m_Shader->SetMat4("model", transform);
        s_RenderLightCube->m_Shader->SetMat4("view", s_SceneData->ViewMatrix);
        s_RenderLightCube->m_Shader->SetMat4("projection", s_SceneData->ProjectionMatrix);
        s_RenderLightCube->m_Vertex_array->Bind();
        RenderCommand::DrawIndexed(s_RenderLightCube->m_Vertex_array);
    }


    void Renderer::BeginScene(const glm::mat4& projection, const glm::mat4& view){
        s_SceneData->ProjectionMatrix = projection;
        s_SceneData->ViewMatrix = view;
        s_RenderLightCube->m_Shader->Bind();
    }

    void Renderer::Flush(){
        s_RenderCube->m_Shader->Bind();
        s_RenderCube->m_Shader->SetMat4("view", s_SceneData->ViewMatrix);
        s_RenderCube->m_Shader->SetMat4("projection", s_SceneData->ProjectionMatrix);
        s_RenderCube->vertexBufferTransformation->SetData(s_RenderCube->m_Transformations.data(), 
            s_RenderCube->m_Transformations.size() * sizeof(glm::mat4));
        s_RenderCube->m_Vertex_array->Bind();
        glDrawElementsInstanced(GL_TRIANGLES, s_RenderCube->m_Vertex_array->GetIndexBuffer()->GetCount(),
            GL_UNSIGNED_INT, 0, s_RenderCube->m_Transformations.size());
        s_RenderCube->m_Shader->Unbind();
        s_RenderCube->Reset();
    }


	void Renderer::EndScene(){
        // Flush();
        s_RenderLightCube->m_Shader->Unbind();
	}

}