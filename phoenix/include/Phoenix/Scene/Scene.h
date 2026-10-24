#pragma once

#include <Phoenix/core/base.h>
#include <Phoenix/core/timestep.h>
#include <entt/entt.hpp>
#include <Phoenix/renderer/Camera.h>
#include <Phoenix/Scene/Sky.h>


#include <Phoenix/renderer/shader.h>
#include <Phoenix/renderer/VertexArray.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <variant>
#include <string>
#include <set>
#include <utility>

namespace Phoenix{
    // Scene-wide physics configuration (serialized with the scene, applied on Run).
    struct ScenePhysicsSettings{
        static constexpr int MaxLayers = 16; // = PhysicsWorld::MaxLayers
        // Gravity and the fixed step rate live in SimulationSettings (shared with the
        // RL environment and the Python API).
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
    class Animator;
    struct RagdollComponent;
    class PhysicsWorld;
    class LuaScript;
    class Environment;

    // One line of the Lua script console (print output or an error).
    struct ScriptLogEntry{
        entt::entity entity = entt::null;
        std::string entityName;  // tag when logged (the entity may be gone later)
        int script = 0;          // index in the entity's LuaScriptComponent
        std::string scriptName;
        bool error = false;
        int line = -1;           // source line of an error, -1 if unknown
        int count = 1;           // identical consecutive messages are collapsed
        float time = 0.0f;       // scene time of the latest occurrence
        std::string text;
    };

    // A value scripts share through the scene (SetShared / GetShared in Lua).
    using ScriptValue = std::variant<double, bool, std::string>;
    // Named values handed to a spawned entity's scripts (the SPAWN table in Lua).
    using ScriptArgs = std::unordered_map<std::string, ScriptValue>;

    // A line of on-screen text set by scripts while playing (ShowText in Lua).
    struct HudText{
        std::string text;
        glm::vec3 color{ 1.0f };
        int order = 0; // first-shown first
    };

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
        static constexpr int MAX_NUM_POINT_LIGHTS = 8; // NR_POINT_LIGHTS in lighting.glsl

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

        // Advance the running physics by one frame of ts seconds: fixed steps of
        // SimulationSettings::fixedTimestep (vehicles, ragdolls, characters, water,
        // force fields and contact callbacks), then interpolated transforms. For tools
        // and tests that run physics without the Environment / rendering.
        void UpdatePhysics(float ts);

        // The live physics world while playing (null in edit mode). Used by Lua
        // bindings for impulses/velocities/raycasts.
        PhysicsWorld* GetPhysicsWorld() { return m_PhysicsWorld.get(); }

        // Entities that have all of the given components (a live registry view).
        template<typename... Components>
        auto GetAllEntitiesWith() { return m_Registry.view<Components...>(); }

        // Live Lua runtime of an entity's script `index` while playing, or null.
        LuaScript* FindScript(Entity entity, int index);
        // While playing, rebuild the entity's Lua runtimes from its LuaScriptComponent
        // (hot reload after editing, adding, removing or toggling scripts).
        void ReloadScripts(Entity entity);

        // Values scripts share with each other while playing (cleared when play starts).
        void SetShared(const std::string& key, ScriptValue value) { m_Shared[key] = std::move(value); }
        void ClearShared(const std::string& key) { m_Shared.erase(key); }
        const ScriptValue* GetShared(const std::string& key) const{
            auto it = m_Shared.find(key);
            return it != m_Shared.end() ? &it->second : nullptr;
        }

        // Whether the entity still exists in this scene.
        bool IsValid(Entity entity) const;
        // First entity with this tag (empty if none).
        Entity FindEntityByTag(const std::string& tag);

        // While playing: a copy of the entity tagged `templateTag` (its drawable,
        // physics and script components) at `position`, with a physics body if it
        // has a rigid body. Its scripts start at once and see `args` as the global
        // SPAWN table (nil in the template itself, so templates can stay inert).
        // An "yaw" arg sets the copy's Y rotation. Returns an empty entity on failure.
        Entity Spawn(const std::string& templateTag, const glm::vec3& position, const ScriptArgs& args = {});
        // Destroys the entity at the end of this frame's script/physics update (safe
        // to call from scripts and collision callbacks). Its scripts stop at once.
        void QueueDestroy(Entity entity);
        bool IsPendingDestroy(entt::entity entity) const { return m_PendingDestroy.count(entity) > 0; }
        // Calls OnMessage(name, args..., senderId) on every script of `target`.
        void SendScriptMessage(Entity target, const std::string& name, const std::vector<ScriptValue>& args, Entity sender);

        // On-screen text scripts show while playing (cleared when play starts/stops).
        void ShowHudText(const std::string& key, const std::string& text, const glm::vec3& color);
        void HideHudText(const std::string& key) { m_Hud.erase(key); }
        const std::unordered_map<std::string, HudText>& GetHudText() const { return m_Hud; }
        // A large centred message, shown for `seconds` (scene time).
        void ShowBanner(const std::string& text, float seconds) { m_Banner = text; m_BannerUntil = m_Time + seconds; }
        const std::string& GetBanner() const { return m_Banner; }
        float GetBannerTimeLeft() const { return m_BannerUntil - m_Time; }

        // Script console: print() output and script errors. Cleared when play starts.
        void LogScript(Entity entity, int script, const std::string& scriptName, bool error,
                       const std::string& text, int line = -1);
        const std::deque<ScriptLogEntry>& GetScriptLog() const { return m_ScriptLog; }
        void ClearScriptLog() { m_ScriptLog.clear(); }

