#include <Phoenix/Scene/LuaScript.h>
#include <Phoenix/Scene/Component.h>
#include <Phoenix/core/log.h>
#include <Phoenix/core/Input.h>
#include <Phoenix/core/keyCodes.h>
#include <algorithm>
#include <cmath>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

namespace Phoenix{

    // The bound LuaScript / entity is stashed in the lua_State's extra space so the C
    // functions below can reach them without globals.
    static LuaScript* ScriptSelf(lua_State* L){
        return *static_cast<LuaScript**>(lua_getextraspace(L));
    }
    static Entity ScriptEntity(lua_State* L){
        return ScriptSelf(L)->GetEntity();
    }

    static int l_GetTranslation(lua_State* L){
        Entity e = ScriptEntity(L);
        glm::vec3 t(0.0f);
        if (e.HasComponent<TransformComponent>()) { t = e.GetComponent<TransformComponent>().Translation; }
        lua_pushnumber(L, t.x); lua_pushnumber(L, t.y); lua_pushnumber(L, t.z);
        return 3;
    }

    static int l_SetTranslation(lua_State* L){
        Entity e = ScriptEntity(L);
        if (e.HasComponent<TransformComponent>()){
            auto& t = e.GetComponent<TransformComponent>();
            t.Translation = { (float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3) };
        }
        return 0;
    }

    static int l_GetRotation(lua_State* L){
        Entity e = ScriptEntity(L);
        glm::vec3 r(0.0f);
        if (e.HasComponent<TransformComponent>()) { r = e.GetComponent<TransformComponent>().Rotation; }
        lua_pushnumber(L, r.x); lua_pushnumber(L, r.y); lua_pushnumber(L, r.z);
        return 3;
    }

    static int l_SetRotation(lua_State* L){
        Entity e = ScriptEntity(L);
        if (e.HasComponent<TransformComponent>()){
            auto& t = e.GetComponent<TransformComponent>();
            t.Rotation = { (float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3) };
        }
        return 0;
    }

    static int l_SetScale(lua_State* L){
        Entity e = ScriptEntity(L);
        if (e.HasComponent<TransformComponent>()){
            auto& t = e.GetComponent<TransformComponent>();
            t.Scale = { (float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3) };
        }
        return 0;
    }

    static int l_SetColor(lua_State* L){
        Entity e = ScriptEntity(L);
        glm::vec3 c((float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3));
        if (e.HasComponent<CubeComponent>()) { e.GetComponent<CubeComponent>().material.diffuse = c; }
        if (e.HasComponent<MeshComponent>()) { e.GetComponent<MeshComponent>().material.diffuse = c; }
        return 0;
    }

    static int l_SetEmissive(lua_State* L){
        Entity e = ScriptEntity(L);
        glm::vec3 c((float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3));
        float strength = (lua_gettop(L) >= 4) ? (float)luaL_checknumber(L, 4) : 1.0f;
        if (e.HasComponent<CubeComponent>()){ auto& m = e.GetComponent<CubeComponent>().material; m.emissive = c; m.emissiveStrength = strength; }
        if (e.HasComponent<MeshComponent>()){ auto& m = e.GetComponent<MeshComponent>().material; m.emissive = c; m.emissiveStrength = strength; }
        return 0;
    }

    // ---- Animation control ----
    // These only WRITE the AnimationComponent's config/request fields; the Scene's
    // animation loop is the single place that drives the live Animator.

    static AnimationComponent* ScriptAnim(lua_State* L){
        Entity e = ScriptEntity(L);
        return e.HasComponent<AnimationComponent>() ? &e.GetComponent<AnimationComponent>() : nullptr;
    }

    // Resolve a clip argument that is either a number (index) or a string (clip name).
    static int ResolveClip(lua_State* L, int argIndex){
        if (lua_isnumber(L, argIndex)) { return (int)lua_tointeger(L, argIndex); }
        Entity e = ScriptEntity(L);
        if (lua_isstring(L, argIndex) && e.HasComponent<MeshComponent>()){
            auto& m = e.GetComponent<MeshComponent>();
            if (m.model) { return m.model->GetAnimationIndex(lua_tostring(L, argIndex)); }
        }
        return -1;
    }

