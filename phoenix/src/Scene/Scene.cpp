
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

    Entity Scene::CreatePointLightEntity(const std::string& name)
    {
        if (m_NumPointLights == MAX_NUM_POINT_LIGHTS)
        {
            throw std::runtime_error("Reached maximum number of point lights");
        }
        Entity entity = { m_Registry.create(), this };
        entity.AddComponent<TransformComponent>();
        entity.GetComponent<TransformComponent>().Scale.x = 0.1;
        entity.GetComponent<TransformComponent>().Scale.y = 0.1;
        entity.GetComponent<TransformComponent>().Scale.z = 0.1;
        entity.AddComponent<CubeComponent>();
        entity.GetComponent<CubeComponent>().material.ambient = glm::vec3(1.0f, 1.0f, 1.0f);
        entity.GetComponent<CubeComponent>().material.diffuse = glm::vec3(1.0f, 1.0f, 1.0f);

        entity.AddComponent<PointLightComponent>();
        auto& tag = entity.AddComponent<TagComponent>();
		tag.Tag = name.empty() ? "Entity" : name;
        m_NumPointLights ++;
        return entity;
    }

    Entity Scene::CreateDirLightEntity(const std::string& name)
    {
        Entity entity = { m_Registry.create(), this };
        entity.AddComponent<TransformComponent>();
        entity.AddComponent<CubeComponent>();
        entity.GetComponent<TransformComponent>().Scale.x = 0.2;
        entity.GetComponent<TransformComponent>().Scale.y = 0.2;
        entity.GetComponent<TransformComponent>().Scale.z = 0.2;
        entity.AddComponent<DirLightComponent>();
        auto& tag = entity.AddComponent<TagComponent>();
		tag.Tag = name.empty() ? "Entity" : name;
        return entity;
    }

    void Scene::DestroyEntity(Entity entity){
        if (entity.HasComponent<PointLightComponent>())
        {
            m_NumPointLights --;
        }
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
        bool anyActiveCamera = false;
        for (auto cam:cameras){
            auto camera = cameras.get<CameraComponent>(cam);
            auto transform = cameras.get<TransformComponent>(cam);
            if (camera.primary){
                editorCamera.SetState(false);
                anyActiveCamera = true;
                sceneCameraProjection = camera.camera.GetProjection();
                sceneCameraView = glm::inverse(transform.GetTransform());
                cameraPos = transform.Translation;
                break;
            }
        }
        if (!anyActiveCamera)
        {
            editorCamera.SetState(true);
        }
        glm::vec3 lightPos = glm::vec3(0.0f);
        DirLightComponent lightComponent;
        bool dirLightExists = false;
        auto lightsView = m_Registry.view<TransformComponent,DirLightComponent>();
        for(auto entity:lightsView){
            auto light = lightsView.get<DirLightComponent>(entity);
            if (!light.isActive) { continue; }
            auto transform = lightsView.get<TransformComponent>(entity);
            lightComponent = light;
            lightPos = transform.Translation;
            dirLightExists = true;
        }

        glm::vec3 pointLightPos[4] = {glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(0.0f)};
        PointLightComponent pLightComponent[4];
        int numPointLight = 0;
        auto pLightsView = m_Registry.view<TransformComponent,PointLightComponent>();
        for(auto entity:pLightsView){
            auto light = pLightsView.get<PointLightComponent>(entity);
            if (!light.isActive) { continue; }
            auto transform = pLightsView.get<TransformComponent>(entity);
            pLightComponent[numPointLight] = light;
            pointLightPos[numPointLight] = transform.Translation;
            numPointLight ++;
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
                    Renderer::SubmitLightCube(cameraPos, cube.material, dirLightExists, lightComponent, lightPos, pLightComponent, pointLightPos, numPointLight, transform.GetTransform());
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
	void Scene::OnComponentAdded<DirLightComponent>(Entity entity, DirLightComponent& component){
	}

    template<>
	void Scene::OnComponentAdded<PointLightComponent>(Entity entity, PointLightComponent& component){
	}



    template<>
	void Scene::OnComponentAdded<CameraComponent>(Entity entity, CameraComponent& component){
		component.camera.SetViewportSize(m_ViewportWidth, m_ViewportHeight);
	}
}