        // Entity owning the given runtime physics body (empty if unknown).
        Entity FindEntityByBodyID(uint32_t bodyID);
        // While playing: replace a terrain's collider after its heights changed
        // (rebuilds the mesh too).
        void RebuildTerrainBody(entt::entity entity);
        // Particle fluids back to their starting blocks, pool spray cleared (Play start
        // and every RL episode).
        void ResetFluids();
        // Fires back to their starting state (lit or not): Play start and every RL episode.
        void ResetFires();

        // Runtime physics body for an entity: its rigid body, terrain collider or
        // character capsule (InvalidBody if none / not running).
        uint32_t GetEntityBody(Entity entity);

        ScenePhysicsSettings& PhysicsSettings() { return m_PhysicsSettings; }
        const ScenePhysicsSettings& PhysicsSettings() const { return m_PhysicsSettings; }
        PhysicsDebugSettings& PhysicsDebug() { return m_PhysicsDebug; }

        // The running Lua scripts of an entity, in component order (empty when not
        // playing / no scripts). Disabled or broken scripts are not included.
        std::vector<LuaScript*> FindScripts(entt::entity entity);

        // The RL environment driving this scene while playing (set by its owner).
        void SetEnvironment(Environment* environment) { m_Environment = environment; }
        Environment* GetEnvironment() { return m_Environment; }

        SimulationSettings& Simulation() { return m_Simulation; }

        // Background / sky drawn behind the scene and mirrored by reflections.
        SkySettings& Sky() { return m_Sky; }
        const SkySettings& Sky() const { return m_Sky; }

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

        // Builds the runtime physics body for one rigid-body entity (play mode).
        void CreateRuntimeBody(entt::entity entity);
        // Builds the static triangle-mesh collider of one terrain (play mode).
        void CreateTerrainBody(entt::entity entity);
        // Builds a pool's basin collider and resets its water (play mode).
        void CreatePoolBody(entt::entity entity);
        // One physics step of every pool: body displacement, waves, buoyancy, spray.
        void UpdatePools(float dt);
        // One physics step of every particle fluid (and pool spray), with the hand-off
        // of particles into pools.
        void UpdateFluids(float dt);
        // Fires, once per rendered frame (edit and play): put out, lit, stepped at 60 Hz.
        void UpdateFires(float dt, const glm::mat4& viewProjection);
        // Starts the entity's enabled scripts; `args` becomes their SPAWN table.
        void StartScripts(Entity entity, const ScriptArgs* args);
        // Destroys everything QueueDestroy collected.
        void FlushPendingDestroy();
        // Moves BoneAttachment entities onto their target bones (after animation).
        void UpdateBoneAttachments();

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
        SkySettings m_Sky;
        float m_Time = 0.0f; // accumulated seconds (drives water animation)
        float m_SimTime = 0.0f;
        SimulationSettings m_Simulation;
        std::vector<ContactRecord> m_ContactLog;
        Environment* m_Environment = nullptr;
        glm::mat4 m_ActiveView{ 1.0f }, m_ActiveProjection{ 1.0f };

        // Debug wireframes of colliders, joints, characters and force fields.
        void DrawPhysicsDebug();
        // Before/after the physics step: characters, force fields, contact dispatch.
        void RunPhysics(int steps, float h, float alpha);
        void SyncExternalEdits();
        void ApplyWaterCoupling(float dt);
        void UpdateCharactersPreStep(float dt);
        void UpdateCharactersPostStep(float dt);
        void ApplyForceFields();
        void DispatchPhysicsEvents();
        // Vehicles (SceneVehicle.cpp).
        void CreateVehicles();
        void UpdateVehiclesPreStep();
        void UpdateVehiclesPostStep();
        // Ragdolls (SceneRagdoll.cpp).
        void ProcessRagdollRequests();
        bool ActivateRagdoll(Entity entity, RagdollComponent& ragdoll, Animator& animator);
        void DeactivateRagdoll(Entity entity, RagdollComponent& ragdoll);
        void PoseRagdoll(Entity entity, RagdollComponent& ragdoll, Animator& animator);
        void UpdateRagdollFollow();

        ScenePhysicsSettings m_PhysicsSettings;
        PhysicsDebugSettings m_PhysicsDebug;
        Scope<PhysicsWorld> m_PhysicsWorld;
        std::vector<Ref<LuaScript>> m_Scripts; // live Lua runtimes while playing
        std::deque<ScriptLogEntry> m_ScriptLog;
        std::unordered_map<std::string, ScriptValue> m_Shared;
        std::unordered_set<entt::entity> m_PendingDestroy;
        int m_MessageDepth = 0; // guards against scripts messaging each other forever
        std::unordered_map<std::string, HudText> m_Hud;
        int m_HudOrder = 0;
        std::string m_Banner;
        float m_BannerUntil = 0.0f;
        // Runtime body id -> owning entity, for contact-event dispatch (rebuilt each run).
        std::unordered_map<uint32_t, entt::entity> m_BodyToEntity;
        // Bodies currently touching a water surface (splash edge detection).
        std::unordered_set<uint32_t> m_SubmergedBodies;
        // Runtime joint id -> owning entity and index in its JointComponent (OnJointBreak).
        std::unordered_map<uint32_t, std::pair<entt::entity, int>> m_JointToEntity;
        float m_PhysicsAccumulator = 0.0f; // fixed-step leftover time
        // Body pairs (min, max) currently touching through a sensor, so the matching
        // exit is reported as a trigger exit too.
        std::set<std::pair<uint32_t, uint32_t>> m_SensorPairs;
        // Contact points of the last step, for the contact debug overlay.
        std::vector<std::pair<glm::vec3, glm::vec3>> m_DebugContacts;
    };

}

