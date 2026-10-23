// The bridge's physics API: lets a client (python/phoenix_env.py, PhoenixPhysics)
// inspect and drive the rigid bodies of the playing scene directly.
//
//   physics_info                              -> gravity, timestep, sim time, body count
//   set_gravity  {gravity}
//   bodies       {tag?, all?}                 -> every rigid body (all: every entity)
//   body         {entity}                     -> one body's state
//   set_body     {entity, position?, rotation?, velocity?, angular_velocity?}
//   apply_force / apply_impulse  {entity, force|impulse, point?}
//   apply_torque {entity, torque}  apply_angular_impulse {entity, impulse}
//   set_material {entity, friction?, restitution?}
//   raycast      {origin, direction, max_distance?, ignore?} -> hit entity, point, normal
//   contacts                                  -> contacts begun/ended since the last call
//   simulate     {steps?}                     -> advance physics without agent actions
//
// Entities are addressed by id (from `bodies`) or by tag (first match). Vectors are
// [x, y, z]; rotations are XYZ Euler angles in radians, as in the editor.
#include <Phoenix/AI/Bridge.h>
#include <Phoenix/AI/Environment.h>
#include <Phoenix/Physics/Physics.h>
#include <Phoenix/Scene/Component.h>
#include <Phoenix/Scene/Scene.h>
#include <yaml-cpp/yaml.h>
#include "Json.h"

namespace Phoenix{

    namespace {
        bool ReadVec3(const YAML::Node& node, glm::vec3& out){
            if (!node || !node.IsSequence() || node.size() != 3) { return false; }
            out = { node[0].as<float>(), node[1].as<float>(), node[2].as<float>() };
            return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
        }

        // Entity by numeric id or by tag (first match).
        entt::entity Resolve(entt::registry& registry, const YAML::Node& node){
            if (!node || !node.IsScalar()) { return entt::null; }
            const std::string text = node.as<std::string>();
            bool numeric = !text.empty() && text.find_first_not_of("0123456789") == std::string::npos;
            if (numeric){
                auto e = (entt::entity)std::stoul(text);
                return registry.valid(e) ? e : entt::null;
            }
            auto view = registry.view<TagComponent>();
            for (auto e : view)
                if (view.get<TagComponent>(e).Tag == text) { return e; }
            return entt::null;
        }

        uint32_t BodyOf(entt::registry& registry, entt::entity e){
            auto* rb = registry.try_get<RigidBodyComponent>(e);
            return rb ? rb->runtimeBodyID : PhysicsWorld::InvalidBody;
        }

        const char* TypeName(entt::registry& registry, entt::entity e){
            auto* rb = registry.try_get<RigidBodyComponent>(e);
            if (!rb) { return "none"; }
            switch (rb->type){
                case RigidBodyComponent::Type::Static:    return "static";
                case RigidBodyComponent::Type::Dynamic:   return "dynamic";
                case RigidBodyComponent::Type::Kinematic: return "kinematic";
            }
            return "none";
        }

        void AppendBody(std::string& out, entt::registry& registry, PhysicsWorld* physics, entt::entity e){
            const auto* t = registry.try_get<TransformComponent>(e);
            uint32_t body = BodyOf(registry, e);
            bool live = physics && body != PhysicsWorld::InvalidBody;
            out += "{\"id\":" + std::to_string((uint32_t)e) + ",\"tag\":";
            AppendString(out, registry.all_of<TagComponent>(e) ? registry.get<TagComponent>(e).Tag : std::string());
            out += ",\"type\":\"";
            out += TypeName(registry, e);
            out += "\",\"position\":";
            AppendVec3(out, t ? t->Translation : glm::vec3(0.0f));
            out += ",\"rotation\":";
            AppendVec3(out, t ? t->Rotation : glm::vec3(0.0f));
            out += ",\"scale\":";
            AppendVec3(out, t ? t->Scale : glm::vec3(1.0f));
            out += ",\"velocity\":";
            AppendVec3(out, live ? physics->GetLinearVelocity(body) : glm::vec3(0.0f));
            out += ",\"angular_velocity\":";
            AppendVec3(out, live ? physics->GetAngularVelocity(body) : glm::vec3(0.0f));
            out += ",\"mass\":";
            AppendNumber(out, live ? physics->GetMass(body) : 0.0f);
            if (const auto* rb = registry.try_get<RigidBodyComponent>(e)){
                out += ",\"friction\":";
                AppendNumber(out, rb->friction);
                out += ",\"restitution\":";
                AppendNumber(out, rb->restitution);
            }
            out += '}';
        }
    }

