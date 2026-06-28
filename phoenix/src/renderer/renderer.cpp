#include <Phoenix/renderer/renderer.h>
#include <Phoenix/renderer/renderer_command.h>
#include <Phoenix/core/base.h>

namespace Phoenix{
    Scope<Renderer::SceneData> Renderer::s_SceneData = CreateScope<Renderer::SceneData>();
    Scope<RenderLightCube> Renderer::s_RenderLightCube = CreateScope<RenderLightCube>();

	void Renderer::Init(){
		RenderCommand::Init();
        s_RenderLightCube->Init();
	}

	void Renderer::Shutdown(){
	}

	void Renderer::OnWindowResize(uint32_t width, uint32_t height){
		RenderCommand::SetViewport(0, 0, width, height);
	}

    void Renderer::BeginScene(const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cameraPos){
        s_SceneData->ProjectionMatrix = projection;
        s_SceneData->ViewMatrix = view;
        s_SceneData->CameraPos = cameraPos;

        auto& shader = s_RenderLightCube->m_Shader;
        shader->Bind();
        shader->SetMat4("view", view);
        shader->SetMat4("projection", projection);
        shader->SetFloat3("cameraPos", cameraPos);
    }

    void Renderer::SetLights(
            bool dirLightExists,
            const DirLightComponent& dirLight,
            const glm::vec3& dirLightDir,
            PointLightComponent pointLight[],
            const glm::vec3 pointLightPos[],
            int numOfPointLight ){

        auto& shader = s_RenderLightCube->m_Shader;

        if (dirLightExists)
        {
            shader->SetFloat3("dirLight.ambient", dirLight.ambient);
            shader->SetFloat3("dirLight.diffuse", dirLight.diffuse);
            shader->SetFloat3("dirLight.specular", dirLight.specular);
            shader->SetFloat3("dirLight.direction", dirLightDir);
        }
        else {
            shader->SetFloat3("dirLight.ambient", glm::vec3(0.0, 0.0, 0.0));
            shader->SetFloat3("dirLight.diffuse", glm::vec3(0.0, 0.0, 0.0));
            shader->SetFloat3("dirLight.specular", glm::vec3(0.0, 0.0, 0.0));
            shader->SetFloat3("dirLight.direction", glm::vec3(0.0, 0.0, 0.0));
        }

        for (int i = 0 ; i < 4 ; i ++)
        {
            std::string base = "pointLights[" + std::to_string(i) + "].";
            if (i < numOfPointLight)
            {
                shader->SetFloat(base + "constant", pointLight[i].constant);
                shader->SetFloat(base + "linear", pointLight[i].linear);
                shader->SetFloat(base + "quadratic", pointLight[i].quadratic);
                shader->SetFloat3(base + "ambient", pointLight[i].ambient);
                shader->SetFloat3(base + "diffuse", pointLight[i].diffuse);
                shader->SetFloat3(base + "specular", pointLight[i].specular);
                shader->SetFloat3(base + "position", pointLightPos[i]);
            }
            else{
                shader->SetFloat(base + "constant", 1.0f);
                shader->SetFloat(base + "linear", 0.03f);
                shader->SetFloat(base + "quadratic", 0.099f);
                shader->SetFloat3(base + "ambient", glm::vec3(0.0, 0.0, 0.0));
                shader->SetFloat3(base + "diffuse", glm::vec3(0.0, 0.0, 0.0));
                shader->SetFloat3(base + "specular", glm::vec3(0.0, 0.0, 0.0));
                shader->SetFloat3(base + "position", glm::vec3(0.0, 0.0, 0.0));
            }
        }
    }

    void Renderer::Submit(const Ref<VertexArray>& vertexArray, const Material& material, const glm::mat4& transform, const Ref<Texture2D>& diffuseMap){
        auto& shader = s_RenderLightCube->m_Shader;

        shader->SetFloat3("material.ambient", material.ambient);
        shader->SetFloat3("material.diffuse", material.diffuse);
        shader->SetFloat3("material.specular", material.specular);
        shader->SetFloat("material.shininess", material.shininess);

        if (diffuseMap){
            diffuseMap->Bind(0);
            shader->SetInt("u_DiffuseMap", 0);
            shader->SetInt("u_HasDiffuseMap", 1);
        }
        else{
            shader->SetInt("u_HasDiffuseMap", 0);
        }

        shader->SetMat4("model", transform);

        vertexArray->Bind();
        RenderCommand::DrawIndexed(vertexArray);
    }

    void Renderer::SubmitCube(const Material& material, const glm::mat4& transform){
        Submit(s_RenderLightCube->m_Vertex_array, material, transform);
    }

	void Renderer::EndScene(){
        s_RenderLightCube->m_Shader->Unbind();
	}

}
