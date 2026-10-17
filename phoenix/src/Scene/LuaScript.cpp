#include <Phoenix/Scene/LuaScript.h>
#include <Phoenix/Scene/Component.h>
#include <Phoenix/Scene/Scene.h>
#include <Phoenix/Physics/Physics.h>
#include <Phoenix/core/log.h>
#include <Phoenix/core/Input.h>
#include <Phoenix/core/keyCodes.h>
#include <algorithm>
#include <cmath>
#include <cctype>

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
        if (e.HasComponent<PrimitiveComponent>()) { e.GetComponent<PrimitiveComponent>().material.diffuse = c; }
        return 0;
    }

    static int l_SetEmissive(lua_State* L){
        Entity e = ScriptEntity(L);
        glm::vec3 c((float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3));
        float strength = (lua_gettop(L) >= 4) ? (float)luaL_checknumber(L, 4) : 1.0f;
        if (e.HasComponent<CubeComponent>()){ auto& m = e.GetComponent<CubeComponent>().material; m.emissive = c; m.emissiveStrength = strength; }
        if (e.HasComponent<MeshComponent>()){ auto& m = e.GetComponent<MeshComponent>().material; m.emissive = c; m.emissiveStrength = strength; }
        if (e.HasComponent<PrimitiveComponent>()){ auto& m = e.GetComponent<PrimitiveComponent>().material; m.emissive = c; m.emissiveStrength = strength; }
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
    static int l_HasAnimation(lua_State* L){
        Entity e = ScriptEntity(L);
        bool has = false;
        if (e.HasComponent<MeshComponent>()){
            auto& m = e.GetComponent<MeshComponent>();
            if (m.model){
                if (lua_isnumber(L, 1)) { int i = (int)lua_tointeger(L, 1); has = (i >= 0 && i < (int)m.model->GetAnimationCount()); }
                else if (lua_isstring(L, 1)) { has = m.model->GetAnimationIndex(lua_tostring(L, 1)) >= 0; }
            }
        }
        lua_pushboolean(L, has);
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

    // ---- Physics ----
    // These act on the script entity's runtime rigid body; they are no-ops in edit
    // mode or when the entity has no body.

    static PhysicsWorld* ScriptPhysics(lua_State* L, uint32_t& outBodyID){
        Entity e = ScriptEntity(L);
        outBodyID = 0xffffffff;
        if (!e.GetScene() || !e.HasComponent<RigidBodyComponent>()) { return nullptr; }
        outBodyID = e.GetComponent<RigidBodyComponent>().runtimeBodyID;
        if (outBodyID == 0xffffffff) { return nullptr; }
        return e.GetScene()->GetPhysicsWorld();
    }

    static glm::vec3 CheckVec3(lua_State* L, int first){
        return { (float)luaL_checknumber(L, first), (float)luaL_checknumber(L, first + 1),
                 (float)luaL_checknumber(L, first + 2) };
    }

    static int l_ApplyImpulse(lua_State* L){
        uint32_t body;
        if (auto* pw = ScriptPhysics(L, body)) { pw->ApplyImpulse(body, CheckVec3(L, 1)); }
        return 0;
    }
    static int l_ApplyForce(lua_State* L){
        uint32_t body;
        if (auto* pw = ScriptPhysics(L, body)) { pw->ApplyForce(body, CheckVec3(L, 1)); }
        return 0;
    }
    static int l_SetVelocity(lua_State* L){
        uint32_t body;
        if (auto* pw = ScriptPhysics(L, body)) { pw->SetLinearVelocity(body, CheckVec3(L, 1)); }
        return 0;
    }
    static int l_GetVelocity(lua_State* L){
        uint32_t body;
        glm::vec3 v(0.0f);
        if (auto* pw = ScriptPhysics(L, body)) { v = pw->GetLinearVelocity(body); }
        lua_pushnumber(L, v.x); lua_pushnumber(L, v.y); lua_pushnumber(L, v.z);
        return 3;
    }
    static int l_SetAngularVelocity(lua_State* L){
        uint32_t body;
        if (auto* pw = ScriptPhysics(L, body)) { pw->SetAngularVelocity(body, CheckVec3(L, 1)); }
        return 0;
    }
    // RayCast(ox,oy,oz, dx,dy,dz, maxDist) -> hitTag (or nil), hitX, hitY, hitZ
    static int l_RayCast(lua_State* L){
        Entity e = ScriptEntity(L);
        Scene* scene = e.GetScene();
        PhysicsWorld* pw = scene ? scene->GetPhysicsWorld() : nullptr;
        if (!pw) { lua_pushnil(L); return 1; }
        glm::vec3 origin = CheckVec3(L, 1);
        glm::vec3 dir = CheckVec3(L, 4);
        float maxDist = (float)luaL_checknumber(L, 7);
        float len = glm::length(dir);
        uint32_t hitBody; glm::vec3 hitPoint;
        if (len < 1e-6f || !pw->RayCast(origin, dir / len, maxDist, hitBody, hitPoint)){
            lua_pushnil(L);
            return 1;
        }
        Entity hit = scene->FindEntityByBodyID(hitBody);
        std::string tag = (hit && hit.HasComponent<TagComponent>()) ? hit.GetComponent<TagComponent>().Tag : "";
        lua_pushstring(L, tag.c_str());
        lua_pushnumber(L, hitPoint.x); lua_pushnumber(L, hitPoint.y); lua_pushnumber(L, hitPoint.z);
        return 4;
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

    // ---- Shared values ----
    // Each script has its own Lua state; these scene-wide values are how scripts
    // talk to each other (one publishes, others read).

    static int l_SetShared(lua_State* L){
        Entity e = ScriptEntity(L);
        Scene* scene = e.GetScene();
        const char* key = luaL_checkstring(L, 1);
        if (!scene) { return 0; }
        switch (lua_type(L, 2)){
            case LUA_TNUMBER:  scene->SetShared(key, ScriptValue((double)lua_tonumber(L, 2))); break;
            case LUA_TBOOLEAN: scene->SetShared(key, ScriptValue((bool)lua_toboolean(L, 2))); break;
            case LUA_TSTRING:  scene->SetShared(key, ScriptValue(std::string(lua_tostring(L, 2)))); break;
            case LUA_TNIL:
            case LUA_TNONE:    scene->ClearShared(key); break;
            default: return luaL_argerror(L, 2, "number, boolean, string or nil expected");
        }
        return 0;
    }

    static int l_GetShared(lua_State* L){
        Entity e = ScriptEntity(L);
        Scene* scene = e.GetScene();
        const char* key = luaL_checkstring(L, 1);
        const ScriptValue* value = scene ? scene->GetShared(key) : nullptr;
        if (!value){
            if (lua_gettop(L) >= 2) { lua_pushvalue(L, 2); } // the default
            else { lua_pushnil(L); }
            return 1;
        }
        if (const double* d = std::get_if<double>(value)) { lua_pushnumber(L, *d); }
        else if (const bool* b = std::get_if<bool>(value)) { lua_pushboolean(L, *b); }
        else { lua_pushstring(L, std::get<std::string>(*value).c_str()); }
        return 1;
    }

    // print(...) -> the scene's script console (tab-separated, like Lua's own print).
    static int l_print(lua_State* L){
        std::string text;
        int n = lua_gettop(L);
        for (int i = 1; i <= n; i++){
            size_t len = 0;
            const char* s = luaL_tolstring(L, i, &len); // pushes the converted value
            if (i > 1) { text += '\t'; }
            text.append(s, len);
            lua_pop(L, 1);
        }
        LuaScript* self = ScriptSelf(L);
        Entity e = self->GetEntity();
        if (Scene* scene = e.GetScene())
            scene->LogScript(e, self->GetIndex(), self->GetName(), false, text);
        return 0;
    }

    // Every bound function with its documentation. Registration and the editor's API
    // help both come from this table, so they can't drift apart.
    struct LuaBinding{
        const char* name;
        lua_CFunction fn; // null: a callback the script defines, not a binding
        const char* category;
        const char* signature;
        const char* doc;
    };
    static const LuaBinding s_Bindings[] = {
        { "OnUpdate",         nullptr, "Callbacks", "function OnUpdate(dt)", "Called every frame while playing; dt is the frame time in seconds." },
        { "OnCollisionEnter", nullptr, "Callbacks", "function OnCollisionEnter(otherTag)", "Called when this entity's body starts touching another (needs a Rigid Body)." },
        { "OnCollisionExit",  nullptr, "Callbacks", "function OnCollisionExit(otherTag)", "Called when this entity's body stops touching another." },
        { "OnAnimationEvent", nullptr, "Callbacks", "function OnAnimationEvent(name)", "Called when the playing clip crosses an animation event (Timeline panel)." },

        { "GetTranslation", l_GetTranslation, "Transform", "GetTranslation() -> x, y, z", "World position of this entity." },
        { "SetTranslation", l_SetTranslation, "Transform", "SetTranslation(x, y, z)", "Moves this entity to a position." },
        { "GetRotation",    l_GetRotation,    "Transform", "GetRotation() -> x, y, z", "Euler rotation in radians." },
        { "SetRotation",    l_SetRotation,    "Transform", "SetRotation(x, y, z)", "Sets the Euler rotation (radians)." },
        { "SetScale",       l_SetScale,       "Transform", "SetScale(x, y, z)", "Sets the scale." },
        { "Translate",      l_Translate,      "Transform", "Translate(dx, dy, dz)", "Adds an offset to the position." },
        { "Rotate",         l_Rotate,         "Transform", "Rotate(dx, dy, dz)", "Adds to the Euler rotation (radians)." },
        { "MoveForward",    l_MoveForward,    "Transform", "MoveForward(distance)", "Moves along the entity's facing (local -Z turned by its yaw)." },

        { "SetColor",    l_SetColor,    "Material", "SetColor(r, g, b)", "Sets the diffuse colour of the cube/shape/mesh material (0..1)." },
        { "SetEmissive", l_SetEmissive, "Material", "SetEmissive(r, g, b [, strength])", "Makes the material glow; strength defaults to 1." },

        { "PlayAnimation",        l_PlayAnimation,        "Animation", "PlayAnimation(clip)", "Plays a clip by index or name." },
        { "CrossFade",            l_CrossFade,            "Animation", "CrossFade(clip [, seconds])", "Blends from the current clip to another." },
        { "PauseAnimation",       l_PauseAnimation,       "Animation", "PauseAnimation()", "Pauses playback." },
        { "ResumeAnimation",      l_ResumeAnimation,      "Animation", "ResumeAnimation()", "Resumes playback." },
        { "StopAnimation",        l_StopAnimation,        "Animation", "StopAnimation()", "Stops and rewinds to the start." },
        { "SetAnimationTime",     l_SetAnimationTime,     "Animation", "SetAnimationTime(seconds)", "Seeks the current clip." },
        { "GetAnimationTime",     l_GetAnimationTime,     "Animation", "GetAnimationTime() -> seconds", "Playback position of the current clip." },
        { "GetAnimationDuration", l_GetAnimationDuration, "Animation", "GetAnimationDuration() -> seconds", "Length of the current clip." },
        { "SetAnimationSpeed",    l_SetAnimationSpeed,    "Animation", "SetAnimationSpeed(speed)", "Playback rate (1 = normal, negative = reverse)." },
        { "SetAnimationLoop",     l_SetAnimationLoop,     "Animation", "SetAnimationLoop(mode)", "0 = loop, 1 = once, 2 = ping-pong." },
        { "IsAnimationPlaying",   l_IsAnimationPlaying,   "Animation", "IsAnimationPlaying() -> bool", "True while a clip is playing." },
        { "IsAnimationFinished",  l_IsAnimationFinished,  "Animation", "IsAnimationFinished() -> bool", "True once a non-looping clip has ended." },
        { "GetAnimationName",     l_GetAnimationName,     "Animation", "GetAnimationName() -> name", "Name of the current clip." },
        { "HasAnimation",         l_HasAnimation,         "Animation", "HasAnimation(clip) -> bool", "Whether the model has a clip with this index or name." },

        { "ApplyImpulse",       l_ApplyImpulse,       "Physics", "ApplyImpulse(x, y, z)", "Instant change of momentum (needs a Rigid Body, while playing)." },
        { "ApplyForce",         l_ApplyForce,         "Physics", "ApplyForce(x, y, z)", "Continuous force for this step." },
        { "SetVelocity",        l_SetVelocity,        "Physics", "SetVelocity(x, y, z)", "Sets the linear velocity." },
        { "GetVelocity",        l_GetVelocity,        "Physics", "GetVelocity() -> x, y, z", "Current linear velocity." },
        { "SetAngularVelocity", l_SetAngularVelocity, "Physics", "SetAngularVelocity(x, y, z)", "Sets the spin (radians/second)." },
        { "RayCast",            l_RayCast,            "Physics", "RayCast(ox, oy, oz, dx, dy, dz, maxDist) -> tag, hx, hy, hz", "Casts a ray; returns the hit entity's tag and point, or nil." },

        { "IsKeyDown",    l_IsKeyDown,    "Input", "IsKeyDown(key) -> bool", "True while the key is held, e.g. IsKeyDown(Key.W)." },
        { "IsKeyPressed", l_IsKeyPressed, "Input", "IsKeyPressed(key) -> bool", "True only on the frame the key goes down." },

        { "SetShared", l_SetShared, "Shared", "SetShared(key, value)", "Publishes a number, boolean or string to every script (nil removes it). Cleared on Run." },
        { "GetShared", l_GetShared, "Shared", "GetShared(key [, default]) -> value", "Reads a value another script published, or default (nil) if unset." },

        { "print", l_print, "Debug", "print(...)", "Writes its arguments to the Script Editor console." },
    };

    // Names exposed in the global `Key` table (Key.Up, Key.Space, Key.W, Key.0 ...).
    struct LuaKey { const char* name; int code; };
    static std::vector<LuaKey> ScriptKeys(){
        std::vector<LuaKey> keys = {
            { "Up", Key::Up }, { "Down", Key::Down }, { "Left", Key::Left }, { "Right", Key::Right },
            { "Space", Key::Space }, { "Enter", Key::Enter }, { "Escape", Key::Escape }, { "Tab", Key::Tab },
            { "LeftShift", Key::LeftShift }, { "RightShift", Key::RightShift },
            { "LeftControl", Key::LeftControl }, { "RightControl", Key::RightControl },
            { "W", Key::W }, { "A", Key::A }, { "S", Key::S }, { "D", Key::D },
            { "Q", Key::Q }, { "E", Key::E }, { "R", Key::R }, { "F", Key::F },
        };
        static const char* digits[] = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9" };
        for (int i = 0; i <= 9; i++) { keys.push_back({ digits[i], Key::D0 + i }); }
        return keys;
    }

    const std::vector<LuaApiDoc>& LuaScript::GetApiDocs(){
        static const std::vector<LuaApiDoc> docs = []{
            std::vector<LuaApiDoc> out;
            for (const LuaBinding& b : s_Bindings)
                out.push_back({ b.name, b.signature, b.doc, b.category });
            for (const LuaKey& k : ScriptKeys()){
                std::string name = std::string("Key.") + k.name;
                out.push_back({ name, name, "Key code for IsKeyDown / IsKeyPressed.", "Keys" });
            }
            return out;
        }();
        return docs;
    }

    int LuaScript::ParseErrorLine(const std::string& error, std::string* outMessage){
        // Lua formats errors as "<chunk>:<line>: <message>"; find the first ":<digits>:".
        for (std::size_t i = error.find(':'); i != std::string::npos; i = error.find(':', i + 1)){
            std::size_t j = i + 1;
            while (j < error.size() && std::isdigit((unsigned char)error[j])) { j++; }
            if (j > i + 1 && j < error.size() && error[j] == ':'){
                if (outMessage){
                    std::size_t start = j + 1;
                    while (start < error.size() && error[start] == ' ') { start++; }
                    *outMessage = error.substr(start);
                }
                return std::stoi(error.substr(i + 1, j - i - 1));
            }
        }
        if (outMessage) { *outMessage = error; }
        return -1;
    }

    bool LuaScript::CheckSyntax(const std::string& source, std::string& outError, int& outLine){
        lua_State* L = luaL_newstate();
        bool ok = luaL_loadbuffer(L, source.data(), source.size(), "=script") == LUA_OK;
        if (!ok){
            const char* msg = lua_tostring(L, -1);
            outLine = ParseErrorLine(msg ? msg : "", &outError);
        }
        lua_close(L);
        return ok;
    }

    // The count hook fires every HOOK_INTERVAL VM instructions; a single call may run
    // MAX_HOOK_TICKS * HOOK_INTERVAL (~100M) instructions, far beyond any per-frame
    // script but short enough (well under a second) to recover from a runaway loop.
    static const int HOOK_INTERVAL = 10000;
    static const int MAX_HOOK_TICKS = 10000;

    void LuaScript::InstructionHook(lua_State* L, lua_Debug* /*ar*/){
        LuaScript* script = *static_cast<LuaScript**>(lua_getextraspace(L));
        if (++script->m_HookTicks > MAX_HOOK_TICKS)
            luaL_error(L, "script exceeded its instruction budget (infinite loop?)");
    }

    bool LuaScript::ProtectedCall(int nargs, const char* what){
        m_HookTicks = 0;
        if (lua_pcall(m_L, nargs, 0, 0) != LUA_OK){
            const char* msg = lua_tostring(m_L, -1);
            std::string error = msg ? msg : "unknown error";
            lua_pop(m_L, 1);
            Fail(what, error);
            return false;
        }
        return true;
    }

    void LuaScript::Fail(const char* what, const std::string& error){
        PHX_CORE_ERROR("Lua {0} error in '{1}': {2}", what, m_Name, error);
        m_Valid = false; // stop running a broken script
        m_ErrorLine = ParseErrorLine(error, &m_Error);
        if (Scene* scene = m_Entity.GetScene())
            scene->LogScript(m_Entity, m_Index, m_Name, true, std::string(what) + ": " + m_Error, m_ErrorLine);
    }

    LuaScript::LuaScript(const std::string& source, Entity entity, int index, const std::string& name)
        : m_Entity(entity), m_Index(index), m_Name(name){
        m_L = luaL_newstate();
        *static_cast<LuaScript**>(lua_getextraspace(m_L)) = this;

        // Scripts come from scene files, which get shared, so only open the pure
        // libraries: no io / os (file access, os.execute) or package (loading native
        // modules). dofile/loadfile/load can read files or run unchecked bytecode.
        const luaL_Reg safeLibs[] = {
            { "_G",              luaopen_base },
            { LUA_TABLIBNAME,    luaopen_table },
            { LUA_STRLIBNAME,    luaopen_string },
            { LUA_MATHLIBNAME,   luaopen_math },
            { LUA_UTF8LIBNAME,   luaopen_utf8 },
            { LUA_COLIBNAME,     luaopen_coroutine },
        };
        for (const luaL_Reg& lib : safeLibs){
            luaL_requiref(m_L, lib.name, lib.func, 1);
            lua_pop(m_L, 1);
        }
        for (const char* unsafe : { "dofile", "loadfile", "load" }){
            lua_pushnil(m_L);
            lua_setglobal(m_L, unsafe);
        }
        lua_sethook(m_L, &LuaScript::InstructionHook, LUA_MASKCOUNT, HOOK_INTERVAL);

        for (const LuaBinding& b : s_Bindings)
            if (b.fn) { lua_register(m_L, b.name, b.fn); }

        // A global `Key` table of keycodes, so scripts write Key.Up / Key.Space / Key.W.
        lua_newtable(m_L);
        for (const LuaKey& k : ScriptKeys()){
            lua_pushinteger(m_L, k.code);
            lua_setfield(m_L, -2, k.name);
        }
        lua_setglobal(m_L, "Key");

        std::string chunkName = "=" + m_Name;
        if (luaL_loadbuffer(m_L, source.data(), source.size(), chunkName.c_str()) != LUA_OK){
            const char* msg = lua_tostring(m_L, -1);
            std::string error = msg ? msg : "unknown error";
            lua_pop(m_L, 1);
            Fail("load", error);
        }
        else{
            m_Valid = true;
            ProtectedCall(0, "load"); // run the chunk (defines OnUpdate etc.)
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
            ProtectedCall(1, "OnUpdate");
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
            ProtectedCall(1, "OnAnimationEvent");
        }
        else{
            lua_pop(m_L, 1);
        }
    }

    void LuaScript::OnCollision(const std::string& otherTag, bool entered){
        if (!m_Valid) { return; }
        lua_getglobal(m_L, entered ? "OnCollisionEnter" : "OnCollisionExit");
        if (lua_isfunction(m_L, -1)){
            lua_pushstring(m_L, otherTag.c_str());
            ProtectedCall(1, entered ? "OnCollisionEnter" : "OnCollisionExit");
        }
        else{
            lua_pop(m_L, 1);
        }
    }
}
