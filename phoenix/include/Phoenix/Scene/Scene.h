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
        Scene() = default;
        ~Scene() = default;

        void OnUpdate(EditorCamera& editorCamera, Timestep ts);
        Entity CreateEntity(const std::string& name);
        Entity CreatePointLightEntity(const std::string& name);
        Entity CreateDirLightEntity(const std::string& name);


        void DestroyEntity(Entity entity);
        void OnResize(float width, float height);
        int GetNumberOfPointLights() { return m_NumPointLights; }
    private:
		template<typename T>
		void OnComponentAdded(Entity entity, T& component);

    private:
        entt::registry m_Registry;
        float m_ViewportWidth = 0, m_ViewportHeight = 0;


        friend class Entity;
        friend class SceneEditor;
    
    private:
        int m_NumPointLights = 0;
    };

}

