#pragma once
#include <Phoenix/core/base.h>
#include <Phoenix/core/assert.h>
#include <Phoenix/renderer/Camera.h>
#include <glm/gtc/matrix_transform.hpp>
#include <Phoenix/renderer/shader.h>
#include <Phoenix/renderer/VertexArray.h>
namespace Phoenix{

    struct TagComponent{
		std::string Tag;

		TagComponent() = default;
		TagComponent(const TagComponent&) = default;
		TagComponent(const std::string& tag) : Tag(tag) {}
	};

    struct CameraComponent{
        PerspectiveCamera camera;

        CameraComponent() = default;
        CameraComponent(const CameraComponent& other) = default;
    };


    struct CubeComponent{
        CubeComponent(){
            m_Shader = Shader::Create("/home/alireza/Programming/C++/MyGameEngineProject/Example/assets/shaders/basic.glsl");
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
            
        }


        Ref<VertexArray> m_Vertex_array;
        Ref<Shader> m_Shader;

        float vertices[218] = {
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


    struct TransformComponent{
        glm::vec3 Translation = {0.0f, 0.0f, 0.0f};
        glm::vec3 Rotation = {0.0f, 0.0f, 0.0f};
        glm::vec3 Scale = {1.0f, 1.0f, 1.0f};
        TransformComponent() = default;
		TransformComponent(const TransformComponent&) = default;
		TransformComponent(const glm::vec3& translation) : Translation(translation) {}

		glm::mat4 GetTransform() const{
			glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), Rotation.x, { 1, 0, 0 })
				* glm::rotate(glm::mat4(1.0f), Rotation.y, { 0, 1, 0 })
				* glm::rotate(glm::mat4(1.0f), Rotation.z, { 0, 0, 1 });

			return glm::translate(glm::mat4(1.0f), Translation)
				* rotation
				* glm::scale(glm::mat4(1.0f), Scale);
		}
    };
}