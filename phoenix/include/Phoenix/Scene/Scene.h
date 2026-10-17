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
#include <deque>
#include <variant>
#include <string>

namespace Phoenix{
    class Entity;
    class SceneEditor;
    class PhysicsWorld;
    class LuaScript;

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

        // Script console: print() output and script errors. Cleared when play starts.
        void LogScript(Entity entity, int script, const std::string& scriptName, bool error,
                       const std::string& text, int line = -1);
        const std::deque<ScriptLogEntry>& GetScriptLog() const { return m_ScriptLog; }
        void ClearScriptLog() { m_ScriptLog.clear(); }

        // Entity owning the given runtime physics body (empty if unknown).
        Entity FindEntityByBodyID(uint32_t bodyID);

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

        Scope<PhysicsWorld> m_PhysicsWorld;
        std::vector<Ref<LuaScript>> m_Scripts; // live Lua runtimes while playing
        std::deque<ScriptLogEntry> m_ScriptLog;
        std::unordered_map<std::string, ScriptValue> m_Shared;
        // Runtime body id -> owning entity, for contact-event dispatch (rebuilt each run).
        std::unordered_map<uint32_t, entt::entity> m_BodyToEntity;
        // Bodies currently touching a water surface (splash edge detection).
        std::unordered_set<uint32_t> m_SubmergedBodies;
    };

}

