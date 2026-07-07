#pragma once

#include <Phoenix/core/base.h>
#include <Phoenix/core/timestep.h>
#include <entt/entt.hpp>
#include <Phoenix/renderer/Camera.h>


#include <Phoenix/renderer/shader.h>
#include <Phoenix/renderer/VertexArray.h>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace Phoenix{
    class Entity;
    class SceneEditor;
    class PhysicsWorld;
    class LuaScript;
    class Scene{
    public:
        Scene();
        ~Scene();

        void OnUpdate(EditorCamera& editorCamera, Timestep ts, Entity selectedEntity);
        Entity CreateEntity(const std::string& name);
        Entity CreatePointLightEntity(const std::string& name);
        Entity CreateDirLightEntity(const std::string& name);


        void DestroyEntity(Entity entity);
        void OnResize(float width, float height);
        int GetNumberOfPointLights() { return m_NumPointLights; }

        // Global ambient light (editable; serialized).
        glm::vec3& AmbientColor() { return m_AmbientColor; }
        const glm::vec3& AmbientColor() const { return m_AmbientColor; }

        // Returns the nearest entity whose (unit-cube) bounds the ray hits, or an
        // empty entity if none. Used for click-to-select in the viewport.
        Entity PickEntity(const glm::vec3& rayOrigin, const glm::vec3& rayDirection);

        // Physics runtime: start creates Jolt bodies from RigidBody + collider
        // entities (and joints between them); while running, OnUpdate steps the
        // simulation and writes the resulting transforms back to the entities.
        void OnRuntimeStart();
        void OnRuntimeStop();
        bool IsRunning() const { return (bool)m_PhysicsWorld; }

        // The live physics world while playing (null in edit mode). Used by Lua
        // bindings for impulses/velocities/raycasts.
        PhysicsWorld* GetPhysicsWorld() { return m_PhysicsWorld.get(); }

        // Entity owning the given runtime physics body (empty if unknown).
        Entity FindEntityByBodyID(uint32_t bodyID);
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
        glm::vec3 m_AmbientColor = glm::vec3(0.1f);
        float m_Time = 0.0f; // accumulated seconds (drives water animation)

        Scope<PhysicsWorld> m_PhysicsWorld;
        std::vector<Ref<LuaScript>> m_Scripts; // live Lua runtimes while playing
        // Runtime body id -> owning entity, for contact-event dispatch (rebuilt each run).
        std::unordered_map<uint32_t, entt::entity> m_BodyToEntity;
        // Bodies currently touching a water surface (splash edge detection).
        std::unordered_set<uint32_t> m_SubmergedBodies;
    };

}

