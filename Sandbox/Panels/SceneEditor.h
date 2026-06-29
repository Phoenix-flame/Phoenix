#pragma once
#include <Phoenix/core/base.h>
#include <Phoenix/Scene/Scene.h>
#include <Phoenix/Scene/Entity.h>
#include <glm/glm.hpp>

namespace Phoenix{
    class SceneEditor{
    public:
        SceneEditor(Ref<Scene> scene): m_ActiveScene(scene) {}
        ~SceneEditor() = default;

        void OnImGuiRender(){
            // Drop the selection if its entity no longer exists (e.g. after an
            // undo that removed it) so nothing references a stale handle.
            if (m_SelectedEntity && !m_ActiveScene->m_Registry.valid((entt::entity)m_SelectedEntity))
                m_SelectedEntity = {};
            ScenePanel();
            EntityPanel();
            ScriptEditorPanel();
        }

        void ScenePanel();
        void EntityPanel();
        void ScriptEditorPanel();
        void EntityNode(Entity entity);
        Entity& GetSelectedEntity() { return m_SelectedEntity; }
        void SetSelectedEntity(Entity entity) { m_SelectedEntity = entity; }

    private:
        void DrawComponents(Entity entity);
    private:
        Ref<Scene> m_ActiveScene;
        Entity m_SelectedEntity;
        bool m_ShowScriptEditor = true;
    };
}



