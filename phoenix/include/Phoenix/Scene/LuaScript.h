#pragma once
#include <Phoenix/core/base.h>
#include <Phoenix/Scene/Entity.h>
#include <string>
#include <unordered_set>

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

        // Keyboard queries for scripts. KeyDown = held this frame; KeyPressed = the
        // press EDGE (true only on the frame the key goes down). Edge state rolls over
        // in OnUpdate, so a script should query the same keys every frame.
        bool KeyDown(int keycode);
        bool KeyPressed(int keycode);
    private:
        Entity m_Entity;
        lua_State* m_L = nullptr;
        bool m_Valid = false;
        std::unordered_set<int> m_KeysPrev; // keys down at the start of last frame
        std::unordered_set<int> m_KeysCur;  // keys queried/down this frame
    };
}
