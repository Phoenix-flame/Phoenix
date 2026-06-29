#include <Phoenix/Scene/LuaScript.h>
#include <Phoenix/Scene/Component.h>
#include <Phoenix/core/log.h>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

namespace Phoenix{

    // The bound entity is stashed in the lua_State's extra space so the C functions
    // below can reach it without globals.
    static Entity ScriptEntity(lua_State* L){
        LuaScript* self = *static_cast<LuaScript**>(lua_getextraspace(L));
        return self->GetEntity();
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

    void LuaScript::OnUpdate(float dt){
        if (!m_Valid) { return; }
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
}
