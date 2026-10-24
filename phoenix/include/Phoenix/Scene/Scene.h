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
    class Environment;

    // Scene-level simulation settings (serialized with the scene).
    struct SimulationSettings{
        float fixedTimestep = 1.0f / 60.0f; // seconds per physics step while playing
        int seed = 0;                       // episode randomization seed (0 = random each run)
        glm::vec3 gravity = { 0.0f, -9.81f, 0.0f };
    };

    // A rigid-body contact that began (entered) or ended while playing.
    struct ContactRecord{
        entt::entity a, b;
        bool entered;
        float time; // simulated seconds since Play
    };

    class Scene{
    public:
        Scene();
        ~Scene();

        // Simulate (when running) and render one frame.
        void OnUpdate(EditorCamera& editorCamera, Timestep ts, Entity selectedEntity);
        // The two halves of OnUpdate, for callers that drive the simulation themselves
        // (the Environment runs fixed physics steps, many per frame when fast-forwarding).
        // StepSimulation runs Lua OnUpdate, physics and contact callbacks; OnRender
        // advances animation/water and draws.
        void StepSimulation(float dt);
        void OnRender(EditorCamera& editorCamera, Timestep ts, Entity selectedEntity);
        Entity CreateEntity(const std::string& name);
        Entity CreatePointLightEntity(const std::string& name);
        Entity CreateDirLightEntity(const std::string& name);


        void DestroyEntity(Entity entity);
        void OnResize(float width, float height);
        // Counted from the registry so it can't drift from the actual components
        // (Add/Remove Component, undo, load all change it).
        int GetNumberOfPointLights();
        static constexpr int MAX_NUM_POINT_LIGHTS = 4; // NR_POINT_LIGHTS in lighting.glsl

        // Global ambient light (editable; serialized).
        glm::vec3& AmbientColor() { return m_AmbientColor; }
        const glm::vec3& AmbientColor() const { return m_AmbientColor; }

        // Returns the nearest entity whose bounds (of what it draws: mesh, terrain,
        // water, cube/shape, camera/light gizmo) the ray hits, or an empty entity if
        // none. Used for click-to-select in the viewport.
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

        // The running Lua script of an entity (null when not playing / no script).
        LuaScript* FindScript(entt::entity entity);

        // The RL environment driving this scene while playing (set by its owner).
        void SetEnvironment(Environment* environment) { m_Environment = environment; }
        Environment* GetEnvironment() { return m_Environment; }

        SimulationSettings& Simulation() { return m_Simulation; }

        // Contacts recorded since the last call (bounded to the most recent few thousand).
        std::vector<ContactRecord> DrainContacts() { std::vector<ContactRecord> out; out.swap(m_ContactLog); return out; }
        // Seconds simulated since play started.
        float GetSimulationTime() const { return m_SimTime; }

        // View/projection the last OnUpdate rendered with: the primary scene camera if
        // there is one, else the editor camera. Editor tools (gizmo, picking, sculpting)
        // use these so they line up with the image on screen.
        const glm::mat4& GetActiveView() const { return m_ActiveView; }
        const glm::mat4& GetActiveProjection() const { return m_ActiveProjection; }
    private:
		template<typename T>
		void OnComponentAdded(Entity entity, T& component);

    private:
        entt::registry m_Registry;
        float m_ViewportWidth = 0, m_ViewportHeight = 0;


        friend class Entity;
        friend class SceneEditor;
        friend class SceneSerializer;
        friend class Environment;
        friend class AIPanels;
        friend class Bridge;
    
    private:
        glm::vec3 m_AmbientColor = glm::vec3(0.1f);
        float m_Time = 0.0f; // accumulated seconds (drives water animation)
        float m_SimTime = 0.0f;
        SimulationSettings m_Simulation;
        std::vector<ContactRecord> m_ContactLog;
        Environment* m_Environment = nullptr;
        glm::mat4 m_ActiveView{ 1.0f }, m_ActiveProjection{ 1.0f };

        Scope<PhysicsWorld> m_PhysicsWorld;
        std::vector<Ref<LuaScript>> m_Scripts; // live Lua runtimes while playing
        // Runtime body id -> owning entity, for contact-event dispatch (rebuilt each run).
        std::unordered_map<uint32_t, entt::entity> m_BodyToEntity;
        // Bodies currently touching a water surface (splash edge detection).
        std::unordered_set<uint32_t> m_SubmergedBodies;
    };

}

