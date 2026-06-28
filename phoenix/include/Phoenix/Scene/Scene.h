#pragma once

#include <Phoenix/core/base.h>
#include <Phoenix/core/timestep.h>
#include <entt/entt.hpp>
#include <Phoenix/renderer/Camera.h>


#include <Phoenix/renderer/shader.h>
#include <Phoenix/renderer/VertexArray.h>

namespace Phoenix{
    class Entity;
    class SceneEditor;
    class PhysicsWorld;
    class Scene{
    public:
        Scene();
        ~Scene();

        void OnUpdate(EditorCamera& editorCamera, Timestep ts);
        Entity CreateEntity(const std::string& name);
        Entity CreatePointLightEntity(const std::string& name);
        Entity CreateDirLightEntity(const std::string& name);


        void DestroyEntity(Entity entity);
        void OnResize(float width, float height);
        int GetNumberOfPointLights() { return m_NumPointLights; }

        // Returns the nearest entity whose (unit-cube) bounds the ray hits, or an
        // empty entity if none. Used for click-to-select in the viewport.
        Entity PickEntity(const glm::vec3& rayOrigin, const glm::vec3& rayDirection);

        // Physics runtime: start creates Jolt bodies from RigidBody/BoxCollider
        // entities; while running, OnUpdate steps the simulation and writes the
        // resulting transforms back to the entities.
        void OnRuntimeStart();
        void OnRuntimeStop();
        bool IsRunning() const { return (bool)m_PhysicsWorld; }
    private:
		template<typename T>
		void OnComponentAdded(Entity entity, T& component);

    private:
        entt::registry m_Registry;
        float m_ViewportWidth = 0, m_ViewportHeight = 0;


        friend class Entity;
        friend class SceneEditor;
        friend class SceneSerializer;
    
    private:
        int m_NumPointLights = 0;
        const int MAX_NUM_POINT_LIGHTS = 4;

        Scope<PhysicsWorld> m_PhysicsWorld;
    };

}

