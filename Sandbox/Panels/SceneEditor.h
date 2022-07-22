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
            ScenePanel();
            EntityPanel();
        }   

        void ScenePanel();
        void EntityPanel();
        void EntityNode(Entity entity);
        Entity& GetSelectedEntity() { return m_SelectedEntity; }
        
    private:
        void DrawComponents(Entity entity);
    private:
        Ref<Scene> m_ActiveScene;
        Entity m_SelectedEntity;
    };
}



