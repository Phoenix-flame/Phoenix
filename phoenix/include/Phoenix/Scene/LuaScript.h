#pragma once
#include <Phoenix/core/base.h>
#include <Phoenix/Scene/Entity.h>
#include <string>
#include <unordered_set>
#include <vector>

struct lua_State;
struct lua_Debug;

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
        // Calls the script's global OnCollisionEnter(otherTag) / OnCollisionExit(otherTag)
        // if defined (fired by the Scene when this entity's body starts/stops touching).
        void OnCollision(const std::string& otherTag, bool entered);

        // ---- RL hooks (called by the Environment) ----
        // OnEpisodeBegin() when an episode starts (after the reset/randomization).
        void OnEpisodeBegin();
        // OnAction(actions) for Custom-control agents: a 1-based table of numbers
        // (discrete: { index }; continuous: values in [-1, 1]).
        void OnAction(const std::vector<float>& action);
        // CollectObservations(), in which the script calls AddObservation(...).
        void CollectObservations();
        // Heuristic() for Custom agents under the Heuristic policy; returns false when
        // the script defines none. Its return values become the action.
        bool Heuristic(std::vector<float>& outAction);

        Entity GetEntity() const { return m_Entity; }

        // Keyboard queries for scripts. KeyDown = held this frame; KeyPressed = the
        // press EDGE (true only on the frame the key goes down). Edge state rolls over
        // in OnUpdate, so a script should query the same keys every frame.
        bool KeyDown(int keycode);
        bool KeyPressed(int keycode);
    private:
        // Pops the function + nargs arguments already pushed and calls it under the
        // instruction budget. On error logs it (prefixed with `what`) and disables the
        // script. Returns false on error.
        bool ProtectedCall(int nargs, const char* what, int nresults = 0);
        // Count hook: aborts a call that runs past the instruction budget, so an
        // infinite loop in a script can't freeze the editor.
        static void InstructionHook(lua_State* L, lua_Debug* ar);
    private:
        Entity m_Entity;
        lua_State* m_L = nullptr;
        bool m_Valid = false;
        int m_HookTicks = 0; // count-hook firings during the current call
        std::unordered_set<int> m_KeysPrev; // keys down at the start of last frame
        std::unordered_set<int> m_KeysCur;  // keys queried/down this frame
    };
}
