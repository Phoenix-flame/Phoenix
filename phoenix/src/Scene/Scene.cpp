
#include <Phoenix/Scene/Scene.h>
#include <Phoenix/Scene/Entity.h>
#include <Phoenix/Scene/Component.h>
#include <Phoenix/renderer/renderer.h>
#include <Phoenix/core/log.h>
#include <Phoenix/core/Profiler.h>
#include <Phoenix/Physics/Physics.h>
namespace Phoenix{
    Scene::~Scene() = default;

    void Scene::OnRuntimeStart(){
        m_PhysicsWorld = CreateScope<PhysicsWorld>();

        auto view = m_Registry.view<RigidBodyComponent, BoxColliderComponent, TransformComponent>();
        for (auto entity : view){
            auto& rb = view.get<RigidBodyComponent>(entity);
            auto& collider = view.get<BoxColliderComponent>(entity);
            auto& transform = view.get<TransformComponent>(entity);

            glm::vec3 halfExtents = collider.halfExtents * transform.Scale;
            PhysicsWorld::BodyType type = (PhysicsWorld::BodyType)(int)rb.type;
            rb.runtimeBodyID = m_PhysicsWorld->CreateBox(transform.Translation, transform.Rotation, halfExtents, type);
        }
        m_PhysicsWorld->OptimizeBroadPhase();
    }

    void Scene::OnRuntimeStop(){
        auto view = m_Registry.view<RigidBodyComponent>();
        for (auto entity : view){
            view.get<RigidBodyComponent>(entity).runtimeBodyID = 0xffffffff;
        }
        m_PhysicsWorld.reset();
    }

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

        // Physics: step the simulation and copy body transforms back to entities.
        if (m_PhysicsWorld){
            PHX_PROFILE("Physics Step");
            m_PhysicsWorld->Step((float)ts);
            auto view = m_Registry.view<RigidBodyComponent, TransformComponent>();
            for (auto entity : view){
                auto& rb = view.get<RigidBodyComponent>(entity);
                if (rb.runtimeBodyID == 0xffffffff) { continue; }
                auto& transform = view.get<TransformComponent>(entity);
                glm::vec3 position, rotation;
                m_PhysicsWorld->GetBodyTransform(rb.runtimeBodyID, position, rotation);
                transform.Translation = position;
                transform.Rotation = rotation;
            }
        }

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
            Renderer::BeginScene(sceneCameraProjection, sceneCameraView, cameraPos);
            Renderer::SetLights(dirLightExists, lightComponent, lightPos, pLightComponent, pointLightPos, numPointLight);
            auto view = m_Registry.view<CubeComponent, TransformComponent>();
            for (auto entity : view) {
                auto cube = view.get<CubeComponent>(entity);
                auto transform = view.get<TransformComponent>(entity);
                // Render
                {
                    Renderer::SubmitCube(cube.material, transform.GetTransform());
                }
            }

            auto meshView = m_Registry.view<MeshComponent, TransformComponent>();
            for (auto entity : meshView) {
                auto& mesh = meshView.get<MeshComponent>(entity);
                auto transform = meshView.get<TransformComponent>(entity);
                if (!mesh.model) { continue; }
                mesh.model->Update(); // completes async GPU upload once parsing finishes
                for (const auto& subMesh : mesh.model->GetMeshes()) {
                    Renderer::Submit(subMesh->GetVertexArray(), mesh.material, transform.GetTransform(), subMesh->GetDiffuseMap());
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
	void Scene::OnComponentAdded<MeshComponent>(Entity entity, MeshComponent& component){
	}

    template<>
	void Scene::OnComponentAdded<RigidBodyComponent>(Entity entity, RigidBodyComponent& component){
	}

    template<>
	void Scene::OnComponentAdded<BoxColliderComponent>(Entity entity, BoxColliderComponent& component){
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