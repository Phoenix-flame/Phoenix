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

		// Up to this many directional / point lights, each optionally with its own shadow.
		// Must match NR_DIR_LIGHTS / NR_POINT_LIGHTS in lighting.glsl.
		static const int MAX_DIR_LIGHTS = 8;
		static const int MAX_POINT_LIGHTS = 4;

		// Upload the lighting environment once per scene, before any Submit calls.
		// Pass arrays of directional lights (with their world-space directions) and point
		// lights. Light i is shadowed by the shadow rendered for light index i (see
		// BeginShadowPass / BeginPointShadowPass).
		static void SetLights(
			const glm::vec3& ambient,
			const DirLightComponent dirLights[],
			const glm::vec3 dirLightDirs[],
			int numDirLights,
			PointLightComponent pointLight[],
			const glm::vec3 pointLightPos[],
			int numOfPointLight);

		// Draw any indexed geometry (position@0, normal@1, texcoord@2) with the lighting
		// shader. If diffuseMap is non-null it is sampled in place of material.diffuse;
		// normalMap adds surface detail unless the material sets its own. alphaCutoff > 0
		// discards fragments whose diffuse alpha falls below it (cut-out foliage).
		static void Submit(const Ref<VertexArray>& vertexArray, const Material& material, const glm::mat4& transform = glm::mat4(1.0f),
			const Ref<Texture2D>& diffuseMap = nullptr, const Ref<Texture2D>& normalMap = nullptr, float alphaCutoff = 0.0f);

		// Draw a skinned mesh: same as Submit but uploads the per-frame bone matrices
		// and enables vertex skinning in the shader.
		static void SubmitAnimated(const Ref<VertexArray>& vertexArray, const Material& material, const glm::mat4& transform,
			const std::vector<glm::mat4>& boneMatrices, const Ref<Texture2D>& diffuseMap = nullptr,
			const Ref<Texture2D>& normalMap = nullptr, float alphaCutoff = 0.0f);

		// Convenience: draw the built-in unit cube.
		static void SubmitCube(const Material& material, const glm::mat4& transform = glm::mat4(1.0f));

		// Toggle polygon fill mode (wireframe) for subsequent draws. Reset to false
		// after drawing the affected objects.
		static void SetWireframe(bool enabled);

		// Shadows. Each frame, BEFORE BeginScene:
		//   BeginShadows(numDir, numPoint)            -- counts pick the atlas tile sizes
		//   for each shadowed directional light i:  BeginShadowPass(i, lightSpace, settings),
		//                                            submit casters, EndShadowPass()
		//   for each shadowed point light i, face 0..5: BeginPointShadowPass(i, face, ...),
		//                                            submit casters, EndShadowPass()
		// Light index i is the light's index in the SetLights arrays. Lights without a
		// pass are unshadowed. Each pass saves/restores the FBO + viewport.
		static void BeginShadows(int numDirShadows, int numPointShadows);
		static void BeginShadowPass(int lightIndex, const glm::mat4& lightSpaceMatrix, const ShadowSettings& settings = {});
		static void BeginPointShadowPass(int lightIndex, int face, const glm::vec3& position, float range,
			const ShadowSettings& settings = {});
		// alphaMap/alphaCutoff: a cut-out caster's diffuse map, so only its opaque texels
		// cast (leaves shadow as leaves, not as quads).
		static void SubmitShadow(const Ref<VertexArray>& vertexArray, const glm::mat4& transform,
			const Ref<Texture2D>& alphaMap = nullptr, float alphaCutoff = 0.0f);
		static void SubmitShadowAnimated(const Ref<VertexArray>& vertexArray, const glm::mat4& transform,
			const std::vector<glm::mat4>& boneMatrices, const Ref<Texture2D>& alphaMap = nullptr, float alphaCutoff = 0.0f);
		static void SubmitShadowCube(const glm::mat4& transform);
		static void EndShadowPass();

		// Edge length (texels) of a directional light's shadow tile when numDirShadows
		// lights cast shadows; fit light frusta to it so they can snap to whole texels.
		static uint32_t GetDirShadowResolution(int numDirShadows);
		// View-projection of one cube face (0..5 = +X,-X,+Y,-Y,+Z,-Z) of a point light's
		// shadow; slightly wider than 90 degrees so soft filtering never runs off a face.
		static glm::mat4 GetPointShadowMatrix(const glm::vec3& position, int face, float range);

		// Global shadow switches (editor Settings). Samples = taps per shadow lookup:
		// more = smoother soft penumbrae, slower.
		static void SetShadowsEnabled(bool enabled) { s_ShadowsEnabled = enabled; }
		static bool AreShadowsEnabled() { return s_ShadowsEnabled; }
		static void SetShadowSamples(int samples);
		static int GetShadowSamples() { return s_ShadowSamples; }

		// Selection outline: stencils the object's silhouette, then draws a slightly
		// enlarged flat-color version only where the stencil is unset, leaving a thin
		// border line around the outer edge. Pass all of an object's sub-VAOs together
		// so they share one silhouette. Uses the camera state from the last BeginScene.
		static void DrawOutline(const std::vector<Ref<VertexArray>>& vertexArrays, const glm::mat4& transform, const glm::vec3& color,
			const std::vector<glm::mat4>* boneMatrices = nullptr);
		static void DrawOutlineCube(const glm::mat4& transform, const glm::vec3& color);

		// Draw a wireframe camera frustum at the given transform. verticalFov is in
		// radians; aspect is width/height. Depth-tested so scene geometry occludes it.
		static void DrawCameraGizmo(const glm::mat4& transform, float verticalFov, float aspect, const glm::vec3& color);

		// Draw an arrow showing a directional light's aim (the transform's -Z).
		static void DrawDirLightGizmo(const glm::mat4& transform, const glm::vec3& color);

		// Draw world-space line segments (pairs of points) in one flat colour. With
		// onTop the lines ignore depth (debug overlays such as physics colliders).
		static void DrawLines(const std::vector<glm::vec3>& points, const glm::vec3& color, bool onTop = true);

		// Water surface render inputs (Gerstner waves + optional dynamic ripple texture).
		struct WaterParams{
			glm::vec3 color = glm::vec3(0.05f, 0.22f, 0.35f);
			float alpha = 0.7f;
			glm::vec3 lightDir = glm::vec3(0.0f, -1.0f, 0.0f);
			float time = 0.0f;
			float amplitude = 0.15f;
			float waveScale = 0.6f;
			float speed = 1.2f;
			float choppiness = 0.6f;   // Gerstner crest sharpness (0 = plain sines)
			float foam = 0.5f;         // crest/ripple foam intensity
			float size = 40.0f;        // world width of the grid (ripple slope scale)
			uint32_t rippleTexture = 0; // R32F heightfield; 0 = no dynamic ripples
		};

		// Draw an animated transparent water surface (flat grid displaced in the shader).
		// Call after the opaque scene; uses the camera state from the last BeginScene.
		static void SubmitWater(const Ref<VertexArray>& vertexArray, const glm::mat4& transform,
			const WaterParams& params);

		// Linear lighting (opt-in; off keeps the original look). When on, colour textures
		// are sampled through their sRGB views, colours picked in the editor (materials,
		// lights, ambient, water, gizmos) are converted from sRGB to linear before shading,
		// and the HDR composite (see Bloom) encodes its output back to sRGB. Lighting and
		// blending then happen in linear space, so falloff and mixing are correct.
		static void SetLinearLighting(bool enabled) { s_LinearLighting = enabled; }
		static bool IsLinearLighting() { return s_LinearLighting; }
		// sRGB -> linear for an editor colour; returns it unchanged when linear lighting is off.
		static glm::vec3 ToLinear(const glm::vec3& srgb);

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

        // Shadow maps are packed into two depth-texture ATLASES (directional: one tile
        // per light; point: six face tiles per light). The fragment shader picks a tile by
        // UV-offset math on a plain sampler2DShadow, avoiding sampler arrays, which this
        // GLSL-ES-on-desktop driver won't dynamically index. Each atlas is also bound a
        // second time through a non-comparing sampler object to read raw depths for the
        // soft-shadow blocker search.
        struct ShadowAtlas{
            uint32_t fbo = 0, texture = 0;
            uint32_t width = 0, height = 0;
            uint32_t tile = 0;          // tile edge in texels for this frame's layout
            void Init(uint32_t w, uint32_t h);
        };
        static ShadowAtlas s_DirAtlas, s_PointAtlas;
        static uint32_t s_RawDepthSampler;
        static Ref<Shader> s_ShadowShader;

        // Per-light shadow state for the current frame, indexed like the SetLights arrays.
        // A zero rect size means "this light has no shadow".
        static glm::mat4 s_LightSpaceMatrix[MAX_DIR_LIGHTS];
        static glm::vec4 s_DirShadowRect[MAX_DIR_LIGHTS];
        static glm::vec4 s_DirShadowParams[MAX_DIR_LIGHTS];
        static glm::vec4 s_DirShadowInfo[MAX_DIR_LIGHTS];
        static glm::mat4 s_PointShadowMatrix[MAX_POINT_LIGHTS * 6];
        static glm::vec4 s_PointShadowRect[MAX_POINT_LIGHTS * 6];
        static glm::vec4 s_PointShadowParams[MAX_POINT_LIGHTS];
        static glm::vec4 s_PointShadowInfo[MAX_POINT_LIGHTS];
        static int s_DirShadowSlots, s_PointShadowSlots;     // atlas slots handed out this frame
        static int s_DirSlotOf[MAX_DIR_LIGHTS], s_PointSlotOf[MAX_POINT_LIGHTS];

        static bool s_ShadowsEnabled;
        static int s_ShadowSamples;
        static int s_PrevFBO;
        static int s_PrevViewport[4];
        static bool s_LinearLighting;
	};
}