    static int l_PlayAnimation(lua_State* L){
        if (auto* a = ScriptAnim(L)){ int c = ResolveClip(L, 1); if (c >= 0) { a->clip = c; } a->playing = true; }
        return 0;
    }
    static int l_CrossFade(lua_State* L){
        if (auto* a = ScriptAnim(L)){
            int c = ResolveClip(L, 1);
            if (lua_gettop(L) >= 2) { a->crossfade = (float)luaL_checknumber(L, 2); }
            if (c >= 0) { a->pendingCrossfade = c; }
            a->playing = true;
        }
        return 0;
    }
    static int l_PauseAnimation(lua_State* L){ if (auto* a = ScriptAnim(L)) { a->playing = false; } return 0; }
    static int l_ResumeAnimation(lua_State* L){ if (auto* a = ScriptAnim(L)) { a->playing = true; } return 0; }
    static int l_StopAnimation(lua_State* L){ if (auto* a = ScriptAnim(L)){ a->playing = false; a->pendingSeek = 0.0f; } return 0; }
    static int l_SetAnimationTime(lua_State* L){ if (auto* a = ScriptAnim(L)) { a->pendingSeek = std::max(0.0f, (float)luaL_checknumber(L, 1)); } return 0; }
    static int l_SetAnimationSpeed(lua_State* L){ if (auto* a = ScriptAnim(L)) { a->speed = (float)luaL_checknumber(L, 1); } return 0; }
    static int l_SetAnimationLoop(lua_State* L){ if (auto* a = ScriptAnim(L)) { a->loopMode = (int)luaL_checkinteger(L, 1); } return 0; }

    static int l_GetAnimationTime(lua_State* L){
        auto* a = ScriptAnim(L);
        lua_pushnumber(L, (a && a->animator) ? a->animator->GetCurrentSeconds() : 0.0);
        return 1;
    }
    static int l_GetAnimationDuration(lua_State* L){
        auto* a = ScriptAnim(L);
        lua_pushnumber(L, (a && a->animator) ? a->animator->GetDurationSeconds() : 0.0);
        return 1;
    }
    static int l_IsAnimationPlaying(lua_State* L){
        auto* a = ScriptAnim(L);
        lua_pushboolean(L, a && a->playing && a->animator && !a->animator->IsFinished());
        return 1;
    }
    static int l_IsAnimationFinished(lua_State* L){
        auto* a = ScriptAnim(L);
        lua_pushboolean(L, a && a->animator && a->animator->IsFinished());
        return 1;
    }
    static int l_GetAnimationName(lua_State* L){
        Entity e = ScriptEntity(L);
        auto* a = ScriptAnim(L);
        std::string name;
        if (a && e.HasComponent<MeshComponent>()){
            auto& m = e.GetComponent<MeshComponent>();
            int c = a->activeClip >= 0 ? a->activeClip : a->clip;
            if (m.model && c >= 0) { name = m.model->GetAnimationName((size_t)c); }
        }
        lua_pushstring(L, name.c_str());
        return 1;
    }

    // ---- Keyboard input ----

    static int l_IsKeyDown(lua_State* L){
        lua_pushboolean(L, ScriptSelf(L)->KeyDown((int)luaL_checkinteger(L, 1)));
        return 1;
    }
    static int l_IsKeyPressed(lua_State* L){
        lua_pushboolean(L, ScriptSelf(L)->KeyPressed((int)luaL_checkinteger(L, 1)));
        return 1;
    }

    // ---- Movement helpers (additive; convenient for keyboard controllers) ----

    static int l_Translate(lua_State* L){
        Entity e = ScriptEntity(L);
        if (e.HasComponent<TransformComponent>())
            e.GetComponent<TransformComponent>().Translation +=
                glm::vec3((float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3));
        return 0;
    }
    static int l_Rotate(lua_State* L){ // radians, added to the Euler rotation
        Entity e = ScriptEntity(L);
        if (e.HasComponent<TransformComponent>())
            e.GetComponent<TransformComponent>().Rotation +=
                glm::vec3((float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3));
        return 0;
    }
    // Move `dist` along the entity's facing (local -Z rotated by its Y/yaw).
    static int l_MoveForward(lua_State* L){
        Entity e = ScriptEntity(L);
        if (e.HasComponent<TransformComponent>()){
            auto& t = e.GetComponent<TransformComponent>();
            float yaw = t.Rotation.y;
            float dist = (float)luaL_checknumber(L, 1);
            t.Translation += dist * glm::vec3(-std::sin(yaw), 0.0f, -std::cos(yaw));
        }
        return 0;
    }