    bool Bridge::HandlePhysics(const std::string& cmd, const YAML::Node& msg, Environment& env, std::string& reply){
        Scene& scene = *env.GetScene();
        entt::registry& registry = scene.m_Registry;
        PhysicsWorld* physics = scene.GetPhysicsWorld();
        if (!physics){
            reply = ErrorJson("the scene isn't playing");
            return true;
        }

        try{
            if (cmd == "physics_info"){
                int bodies = 0;
                for (auto e : registry.view<RigidBodyComponent>())
                    if (BodyOf(registry, e) != PhysicsWorld::InvalidBody) { bodies++; }
                reply = "{\"ok\":true,\"gravity\":";
                AppendVec3(reply, physics->GetGravity());
                reply += ",\"timestep\":";
                AppendNumber(reply, scene.Simulation().fixedTimestep);
                reply += ",\"time\":";
                AppendNumber(reply, scene.GetSimulationTime());
                reply += ",\"bodies\":" + std::to_string(bodies) + "}";
                return true;
            }
            if (cmd == "set_gravity"){
                glm::vec3 g;
                if (!ReadVec3(msg["gravity"], g)) { reply = ErrorJson("gravity must be [x, y, z]"); return true; }
                physics->SetGravity(g);
                reply = "{\"ok\":true}";
                return true;
            }
            if (cmd == "bodies"){
                const bool all = msg["all"] && msg["all"].as<bool>(false);
                const std::string tag = msg["tag"] ? msg["tag"].as<std::string>("") : "";
                reply = "{\"ok\":true,\"bodies\":[";
                bool first = true;
                for (auto e : registry.view<TransformComponent>()){
                    if (!all && !registry.all_of<RigidBodyComponent>(e)) { continue; }
                    if (!tag.empty() && (!registry.all_of<TagComponent>(e) || registry.get<TagComponent>(e).Tag != tag)) { continue; }
                    if (!first) { reply += ','; }
                    first = false;
                    AppendBody(reply, registry, physics, e);
                }
                reply += "]}";
                return true;
            }
            if (cmd == "contacts"){
                reply = "{\"ok\":true,\"contacts\":[";
                bool first = true;
                for (const auto& c : scene.DrainContacts()){
                    if (!registry.valid(c.a) || !registry.valid(c.b)) { continue; }
                    if (!first) { reply += ','; }
                    first = false;
                    reply += "{\"a\":" + std::to_string((uint32_t)c.a) + ",\"b\":" + std::to_string((uint32_t)c.b);
                    reply += ",\"tag_a\":";
                    AppendString(reply, registry.all_of<TagComponent>(c.a) ? registry.get<TagComponent>(c.a).Tag : std::string());
                    reply += ",\"tag_b\":";
                    AppendString(reply, registry.all_of<TagComponent>(c.b) ? registry.get<TagComponent>(c.b).Tag : std::string());
                    reply += c.entered ? ",\"entered\":true" : ",\"entered\":false";
                    reply += ",\"time\":";
                    AppendNumber(reply, c.time);
                    reply += '}';
                }
                reply += "]}";
                return true;
            }
            if (cmd == "simulate"){
                int steps = msg["steps"] ? msg["steps"].as<int>(1) : 1;
                if (steps < 0 || steps > 100000) { reply = ErrorJson("steps must be in [0, 100000]"); return true; }
                env.Simulate(steps);
                reply = "{\"ok\":true,\"time\":";
                AppendNumber(reply, scene.GetSimulationTime());
                reply += "}";
                return true;
            }
            if (cmd == "raycast"){
                glm::vec3 origin, direction;
                if (!ReadVec3(msg["origin"], origin) || !ReadVec3(msg["direction"], direction) || glm::length(direction) < 1e-6f){
                    reply = ErrorJson("origin and direction must be [x, y, z] (direction non-zero)");
                    return true;
                }
                float maxDistance = msg["max_distance"] ? msg["max_distance"].as<float>(100.0f) : 100.0f;
                uint32_t ignore = PhysicsWorld::InvalidBody;
                if (msg["ignore"]){
                    entt::entity e = Resolve(registry, msg["ignore"]);
                    if (e != entt::null) { ignore = BodyOf(registry, e); }
                }
                PhysicsWorld::RayHit hit;
                if (!physics->RayCast(origin, glm::normalize(direction), std::max(0.0f, maxDistance), hit, ignore)){
                    reply = "{\"ok\":true,\"hit\":false}";
                    return true;
                }
                Entity entity = scene.FindEntityByBodyID(hit.body);
                reply = "{\"ok\":true,\"hit\":true,\"entity\":";
                reply += entity ? std::to_string((uint32_t)(entt::entity)entity) : "null";
                reply += ",\"tag\":";
                AppendString(reply, entity && entity.HasComponent<TagComponent>() ? entity.GetComponent<TagComponent>().Tag : std::string());
                reply += ",\"point\":";
                AppendVec3(reply, hit.point);
                reply += ",\"normal\":";
                AppendVec3(reply, hit.normal);
                reply += ",\"distance\":";
                AppendNumber(reply, hit.distance);
                reply += "}";
                return true;
            }

            // ---- Commands on one entity ----
            static const char* const entityCommands[] = {
                "body", "set_body", "apply_force", "apply_impulse", "apply_torque", "apply_angular_impulse", "set_material",
            };
            bool isEntityCommand = false;
            for (const char* name : entityCommands) { if (cmd == name) { isEntityCommand = true; } }
            if (!isEntityCommand) { return false; }

            entt::entity e = Resolve(registry, msg["entity"]);
            if (e == entt::null){ reply = ErrorJson("no entity matches 'entity' (an id or a tag)"); return true; }
            uint32_t body = BodyOf(registry, e);

            if (cmd == "body"){
                reply = "{\"ok\":true,\"body\":";
                AppendBody(reply, registry, physics, e);
                reply += "}";
                return true;
            }
            if (cmd == "set_body"){
                auto* t = registry.try_get<TransformComponent>(e);
                if (!t) { reply = ErrorJson("entity has no transform"); return true; }
                glm::vec3 v;
                bool moved = false;
                if (ReadVec3(msg["position"], v)) { t->Translation = v; moved = true; }
                if (ReadVec3(msg["rotation"], v)) { t->Rotation = v; moved = true; }
                if (body != PhysicsWorld::InvalidBody){
                    if (moved) { physics->SetBodyTransform(body, t->Translation, t->Rotation); }
                    if (physics->IsDynamic(body)){
                        if (ReadVec3(msg["velocity"], v))         { physics->SetLinearVelocity(body, v); }
                        if (ReadVec3(msg["angular_velocity"], v)) { physics->SetAngularVelocity(body, v); }
                    }
                }
                reply = "{\"ok\":true,\"body\":";
                AppendBody(reply, registry, physics, e);
                reply += "}";
                return true;
            }
            if (cmd == "set_material"){
                auto* rb = registry.try_get<RigidBodyComponent>(e);
                if (!rb) { reply = ErrorJson("entity has no rigid body"); return true; }
                if (msg["friction"])    { rb->friction = std::max(0.0f, msg["friction"].as<float>());       physics->SetFriction(body, rb->friction); }
                if (msg["restitution"]) { rb->restitution = std::clamp(msg["restitution"].as<float>(), 0.0f, 1.0f); physics->SetRestitution(body, rb->restitution); }
                reply = "{\"ok\":true}";
                return true;
            }

            // Forces and impulses need a simulated (dynamic) body.
            if (body == PhysicsWorld::InvalidBody || !physics->IsDynamic(body)){
                reply = ErrorJson("forces need a dynamic rigid body");
                return true;
            }
            glm::vec3 value, point;
            const char* key = (cmd == "apply_force") ? "force" : (cmd == "apply_torque") ? "torque" : "impulse";
            if (!ReadVec3(msg[key], value)){ reply = ErrorJson(std::string(key) + " must be [x, y, z]"); return true; }
            bool atPoint = ReadVec3(msg["point"], point);
            if (cmd == "apply_force")        { atPoint ? physics->ApplyForceAtPoint(body, value, point) : physics->ApplyForce(body, value); }
            else if (cmd == "apply_impulse") { atPoint ? physics->ApplyImpulseAtPoint(body, value, point) : physics->ApplyImpulse(body, value); }
            else if (cmd == "apply_torque")  { physics->ApplyTorque(body, value); }
            else                             { physics->ApplyAngularImpulse(body, value); }
            reply = "{\"ok\":true}";
            return true;
        }
        catch (const std::exception& e){
            reply = ErrorJson(std::string("bad arguments: ") + e.what());
            return true;
        }
    }
}
