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
            Material material,
            bool dirLightExists,
            DirLightComponent dirLight,
            const glm::vec3& lightPos,
            PointLightComponent pointLight[],
            const glm::vec3 pointLightPos[],
            int numOfPointLight,
            const glm::mat4& transform ){

        if (dirLightExists)
        {
            s_RenderLightCube->m_Shader->SetFloat3("dirLight.ambient", dirLight.ambient);
            s_RenderLightCube->m_Shader->SetFloat3("dirLight.diffuse", dirLight.diffuse);
            s_RenderLightCube->m_Shader->SetFloat3("dirLight.specular", dirLight.specular);
            s_RenderLightCube->m_Shader->SetFloat3("dirLight.direction", lightPos);
        }
        else {
            s_RenderLightCube->m_Shader->SetFloat3("dirLight.ambient", glm::vec3(0.0, 0.0, 0.0));
            s_RenderLightCube->m_Shader->SetFloat3("dirLight.diffuse", glm::vec3(0.0, 0.0, 0.0));
            s_RenderLightCube->m_Shader->SetFloat3("dirLight.specular", glm::vec3(0.0, 0.0, 0.0));
            s_RenderLightCube->m_Shader->SetFloat3("dirLight.direction", glm::vec3(0.0, 0.0, 0.0));
        }

        for (int i = 0 ; i < 4 ; i ++)
        {
            if (i < numOfPointLight)
            {
                s_RenderLightCube->m_Shader->SetFloat("pointLights[" + std::to_string(i) + "].constant", pointLight[i].constant);
                s_RenderLightCube->m_Shader->SetFloat("pointLights[" + std::to_string(i) + "].linear", pointLight[i].linear);
                s_RenderLightCube->m_Shader->SetFloat("pointLights[" + std::to_string(i) + "].quadratic", pointLight[i].quadratic);
                s_RenderLightCube->m_Shader->SetFloat3("pointLights[" + std::to_string(i) + "].ambient", pointLight[i].ambient);
                s_RenderLightCube->m_Shader->SetFloat3("pointLights[" + std::to_string(i) + "].diffuse", pointLight[i].diffuse);
                s_RenderLightCube->m_Shader->SetFloat3("pointLights[" + std::to_string(i) + "].specular", pointLight[i].specular);
                s_RenderLightCube->m_Shader->SetFloat3("pointLights[" + std::to_string(i) + "].position", pointLightPos[i]);
            }
            else{
                s_RenderLightCube->m_Shader->SetFloat("pointLights[" + std::to_string(i) + "].constant", 1.0f);
                s_RenderLightCube->m_Shader->SetFloat("pointLights[" + std::to_string(i) + "].linear", 0.03f);
                s_RenderLightCube->m_Shader->SetFloat("pointLights[" + std::to_string(i) + "].quadratic", 0.099f);
                s_RenderLightCube->m_Shader->SetFloat3("pointLights[" + std::to_string(i) + "].ambient", glm::vec3(0.0, 0.0, 0.0));
                s_RenderLightCube->m_Shader->SetFloat3("pointLights[" + std::to_string(i) + "].diffuse", glm::vec3(0.0, 0.0, 0.0));
                s_RenderLightCube->m_Shader->SetFloat3("pointLights[" + std::to_string(i) + "].specular", glm::vec3(0.0, 0.0, 0.0));
                s_RenderLightCube->m_Shader->SetFloat3("pointLights[" + std::to_string(i) + "].position", glm::vec3(0.0, 0.0, 0.0));
            }
        }
      

        s_RenderLightCube->m_Shader->SetFloat3("material.ambient", material.ambient);
        s_RenderLightCube->m_Shader->SetFloat3("material.diffuse", material.diffuse);
        s_RenderLightCube->m_Shader->SetFloat3("material.specular", material.specular);
        s_RenderLightCube->m_Shader->SetFloat("material.shininess", material.shininess);

        s_RenderLightCube->m_Shader->SetFloat3("cameraPos", cameraPos);

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