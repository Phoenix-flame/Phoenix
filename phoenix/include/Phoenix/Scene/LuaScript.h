#pragma once
#include <Phoenix/core/base.h>
#include <Phoenix/Scene/Entity.h>
#include <string>

struct lua_State;

namespace Phoenix{

    // A Lua script instance bound to one entity. Created when the scene starts
    // running; OnUpdate(dt) calls the script's global OnUpdate function. The script
    // can read/modify its entity via the bound globals (SetTranslation, SetColor, ...).
    class LuaScript{
    public:
        LuaScript(const std::string& source, Entity entity);
        ~LuaScript();

        LuaScript(const LuaScript&) = delete;
        LuaScript& operator=(const LuaScript&) = delete;

        void OnUpdate(float dt);
        // Calls the script's global OnAnimationEvent(name) if defined (fired by the Scene
        // when an animation event time is crossed).
        void OnAnimationEvent(const std::string& name);

        Entity GetEntity() const { return m_Entity; }
    private:
        Entity m_Entity;
        lua_State* m_L = nullptr;
        bool m_Valid = false;
    };
}
