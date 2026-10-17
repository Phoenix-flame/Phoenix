#pragma once
#include <Phoenix/core/base.h>
#include <Phoenix/Scene/Entity.h>
#include <string>
#include <vector>
#include <unordered_set>

struct lua_State;
struct lua_Debug;

namespace Phoenix{

    // Documentation for one name scripts can use (a bound function, a callback the
    // engine calls, or a Key.* constant). Drives the editor's completion, hover help
    // and API reference; built from the same table that registers the bindings.
    struct LuaApiDoc{
        std::string name;      // "SetTranslation", "OnUpdate", "Key.Space"
        std::string signature; // "SetTranslation(x, y, z)"
        std::string doc;
        std::string category;  // "Transform", "Physics", "Callbacks", "Keys", ...
    };

    // A Lua script instance bound to one entity. Created when the scene starts
    // running, one per enabled script on the entity; OnUpdate(dt) calls the script's
    // global OnUpdate function. The script can read/modify its entity via the bound
    // globals (SetTranslation, SetColor, ...).
    class LuaScript{
    public:
        // `index` is the script's position in the entity's LuaScriptComponent; `name`
        // is used as the chunk name, so errors read "Name:12: message".
        LuaScript(const std::string& source, Entity entity, int index = 0, const std::string& name = "Script");
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

        Entity GetEntity() const { return m_Entity; }
        int GetIndex() const { return m_Index; }
        const std::string& GetName() const { return m_Name; }

        // False once the script failed to load or raised an error (it stops running).
        bool IsValid() const { return m_Valid; }
        // The error that stopped the script ("" if none) and its line (-1 if unknown).
        const std::string& GetError() const { return m_Error; }
        int GetErrorLine() const { return m_ErrorLine; }

        // Keyboard queries for scripts. KeyDown = held this frame; KeyPressed = the
        // press EDGE (true only on the frame the key goes down). Edge state rolls over
        // in OnUpdate, so a script should query the same keys every frame.
        bool KeyDown(int keycode);
        bool KeyPressed(int keycode);

        // Compiles `source` without running it. On a syntax error returns false and
        // fills the message (without the "name:line:" prefix) and its line.
        static bool CheckSyntax(const std::string& source, std::string& outError, int& outLine);

        // Everything scripts can call or define, for editor help.
        static const std::vector<LuaApiDoc>& GetApiDocs();

        // Splits a Lua error "chunk:12: message" into its line and message.
        static int ParseErrorLine(const std::string& error, std::string* outMessage = nullptr);
    private:
        // Pops the function + nargs arguments already pushed and calls it under the
        // instruction budget. On error logs it (prefixed with `what`) and disables the
        // script. Returns false on error.
        bool ProtectedCall(int nargs, const char* what);
        // Records the error, logs it to the scene's script console and stops the script.
        void Fail(const char* what, const std::string& error);
        // Count hook: aborts a call that runs past the instruction budget, so an
        // infinite loop in a script can't freeze the editor.
        static void InstructionHook(lua_State* L, lua_Debug* ar);
    private:
        Entity m_Entity;
        int m_Index = 0;
        std::string m_Name;
        lua_State* m_L = nullptr;
        bool m_Valid = false;
        std::string m_Error;
        int m_ErrorLine = -1;
        int m_HookTicks = 0; // count-hook firings during the current call
        std::unordered_set<int> m_KeysPrev; // keys down at the start of last frame
        std::unordered_set<int> m_KeysCur;  // keys queried/down this frame
    };
}
