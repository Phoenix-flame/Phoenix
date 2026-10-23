#include <Phoenix/Scene/LuaScript.h>
#include <Phoenix/Scene/Component.h>
#include <Phoenix/Scene/Scene.h>
#include <Phoenix/Physics/Physics.h>
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
    // These act on the script entity's runtime body (rigid body or character
    // capsule); they are no-ops in edit mode or when the entity has no body.

    static PhysicsWorld* ScriptPhysics(lua_State* L, uint32_t& outBodyID){
        Entity e = ScriptEntity(L);
        outBodyID = PhysicsWorld::InvalidBody;
        Scene* scene = e.GetScene();
        if (!scene || !scene->GetPhysicsWorld()) { return nullptr; }
        outBodyID = scene->GetEntityBody(e);
        if (outBodyID == PhysicsWorld::InvalidBody) { return nullptr; }
        return scene->GetPhysicsWorld();
    }

    static glm::vec3 CheckVec3(lua_State* L, int first){
        return { (float)luaL_checknumber(L, first), (float)luaL_checknumber(L, first + 1),
                 (float)luaL_checknumber(L, first + 2) };
    }

    static int PushVec3(lua_State* L, const glm::vec3& v){
        lua_pushnumber(L, v.x); lua_pushnumber(L, v.y); lua_pushnumber(L, v.z);
        return 3;
    }

    static CharacterControllerComponent* ScriptCharacter(lua_State* L){
        Entity e = ScriptEntity(L);
        return e.HasComponent<CharacterControllerComponent>() ? &e.GetComponent<CharacterControllerComponent>() : nullptr;
    }

    // Body-only helpers: f(world, body, L) for the script entity's body.
    template<typename F>
    static int WithBody(lua_State* L, F f){
        uint32_t body;
        if (auto* pw = ScriptPhysics(L, body)) { f(*pw, body); }
        return 0;
    }

    static int l_ApplyImpulse(lua_State* L){ return WithBody(L, [&](PhysicsWorld& pw, uint32_t b){ pw.ApplyImpulse(b, CheckVec3(L, 1)); }); }
    static int l_ApplyImpulseAtPoint(lua_State* L){ return WithBody(L, [&](PhysicsWorld& pw, uint32_t b){ pw.ApplyImpulseAtPoint(b, CheckVec3(L, 1), CheckVec3(L, 4)); }); }
    static int l_ApplyAngularImpulse(lua_State* L){ return WithBody(L, [&](PhysicsWorld& pw, uint32_t b){ pw.ApplyAngularImpulse(b, CheckVec3(L, 1)); }); }
    static int l_ApplyForce(lua_State* L){ return WithBody(L, [&](PhysicsWorld& pw, uint32_t b){ pw.ApplyForce(b, CheckVec3(L, 1)); }); }
    static int l_ApplyForceAtPoint(lua_State* L){ return WithBody(L, [&](PhysicsWorld& pw, uint32_t b){ pw.ApplyForceAtPoint(b, CheckVec3(L, 1), CheckVec3(L, 4)); }); }
    static int l_ApplyTorque(lua_State* L){ return WithBody(L, [&](PhysicsWorld& pw, uint32_t b){ pw.ApplyTorque(b, CheckVec3(L, 1)); }); }
    static int l_SetVelocity(lua_State* L){ return WithBody(L, [&](PhysicsWorld& pw, uint32_t b){ pw.SetLinearVelocity(b, CheckVec3(L, 1)); }); }
    static int l_SetAngularVelocity(lua_State* L){ return WithBody(L, [&](PhysicsWorld& pw, uint32_t b){ pw.SetAngularVelocity(b, CheckVec3(L, 1)); }); }
    static int l_SetGravityFactor(lua_State* L){ return WithBody(L, [&](PhysicsWorld& pw, uint32_t b){ pw.SetGravityFactor(b, (float)luaL_checknumber(L, 1)); }); }
    static int l_SetFriction(lua_State* L){ return WithBody(L, [&](PhysicsWorld& pw, uint32_t b){ pw.SetFriction(b, (float)luaL_checknumber(L, 1)); }); }
    static int l_SetRestitution(lua_State* L){ return WithBody(L, [&](PhysicsWorld& pw, uint32_t b){ pw.SetRestitution(b, (float)luaL_checknumber(L, 1)); }); }
    static int l_WakeUp(lua_State* L){ return WithBody(L, [&](PhysicsWorld& pw, uint32_t b){ pw.WakeUp(b); }); }

    static int l_GetVelocity(lua_State* L){
        if (auto* cc = ScriptCharacter(L)) { return PushVec3(L, cc->velocity); }
        uint32_t body;
        glm::vec3 v(0.0f);
        if (auto* pw = ScriptPhysics(L, body)) { v = pw->GetLinearVelocity(body); }
        return PushVec3(L, v);
    }
    static int l_GetAngularVelocity(lua_State* L){
        uint32_t body;
        glm::vec3 v(0.0f);
        if (auto* pw = ScriptPhysics(L, body)) { v = pw->GetAngularVelocity(body); }
        return PushVec3(L, v);
    }
    static int l_GetMass(lua_State* L){
        uint32_t body;
        float m = 0.0f;
        if (auto* pw = ScriptPhysics(L, body)) { m = pw->GetMass(body); }
        lua_pushnumber(L, m);
        return 1;
    }
    static int l_IsSleeping(lua_State* L){
        uint32_t body;
        auto* pw = ScriptPhysics(L, body);
        lua_pushboolean(L, pw && !pw->IsAwake(body));
        return 1;
    }

    // Teleport(x,y,z [, rx,ry,rz]): move the entity AND its body/character (a plain
    // SetTranslation on a dynamic body is overwritten by the simulation next step).
    static int l_Teleport(lua_State* L){
        Entity e = ScriptEntity(L);
        if (!e.HasComponent<TransformComponent>()) { return 0; }
        auto& t = e.GetComponent<TransformComponent>();
        t.Translation = CheckVec3(L, 1);
        if (lua_gettop(L) >= 6) { t.Rotation = CheckVec3(L, 4); }
        PhysicsWorld* pw = e.GetScene() ? e.GetScene()->GetPhysicsWorld() : nullptr;
        if (!pw) { return 0; }
        if (auto* cc = ScriptCharacter(L)) { pw->SetCharacterPosition(cc->runtimeCharacterID, t.Translation); }
        else if (e.HasComponent<RigidBodyComponent>()) { pw->SetBodyTransform(e.GetComponent<RigidBodyComponent>().runtimeBodyID, t.Translation, t.Rotation); }
        return 0;
    }

    // ---- Queries ----

    static PhysicsWorld::QueryFilter ScriptQueryFilter(lua_State* L, int maskArg){
        PhysicsWorld::QueryFilter filter;
        Entity e = ScriptEntity(L);
        if (e.GetScene()) { filter.ignoreBody = e.GetScene()->GetEntityBody(e); } // never hit yourself
        if (lua_gettop(L) >= maskArg && lua_isinteger(L, maskArg)) { filter.layerMask = (uint16_t)lua_tointeger(L, maskArg); }
        return filter;
    }

    static std::string TagOfBody(Scene* scene, uint32_t body){
        Entity hit = scene->FindEntityByBodyID(body);
        return (hit && hit.HasComponent<TagComponent>()) ? hit.GetComponent<TagComponent>().Tag : "";
    }

    static int PushHit(lua_State* L, Scene* scene, const PhysicsWorld::Hit& hit){
        lua_pushstring(L, TagOfBody(scene, hit.body).c_str());
        PushVec3(L, hit.point);
        PushVec3(L, hit.normal);
        lua_pushnumber(L, hit.distance);
        return 8;
    }

    // RayCast(ox,oy,oz, dx,dy,dz, maxDist [, layerMask])
    //   -> hitTag (or nil), hitX, hitY, hitZ, normalX, normalY, normalZ, distance
    static int l_RayCast(lua_State* L){
        Scene* scene = ScriptEntity(L).GetScene();
        PhysicsWorld* pw = scene ? scene->GetPhysicsWorld() : nullptr;
        PhysicsWorld::Hit hit;
        if (!pw || !pw->RayCast(CheckVec3(L, 1), CheckVec3(L, 4), (float)luaL_checknumber(L, 7), hit, ScriptQueryFilter(L, 8))){
            lua_pushnil(L);
            return 1;
        }
        return PushHit(L, scene, hit);
    }

    // SphereCast(ox,oy,oz, dx,dy,dz, radius, maxDist [, layerMask]) -> same as RayCast
    static int l_SphereCast(lua_State* L){
        Scene* scene = ScriptEntity(L).GetScene();
        PhysicsWorld* pw = scene ? scene->GetPhysicsWorld() : nullptr;
        PhysicsWorld::Hit hit;
        if (!pw || !pw->SphereCast(CheckVec3(L, 1), CheckVec3(L, 4), (float)luaL_checknumber(L, 7),
                                   (float)luaL_checknumber(L, 8), hit, ScriptQueryFilter(L, 9))){
            lua_pushnil(L);
            return 1;
        }
        return PushHit(L, scene, hit);
    }

    static int PushTagList(lua_State* L, Scene* scene, const std::vector<uint32_t>& bodies){
        lua_createtable(L, (int)bodies.size(), 0);
        int i = 1;
        for (uint32_t body : bodies){
            lua_pushstring(L, TagOfBody(scene, body).c_str());
            lua_rawseti(L, -2, i++);
        }
        return 1;
    }

    // OverlapSphere(x,y,z, radius [, layerMask]) -> { tags of bodies inside }
    static int l_OverlapSphere(lua_State* L){
        Scene* scene = ScriptEntity(L).GetScene();
        PhysicsWorld* pw = scene ? scene->GetPhysicsWorld() : nullptr;
        if (!pw) { lua_newtable(L); return 1; }
        return PushTagList(L, scene, pw->OverlapSphere(CheckVec3(L, 1), (float)luaL_checknumber(L, 4), ScriptQueryFilter(L, 5)));
    }

    // OverlapBox(x,y,z, hx,hy,hz [, layerMask]) -> { tags of bodies inside } (axis aligned)
    static int l_OverlapBox(lua_State* L){
        Scene* scene = ScriptEntity(L).GetScene();
        PhysicsWorld* pw = scene ? scene->GetPhysicsWorld() : nullptr;
        if (!pw) { lua_newtable(L); return 1; }
        return PushTagList(L, scene, pw->OverlapBox(CheckVec3(L, 1), CheckVec3(L, 4), glm::vec3(0.0f), ScriptQueryFilter(L, 7)));
    }

    // ---- Character controller ----

    // Move(vx, vz): desired horizontal velocity (m/s, world space) until changed.
    static int l_Move(lua_State* L){
        if (auto* cc = ScriptCharacter(L))
            cc->desiredVelocity = { (float)luaL_checknumber(L, 1), 0.0f, (float)luaL_checknumber(L, 2) };
        return 0;
    }
    // Jump([speed]): jumps on the next step if standing on the ground.
    static int l_Jump(lua_State* L){
        if (auto* cc = ScriptCharacter(L)){
            if (lua_gettop(L) >= 1) { cc->jumpSpeed = (float)luaL_checknumber(L, 1); }
            cc->jumpRequested = true;
        }
        return 0;
    }
    static int l_IsGrounded(lua_State* L){
        auto* cc = ScriptCharacter(L);
        lua_pushboolean(L, cc && cc->grounded);
        return 1;
    }

    // ---- Joints (the script entity's JointComponent) ----

    static JointComponent* ScriptJoint(lua_State* L, PhysicsWorld*& outWorld){
        Entity e = ScriptEntity(L);
        outWorld = e.GetScene() ? e.GetScene()->GetPhysicsWorld() : nullptr;
        return e.HasComponent<JointComponent>() ? &e.GetComponent<JointComponent>() : nullptr;
    }

    // SetMotor(mode, target): mode "off" | "velocity" | "position" (or 0/1/2).
    static int l_SetMotor(lua_State* L){
        PhysicsWorld* pw;
        JointComponent* j = ScriptJoint(L, pw);
        if (!j) { return 0; }
        int mode = 0;
        if (lua_isnumber(L, 1)) { mode = (int)lua_tointeger(L, 1); }
        else{
            std::string m = luaL_checkstring(L, 1);
            mode = m == "velocity" ? 1 : m == "position" ? 2 : 0;
        }
        j->motor = (JointComponent::Motor)std::clamp(mode, 0, 2);
        if (lua_gettop(L) >= 2) { j->motorTarget = (float)luaL_checknumber(L, 2); }
        if (pw) { pw->SetJointMotor(j->runtimeJointID, (PhysicsWorld::MotorMode)(int)j->motor, j->motorTarget); }
        return 0;
    }
    static int l_SetMotorTarget(lua_State* L){
        PhysicsWorld* pw;
        JointComponent* j = ScriptJoint(L, pw);
        if (!j) { return 0; }
        j->motorTarget = (float)luaL_checknumber(L, 1);
        if (pw) { pw->SetJointMotor(j->runtimeJointID, (PhysicsWorld::MotorMode)(int)j->motor, j->motorTarget); }
        return 0;
    }
    // Hinge angle (rad) / slider position (m) / distance (m).
    static int l_GetJointValue(lua_State* L){
        PhysicsWorld* pw;
        JointComponent* j = ScriptJoint(L, pw);
        lua_pushnumber(L, (j && pw) ? pw->GetJointValue(j->runtimeJointID) : 0.0f);
        return 1;
    }
    static int l_BreakJoint(lua_State* L){
        PhysicsWorld* pw;
        JointComponent* j = ScriptJoint(L, pw);
        if (j && pw){
            pw->RemoveJoint(j->runtimeJointID);
            j->runtimeJointID = PhysicsWorld::InvalidJoint;
        }
        return 0;
    }
    static int l_IsJointBroken(lua_State* L){
        PhysicsWorld* pw;
        JointComponent* j = ScriptJoint(L, pw);
        lua_pushboolean(L, j && pw && !pw->IsJointAlive(j->runtimeJointID));
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
            PHX_CORE_ERROR("Lua {0} error: {1}", what, lua_tostring(m_L, -1));
            lua_pop(m_L, 1);
            m_Valid = false; // stop running a broken script
            return false;
        }
        return true;
    }

    LuaScript::LuaScript(const std::string& source, Entity entity) : m_Entity(entity){
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
        lua_register(m_L, "HasAnimation",       l_HasAnimation);

        // Physics: bodies
        lua_register(m_L, "ApplyImpulse",        l_ApplyImpulse);
        lua_register(m_L, "ApplyImpulseAtPoint", l_ApplyImpulseAtPoint);
        lua_register(m_L, "ApplyAngularImpulse", l_ApplyAngularImpulse);
        lua_register(m_L, "ApplyForce",          l_ApplyForce);
        lua_register(m_L, "ApplyForceAtPoint",   l_ApplyForceAtPoint);
        lua_register(m_L, "ApplyTorque",         l_ApplyTorque);
        lua_register(m_L, "SetVelocity",         l_SetVelocity);
        lua_register(m_L, "GetVelocity",         l_GetVelocity);
        lua_register(m_L, "SetAngularVelocity",  l_SetAngularVelocity);
        lua_register(m_L, "GetAngularVelocity",  l_GetAngularVelocity);
        lua_register(m_L, "GetMass",             l_GetMass);
        lua_register(m_L, "SetGravityFactor",    l_SetGravityFactor);
        lua_register(m_L, "SetFriction",         l_SetFriction);
        lua_register(m_L, "SetRestitution",      l_SetRestitution);
        lua_register(m_L, "WakeUp",              l_WakeUp);
        lua_register(m_L, "IsSleeping",          l_IsSleeping);
        lua_register(m_L, "Teleport",            l_Teleport);
        // Physics: queries
        lua_register(m_L, "RayCast",             l_RayCast);
        lua_register(m_L, "SphereCast",          l_SphereCast);
        lua_register(m_L, "OverlapSphere",       l_OverlapSphere);
        lua_register(m_L, "OverlapBox",          l_OverlapBox);
        // Physics: character controller
        lua_register(m_L, "Move",                l_Move);
        lua_register(m_L, "Jump",                l_Jump);
        lua_register(m_L, "IsGrounded",          l_IsGrounded);
        // Physics: joints
        lua_register(m_L, "SetMotor",            l_SetMotor);
        lua_register(m_L, "SetMotorTarget",      l_SetMotorTarget);
        lua_register(m_L, "GetJointValue",       l_GetJointValue);
        lua_register(m_L, "BreakJoint",          l_BreakJoint);
        lua_register(m_L, "IsJointBroken",       l_IsJointBroken);

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

        if (luaL_loadstring(m_L, source.c_str()) != LUA_OK){
            PHX_CORE_ERROR("Lua load error: {0}", lua_tostring(m_L, -1));
            lua_pop(m_L, 1);
            m_Valid = false;
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

    bool LuaScript::PushCallback(const char* name){
        lua_getglobal(m_L, name);
        if (lua_isfunction(m_L, -1)) { return true; }
        lua_pop(m_L, 1);
        return false;
    }

    void LuaScript::OnCollision(const std::string& otherTag, bool entered,
                                const glm::vec3& point, const glm::vec3& normal, float speed){
        if (!m_Valid) { return; }
        const char* name = entered ? "OnCollisionEnter" : "OnCollisionExit";
        if (!PushCallback(name)) { return; }
        lua_pushstring(m_L, otherTag.c_str());
        if (!entered) { ProtectedCall(1, name); return; }
        PushVec3(m_L, point);
        PushVec3(m_L, normal);
        lua_pushnumber(m_L, speed);
        ProtectedCall(8, name);
    }

    void LuaScript::OnTrigger(const std::string& otherTag, bool entered){
        if (!m_Valid) { return; }
        const char* name = entered ? "OnTriggerEnter" : "OnTriggerExit";
        // Scripts written before triggers existed handle them in OnCollisionEnter/Exit.
        if (!PushCallback(name)){
            name = entered ? "OnCollisionEnter" : "OnCollisionExit";
            if (!PushCallback(name)) { return; }
        }
        lua_pushstring(m_L, otherTag.c_str());
        ProtectedCall(1, name);
    }

    void LuaScript::OnJointBreak(const std::string& otherTag){
        if (!m_Valid || !PushCallback("OnJointBreak")) { return; }
        lua_pushstring(m_L, otherTag.c_str());
        ProtectedCall(1, "OnJointBreak");
    }
}