    LuaScript::LuaScript(const std::string& source, Entity entity) : m_Entity(entity){
        m_L = luaL_newstate();
        luaL_openlibs(m_L);
        *static_cast<LuaScript**>(lua_getextraspace(m_L)) = this;

        lua_register(m_L, "GetTranslation", l_GetTranslation);
        lua_register(m_L, "SetTranslation", l_SetTranslation);
        lua_register(m_L, "GetRotation",    l_GetRotation);
        lua_register(m_L, "SetRotation",    l_SetRotation);
        lua_register(m_L, "SetScale",       l_SetScale);
        lua_register(m_L, "SetColor",       l_SetColor);
        lua_register(m_L, "SetEmissive",    l_SetEmissive);

        // Animation control
        lua_register(m_L, "PlayAnimation",      l_PlayAnimation);
        lua_register(m_L, "CrossFade",          l_CrossFade);
        lua_register(m_L, "PauseAnimation",     l_PauseAnimation);
        lua_register(m_L, "ResumeAnimation",    l_ResumeAnimation);
        lua_register(m_L, "StopAnimation",      l_StopAnimation);
        lua_register(m_L, "SetAnimationTime",   l_SetAnimationTime);
        lua_register(m_L, "GetAnimationTime",   l_GetAnimationTime);
        lua_register(m_L, "GetAnimationDuration", l_GetAnimationDuration);
        lua_register(m_L, "SetAnimationSpeed",  l_SetAnimationSpeed);
        lua_register(m_L, "SetAnimationLoop",   l_SetAnimationLoop);
        lua_register(m_L, "IsAnimationPlaying", l_IsAnimationPlaying);
        lua_register(m_L, "IsAnimationFinished",l_IsAnimationFinished);
        lua_register(m_L, "GetAnimationName",   l_GetAnimationName);

        // Input + movement helpers
        lua_register(m_L, "IsKeyDown",    l_IsKeyDown);
        lua_register(m_L, "IsKeyPressed", l_IsKeyPressed);
        lua_register(m_L, "Translate",    l_Translate);
        lua_register(m_L, "Rotate",       l_Rotate);
        lua_register(m_L, "MoveForward",  l_MoveForward);

        // A global `Key` table of keycodes, so scripts write Key.Up / Key.Space / Key.W.
        lua_newtable(m_L);
        auto setKey = [&](const char* name, int code){
            lua_pushinteger(m_L, code); lua_setfield(m_L, -2, name);
        };
        setKey("Up", Key::Up); setKey("Down", Key::Down); setKey("Left", Key::Left); setKey("Right", Key::Right);
        setKey("Space", Key::Space); setKey("Enter", Key::Enter); setKey("Escape", Key::Escape); setKey("Tab", Key::Tab);
        setKey("LeftShift", Key::LeftShift); setKey("RightShift", Key::RightShift);
        setKey("LeftControl", Key::LeftControl); setKey("RightControl", Key::RightControl);
        setKey("W", Key::W); setKey("A", Key::A); setKey("S", Key::S); setKey("D", Key::D);
        setKey("Q", Key::Q); setKey("E", Key::E); setKey("R", Key::R); setKey("F", Key::F);
        for (int i = 0; i <= 9; i++){ char n[2] = { (char)('0' + i), 0 }; setKey(n, Key::D0 + i); }
        lua_setglobal(m_L, "Key");

        if (luaL_dostring(m_L, source.c_str()) != LUA_OK){
            PHX_CORE_ERROR("Lua load error: {0}", lua_tostring(m_L, -1));
            lua_pop(m_L, 1);
            m_Valid = false;
        }
        else{
            m_Valid = true;
        }
    }

    LuaScript::~LuaScript(){
        if (m_L) { lua_close(m_L); }
    }

    bool LuaScript::KeyDown(int keycode){
        bool down = Input::IsKeyPressed((KeyCode)keycode);
        if (down) { m_KeysCur.insert(keycode); }
        return down;
    }

    bool LuaScript::KeyPressed(int keycode){
        bool down = Input::IsKeyPressed((KeyCode)keycode);
        if (down) { m_KeysCur.insert(keycode); }
        return down && m_KeysPrev.find(keycode) == m_KeysPrev.end();
    }

    void LuaScript::OnUpdate(float dt){
        if (!m_Valid) { return; }
        // Roll this frame's key state to "previous" for edge detection.
        m_KeysPrev = m_KeysCur;
        m_KeysCur.clear();
        lua_getglobal(m_L, "OnUpdate");
        if (lua_isfunction(m_L, -1)){
            lua_pushnumber(m_L, dt);
            if (lua_pcall(m_L, 1, 0, 0) != LUA_OK){
                PHX_CORE_ERROR("Lua OnUpdate error: {0}", lua_tostring(m_L, -1));
                lua_pop(m_L, 1);
                m_Valid = false; // stop running a broken script
            }
        }
        else{
            lua_pop(m_L, 1);
        }
    }

    void LuaScript::OnAnimationEvent(const std::string& name){
        if (!m_Valid) { return; }
        lua_getglobal(m_L, "OnAnimationEvent");
        if (lua_isfunction(m_L, -1)){
            lua_pushstring(m_L, name.c_str());
            if (lua_pcall(m_L, 1, 0, 0) != LUA_OK){
                PHX_CORE_ERROR("Lua OnAnimationEvent error: {0}", lua_tostring(m_L, -1));
                lua_pop(m_L, 1);
                m_Valid = false;
            }
        }
        else{
            lua_pop(m_L, 1);
        }
    }
}
