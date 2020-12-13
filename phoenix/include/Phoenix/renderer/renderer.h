#pragma once 
#include <Phoenix/renderer/renderer_api.h>
#include <Phoenix/renderer/shader.h>
#include <Phoenix/renderer/Camera.h>

namespace Phoenix{

    struct RenderCube{
        RenderCube(){
            Init();
        }
        void Init(){
            m_Shader = Shader::Create("/home/alireza/Programming/C++/MyGameEngineProject/Sandbox/assets/shaders/basic.glsl");
            m_Vertex_array = CreateRef<VertexArray>();
            m_Vertex_array->Bind();
            Ref<VertexBuffer> vertexBuffer = CreateRef<VertexBuffer>(this->vertices, sizeof(this->vertices));
            BufferLayout layout = {
                { ShaderDataType::Float3, "a_Position" },
                { ShaderDataType::Float3, "a_Color" }
            };
            vertexBuffer->SetLayout(layout);
            m_Vertex_array->AddVertexBuffer(vertexBuffer);
            
            Ref<IndexBuffer> indexBuffer = CreateRef<IndexBuffer>(indices, sizeof(indices) / sizeof(unsigned int));
            m_Vertex_array->SetIndexBuffer(indexBuffer);

            glm::mat4 test[1] = {
                glm::mat4(1.0)
            };
            vertexBufferTransformation = CreateRef<VertexBuffer>(1000*sizeof(test));
            BufferLayout layoutTransformation = {
                { ShaderDataType::Mat4, "a_Position" }
            };
            // vertexBufferTransformation->SetData(test, sizeof(test));
            vertexBufferTransformation->SetLayout(layoutTransformation);
            m_Vertex_array->AddVertexBuffer(vertexBufferTransformation);

        }

        void Reset(){
            m_Transformations.clear();
        }
        
        operator int(){
            return m_Transformations.size();
        }

        Ref<VertexArray> m_Vertex_array;
        Ref<VertexBuffer> vertexBufferTransformation;
        Ref<Shader> m_Shader;
        std::vector<glm::mat4> m_Transformations;
        float vertices[216] = {
            -0.5f, -0.5f, -0.5f,  0.4f,  0.4f,  1.0f,
            0.5f, -0.5f, -0.5f,  0.4f,  0.4f,  1.0f,
            0.5f,  0.5f, -0.5f,  0.4f,  0.4f,  1.0f,
            0.5f,  0.5f, -0.5f,  0.4f,  0.4f,  1.0f,
            -0.5f,  0.5f, -0.5f,  0.4f,  0.4f,  1.0f,
            -0.5f, -0.5f, -0.5f,  0.4f,  0.4f,  1.0f,

            -0.5f, -0.5f,  0.5f,  0.4f,  0.4f,  1.0f,
            0.5f, -0.5f,  0.5f,  0.4f,  0.4f,  1.0f,
            0.5f,  0.5f,  0.5f,  0.4f,  0.4f,  1.0f,
            0.5f,  0.5f,  0.5f,  0.4f,  0.4f,  1.0f,
            -0.5f,  0.5f,  0.5f,  0.4f,  0.4f,  1.0f,
            -0.5f, -0.5f,  0.5f,  0.4f,  0.4f,  1.0f,

            -0.5f,  0.5f,  0.5f,  1.0f,  0.4f,  0.4f,
            -0.5f,  0.5f, -0.5f,  1.0f,  0.4f,  0.4f,
            -0.5f, -0.5f, -0.5f,  1.0f,  0.4f,  0.4f,
            -0.5f, -0.5f, -0.5f,  1.0f,  0.4f,  0.4f,
            -0.5f, -0.5f,  0.5f,  1.0f,  0.4f,  0.4f,
            -0.5f,  0.5f,  0.5f,  1.0f,  0.4f,  0.4f,

            0.5f,  0.5f,  0.5f,  1.0f,  0.4f,  0.4f,
            0.5f,  0.5f, -0.5f,  1.0f,  0.4f,  0.4f,
            0.5f, -0.5f, -0.5f,  1.0f,  0.4f,  0.4f,
            0.5f, -0.5f, -0.5f,  1.0f,  0.4f,  0.4f,
            0.5f, -0.5f,  0.5f,  1.0f,  0.4f,  0.4f,
            0.5f,  0.5f,  0.5f,  1.0f,  0.4f,  0.4f,

            -0.5f, -0.5f, -0.5f,  0.4f,  1.0f,  0.4f,
            0.5f, -0.5f, -0.5f,  0.4f,  1.0f,  0.4f,
            0.5f, -0.5f,  0.5f,  0.4f,  1.0f,  0.4f,
            0.5f, -0.5f,  0.5f,  0.4f,  1.0f,  0.4f,
            -0.5f, -0.5f,  0.5f,  0.4f,  1.0f,  0.4f,
            -0.5f, -0.5f, -0.5f,  0.4f,  1.0f,  0.4f,

            -0.5f,  0.5f, -0.5f,  0.4f,  1.0f,  0.4f,
            0.5f,  0.5f, -0.5f,  0.4f,  1.0f,  0.4f,
            0.5f,  0.5f,  0.5f,  0.4f,  1.0f,  0.4f,
            0.5f,  0.5f,  0.5f,  0.4f,  1.0f,  0.4f,
            -0.5f,  0.5f,  0.5f,  0.4f,  1.0f,  0.4f,
            -0.5f,  0.5f, -0.5f,  0.4f,  1.0f,  0.4f
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
    };


    class Renderer{
	public:
		static void Init();
		static void Shutdown();
		
		static void OnWindowResize(uint32_t width, uint32_t height);

		static void BeginScene(const glm::mat4& projection, const glm::mat4& view);
		static void EndScene();

        static void Flush();

		static void Submit(const glm::mat4& transform = glm::mat4(1.0f));

		static RendererAPI::API GetAPI() { return RendererAPI::GetAPI(); }
	private:
		struct SceneData{
			glm::mat4 ViewMatrix;
            glm::mat4 ProjectionMatrix;
		};
		static Scope<SceneData> s_SceneData;
        static Scope<RenderCube> s_RenderCube;
	};
}