#pragma once

#include <Phoenix/core/base.h>
#include <Phoenix/core/timestep.h>
#include <entt/entity/registry.hpp>
#include <Phoenix/renderer/Camera.h>
// #include <Phoenix/Scene/Entity.h>

namespace Phoenix{
    class Entity;
    class SceneEditor;
    class Scene{
    public:
        Scene() = default;
        ~Scene() = default;

        void OnUpdate(PerspectiveCamera& cam, Timestep ts);
        Entity CreateEntity(const std::string& name);
        void DestroyEntity(Entity entity);

    private:
        entt::registry m_Registry;

        friend class Entity;
        friend class SceneEditor;
        
    };

}

