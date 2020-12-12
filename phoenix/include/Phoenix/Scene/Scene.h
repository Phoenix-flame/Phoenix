#pragma once

#include <Phoenix/core/base.h>
#include <Phoenix/core/timestep.h>
#include <entt/entity/registry.hpp>
#include <Phoenix/renderer/Camera.h>


#include <Phoenix/renderer/shader.h>
#include <Phoenix/renderer/VertexArray.h>

namespace Phoenix{
    class Entity;
    class SceneEditor;
    class Scene{
    public:
        Scene() {
            m_Shader = Shader::Create("/home/alireza/Programming/C++/MyGameEngineProject/Example/assets/shaders/basic.glsl");
            m_Vertex_array = CreateRef<VertexArray>();
            m_Vertex_array->Bind();
            std::vector<float> vertices;
            std::vector<uint32_t> indices;
            int count = 0;
            for (float i = -5 ; i <= 5 ; i+=0.5){
                vertices.push_back(i); indices.push_back(count++);
                vertices.push_back(0.0f);
                vertices.push_back(5.0f);
                vertices.push_back(0.0f);vertices.push_back(0.0f);vertices.push_back(0.0f);
                vertices.push_back(i);indices.push_back(count++);
                vertices.push_back(0.0f);
                vertices.push_back(-5.0f);
                vertices.push_back(0.0f);vertices.push_back(0.0f);vertices.push_back(0.0f);
            }
            for (float i = -5 ; i <= 5 ; i+=0.5){
                vertices.push_back(5.0f); indices.push_back(count++);
                vertices.push_back(0.0f);
                vertices.push_back(i);
                vertices.push_back(0.0f);vertices.push_back(0.0f);vertices.push_back(0.0f);
                vertices.push_back(-5.0f); indices.push_back(count++);
                vertices.push_back(0.0f);
                vertices.push_back(i);
                vertices.push_back(0.0f);vertices.push_back(0.0f);vertices.push_back(0.0f);
            }
            Ref<VertexBuffer> vertexBuffer = CreateRef<VertexBuffer>(vertices.data(), vertices.size()*sizeof(float));
            BufferLayout layout = {
                { ShaderDataType::Float3, "a_Position" },
                { ShaderDataType::Float3, "a_Color" }
            };
            vertexBuffer->SetLayout(layout);
            m_Vertex_array->AddVertexBuffer(vertexBuffer);
            Ref<IndexBuffer> indexBuffer = CreateRef<IndexBuffer>(indices.data(), indices.size());
            m_Vertex_array->SetIndexBuffer(indexBuffer);
        };
        ~Scene() = default;

        void OnUpdate(const glm::mat4& projection, Timestep ts);
        Entity CreateEntity(const std::string& name);
        void DestroyEntity(Entity entity);
        void OnResize(float width, float height);

    private:
		template<typename T>
		void OnComponentAdded(Entity entity, T& component);

    private:
        entt::registry m_Registry;
        float m_ViewportWidth = 0, m_ViewportHeight = 0;


        // Temporary
        Ref<VertexArray> m_Vertex_array;
        Ref<Shader> m_Shader;
        void DrawPlatform(const glm::mat4& projection);


        friend class Entity;
        friend class SceneEditor;
        
    };

}

