#include <Phoenix/renderer/renderer.h>
#include <Phoenix/renderer/renderer_command.h>
#include <Phoenix/core/base.h>
#include <cmath>

namespace Phoenix{
    Scope<Renderer::SceneData> Renderer::s_SceneData = CreateScope<Renderer::SceneData>();
    Scope<RenderLightCube> Renderer::s_RenderLightCube = CreateScope<RenderLightCube>();
    Scope<RenderCameraGizmo> Renderer::s_CameraGizmo = CreateScope<RenderCameraGizmo>();
    Scope<RenderDirLightGizmo> Renderer::s_DirLightGizmo = CreateScope<RenderDirLightGizmo>();
    Ref<Shader> Renderer::s_OutlineShader;

	void Renderer::Init(){
		RenderCommand::Init();
        s_RenderLightCube->Init();
        s_CameraGizmo->Init();
        s_DirLightGizmo->Init();
        s_OutlineShader = Shader::Create("assets/shaders/outline.glsl");
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
            const glm::vec3& ambient,
            bool dirLightExists,
            const DirLightComponent& dirLight,
            const glm::vec3& dirLightDir,
            PointLightComponent pointLight[],
            const glm::vec3 pointLightPos[],
            int numOfPointLight ){

        auto& shader = s_RenderLightCube->m_Shader;

        shader->SetFloat3("u_Ambient", ambient);

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

    void Renderer::SetWireframe(bool enabled){
        glPolygonMode(GL_FRONT_AND_BACK, enabled ? GL_LINE : GL_FILL);
    }

    void Renderer::DrawOutline(const std::vector<Ref<VertexArray>>& vertexArrays, const glm::mat4& transform, const glm::vec3& color){
        if (vertexArrays.empty()) { return; }

        s_OutlineShader->Bind();
        s_OutlineShader->SetMat4("view", s_SceneData->ViewMatrix);
        s_OutlineShader->SetMat4("projection", s_SceneData->ProjectionMatrix);
        s_OutlineShader->SetFloat3("u_Color", color);

        glEnable(GL_STENCIL_TEST);
        glClear(GL_STENCIL_BUFFER_BIT);
        glDisable(GL_DEPTH_TEST); // keep the outline visible even when occluded

        // Pass 1: mark the silhouette (stencil = 1), writing neither colour nor depth.
        glStencilFunc(GL_ALWAYS, 1, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        glStencilMask(0xFF);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        s_OutlineShader->SetMat4("model", transform);
        for (const auto& va : vertexArrays){ va->Bind(); RenderCommand::DrawIndexed(va); }
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

        // Pass 2: draw the enlarged object only OUTSIDE the silhouette -> a thin border.
        glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
        glStencilMask(0x00);
        s_OutlineShader->SetMat4("model", transform * glm::scale(glm::mat4(1.0f), glm::vec3(1.03f)));
        for (const auto& va : vertexArrays){ va->Bind(); RenderCommand::DrawIndexed(va); }

        // Restore state.
        glStencilMask(0xFF);
        glStencilFunc(GL_ALWAYS, 0, 0xFF);
        glDisable(GL_STENCIL_TEST);
        glEnable(GL_DEPTH_TEST);
        s_OutlineShader->Unbind();
    }

    void Renderer::DrawOutlineCube(const glm::mat4& transform, const glm::vec3& color){
        DrawOutline({ s_RenderLightCube->m_Vertex_array }, transform, color);
    }

    void Renderer::DrawCameraGizmo(const glm::mat4& transform, float verticalFov, float aspect, const glm::vec3& color){
        // Shape the unit frustum (base half-size 1 at z=-1) to the camera's FOV/aspect.
        const float length = 1.5f;
        float halfHeight = length * std::tan(verticalFov * 0.5f);
        float halfWidth = halfHeight * aspect;
        glm::mat4 model = transform * glm::scale(glm::mat4(1.0f), glm::vec3(halfWidth, halfHeight, length));

        s_OutlineShader->Bind();
        s_OutlineShader->SetMat4("view", s_SceneData->ViewMatrix);
        s_OutlineShader->SetMat4("projection", s_SceneData->ProjectionMatrix);
        s_OutlineShader->SetMat4("model", model);
        s_OutlineShader->SetFloat3("u_Color", color);

        s_CameraGizmo->m_Vertex_array->Bind();
        glLineWidth(2.0f);
        glDrawElements(GL_LINES, 16, GL_UNSIGNED_INT, 0);

        s_OutlineShader->Unbind();
    }

    void Renderer::DrawDirLightGizmo(const glm::mat4& transform, const glm::vec3& color){
        // Use rotation+translation only (drop scale) so the arrow keeps a fixed size.
        glm::vec3 pos = glm::vec3(transform[3]);
        glm::mat3 rotation = glm::mat3(transform);
        rotation[0] = glm::normalize(rotation[0]);
        rotation[1] = glm::normalize(rotation[1]);
        rotation[2] = glm::normalize(rotation[2]);
        glm::mat4 model = glm::translate(glm::mat4(1.0f), pos) * glm::mat4(rotation);

        s_OutlineShader->Bind();
        s_OutlineShader->SetMat4("view", s_SceneData->ViewMatrix);
        s_OutlineShader->SetMat4("projection", s_SceneData->ProjectionMatrix);
        s_OutlineShader->SetMat4("model", model);
        s_OutlineShader->SetFloat3("u_Color", color);

        s_DirLightGizmo->m_Vertex_array->Bind();
        glLineWidth(2.0f);
        glDrawElements(GL_LINES, 10, GL_UNSIGNED_INT, 0);

        s_OutlineShader->Unbind();
    }

	void Renderer::EndScene(){
        s_RenderLightCube->m_Shader->Unbind();
	}

}
