
#include <Phoenix/Scene/Scene.h>
#include <Phoenix/Scene/Entity.h>
#include <Phoenix/Scene/Component.h>
#include <Phoenix/renderer/renderer.h>
#include <Phoenix/core/log.h>

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

    void Scene::OnUpdate(const glm::mat4& projection, Timestep ts){
        auto view = m_Registry.view<CubeComponent, TransformComponent>();

        auto cameras = m_Registry.view<CameraComponent>();
        glm::mat4 sceneCameraProjection = projection;
        for (auto cam:cameras){
            auto camera = cameras.get<CameraComponent>(cam);
            if (camera.primaryCamera){
                sceneCameraProjection = camera.camera.GetViewProjectionMatrix();
                break;
            }
        }

        for (auto entity : view) {
            auto cube = view.get<CubeComponent>(entity);
            auto transform = view.get<TransformComponent>(entity);
            // Render
            {
                Renderer::Submit(cube.m_Shader, cube.m_Vertex_array, sceneCameraProjection, transform.GetTransform());
            }
        }

        auto origins = m_Registry.view<OriginComponent>();
        for (auto o:origins){
            auto origin = origins.get<OriginComponent>(o);
            origin.Draw(sceneCameraProjection);
        }
    }


    void Scene::OnResize(uint32_t width, uint32_t height){
        m_ViewportWidth = width;
		m_ViewportHeight = height;
        // Update camera component
        auto cameras = m_Registry.view<CameraComponent>();
        for (auto cam:cameras){
            auto camera = cameras.get<CameraComponent>(cam);
            PHX_CORE_WARN("Here");
            camera.camera.SetViewportSize(m_ViewportWidth, m_ViewportHeight);
        }
    }
}