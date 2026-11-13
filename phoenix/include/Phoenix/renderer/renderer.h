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


    // Textures a mesh brings to a draw; the material's own texture paths override them.
    struct SurfaceMaps{
        Ref<Texture2D> diffuse;  // sampled in place of material.diffuse
        Ref<Texture2D> normal;   // tangent-space surface detail
        Ref<Texture2D> height;   // parallax occlusion mapping (white = high)
        float alphaCutoff = 0.0f; // > 0: cut out where the diffuse alpha is below it (foliage)
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
		static const int MAX_POINT_LIGHTS = 8;

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

		// Draw any indexed geometry (position@0, normal@1, texcoord@2) with the lighting shader.
		static void Submit(const Ref<VertexArray>& vertexArray, const Material& material, const glm::mat4& transform = glm::mat4(1.0f),
			const SurfaceMaps& maps = {});

		// Draw a skinned mesh: same as Submit but uploads the per-frame bone matrices
		// and enables vertex skinning in the shader.
		static void SubmitAnimated(const Ref<VertexArray>& vertexArray, const Material& material, const glm::mat4& transform,
			const std::vector<glm::mat4>& boneMatrices, const SurfaceMaps& maps = {});

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

		// Screen-space ambient occlusion. Each frame, after the shadow passes and before
		// BeginScene: BeginDepthPrepass(viewProj), submit every opaque object with the
		// SubmitShadow* calls, EndDepthPrepass(projection). The occlusion then darkens
		// ambient light in that frame's lighting pass. Radius is in world units.
		static void SetSSAOEnabled(bool enabled) { s_SSAO.enabled = enabled; }
		static bool IsSSAOEnabled() { return s_SSAO.enabled; }
		static void SetSSAORadius(float radius) { s_SSAO.radius = radius; }
		static float GetSSAORadius() { return s_SSAO.radius; }
		static void SetSSAOStrength(float strength) { s_SSAO.strength = strength; }
		static float GetSSAOStrength() { return s_SSAO.strength; }
		static void BeginDepthPrepass(const glm::mat4& viewProjection);
		static void EndDepthPrepass(const glm::mat4& projection);

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

		// The scene's sky, before BeginScene: lit materials and water reflect it, and
		// DrawSky paints it behind the scene. sunDir points towards the sun (the disc);
		// hasSun false hides the disc.
		static void SetSky(const SkySettings& sky, bool hasSun, const glm::vec3& sunDir, const glm::vec3& sunColor);
		// Fill the background with the sky (Gradient / Panorama modes; Color mode is the
		// clear colour). Call right after BeginScene, before any geometry.
		static void DrawSky();

		// Draw an animated transparent water surface (flat grid displaced in the shader).
		// Call after the opaque scene; uses the camera state from the last BeginScene.
		static void SubmitWater(const Ref<VertexArray>& vertexArray, const glm::mat4& transform,
			const WaterParams& params);

		// Pools (PoolComponent): the basin, its caustics, the water surface and the
		// underwater view. Order per frame: RenderPoolCaustics before the scene pass,
		// SubmitPoolShell in it, then after EndScene: CaptureScene once, DrawPoolUnderwater
		// if the camera is in a pool's water, and SubmitPoolWater for every pool.
		struct PoolParams{
			glm::mat4 model = glm::mat4(1.0f);   // rim centre and yaw
			glm::vec2 size = glm::vec2(8.0f, 5.0f);
			float depth = 2.0f;
			float waterLevel = -0.2f;            // still surface, local y
			glm::vec2 spacing = glm::vec2(0.05f); // between simulation vertices (x, z)
			glm::vec3 tileColor = glm::vec3(0.86f, 0.93f, 0.97f);
			glm::vec3 groutColor = glm::vec3(0.55f, 0.66f, 0.72f);
			float tileSize = 0.25f;
			glm::vec3 waterColor = glm::vec3(0.12f, 0.55f, 0.68f);
			float clarity = 25.0f;               // metres one can see under water
			float causticStrength = 0.8f;
			glm::vec3 lightDir = glm::vec3(0.0f, -1.0f, 0.0f); // direction sunlight travels (world)
			glm::vec3 sunColor = glm::vec3(1.0f);
			glm::vec3 ambient = glm::vec3(0.2f);
			uint32_t heightTexture = 0;          // R32F surface offsets (PoolWater)
			uint32_t causticsTexture = 0, causticsFramebuffer = 0;
			glm::ivec2 causticsSize = glm::ivec2(512);
		};
		static void RenderPoolCaustics(const Ref<VertexArray>& surface, const PoolParams& params);
		static void SubmitPoolShell(const Ref<VertexArray>& shell, const PoolParams& params);
		// Copies the colour and depth rendered so far into textures the water reads.
		static void CaptureScene();
		// Underwater haze over the whole view; surfaceY is the still level in world space.
		static void DrawPoolUnderwater(const PoolParams& params, float surfaceY);
		static void SubmitPoolWater(const Ref<VertexArray>& surface, const PoolParams& params, bool cameraUnderwater);

		// Particle liquids (FluidComponent, pool spray) drawn as one smooth surface each
		// (screen-space fluid rendering). Call after CaptureScene: the liquid refracts that
		// copy and is hidden behind its depth.
		struct FluidParams{
			float radius = 0.025f;                              // particle radius
			glm::vec3 color = glm::vec3(0.55f, 0.82f, 0.95f);   // tint of thick liquid
			float clarity = 2.0f;                               // metres before most light is absorbed
			glm::vec3 lightDir = glm::vec3(0.0f, -1.0f, 0.0f);
			glm::vec3 sunColor = glm::vec3(1.0f);
			glm::vec3 ambient = glm::vec3(0.2f);
		};
		static void RenderFluid(const std::vector<glm::vec4>& positions, const FluidParams& params);

		// Fires (FireComponent): each flame's grid ray-marched as glowing soot (the black-body
		// colour of its temperature where fuel burns, a faint blue in the reaction zone) with
		// smoke and the shimmer of the hot air above it; then its embers as glowing streaks.
		// Call after CaptureScene (the shimmer bends that copy and the march stops at its
		// depth), back to front.
		struct FireParams{
			glm::vec3 origin = glm::vec3(0.0f);                 // world box: min corner
			glm::vec3 size = glm::vec3(1.0f);                   // and extent
			glm::ivec3 dims = glm::ivec3(1);                    // cells
			uint32_t volume = 0;                                // FireVolume texture
			float brightness = 1.0f;
			float flameTemperature = 1900.0f;                   // kelvin at temperature 1
			glm::vec3 smokeColor = glm::vec3(0.12f);
			float smokeDensity = 1.0f;
			float heatHaze = 1.0f;
			float time = 0.0f;
			glm::vec3 ambient = glm::vec3(0.1f);                // light on the smoke
		};
		static void RenderFire(const FireParams& params);
		// One glowing ember: world position, temperature (1 = the flame's), velocity, radius.
		struct EmberPoint{ glm::vec3 position; float temperature; glm::vec3 velocity; float radius; };
		static void RenderEmbers(const std::vector<EmberPoint>& embers, float flameTemperature, float brightness);
		// Colour of a black body at `kelvin` (linear RGB, normalised to a maximum component of 1).
		static glm::vec3 BlackBodyColor(float kelvin);

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
        static Ref<Shader> s_PoolShellShader, s_PoolWaterShader, s_PoolCausticsShader, s_PoolUnderwaterShader;

        // Copy of the scene's colour and depth (CaptureScene), formats matching the target.
        struct SceneCopy{
            uint32_t fbo = 0, color = 0, depth = 0, emptyVAO = 0;
            int width = 0, height = 0;
            int colorFormat = 0;
        };
        static SceneCopy s_SceneCopy;

        // Screen-space fluid targets (the scene copy's size) and the particle buffer.
        static Ref<Shader> s_FluidParticleShader, s_FluidSmoothShader, s_FluidCompositeShader;
        struct FluidTargets{
            uint32_t depthFBO = 0, depthTex = 0, depthBuffer = 0;      // pass 1: sphere depth
            uint32_t blurFBO = 0, blurTex = 0;                          // pass 3 ping-pong
            uint32_t thickFBO = 0, thickTex = 0;                        // pass 2
            uint32_t vao = 0, vbo = 0;
            size_t capacity = 0;
            int width = 0, height = 0;
        };
        static FluidTargets s_FluidTargets;

        // Fire: volume and ember shaders, the black-body table, the ember buffer.
        static Ref<Shader> s_FireVolumeShader, s_FireEmberShader;
        struct FireTargets{
            uint32_t blackBody = 0;     // RGB16F, 0..FIRE_MAX_KELVIN
            uint32_t vao = 0, vbo = 0;
            size_t capacity = 0;
            uint32_t frame = 0;
        };
        static FireTargets s_FireTargets;
        // Binds the sun's shadow (directional light 0) for a shader that declares it.
        static void BindSunShadow(const Ref<Shader>& shader);

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

        // SSAO targets, sized to the scene's render target: camera depth, raw occlusion,
        // and the blurred occlusion the lighting pass reads (valid until EndScene).
        struct SSAOState{
            bool enabled = true;
            float radius = 0.5f;
            float strength = 1.5f;
            uint32_t width = 0, height = 0;
            uint32_t depthFBO = 0, depthTexture = 0;
            uint32_t aoFBO = 0, aoTexture = 0;
            uint32_t blurFBO = 0, blurTexture = 0;
            bool ready = false;
            Ref<Shader> aoShader, blurShader;
        };
        static SSAOState s_SSAO;
        static void ResizeSSAOTargets(uint32_t width, uint32_t height);

        static void BindSkyUniforms(const Ref<Shader>& shader);
        static Ref<Shader> s_SkyShader;
        static uint32_t s_SkyVAO; // empty: the sky triangle comes from gl_VertexID
        static SkySettings s_Sky;
        static Ref<Texture2D> s_SkyPanorama; // null unless Panorama mode with a loadable image
        static bool s_HasSun;
        static glm::vec3 s_SunDir, s_SunColor;
	};
}