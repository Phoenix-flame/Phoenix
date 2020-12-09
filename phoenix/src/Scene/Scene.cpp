
#include <Phoenix/Scene/Scene.h>
#include <Phoenix/Scene/Entity.h>
#include <Phoenix/Scene/Component.h>
#include <Phoenix/renderer/renderer.h>


namespace Phoenix{
    Entity Scene::CreateEntity(const std::string& name){
        Entity entity = { m_Registry.create(), this };
        entity.AddComponent<TransformComponent>();
        auto& tag = entity.AddComponent<TagComponent>();
		tag.Tag = name.empty() ? "Entity" : name;
        return entity;
    }

    void Scene::DestroyEntity(Entity entity){
        m_Registry.destroy(entity);
    }

    void Scene::OnUpdate(PerspectiveCamera& cam, Timestep ts){
        auto view = m_Registry.view<CubeComponent, TransformComponent>();
        for (auto entity : view) {
            auto cube = view.get<CubeComponent>(entity);
            auto transform = view.get<TransformComponent>(entity);
            // Render
            {
                Renderer::Submit(cube.m_Shader, cube.m_Vertex_array, cam.GetViewProjectionMatrix(), transform.GetTransform());
            }
        }
        // auto view = m_Registry.view<TransformComponent>();
        // for (auto entity : view){
        //     auto transform = view.get<TransformComponent>(entity);
        // }
    }
}