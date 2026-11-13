#include <Phoenix/Scene/LuaScript.h>
#include <Phoenix/Scene/Component.h>
#include <Phoenix/Scene/Scene.h>
#include <Phoenix/Physics/Physics.h>
#include <Phoenix/AI/Environment.h>
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
        Entity e = scene->FindEntityByBodyID(hit.body);
        if (e) { lua_pushinteger(L, (lua_Integer)(uint32_t)e); } else { lua_pushnil(L); }
        PushVec3(L, hit.normal);
        lua_pushnumber(L, hit.distance);
        return 9;
    }

    // RayCast(ox,oy,oz, dx,dy,dz, maxDist [, layerMask])
    //   -> hitTag (or nil), hitX, hitY, hitZ, hitId, normalX, normalY, normalZ, distance
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

    // ---- Fire (the script entity's FireComponent) ----

    static FireComponent* ScriptFire(lua_State* L){
        Entity e = ScriptEntity(L);
        return e.HasComponent<FireComponent>() ? &e.GetComponent<FireComponent>() : nullptr;
    }
    static int l_Ignite(lua_State* L){
        if (auto* f = ScriptFire(L)) { f->burning = true; f->started = true; f->doused = 0.0f; }
        return 0;
    }
    static int l_Extinguish(lua_State* L){
        if (auto* f = ScriptFire(L)) { f->burning = false; f->started = true; }
        return 0;
    }
    static int l_IsBurning(lua_State* L){
        auto* f = ScriptFire(L);
        lua_pushboolean(L, f && f->burning);
        return 1;
    }
    static int l_SetFireWind(lua_State* L){
        if (auto* f = ScriptFire(L))
            f->wind = { (float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3) };
        return 0;
    }

    // ---- Joints (the script entity's JointComponent) ----
    // Every function takes an optional joint number (1-based, default 1) as its last
    // argument, for entities with several joints.

    static JointComponent::Joint* ScriptJoint(lua_State* L, PhysicsWorld*& outWorld, int indexArg){
        Entity e = ScriptEntity(L);
        outWorld = e.GetScene() ? e.GetScene()->GetPhysicsWorld() : nullptr;
        if (!e.HasComponent<JointComponent>()) { return nullptr; }
        auto& joints = e.GetComponent<JointComponent>().joints;
        int index = (lua_gettop(L) >= indexArg && lua_isinteger(L, indexArg)) ? (int)lua_tointeger(L, indexArg) - 1 : 0;
        return (index >= 0 && index < (int)joints.size()) ? &joints[index] : nullptr;
    }

    // SetMotor(mode, target [, joint]): mode "off" | "velocity" | "position" (or 0/1/2).
    static int l_SetMotor(lua_State* L){
        PhysicsWorld* pw;
        JointComponent::Joint* j = ScriptJoint(L, pw, 3);
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
        JointComponent::Joint* j = ScriptJoint(L, pw, 2);
        if (!j) { return 0; }
        j->motorTarget = (float)luaL_checknumber(L, 1);
        if (pw) { pw->SetJointMotor(j->runtimeJointID, (PhysicsWorld::MotorMode)(int)j->motor, j->motorTarget); }
        return 0;
    }
    // Hinge angle (rad) / slider position (m) / distance (m).
    static int l_GetJointValue(lua_State* L){
        PhysicsWorld* pw;
        JointComponent::Joint* j = ScriptJoint(L, pw, 1);
        lua_pushnumber(L, (j && pw) ? pw->GetJointValue(j->runtimeJointID) : 0.0f);
        return 1;
    }
    static int l_BreakJoint(lua_State* L){
        PhysicsWorld* pw;
        JointComponent::Joint* j = ScriptJoint(L, pw, 1);
        if (j && pw){
            pw->RemoveJoint(j->runtimeJointID);
            j->runtimeJointID = PhysicsWorld::InvalidJoint;
        }
        return 0;
    }
    static int l_IsJointBroken(lua_State* L){
        PhysicsWorld* pw;
        JointComponent::Joint* j = ScriptJoint(L, pw, 1);
        lua_pushboolean(L, j && pw && !pw->IsJointAlive(j->runtimeJointID));
        return 1;
    }
    static int l_GetJointCount(lua_State* L){
        Entity e = ScriptEntity(L);
        lua_pushinteger(L, e.HasComponent<JointComponent>() ? (lua_Integer)e.GetComponent<JointComponent>().joints.size() : 0);
        return 1;
    }

    // ---- Ragdoll ----

    static RagdollComponent* ScriptRagdoll(lua_State* L){
        Entity e = ScriptEntity(L);
        return e.HasComponent<RagdollComponent>() ? &e.GetComponent<RagdollComponent>() : nullptr;
    }
    // SetRagdoll(on): go limp (bodies from the current pose) or back to animation.
    static int l_SetRagdoll(lua_State* L){
        if (auto* rd = ScriptRagdoll(L)) { rd->request = lua_toboolean(L, 1) ? 1 : 0; }
        return 0;
    }
    static int l_IsRagdoll(lua_State* L){
        auto* rd = ScriptRagdoll(L);
        lua_pushboolean(L, rd && (rd->active || rd->request == 1));
        return 1;
    }
    // RagdollImpulse(vx,vy,vz): velocity change (m/s) for every part (a hit, a blast).
    static int l_RagdollImpulse(lua_State* L){
        auto* rd = ScriptRagdoll(L);
        PhysicsWorld* pw = ScriptEntity(L).GetScene() ? ScriptEntity(L).GetScene()->GetPhysicsWorld() : nullptr;
        if (!rd || !pw) { return 0; }
        glm::vec3 dv = CheckVec3(L, 1);
        for (const auto& part : rd->parts) { pw->ApplyImpulse(part.body, dv * pw->GetMass(part.body)); }
        return 0;
    }

    // ---- Vehicle ----

    static VehicleComponent* ScriptVehicle(lua_State* L){
        Entity e = ScriptEntity(L);
        return e.HasComponent<VehicleComponent>() ? &e.GetComponent<VehicleComponent>() : nullptr;
    }
    // SetVehicleInput(forward, right [, brake, handbrake]): forward/right in -1..1
    // (forward < 0 reverses), brake/handbrake in 0..1. Kept until changed.
    static int l_SetVehicleInput(lua_State* L){
        if (auto* v = ScriptVehicle(L)){
            v->forward = (float)luaL_checknumber(L, 1);
            v->right = (float)luaL_checknumber(L, 2);
            v->brake = lua_gettop(L) >= 3 ? (float)luaL_checknumber(L, 3) : 0.0f;
            v->handBrake = lua_gettop(L) >= 4 ? (float)luaL_checknumber(L, 4) : 0.0f;
        }
        return 0;
    }
    static int l_GetVehicleSpeed(lua_State* L){
        auto* v = ScriptVehicle(L);
        lua_pushnumber(L, v ? v->speed : 0.0f);
        return 1;
    }
    static int l_GetEngineRPM(lua_State* L){
        auto* v = ScriptVehicle(L);
        lua_pushnumber(L, v ? v->rpm : 0.0f);
        lua_pushinteger(L, v ? v->gear : 0);
        return 2;
    }

    // ---- Reinforcement learning ----
    // These act on the script's own entity when it is an Agent (no-ops otherwise).

    static Environment* ScriptEnvironment(lua_State* L){
        Scene* scene = ScriptEntity(L).GetScene();
        return scene ? scene->GetEnvironment() : nullptr;
    }
    static int l_AddReward(lua_State* L){
        if (Environment* env = ScriptEnvironment(L)) { env->AddReward((entt::entity)ScriptEntity(L), (float)luaL_checknumber(L, 1)); }
        return 0;
    }
    static int l_EndEpisode(lua_State* L){
        if (Environment* env = ScriptEnvironment(L)) { env->EndEpisode((entt::entity)ScriptEntity(L)); }
        return 0;
    }
    // AddObservation(v1, v2, ...) inside CollectObservations().
    static int l_AddObservation(lua_State* L){
        Environment* env = ScriptEnvironment(L);
        int n = lua_gettop(L);
        for (int i = 1; i <= n && env; i++) { env->AddObservation((entt::entity)ScriptEntity(L), (float)luaL_checknumber(L, i)); }
        return 0;
    }
    static int l_GetEpisodeStep(lua_State* L){
        Environment* env = ScriptEnvironment(L);
        AgentState* agent = env ? env->FindAgent((entt::entity)ScriptEntity(L)) : nullptr;
        lua_pushinteger(L, agent ? agent->episodeStep : 0);
        return 1;
    }
    static int l_GetCumulativeReward(lua_State* L){
        Environment* env = ScriptEnvironment(L);
        AgentState* agent = env ? env->FindAgent((entt::entity)ScriptEntity(L)) : nullptr;
        lua_pushnumber(L, agent ? agent->episodeReturn : 0.0);
        return 1;
    }
    static int l_GetEpisode(lua_State* L){
        Environment* env = ScriptEnvironment(L);
        lua_pushinteger(L, env ? env->Episode() : 0);
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

    // ---- Entities: ids, lookup, spawning, messages ----
    // Scripts refer to other entities by integer id (GetId, Find, Spawn, RayCast,
    // collision callbacks). An id is only meaningful in the scene that issued it.

    static void PushEntityId(lua_State* L, Entity e){
        if (e) { lua_pushinteger(L, (lua_Integer)(uint32_t)e); }
        else { lua_pushnil(L); }
    }
    // The entity for an id argument, or an empty entity if it no longer exists.
    static Entity CheckEntityId(lua_State* L, int arg){
        Entity self = ScriptEntity(L);
        Scene* scene = self.GetScene();
        Entity e{ (entt::entity)(uint32_t)luaL_checkinteger(L, arg), scene };
        if (!scene || !scene->IsValid(e) || scene->IsPendingDestroy((entt::entity)e)) { return {}; }
        return e;
    }
    static bool ToScriptValue(lua_State* L, int idx, ScriptValue& out){
        switch (lua_type(L, idx)){
            case LUA_TNUMBER:  out = (double)lua_tonumber(L, idx); return true;
            case LUA_TBOOLEAN: out = (bool)lua_toboolean(L, idx); return true;
            case LUA_TSTRING:  out = std::string(lua_tostring(L, idx)); return true;
            default: return false;
        }
    }
    static void PushScriptValue(lua_State* L, const ScriptValue& v){
        if (const double* d = std::get_if<double>(&v)) { lua_pushnumber(L, *d); }
        else if (const bool* b = std::get_if<bool>(&v)) { lua_pushboolean(L, *b); }
        else { lua_pushstring(L, std::get<std::string>(v).c_str()); }
    }

    static int l_GetId(lua_State* L){
        PushEntityId(L, ScriptEntity(L));
        return 1;
    }
    static int l_Find(lua_State* L){
        Scene* scene = ScriptEntity(L).GetScene();
        PushEntityId(L, scene ? scene->FindEntityByTag(luaL_checkstring(L, 1)) : Entity{});
        return 1;
    }
    static int l_GetPositionOf(lua_State* L){
        Entity e = CheckEntityId(L, 1);
        if (!e || !e.HasComponent<TransformComponent>()) { lua_pushnil(L); return 1; }
        const glm::vec3& t = e.GetComponent<TransformComponent>().Translation;
        lua_pushnumber(L, t.x); lua_pushnumber(L, t.y); lua_pushnumber(L, t.z);
        return 3;
    }
    static int l_GetTagOf(lua_State* L){
        Entity e = CheckEntityId(L, 1);
        if (!e || !e.HasComponent<TagComponent>()) { lua_pushnil(L); return 1; }
        lua_pushstring(L, e.GetComponent<TagComponent>().Tag.c_str());
        return 1;
    }
    // Spawn(templateTag, x, y, z [, args]) -> id (or nil)
    static int l_Spawn(lua_State* L){
        Scene* scene = ScriptEntity(L).GetScene();
        const char* tag = luaL_checkstring(L, 1);
        glm::vec3 pos = CheckVec3(L, 2);
        ScriptArgs args;
        if (lua_istable(L, 5)){
            lua_pushnil(L);
            while (lua_next(L, 5)){
                ScriptValue v;
                if (lua_type(L, -2) == LUA_TSTRING && ToScriptValue(L, -1, v)) { args[lua_tostring(L, -2)] = v; }
                lua_pop(L, 1);
            }
        }
        PushEntityId(L, scene ? scene->Spawn(tag, pos, args) : Entity{});
        return 1;
    }
    // Destroy([id]): removes this entity (or the given one) at the end of the frame.
    static int l_Destroy(lua_State* L){
        Entity self = ScriptEntity(L);
        Scene* scene = self.GetScene();
        Entity target = lua_isnoneornil(L, 1) ? self : CheckEntityId(L, 1);
        if (scene && target) { scene->QueueDestroy(target); }
        return 0;
    }
    // Send(id, name, ...): calls OnMessage(name, ..., senderId) in the target's scripts.
    static int l_Send(lua_State* L){
        Entity self = ScriptEntity(L);
        Scene* scene = self.GetScene();
        Entity target = CheckEntityId(L, 1);
        std::string name = luaL_checkstring(L, 2);
        std::vector<ScriptValue> args;
        for (int i = 3; i <= lua_gettop(L) && args.size() < 8; i++){
            ScriptValue v;
            if (ToScriptValue(L, i, v)) { args.push_back(v); }
            else { args.push_back(false); } // nil and tables arrive as false
        }
        if (scene && target) { scene->SendScriptMessage(target, name, args, self); }
        return 0;
    }

    // ---- On-screen text ----

    static int l_ShowText(lua_State* L){
        Scene* scene = ScriptEntity(L).GetScene();
        const char* key = luaL_checkstring(L, 1);
        size_t len = 0;
        const char* text = luaL_tolstring(L, 2, &len); // any value, like print
        std::string str(text, len);
        lua_pop(L, 1);
        glm::vec3 color(1.0f);
        if (lua_gettop(L) >= 5) { color = CheckVec3(L, 3); }
        if (scene) { scene->ShowHudText(key, str, color); }
        return 0;
    }
    static int l_HideText(lua_State* L){
        if (Scene* scene = ScriptEntity(L).GetScene()) { scene->HideHudText(luaL_checkstring(L, 1)); }
        return 0;
    }
    static int l_ShowBanner(lua_State* L){
        Scene* scene = ScriptEntity(L).GetScene();
        const char* text = luaL_checkstring(L, 1);
        float seconds = lua_gettop(L) >= 2 ? (float)luaL_checknumber(L, 2) : 2.0f;
        if (scene) { scene->ShowBanner(text, seconds); }
        return 0;
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
        { "OnCollisionEnter", nullptr, "Callbacks", "function OnCollisionEnter(otherTag, otherId, px, py, pz, nx, ny, nz, speed)", "Called when this entity's body starts touching another (needs a Rigid Body): contact point, normal (towards the other body) and impact speed." },
        { "OnCollisionExit",  nullptr, "Callbacks", "function OnCollisionExit(otherTag, otherId)", "Called when this entity's body stops touching another." },
        { "OnTriggerEnter",   nullptr, "Callbacks", "function OnTriggerEnter(otherTag, otherId)", "Called when a sensor body starts overlapping another (falls back to OnCollisionEnter if undefined)." },
        { "OnTriggerExit",    nullptr, "Callbacks", "function OnTriggerExit(otherTag, otherId)", "Called when a sensor overlap ends." },
        { "OnJointBreak",     nullptr, "Callbacks", "function OnJointBreak(otherTag)", "Called when a breakable joint on or to this entity snaps." },
        { "OnMessage",        nullptr, "Callbacks", "function OnMessage(name, ...)", "Called when another script uses Send(id, name, ...); the last argument is the sender's id." },
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

        { "ApplyImpulse",        l_ApplyImpulse,        "Physics", "ApplyImpulse(x, y, z)", "Instant change of momentum at the centre of mass (needs a Rigid Body, while playing)." },
        { "ApplyImpulseAtPoint", l_ApplyImpulseAtPoint, "Physics", "ApplyImpulseAtPoint(x, y, z, px, py, pz)", "Impulse at a world point (also spins the body)." },
        { "ApplyAngularImpulse", l_ApplyAngularImpulse, "Physics", "ApplyAngularImpulse(x, y, z)", "Instant change of angular momentum." },
        { "ApplyForce",          l_ApplyForce,          "Physics", "ApplyForce(x, y, z)", "Continuous force for this step." },
        { "ApplyForceAtPoint",   l_ApplyForceAtPoint,   "Physics", "ApplyForceAtPoint(x, y, z, px, py, pz)", "Force at a world point for this step." },
        { "ApplyTorque",         l_ApplyTorque,         "Physics", "ApplyTorque(x, y, z)", "Torque (N m) for this step." },
        { "SetVelocity",         l_SetVelocity,         "Physics", "SetVelocity(x, y, z)", "Sets the linear velocity." },
        { "GetVelocity",         l_GetVelocity,         "Physics", "GetVelocity() -> x, y, z", "Current linear velocity." },
        { "SetAngularVelocity",  l_SetAngularVelocity,  "Physics", "SetAngularVelocity(x, y, z)", "Sets the spin (radians/second)." },
        { "GetAngularVelocity",  l_GetAngularVelocity,  "Physics", "GetAngularVelocity() -> x, y, z", "Current spin (radians/second)." },
        { "GetMass",             l_GetMass,             "Physics", "GetMass() -> kg", "Mass of a dynamic body (0 otherwise)." },
        { "SetGravityFactor",    l_SetGravityFactor,    "Physics", "SetGravityFactor(factor)", "0 floats, 1 normal, >1 falls faster." },
        { "SetFriction",         l_SetFriction,         "Physics", "SetFriction(value)", "Surface friction." },
        { "SetRestitution",      l_SetRestitution,      "Physics", "SetRestitution(value)", "Bounciness (0 dead .. 1 superball)." },
        { "WakeUp",              l_WakeUp,              "Physics", "WakeUp()", "Wakes a sleeping body." },
        { "IsSleeping",          l_IsSleeping,          "Physics", "IsSleeping() -> bool", "True while the body is asleep (at rest)." },
        { "Teleport",            l_Teleport,            "Physics", "Teleport(x, y, z [, rx, ry, rz])", "Moves the entity and its body/character (SetTranslation on a dynamic body is overwritten by the simulation)." },
        { "RayCast",             l_RayCast,             "Physics", "RayCast(ox, oy, oz, dx, dy, dz, maxDist [, layerMask]) -> tag, hx, hy, hz, id, nx, ny, nz, dist", "First body along the ray (never the caller or sensors), or nil." },
        { "SphereCast",          l_SphereCast,          "Physics", "SphereCast(ox, oy, oz, dx, dy, dz, radius, maxDist [, layerMask]) -> same as RayCast", "Sweeps a sphere along the ray." },
        { "OverlapSphere",       l_OverlapSphere,       "Physics", "OverlapSphere(x, y, z, r [, layerMask]) -> { tags }", "Tags of the bodies inside a sphere." },
        { "OverlapBox",          l_OverlapBox,          "Physics", "OverlapBox(x, y, z, hx, hy, hz [, layerMask]) -> { tags }", "Tags of the bodies inside an axis-aligned box." },

        { "Move",       l_Move,       "Character", "Move(vx, vz)", "Desired horizontal velocity (m/s, world space) of the Character Controller until changed." },
        { "Jump",       l_Jump,       "Character", "Jump([speed])", "Jumps on the next step if grounded." },
        { "IsGrounded", l_IsGrounded, "Character", "IsGrounded() -> bool", "True while standing on something." },

        { "SetMotor",       l_SetMotor,       "Joints", "SetMotor(mode, target [, joint])", "mode \"off\" | \"velocity\" | \"position\" for a hinge/slider joint (joint number is 1-based)." },
        { "SetMotorTarget", l_SetMotorTarget, "Joints", "SetMotorTarget(target [, joint])", "Changes the motor's target velocity/position." },
        { "GetJointValue",  l_GetJointValue,  "Joints", "GetJointValue([joint]) -> value", "Hinge angle (rad), slider position or distance (m)." },
        { "BreakJoint",     l_BreakJoint,     "Joints", "BreakJoint([joint])", "Removes the joint." },
        { "IsJointBroken",  l_IsJointBroken,  "Joints", "IsJointBroken([joint]) -> bool", "True once the joint has snapped or been broken." },
        { "GetJointCount",  l_GetJointCount,  "Joints", "GetJointCount() -> n", "Number of joints on this entity." },

        { "Ignite",      l_Ignite,      "Fire", "Ignite()", "Lights this entity's fire." },
        { "Extinguish",  l_Extinguish,  "Fire", "Extinguish()", "Puts this entity's fire out (it dies down over a moment)." },
        { "IsBurning",   l_IsBurning,   "Fire", "IsBurning() -> bool", "True while the fire burns (water puts it out; a flame touching it lights it)." },
        { "SetFireWind", l_SetFireWind, "Fire", "SetFireWind(x, y, z)", "Wind blowing on the flame (m/s, world)." },

        { "SetVehicleInput", l_SetVehicleInput, "Vehicle", "SetVehicleInput(forward, right [, brake, handbrake])", "forward/right in -1..1, brakes in 0..1; kept until changed." },
        { "GetVehicleSpeed", l_GetVehicleSpeed, "Vehicle", "GetVehicleSpeed() -> m/s", "Speed along the car's forward axis." },
        { "GetEngineRPM",    l_GetEngineRPM,    "Vehicle", "GetEngineRPM() -> rpm, gear", "Engine speed and current gear." },

        { "SetRagdoll",     l_SetRagdoll,     "Ragdoll", "SetRagdoll(on)", "Go limp from the current pose, or back to animation." },
        { "IsRagdoll",      l_IsRagdoll,      "Ragdoll", "IsRagdoll() -> bool", "True while ragdolling." },
        { "RagdollImpulse", l_RagdollImpulse, "Ragdoll", "RagdollImpulse(vx, vy, vz)", "Velocity change for every ragdoll part (a hit, a blast)." },

        { "OnEpisodeBegin",      nullptr, "Callbacks", "function OnEpisodeBegin()", "RL agents: called when an episode starts; reset the agent and its goal here." },
        { "CollectObservations", nullptr, "Callbacks", "function CollectObservations()", "RL agents: called each decision step; report the state with AddObservation." },
        { "OnAction",            nullptr, "Callbacks", "function OnAction(action)", "RL agents: called with the action chosen by the policy (a table of numbers)." },
        { "Heuristic",           nullptr, "Callbacks", "function Heuristic() -> a1, a2, ...", "RL agents: returns an action when no policy is connected (manual control)." },

        { "AddReward",           l_AddReward,           "Reinforcement learning", "AddReward(value)", "Adds to this agent's reward for the current step." },
        { "EndEpisode",          l_EndEpisode,          "Reinforcement learning", "EndEpisode()", "Ends this agent's episode (it resets and OnEpisodeBegin runs)." },
        { "AddObservation",      l_AddObservation,      "Reinforcement learning", "AddObservation(v1, v2, ...)", "Appends numbers to the observation, inside CollectObservations()." },
        { "GetEpisodeStep",      l_GetEpisodeStep,      "Reinforcement learning", "GetEpisodeStep() -> n", "Steps taken in this agent's current episode." },
        { "GetCumulativeReward", l_GetCumulativeReward, "Reinforcement learning", "GetCumulativeReward() -> value", "Total reward this agent has earned in the current episode." },
        { "GetEpisode",          l_GetEpisode,          "Reinforcement learning", "GetEpisode() -> n", "Index of the environment's current episode." },

        { "IsKeyDown",    l_IsKeyDown,    "Input", "IsKeyDown(key) -> bool", "True while the key is held, e.g. IsKeyDown(Key.W)." },
        { "IsKeyPressed", l_IsKeyPressed, "Input", "IsKeyPressed(key) -> bool", "True only on the frame the key goes down." },

        { "SetShared", l_SetShared, "Shared", "SetShared(key, value)", "Publishes a number, boolean or string to every script (nil removes it). Cleared on Run." },
        { "GetShared", l_GetShared, "Shared", "GetShared(key [, default]) -> value", "Reads a value another script published, or default (nil) if unset." },

        { "GetId",         l_GetId,         "Entities", "GetId() -> id", "This entity's id, for Send / Find results / comparisons." },
        { "Find",          l_Find,          "Entities", "Find(tag) -> id", "The first entity with this tag, or nil." },
        { "GetPositionOf", l_GetPositionOf, "Entities", "GetPositionOf(id) -> x, y, z", "Position of another entity, or nil if it's gone." },
        { "GetTagOf",      l_GetTagOf,      "Entities", "GetTagOf(id) -> tag", "Tag (name) of another entity, or nil if it's gone." },
        { "Spawn",         l_Spawn,         "Entities", "Spawn(templateTag, x, y, z [, args]) -> id", "Copies the entity with that tag (shape, physics, scripts) to a position while playing. The copy's scripts see args as the global SPAWN table (nil in the template); args.yaw sets its Y rotation." },
        { "Destroy",       l_Destroy,       "Entities", "Destroy([id])", "Removes this entity (or the given one) at the end of the frame." },
        { "Send",          l_Send,          "Entities", "Send(id, name, ...)", "Calls OnMessage(name, ..., senderId) in the other entity's scripts (numbers, booleans, strings)." },

        { "ShowText",   l_ShowText,   "Screen", "ShowText(key, text [, r, g, b])", "Shows (or updates) a line of text in the viewport while playing." },
        { "HideText",   l_HideText,   "Screen", "HideText(key)", "Removes a line shown with ShowText." },
        { "ShowBanner", l_ShowBanner, "Screen", "ShowBanner(text [, seconds])", "A large centred message (default 2 seconds)." },

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
        };
        static const char* letters[] = { "A","B","C","D","E","F","G","H","I","J","K","L","M",
                                         "N","O","P","Q","R","S","T","U","V","W","X","Y","Z" };
        for (int i = 0; i < 26; i++) { keys.push_back({ letters[i], Key::A + i }); }
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

    bool LuaScript::ProtectedCall(int nargs, const char* what, int nresults){
        m_HookTicks = 0;
        if (lua_pcall(m_L, nargs, nresults, 0) != LUA_OK){
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

    LuaScript::LuaScript(const std::string& source, Entity entity, int index, const std::string& name,
                         const ScriptArgs* args)
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

        // Spawned copies get their spawn arguments; templates (and placed entities) don't.
        if (args){
            lua_newtable(m_L);
            for (const auto& [key, value] : *args){
                PushScriptValue(m_L, value);
                lua_setfield(m_L, -2, key.c_str());
            }
            lua_setglobal(m_L, "SPAWN");
        }

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

    bool LuaScript::PushCallback(const char* name){
        lua_getglobal(m_L, name);
        if (lua_isfunction(m_L, -1)) { return true; }
        lua_pop(m_L, 1);
        return false;
    }

    void LuaScript::OnCollision(const std::string& otherTag, bool entered, Entity other,
                                const glm::vec3& point, const glm::vec3& normal, float speed){
        if (!m_Valid) { return; }
        const char* name = entered ? "OnCollisionEnter" : "OnCollisionExit";
        if (!PushCallback(name)) { return; }
        lua_pushstring(m_L, otherTag.c_str());
        PushEntityId(m_L, other);
        if (!entered) { ProtectedCall(2, name); return; }
        PushVec3(m_L, point);
        PushVec3(m_L, normal);
        lua_pushnumber(m_L, speed);
        ProtectedCall(9, name);
    }

    void LuaScript::OnTrigger(const std::string& otherTag, bool entered, Entity other){
        if (!m_Valid) { return; }
        const char* name = entered ? "OnTriggerEnter" : "OnTriggerExit";
        // Scripts written before triggers existed handle them in OnCollisionEnter/Exit.
        if (!PushCallback(name)){
            name = entered ? "OnCollisionEnter" : "OnCollisionExit";
            if (!PushCallback(name)) { return; }
        }
        lua_pushstring(m_L, otherTag.c_str());
        PushEntityId(m_L, other);
        ProtectedCall(2, name);
    }

    void LuaScript::OnJointBreak(const std::string& otherTag){
        if (!m_Valid || !PushCallback("OnJointBreak")) { return; }
        lua_pushstring(m_L, otherTag.c_str());
        ProtectedCall(1, "OnJointBreak");
    }

    void LuaScript::OnMessage(const std::string& name, const std::vector<ScriptValue>& args, Entity sender){
        if (!m_Valid) { return; }
        lua_getglobal(m_L, "OnMessage");
        if (lua_isfunction(m_L, -1)){
            lua_pushstring(m_L, name.c_str());
            for (const ScriptValue& v : args) { PushScriptValue(m_L, v); }
            PushEntityId(m_L, sender);
            ProtectedCall((int)args.size() + 2, "OnMessage");
        }
        else{
            lua_pop(m_L, 1);
        }
    }

    void LuaScript::OnEpisodeBegin(){
        if (!m_Valid) { return; }
        lua_getglobal(m_L, "OnEpisodeBegin");
        if (lua_isfunction(m_L, -1)) { ProtectedCall(0, "OnEpisodeBegin"); }
        else                         { lua_pop(m_L, 1); }
    }

    void LuaScript::OnAction(const std::vector<float>& action){
        if (!m_Valid) { return; }
        lua_getglobal(m_L, "OnAction");
        if (!lua_isfunction(m_L, -1)) { lua_pop(m_L, 1); return; }
        lua_createtable(m_L, (int)action.size(), 0);
        for (size_t i = 0; i < action.size(); i++){
            lua_pushnumber(m_L, action[i]);
            lua_rawseti(m_L, -2, (lua_Integer)i + 1);
        }
        ProtectedCall(1, "OnAction");
    }

    void LuaScript::CollectObservations(){
        if (!m_Valid) { return; }
        lua_getglobal(m_L, "CollectObservations");
        if (lua_isfunction(m_L, -1)) { ProtectedCall(0, "CollectObservations"); }
        else                         { lua_pop(m_L, 1); }
    }

    bool LuaScript::Heuristic(std::vector<float>& outAction){
        if (!m_Valid) { return false; }
        lua_getglobal(m_L, "Heuristic");
        if (!lua_isfunction(m_L, -1)) { lua_pop(m_L, 1); return false; }
        int base = lua_gettop(m_L) - 1;
        if (!ProtectedCall(0, "Heuristic", LUA_MULTRET)) { return false; }
        int results = lua_gettop(m_L) - base;
        outAction.clear();
        for (int i = 1; i <= results; i++) { outAction.push_back((float)lua_tonumber(m_L, base + i)); }
        lua_settop(m_L, base);
        return true;
    }
}
