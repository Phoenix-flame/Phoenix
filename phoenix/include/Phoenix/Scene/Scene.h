#pragma once

#include <Phoenix/core/base.h>
#include <Phoenix/core/timestep.h>
#include <entt/entt.hpp>
#include <Phoenix/renderer/Camera.h>


#include <Phoenix/renderer/shader.h>
#include <Phoenix/renderer/VertexArray.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <utility>

namespace Phoenix{
    // Scene-wide physics configuration (serialized with the scene, applied on Run).
    struct ScenePhysicsSettings{
        static constexpr int MaxLayers = 16; // = PhysicsWorld::MaxLayers
        glm::vec3 gravity = { 0.0f, -9.81f, 0.0f };
        int velocitySteps = 10;      // solver iterations (stiffer stacks/chains when higher)
        int positionSteps = 2;
        float substepRate = 60.0f;   // max collision step length = 1 / rate seconds
        // Collision layers: layerMasks[i] bit j = layer i collides with layer j
        // (kept symmetric by the editor). Every body is on one layer (default 0).
        std::string layerNames[MaxLayers];
        uint16_t layerMasks[MaxLayers];
        ScenePhysicsSettings(){
            for (int i = 0; i < MaxLayers; i++){
                layerNames[i] = i == 0 ? "Default" : "Layer " + std::to_string(i);
                layerMasks[i] = 0xffff;
            }
        }
        void SetLayersCollide(int a, int b, bool collide){
            if (collide) { layerMasks[a] |= (uint16_t)(1u << b); layerMasks[b] |= (uint16_t)(1u << a); }
            else { layerMasks[a] &= (uint16_t)~(1u << b); layerMasks[b] &= (uint16_t)~(1u << a); }
        }
        bool LayersCollide(int a, int b) const { return (layerMasks[a] >> b) & 1; }
    };

    // Editor-only physics visualisation toggles (not serialized).
    struct PhysicsDebugSettings{
        bool showColliders = false;
        bool showJoints = true;
        bool showForceFields = true;
        bool showCharacters = true;
        bool showContacts = false; // contact points + normals from the last step (playing)
    };

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
        // First entity with the given tag (empty if none).
        Entity FindEntityByTag(const std::string& tag);

        // Runtime physics body for an entity: its rigid body, terrain collider or
        // character capsule (InvalidBody if none / not running).
        uint32_t GetEntityBody(Entity entity);

        ScenePhysicsSettings& PhysicsSettings() { return m_PhysicsSettings; }
        const ScenePhysicsSettings& PhysicsSettings() const { return m_PhysicsSettings; }
        PhysicsDebugSettings& PhysicsDebug() { return m_PhysicsDebug; }

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
    
    private:
        glm::vec3 m_AmbientColor = glm::vec3(0.1f);
        float m_Time = 0.0f; // accumulated seconds (drives water animation)
        glm::mat4 m_ActiveView{ 1.0f }, m_ActiveProjection{ 1.0f };

        // Debug wireframes of colliders, joints, characters and force fields.
        void DrawPhysicsDebug();
        // Before/after the physics step: characters, force fields, contact dispatch.
        void UpdateCharactersPreStep(float dt);
        void UpdateCharactersPostStep(float dt);
        void ApplyForceFields();
        void DispatchPhysicsEvents();

        ScenePhysicsSettings m_PhysicsSettings;
        PhysicsDebugSettings m_PhysicsDebug;
        Scope<PhysicsWorld> m_PhysicsWorld;
        std::vector<Ref<LuaScript>> m_Scripts; // live Lua runtimes while playing
        // Runtime body id -> owning entity, for contact-event dispatch (rebuilt each run).
        std::unordered_map<uint32_t, entt::entity> m_BodyToEntity;
        // Bodies currently touching a water surface (splash edge detection).
        std::unordered_set<uint32_t> m_SubmergedBodies;
        // Runtime joint id -> owning entity (for OnJointBreak).
        std::unordered_map<uint32_t, entt::entity> m_JointToEntity;
        // Body pairs (min, max) currently touching through a sensor, so the matching
        // exit is reported as a trigger exit too.
        std::set<std::pair<uint32_t, uint32_t>> m_SensorPairs;
        // Contact points of the last step, for the contact debug overlay.
        std::vector<std::pair<glm::vec3, glm::vec3>> m_DebugContacts;
    };

}

