#pragma once 
#include <Phoenix/renderer/renderer_api.h>
#include <Phoenix/renderer/shader.h>
#include <Phoenix/renderer/Camera.h>
#include <Phoenix/renderer/Texture.h>
#include <Phoenix/Scene/Component.h>
#include <vector>

namespace Phoenix{

    struct RenderLightCube{
        RenderLightCube() = default;
        void Init(){
            m_Shader = Shader::Create("assets/shaders/lighting.glsl");
            m_Vertex_array = CreateRef<VertexArray>();
            m_Vertex_array->Bind();
            Ref<VertexBuffer> vertexBuffer = CreateRef<VertexBuffer>(this->vertices, sizeof(this->vertices));
            BufferLayout layout = {
                { ShaderDataType::Float3, "a_Position" },
                { ShaderDataType::Float3, "a_Normal" }
            };
            vertexBuffer->SetLayout(layout);
            m_Vertex_array->AddVertexBuffer(vertexBuffer);
            
            Ref<IndexBuffer> indexBuffer = CreateRef<IndexBuffer>(indices, sizeof(indices) / sizeof(unsigned int));
            m_Vertex_array->SetIndexBuffer(indexBuffer);
        }

        float vertices[216] = {
            -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
             0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
             0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
             0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
            -0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
            -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,

            -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
             0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
             0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
             0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
            -0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
            -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,

            -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,
            -0.5f,  0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
            -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
            -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
            -0.5f, -0.5f,  0.5f, -1.0f,  0.0f,  0.0f,
            -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,

             0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,
             0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
             0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
             0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
             0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f,
             0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,

            -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,
             0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,
             0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
             0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
            -0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
            -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,

            -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,
             0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,
             0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
             0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
            -0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
            -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f
        };
        unsigned int indices[36] = {
            0, 1, 2,   // first Box
            3, 4, 5,
            6, 7, 8,
            9, 10, 11,
            12, 13, 14, 
            15, 16, 17, 
            18, 19, 20,
            21, 22, 23,
            24, 25, 26, 
            27, 28, 29,
            30, 31, 32,
            33, 34, 35
        };  

        Ref<VertexArray> m_Vertex_array;
        Ref<Shader> m_Shader;
    };


    // Wireframe frustum pyramid drawn as GL_LINES to visualize a camera in the scene.
    // Unit shape: apex at the origin, square base of half-size 1 at z = -1 (looking -Z).
    struct RenderCameraGizmo{
        void Init(){
            m_Vertex_array = CreateRef<VertexArray>();
            m_Vertex_array->Bind();
            Ref<VertexBuffer> vertexBuffer = CreateRef<VertexBuffer>(vertices, sizeof(vertices));
            BufferLayout layout = { { ShaderDataType::Float3, "a_Position" } };
            vertexBuffer->SetLayout(layout);
            m_Vertex_array->AddVertexBuffer(vertexBuffer);
            Ref<IndexBuffer> indexBuffer = CreateRef<IndexBuffer>(indices, sizeof(indices) / sizeof(unsigned int));
            m_Vertex_array->SetIndexBuffer(indexBuffer);
        }

        Ref<VertexArray> m_Vertex_array;
        float vertices[15] = {
             0.0f,  0.0f,  0.0f, // apex (camera position)
            -1.0f, -1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,
             1.0f,  1.0f, -1.0f,
            -1.0f,  1.0f, -1.0f
        };
        unsigned int indices[16] = {
            0,1, 0,2, 0,3, 0,4, // apex to base corners
            1,2, 2,3, 3,4, 4,1  // base square
        };
    };


    // Arrow drawn as GL_LINES to visualize a directional light's aim (local -Z).
    struct RenderDirLightGizmo{
        void Init(){
            m_Vertex_array = CreateRef<VertexArray>();
            m_Vertex_array->Bind();
            Ref<VertexBuffer> vertexBuffer = CreateRef<VertexBuffer>(vertices, sizeof(vertices));
            BufferLayout layout = { { ShaderDataType::Float3, "a_Position" } };
            vertexBuffer->SetLayout(layout);
            m_Vertex_array->AddVertexBuffer(vertexBuffer);
            Ref<IndexBuffer> indexBuffer = CreateRef<IndexBuffer>(indices, sizeof(indices) / sizeof(unsigned int));
            m_Vertex_array->SetIndexBuffer(indexBuffer);
        }

        Ref<VertexArray> m_Vertex_array;
        float vertices[18] = {
             0.0f,  0.0f,  0.0f, // tail
             0.0f,  0.0f, -1.0f, // tip (points along -Z)
             0.15f, 0.0f, -0.7f,
            -0.15f, 0.0f, -0.7f,
             0.0f,  0.15f,-0.7f,
             0.0f, -0.15f,-0.7f
        };
        unsigned int indices[10] = {
            0,1,        // shaft
            1,2, 1,3, 1,4, 1,5  // arrowhead
        };
    };


