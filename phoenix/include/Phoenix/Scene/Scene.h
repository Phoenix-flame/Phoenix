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

        void OnUpdate(const glm::mat4& projection, Timestep ts);
        Entity CreateEntity(const std::string& name);
        void DestroyEntity(Entity entity);
        void OnResize(uint32_t width, uint32_t height);

    private:
        entt::registry m_Registry;
        uint32_t m_ViewportWidth = 0, m_ViewportHeight = 0;

        friend class Entity;
        friend class SceneEditor;
        
    };

}

