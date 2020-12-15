
#include <Phoenix/Scene/Scene.h>
#include <Phoenix/Scene/Entity.h>
#include <Phoenix/Scene/Component.h>
#include <Phoenix/renderer/renderer.h>
#include <Phoenix/core/log.h>
#include <Phoenix/core/Profiler.h>
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

    void Scene::OnUpdate(EditorCamera& editorCamera, Timestep ts){

        {
			m_Registry.view<NativeScriptComponent>().each([=](auto entity, auto& nsc)
			{
				if (!nsc.Instance)
				{
					nsc.Instance = nsc.InstantiateScript();
					nsc.Instance->m_Entity = Entity{ entity, this };
					nsc.Instance->OnCreate();
				}

				nsc.Instance->OnUpdate(ts);
			});
		}


        auto cameras = m_Registry.view<TransformComponent,CameraComponent>();
        glm::mat4 sceneCameraProjection = editorCamera.GetProjection();
        glm::mat4 sceneCameraView = editorCamera.GetView();
        glm::vec3 cameraPos = editorCamera.GetPosition();
        for (auto cam:cameras){
            auto camera = cameras.get<CameraComponent>(cam);
            auto transform = cameras.get<TransformComponent>(cam);
            if (camera.primary){
                sceneCameraProjection = camera.camera.GetProjection();
                sceneCameraView = glm::inverse(transform.GetTransform());
                cameraPos = transform.Translation;
                break;
            }
        }

        glm::vec3 lightPos = glm::vec3(0.0f);
        LightComponent lightComponent;
        auto lightsView = m_Registry.view<TransformComponent,LightComponent>();
        for(auto entity:lightsView){
            auto light = lightsView.get<LightComponent>(entity);
            auto transform = lightsView.get<TransformComponent>(entity);
            lightComponent = light;
            lightPos = transform.Translation;
        }


        {
            PHX_PROFILE("Scene Components");
            Renderer::BeginScene(sceneCameraProjection, sceneCameraView);
            auto view = m_Registry.view<CubeComponent, TransformComponent>();
            for (auto entity : view) {
                auto cube = view.get<CubeComponent>(entity);
                auto transform = view.get<TransformComponent>(entity);
                // Render
                {
                    Renderer::SubmitLightCube(cameraPos, lightPos, cube.ambient, cube.diffuse,
                        cube.specular, cube.shininess, lightComponent.ambient,
                         lightComponent.diffuse, lightComponent.specular, transform.GetTransform());
                    // Renderer::SubmitCube(transform.GetTransform());
                }
            }
            Renderer::EndScene();
        }
        

    }



    void Scene::OnResize(float width, float height){
        m_ViewportWidth = width;
		m_ViewportHeight = height;
        // Update camera component
        auto view = m_Registry.view<CameraComponent>();
        for (auto& entity:view){
            auto& cameraComponent = view.get<CameraComponent>(entity);
            cameraComponent.camera.SetViewportSize(m_ViewportWidth, m_ViewportHeight);
        }
    }

    template<typename T>
	void Scene::OnComponentAdded(Entity entity, T& component){
	}

    template<>
	void Scene::OnComponentAdded<TransformComponent>(Entity entity, TransformComponent& component){
	}

    template<>
	void Scene::OnComponentAdded<CubeComponent>(Entity entity, CubeComponent& component){
	}



    template<>
	void Scene::OnComponentAdded<TagComponent>(Entity entity, TagComponent& component){
	}

    template<>
	void Scene::OnComponentAdded<NativeScriptComponent>(Entity entity, NativeScriptComponent& component){
	}

    template<>
	void Scene::OnComponentAdded<LightComponent>(Entity entity, LightComponent& component){
	}



    template<>
	void Scene::OnComponentAdded<CameraComponent>(Entity entity, CameraComponent& component){
		component.camera.SetViewportSize(m_ViewportWidth, m_ViewportHeight);
	}
}