    class Renderer{
	public:
		static void Init();
		static void Shutdown();

		static void OnWindowResize(uint32_t width, uint32_t height);

		// Begin a scene: binds the lighting shader and uploads camera state once.
		static void BeginScene(const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cameraPos);
		static void EndScene();

		// Upload the lighting environment once per scene, before any Submit calls.
		static void SetLights(
			const glm::vec3& ambient,
			bool dirLightExists,
			const DirLightComponent& dirLight,
			const glm::vec3& dirLightDir,
			PointLightComponent pointLight[],
			const glm::vec3 pointLightPos[],
			int numOfPointLight);

		// Draw any indexed geometry (position@0, normal@1, texcoord@2) with the lighting
		// shader. If diffuseMap is non-null it is sampled in place of material.diffuse.
		static void Submit(const Ref<VertexArray>& vertexArray, const Material& material, const glm::mat4& transform = glm::mat4(1.0f), const Ref<Texture2D>& diffuseMap = nullptr);

		// Draw a skinned mesh: same as Submit but uploads the per-frame bone matrices
		// and enables vertex skinning in the shader.
		static void SubmitAnimated(const Ref<VertexArray>& vertexArray, const Material& material, const glm::mat4& transform,
			const std::vector<glm::mat4>& boneMatrices, const Ref<Texture2D>& diffuseMap = nullptr);

		// Convenience: draw the built-in unit cube.
		static void SubmitCube(const Material& material, const glm::mat4& transform = glm::mat4(1.0f));

		// Toggle polygon fill mode (wireframe) for subsequent draws. Reset to false
		// after drawing the affected objects.
		static void SetWireframe(bool enabled);

		// Directional shadow map. Call BeginShadowPass, submit all shadow casters,
		// then EndShadowPass BEFORE BeginScene; the lighting pass then samples the
		// resulting depth map. The pass saves/restores the bound framebuffer+viewport.
		static void BeginShadowPass(const glm::mat4& lightSpaceMatrix);
		static void SubmitShadow(const Ref<VertexArray>& vertexArray, const glm::mat4& transform);
		static void SubmitShadowAnimated(const Ref<VertexArray>& vertexArray, const glm::mat4& transform,
			const std::vector<glm::mat4>& boneMatrices);
		static void SubmitShadowCube(const glm::mat4& transform);
		static void EndShadowPass();

		// Selection outline: stencils the object's silhouette, then draws a slightly
		// enlarged flat-color version only where the stencil is unset, leaving a thin
		// border line around the outer edge. Pass all of an object's sub-VAOs together
		// so they share one silhouette. Uses the camera state from the last BeginScene.
		static void DrawOutline(const std::vector<Ref<VertexArray>>& vertexArrays, const glm::mat4& transform, const glm::vec3& color);
		static void DrawOutlineCube(const glm::mat4& transform, const glm::vec3& color);

		// Draw a wireframe camera frustum at the given transform. verticalFov is in
		// radians; aspect is width/height. Depth-tested so scene geometry occludes it.
		static void DrawCameraGizmo(const glm::mat4& transform, float verticalFov, float aspect, const glm::vec3& color);

		// Draw an arrow showing a directional light's aim (the transform's -Z).
		static void DrawDirLightGizmo(const glm::mat4& transform, const glm::vec3& color);

		// Draw an animated transparent water surface (flat grid displaced in the shader).
		// Call after the opaque scene; uses the camera state from the last BeginScene.
		static void SubmitWater(const Ref<VertexArray>& vertexArray, const glm::mat4& transform,
			const glm::vec3& color, float alpha, const glm::vec3& lightDir, float time,
			float amplitude, float waveScale, float speed);

		static RendererAPI::API GetAPI() { return RendererAPI::GetAPI(); }
	private:
		struct SceneData{
			glm::mat4 ViewMatrix;
            glm::mat4 ProjectionMatrix;
            glm::vec3 CameraPos;
		};
		static Scope<SceneData> s_SceneData;
        static Scope<RenderLightCube> s_RenderLightCube;
        static Scope<RenderCameraGizmo> s_CameraGizmo;
        static Scope<RenderDirLightGizmo> s_DirLightGizmo;
        static Ref<Shader> s_OutlineShader;
        static Ref<Shader> s_WaterShader;

        // Directional shadow map state.
        static uint32_t s_ShadowFBO, s_ShadowDepthTex;
        static Ref<Shader> s_ShadowShader;
        static glm::mat4 s_LightSpaceMatrix;
        static bool s_ShadowsEnabled;
        static int s_PrevFBO;
        static int s_PrevViewport[4];
	};
}