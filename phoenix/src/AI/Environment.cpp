#include <Phoenix/AI/Environment.h>
#include <Phoenix/Scene/Scene.h>
#include <Phoenix/Scene/Entity.h>
#include <Phoenix/Scene/LuaScript.h>
#include <Phoenix/Physics/Physics.h>
#include <Phoenix/core/Input.h>
#include <Phoenix/core/keyCodes.h>
#include <Phoenix/core/log.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace Phoenix{

    static double NowSeconds(){
        using namespace std::chrono;
        return duration<double>(steady_clock::now().time_since_epoch()).count();
    }

    glm::vec3 Environment::Forward(float yaw){
        // Engine forward is local -Z rotated by the yaw (see Lua MoveForward).
        return { -std::sin(yaw), 0.0f, -std::cos(yaw) };
    }

    static glm::vec3 RightOf(float yaw){
        return { std::cos(yaw), 0.0f, -std::sin(yaw) };
    }

    void Environment::SensorRays(const glm::vec3& position, float yaw, const RaySensorComponent& sensor,
                                 std::vector<glm::vec3>& outDirections, glm::vec3& outOrigin){
        outOrigin = position + glm::vec3(0.0f, sensor.heightOffset, 0.0f);
        int n = std::max(1, sensor.rays);
        outDirections.resize((size_t)n);
        float fov = glm::radians(std::clamp(sensor.fov, 0.0f, 360.0f));
        for (int i = 0; i < n; i++){
            // Spread from left (+) to right (-) across the fov; a single ray looks straight ahead.
            float offset = (n == 1) ? 0.0f : fov * 0.5f - fov * (float)i / (float)(n - 1);
            outDirections[i] = Forward(yaw + offset);
        }
    }

    static bool InsideZone(const glm::vec3& point, const glm::mat4& model, const glm::vec3& halfExtents){
        glm::vec3 p = glm::vec3(glm::inverse(model) * glm::vec4(point, 1.0f));
        return std::abs(p.x) <= halfExtents.x && std::abs(p.y) <= halfExtents.y && std::abs(p.z) <= halfExtents.z;
    }

    void Environment::SyncRayBackend(){
        if (gpuRaySensors == m_RayBackendRequest) { return; }
        m_RayBackendRequest = gpuRaySensors;
        bool on = m_RayCaster.UseOpenCL(gpuRaySensors);
        if (on) { PHX_CORE_INFO("Ray sensors: OpenCL on {}", m_RayCaster.Status()); }
        else if (gpuRaySensors) { PHX_CORE_WARN("Ray sensors: OpenCL unavailable ({}), using the CPU", m_RayCaster.Status()); }
    }

    // Whether the segment origin + t * delta (t in [0, 1]) touches the box (with a
    // millimetre of slack, so it never skips a body Jolt would hit).
    static bool SegmentTouchesBounds(const glm::vec3& origin, const glm::vec3& delta, const glm::vec3& lo, const glm::vec3& hi){
        float tMin = 0.0f, tMax = 1.0f;
        for (int i = 0; i < 3; i++){
            const float a = lo[i] - 1e-3f, b = hi[i] + 1e-3f;
            if (std::abs(delta[i]) < 1e-12f){
                if (origin[i] < a || origin[i] > b) { return false; }
                continue;
            }
            float t1 = (a - origin[i]) / delta[i], t2 = (b - origin[i]) / delta[i];
            if (t1 > t2) { std::swap(t1, t2); }
            tMin = std::max(tMin, t1);
            tMax = std::min(tMax, t2);
            if (tMin > tMax) { return false; }
        }
        return true;
    }

    // A Jolt leaf shape as a batch-caster shape (local frame, size, bounding sphere).
    static RayBatchCaster::Shape MirrorShape(const PhysicsWorld::RayPrimitive& p){
        using Type = PhysicsWorld::RayPrimitive::Type;
        RayBatchCaster::Shape shape;
        switch (p.type){
            case Type::Box:
                shape.type = RayBatchCaster::Shape::Type::Box;
                shape.size = p.halfExtents;
                shape.radius = glm::length(p.halfExtents);
                break;
            case Type::Sphere:
                shape.type = RayBatchCaster::Shape::Type::Sphere;
                shape.size = glm::vec3(p.radius, 0.0f, 0.0f);
                shape.radius = p.radius;
                break;
            case Type::Capsule:
                shape.type = RayBatchCaster::Shape::Type::Capsule;
                shape.size = glm::vec3(p.halfHeight, p.radius, 0.0f);
                shape.radius = p.halfHeight + p.radius;
                break;
            case Type::Cylinder:
                shape.type = RayBatchCaster::Shape::Type::Cylinder;
                shape.size = glm::vec3(p.halfHeight, p.radius, 0.0f);
                shape.radius = std::sqrt(p.halfHeight * p.halfHeight + p.radius * p.radius);
                break;
        }
        shape.inverse = glm::inverse(glm::translate(glm::mat4(1.0f), p.position) * glm::mat4_cast(p.rotation));
        shape.center = p.position;
        shape.owner = (int)p.body;
        return shape;
    }

    // Ray Sensor casts for several agents at once, so the OpenCL backend gets enough
    // work. Reward Zones always go through the batch caster. With gpuRaySensors,
    // physics bodies with simple shapes do too (mirrored from Jolt, on the device or,
    // without one, on the CPU with the same results); CollectObservation casts the
    // remaining bodies with Jolt and merges everything.
    void Environment::CastSensors(const std::vector<AgentState*>& agents){
        SyncRayBackend();
        auto& registry = m_Scene->m_Registry;
        PhysicsWorld* physics = m_Scene->GetPhysicsWorld();
        const bool mirror = physics && gpuRaySensors;

        m_ZoneShapes.clear();
        auto zones = registry.view<RewardZoneComponent, TransformComponent>();
        for (auto zone : zones){
            const auto& rz = zones.get<RewardZoneComponent>(zone);
            if (!rz.visibleToRays) { continue; }
            RayBatchCaster::Shape shape;
            shape.type = RayBatchCaster::Shape::Type::Zone;
            const glm::mat4 model = zones.get<TransformComponent>(zone).GetTransform();
            shape.inverse = glm::inverse(model);
            shape.size = rz.halfExtents;
            shape.center = glm::vec3(model[3]);
            shape.radius = rz.halfExtents.x * glm::length(glm::vec3(model[0])) + rz.halfExtents.y * glm::length(glm::vec3(model[1]))
                         + rz.halfExtents.z * glm::length(glm::vec3(model[2]));
            shape.owner = (int)(uint32_t)zone;   // an agent that is itself a zone doesn't see it
            m_ZoneShapes.push_back(shape);
        }

        m_SensorRays.clear();
        m_BodyRays.clear();
        m_SensorGroups.clear();
        m_FanBounds.clear();
        glm::vec3 boundsMin(std::numeric_limits<float>::max()), boundsMax(-std::numeric_limits<float>::max());
        std::vector<glm::vec3> dirs;
        glm::vec3 origin;
        for (AgentState* agent : agents){
            auto* sensor = registry.try_get<RaySensorComponent>(agent->entity);
            if (!sensor) { continue; }
            const auto& t = registry.get<TransformComponent>(agent->entity);
            SensorRays(t.Translation, t.Rotation.y, *sensor, dirs, origin);
            const float length = std::max(0.01f, sensor->length);
            uint32_t body = PhysicsWorld::InvalidBody;
            if (auto* rb = registry.try_get<RigidBodyComponent>(agent->entity)) { body = rb->runtimeBodyID; }
            // Each agent's fan is one culling group (body rays line up with zone rays).
            m_SensorGroups.push_back({ (int)m_SensorRays.size(), (int)dirs.size() });
            glm::vec3 fanMin = origin, fanMax = origin;
            for (const auto& dir : dirs){
                m_SensorRays.push_back({ origin, dir * length, (int)(uint32_t)agent->entity });
                fanMin = glm::min(fanMin, origin + dir * length);
                fanMax = glm::max(fanMax, origin + dir * length);
                if (mirror) { m_BodyRays.push_back({ origin, dir * length, (int)body }); }
            }
            m_FanBounds.push_back(fanMin);
            m_FanBounds.push_back(fanMax);
            boundsMin = glm::min(boundsMin, fanMin);
            boundsMax = glm::max(boundsMax, fanMax);
        }
        m_RayCaster.Cast(m_ZoneShapes, m_SensorRays, m_SensorGroups, 1.0f, m_SensorHits);

        m_BodyShapes.clear();
        m_OtherBodies.clear();
        if (mirror && !m_BodyRays.empty()){
            // Sensors are skipped, as in the Jolt cast (QueryFilter's default).
            const glm::vec3 margin(0.01f);
            physics->CollectRayPrimitives(boundsMin - margin, boundsMax + margin, PhysicsWorld::QueryFilter(),
                                          m_BodyPrimitives, m_OtherBodies);
            using Type = PhysicsWorld::RayPrimitive::Type;
            for (const auto& p : m_BodyPrimitives) { m_BodyShapes.push_back(MirrorShape(p)); }
            // Jolt accepts hits up to its initial fraction, 1 + FLT_EPSILON.
            m_RayCaster.Cast(m_BodyShapes, m_BodyRays, m_SensorGroups, 1.0f + std::numeric_limits<float>::epsilon(), m_BodyHits);
        }

        size_t next = 0, fan = 0;
        for (AgentState* agent : agents){
            agent->zoneHits.clear();
            agent->bodyHits.clear();
            agent->otherBodies.clear();
            agent->sensorHitsReady = false;
            agent->bodiesMirrored = false;
            auto* sensor = registry.try_get<RaySensorComponent>(agent->entity);
            if (!sensor) { continue; }
            for (int r = 0; r < std::max(1, sensor->rays); r++, next++){
                const auto& hit = m_SensorHits[next];
                agent->zoneHits.push_back({ hit.t, hit.shape >= 0 ? (entt::entity)(uint32_t)m_ZoneShapes[(size_t)hit.shape].owner : entt::entity(entt::null) });
                if (!mirror) { continue; }
                const auto& bodyHit = m_BodyHits.empty() ? RayBatchCaster::Hit{} : m_BodyHits[next];
                agent->bodyHits.push_back({ bodyHit.t, bodyHit.shape >= 0 ? (uint32_t)m_BodyShapes[(size_t)bodyHit.shape].owner : PhysicsWorld::InvalidBody });
            }
            // Bodies left to Jolt that this agent's rays could reach.
            const glm::vec3 fanMin = m_FanBounds[fan * 2], fanMax = m_FanBounds[fan * 2 + 1];
            fan++;
            for (const auto& other : m_OtherBodies)
                if (glm::all(glm::lessThanEqual(other.boundsMin, fanMax)) && glm::all(glm::lessThanEqual(fanMin, other.boundsMax)))
                    agent->otherBodies.push_back(other);
            agent->bodiesMirrored = mirror;
            agent->sensorHitsReady = true;
        }
    }

    Environment::Environment(Scene* scene, EpisodeLog* log)
        : m_Scene(scene), m_Log(log) {}

    Environment::~Environment(){
        if (m_Scene && m_Scene->GetEnvironment() == this)
            m_Scene->SetEnvironment(nullptr);
    }

    void Environment::Start(){
        auto& registry = m_Scene->m_Registry;
        int seed = m_Scene->Simulation().seed;
        m_Rng.seed(seed != 0 ? (unsigned)seed : std::random_device{}());

        // Edit-time layout that every episode resets to.
        m_InitialTransforms.clear();
        for (auto entity : registry.view<TransformComponent>())
            m_InitialTransforms[(uint32_t)entity] = registry.get<TransformComponent>(entity);

        m_Agents.clear();
        auto view = registry.view<AgentComponent, TransformComponent>();
        for (auto entity : view){
            const auto& config = view.get<AgentComponent>(entity);
            AgentState agent;
            agent.entity = entity;
            agent.name = registry.all_of<TagComponent>(entity) ? registry.get<TagComponent>(entity).Tag : "Agent";
            agent.policy = config.policy;
            agent.actionSize = config.ActionSize();
            agent.discrete = config.actionType == AgentComponent::ActionType::Discrete;
            BuildObservationLabels(agent);
            m_Agents.push_back(std::move(agent));
        }

        m_Scene->SetEnvironment(this);
        m_Episode = 0;
        m_PhysicsSteps = 0;
        m_Accumulator = 0.0f;
        m_RateWindowStart = NowSeconds();
        m_RateSteps = 0;
        m_RateSimTime = 0.0f;
        ResetEpisode();
    }

    std::vector<std::string> Environment::DescribeObservations(Scene& scene, entt::entity entity){
        auto& registry = scene.m_Registry;
        std::vector<std::string> labels;
        if (!registry.valid(entity) || !registry.all_of<AgentComponent>(entity)) { return labels; }
        const auto& c = registry.get<AgentComponent>(entity);
        if (c.observePosition) { labels.insert(labels.end(), { "position.x", "position.y", "position.z" }); }
        if (c.observeVelocity) { labels.insert(labels.end(), { "velocity.right", "velocity.forward" }); }
        if (c.observeHeading)  { labels.insert(labels.end(), { "heading.sin", "heading.cos" }); }
        if (c.observeTarget)   { labels.insert(labels.end(), { "target.right", "target.forward", "target.distance" }); }
        if (auto* sensor = registry.try_get<RaySensorComponent>(entity)){
            for (int r = 0; r < std::max(1, sensor->rays); r++){
                labels.push_back("ray" + std::to_string(r) + ".distance");
                for (const auto& tag : sensor->detectableTags)
                    labels.push_back("ray" + std::to_string(r) + "." + tag);
            }
        }
        for (int i = 0; i < c.customObservationSize; i++)
            labels.push_back("custom" + std::to_string(i));
        return labels;
    }

    std::vector<std::string> Environment::DescribeActions(const AgentComponent& c){
        const bool discrete = c.actionType == AgentComponent::ActionType::Discrete;
        switch (c.control){
            case AgentComponent::Control::Move:
                if (discrete) { return { "Idle", "Forward", "Back", "Turn left", "Turn right" }; }
                return { "Move", "Turn" };
            case AgentComponent::Control::Force:
                if (discrete) { return { "None", "Push -Z", "Push +Z", "Push -X", "Push +X" }; }
                return { "Force X", "Force Z" };
            case AgentComponent::Control::Custom:
            default:{
                std::vector<std::string> out;
                for (int i = 0; i < c.ActionSize(); i++) { out.push_back((discrete ? "Choice " : "Action ") + std::to_string(i)); }
                return out;
            }
        }
    }

    void Environment::BuildObservationLabels(AgentState& agent){
        agent.observationLabels = DescribeObservations(*m_Scene, agent.entity);
    }

    // The nearest entity carrying the target tag, so several agents in parallel
    // arenas each track their own goal.
    bool Environment::FindTarget(const AgentComponent& config, const glm::vec3& from, glm::vec3& outPosition){
        if (config.targetTag.empty()) { return false; }
        auto& registry = m_Scene->m_Registry;
        auto view = registry.view<TagComponent, TransformComponent>();
        float best = std::numeric_limits<float>::max();
        bool found = false;
        for (auto entity : view){
            if (view.get<TagComponent>(entity).Tag != config.targetTag) { continue; }
            const glm::vec3& p = view.get<TransformComponent>(entity).Translation;
            float d = glm::length(p - from);
            if (d < best){ best = d; outPosition = p; found = true; }
        }
        return found;
    }

    float Environment::TargetDistance(AgentState& agent){
        auto& registry = m_Scene->m_Registry;
        const auto& config = registry.get<AgentComponent>(agent.entity);
        glm::vec3 target;
        const glm::vec3& pos = registry.get<TransformComponent>(agent.entity).Translation;
        if (!FindTarget(config, pos, target)) { return -1.0f; }
        glm::vec3 d = target - pos;
        d.y = 0.0f;
        return glm::length(d);
    }

    void Environment::ResetEpisode(){
        auto& registry = m_Scene->m_Registry;
        PhysicsWorld* physics = m_Scene->GetPhysicsWorld();

        // Edit-time layout, then randomization.
        for (auto entity : registry.view<TransformComponent>()){
            auto it = m_InitialTransforms.find((uint32_t)entity);
            if (it != m_InitialTransforms.end()) { registry.get<TransformComponent>(entity) = it->second; }
        }
        auto randomizers = registry.view<RandomizerComponent, TransformComponent>();
        for (auto entity : randomizers){
            const auto& r = randomizers.get<RandomizerComponent>(entity);
            auto& t = randomizers.get<TransformComponent>(entity);
            for (int i = 0; i < 3; i++){
                float range = std::abs(r.positionRange[i]);
                if (range > 0.0f) { t.Translation[i] += std::uniform_real_distribution<float>(-range, range)(m_Rng); }
            }
            if (r.randomYaw) { t.Rotation.y = std::uniform_real_distribution<float>(0.0f, glm::two_pi<float>())(m_Rng); }
        }

        // Terrains that randomize get a fresh landscape (and collider) every episode.
        for (auto entity : registry.view<TerrainComponent>()){
            auto& terrain = registry.get<TerrainComponent>(entity);
            if (!terrain.randomizeEachEpisode) { continue; }
            TerrainNoise noise = terrain.noise;
            noise.seed = (uint32_t)m_Rng();
            TerrainGenerator::Generate(terrain.resolution, terrain.size, noise, terrain.heights);
            m_Scene->RebuildTerrainBody(entity);
        }

        // Pools start every episode with still water, fluids with their starting blocks.
        for (auto entity : registry.view<PoolComponent>())
            if (auto& water = registry.get<PoolComponent>(entity).water) { water->Reset(); }
        m_Scene->ResetFluids();
        m_Scene->ResetFires();

        // Teleport the physics bodies to match and stop them.
        if (physics){
            auto bodies = registry.view<RigidBodyComponent, TransformComponent>();
            for (auto entity : bodies){
                auto& rb = bodies.get<RigidBodyComponent>(entity);
                if (rb.runtimeBodyID == PhysicsWorld::InvalidBody) { continue; }
                const auto& t = bodies.get<TransformComponent>(entity);
                physics->ResetBody(rb.runtimeBodyID, t.Translation, t.Rotation);
                if (rb.type == RigidBodyComponent::Type::Dynamic && glm::length(rb.initialVelocity) > 0.0f)
                    physics->SetLinearVelocity(rb.runtimeBodyID, rb.initialVelocity);
            }
        }

        // Contacts from the previous episode (and the teleports above) don't belong to this one.
        m_Scene->DrainContacts();

        m_Episode++;
        m_SensorBatch.clear();
        for (auto& agent : m_Agents) { m_SensorBatch.push_back(&agent); }
        CastSensors(m_SensorBatch);
        for (auto& agent : m_Agents) { BeginAgentEpisode(agent); }

        // Scripts (scene logic, custom agents) restart their episode state.
        for (auto entity : registry.view<LuaScriptComponent>())
            for (LuaScript* script : m_Scene->FindScripts(entity)) { script->OnEpisodeBegin(); }
    }

    void Environment::BeginAgentEpisode(AgentState& agent){
        auto& registry = m_Scene->m_Registry;
        agent.done = false;
        agent.truncated = false;
        agent.episodeReturn = 0.0f;
        agent.episodeStep = 0;
        agent.lastReward = 0.0f;
        agent.pendingReward = 0.0f;
        agent.pendingEnd = false;
        agent.pendingTruncate = false;
        agent.stepsUntilDecision = 0;
        agent.awaitingAction = false;
        agent.externalActionReady = false;
        agent.returnTrace.clear();
        agent.action.assign(agent.discrete ? 1 : (size_t)agent.actionSize, 0.0f);
        agent.previousTargetDistance = TargetDistance(agent);

        // Zones the agent starts inside don't count as entered.
        agent.zonesInside.clear();
        const glm::vec3 pos = registry.get<TransformComponent>(agent.entity).Translation;
        auto zones = registry.view<RewardZoneComponent, TransformComponent>();
        for (auto zone : zones){
            if (zone == agent.entity) { continue; }
            if (InsideZone(pos, zones.get<TransformComponent>(zone).GetTransform(), zones.get<RewardZoneComponent>(zone).halfExtents))
                agent.zonesInside.insert((uint32_t)zone);
        }

        CollectObservation(agent);
    }

    void Environment::CollectObservation(AgentState& agent){
        auto& registry = m_Scene->m_Registry;
        const auto& c = registry.get<AgentComponent>(agent.entity);
        const auto& t = registry.get<TransformComponent>(agent.entity);
        PhysicsWorld* physics = m_Scene->GetPhysicsWorld();
        auto& obs = agent.observation;
        obs.clear();

        // Force agents (rolling balls) spin, so their yaw means nothing: use world axes.
        const bool worldFrame = c.control == AgentComponent::Control::Force;
        const float yaw = worldFrame ? 0.0f : t.Rotation.y;
        const glm::vec3 forward = worldFrame ? glm::vec3(0, 0, -1) : Forward(yaw);
        const glm::vec3 right = worldFrame ? glm::vec3(1, 0, 0) : RightOf(yaw);
        const float scale = std::max(0.01f, c.observationScale);

        uint32_t body = PhysicsWorld::InvalidBody;
        if (auto* rb = registry.try_get<RigidBodyComponent>(agent.entity)) { body = rb->runtimeBodyID; }

        if (c.observePosition){
            obs.push_back(t.Translation.x / scale);
            obs.push_back(t.Translation.y / scale);
            obs.push_back(t.Translation.z / scale);
        }
        if (c.observeVelocity){
            glm::vec3 v(0.0f);
            if (physics && body != PhysicsWorld::InvalidBody) { v = physics->GetLinearVelocity(body); }
            float speed = std::max(0.01f, c.moveSpeed);
            obs.push_back(glm::dot(v, right) / speed);
            obs.push_back(glm::dot(v, forward) / speed);
        }
        if (c.observeHeading){
            obs.push_back(std::sin(t.Rotation.y));
            obs.push_back(std::cos(t.Rotation.y));
        }
        if (c.observeTarget){
            glm::vec3 target;
            if (FindTarget(c, t.Translation, target)){
                glm::vec3 d = target - t.Translation;
                d.y = 0.0f;
                obs.push_back(glm::dot(d, right) / scale);
                obs.push_back(glm::dot(d, forward) / scale);
                obs.push_back(glm::length(d) / scale);
            }
            else { obs.insert(obs.end(), { 0.0f, 0.0f, 0.0f }); }
        }

        if (auto* sensor = registry.try_get<RaySensorComponent>(agent.entity)){
            std::vector<glm::vec3> dirs;
            glm::vec3 origin;
            SensorRays(t.Translation, t.Rotation.y, *sensor, dirs, origin);
            const float length = std::max(0.01f, sensor->length);
            agent.rayFractions.assign(dirs.size(), 1.0f);
            agent.rayTags.assign(dirs.size(), -1);
            auto tagIndex = [&](entt::entity e){
                if (!registry.valid(e) || !registry.all_of<TagComponent>(e)) { return -1; }
                const std::string& tag = registry.get<TagComponent>(e).Tag;
                for (size_t k = 0; k < sensor->detectableTags.size(); k++)
                    if (sensor->detectableTags[k] == tag) { return (int)k; }
                return -1;
            };
            if (!agent.sensorHitsReady) { CastSensors({ &agent }); }
            agent.sensorHitsReady = false;
            for (size_t r = 0; r < dirs.size(); r++){
                float best = 1.0f;
                uint32_t hitBody = PhysicsWorld::InvalidBody;
                PhysicsWorld::Hit rayHit;
                PhysicsWorld::QueryFilter filter;
                filter.ignoreBody = body;
                if (agent.bodiesMirrored){
                    // Mirrored bodies were cast in the batch; Jolt only tests the others
                    // whose bounds this ray passes through.
                    const auto& bodyHit = agent.bodyHits[r];
                    if (bodyHit.body != PhysicsWorld::InvalidBody) { best = bodyHit.t; hitBody = bodyHit.body; }
                    m_RayOthers.clear();
                    for (const auto& other : agent.otherBodies)
                        if (other.body != body && SegmentTouchesBounds(origin, dirs[r] * length, other.boundsMin, other.boundsMax))
                            m_RayOthers.push_back(other.body);
                    filter.onlyBodies = &m_RayOthers;
                    if (!m_RayOthers.empty() && physics->RayCast(origin, dirs[r], length, rayHit, filter)
                        && rayHit.distance / length < best){
                        best = rayHit.distance / length;
                        hitBody = rayHit.body;
                    }
                }
                else if (physics && physics->RayCast(origin, dirs[r], length, rayHit, filter)){
                    best = rayHit.distance / length;
                    hitBody = rayHit.body;
                }
                int tag = -1;
                if (hitBody != PhysicsWorld::InvalidBody){
                    Entity hit = m_Scene->FindEntityByBodyID(hitBody);
                    tag = hit ? tagIndex((entt::entity)hit) : -1;
                }
                const auto& zoneHit = agent.zoneHits[r];
                if (zoneHit.zone != entt::null && zoneHit.t < best){
                    best = zoneHit.t;
                    tag = tagIndex(zoneHit.zone);
                }
                agent.rayFractions[r] = best;
                agent.rayTags[r] = tag;
                obs.push_back(best);
                for (size_t k = 0; k < sensor->detectableTags.size(); k++)
                    obs.push_back(tag == (int)k ? 1.0f : 0.0f);
            }
        }

        if (c.customObservationSize > 0){
            agent.customObservations.clear();
            for (LuaScript* script : m_Scene->FindScripts(agent.entity)) { script->CollectObservations(); }
            agent.customObservations.resize((size_t)c.customObservationSize, 0.0f);
            obs.insert(obs.end(), agent.customObservations.begin(), agent.customObservations.end());
        }

        // Shapes are fixed for the run (clients rely on it).
        obs.resize(agent.observationLabels.size(), 0.0f);
        for (float& v : obs) { if (!std::isfinite(v)) { v = 0.0f; } }
    }

    void Environment::ChooseAction(AgentState& agent){
        auto& registry = m_Scene->m_Registry;
        const auto& c = registry.get<AgentComponent>(agent.entity);
        const int n = agent.actionSize;
        std::vector<float>& a = agent.action;

        switch (agent.policy){
            case AgentComponent::Policy::Heuristic:{
                auto key = [](KeyCode k1, KeyCode k2){ return Input::IsKeyPressed(k1) || Input::IsKeyPressed(k2); };
                bool up = key(Key::W, Key::Up), down = key(Key::S, Key::Down);
                bool left = key(Key::A, Key::Left), right = key(Key::D, Key::Right);
                // The first of the agent's scripts that defines Heuristic() drives it.
                bool scripted = false;
                if (c.control == AgentComponent::Control::Custom)
                    for (LuaScript* script : m_Scene->FindScripts(agent.entity))
                        if (script->Heuristic(a)) { scripted = true; break; }
                if (scripted){
                    a.resize(agent.discrete ? 1 : (size_t)n, 0.0f);
                }
                else if (agent.discrete){
                    // Move: 1 forward, 2 back, 3 turn left, 4 turn right.
                    // Force: 1 -Z (up/away), 2 +Z, 3 -X, 4 +X.
                    int index = up ? 1 : down ? 2 : left ? 3 : right ? 4 : 0;
                    a.assign(1, (float)std::min(index, n - 1));
                }
                else{
                    a.assign((size_t)n, 0.0f);
                    if (c.control == AgentComponent::Control::Force){
                        if (n > 0) { a[0] = (right ? 1.0f : 0.0f) - (left ? 1.0f : 0.0f); }
                        if (n > 1) { a[1] = (down ? 1.0f : 0.0f) - (up ? 1.0f : 0.0f); }
                    }
                    else{
                        if (n > 0) { a[0] = (up ? 1.0f : 0.0f) - (down ? 1.0f : 0.0f); }
                        if (n > 1) { a[1] = (left ? 1.0f : 0.0f) - (right ? 1.0f : 0.0f); }
                    }
                }
                break;
            }
            case AgentComponent::Policy::Random:
            default:
                if (agent.discrete){
                    a.assign(1, (float)std::uniform_int_distribution<int>(0, std::max(0, n - 1))(m_Rng));
                }
                else{
                    a.resize((size_t)n);
                    for (float& v : a) { v = std::uniform_real_distribution<float>(-1.0f, 1.0f)(m_Rng); }
                }
                break;
        }
    }

    void Environment::ApplyAction(AgentState& agent, float dt){
        auto& registry = m_Scene->m_Registry;
        const auto& c = registry.get<AgentComponent>(agent.entity);
        auto& t = registry.get<TransformComponent>(agent.entity);
        PhysicsWorld* physics = m_Scene->GetPhysicsWorld();
        uint32_t body = PhysicsWorld::InvalidBody;
        if (auto* rb = registry.try_get<RigidBodyComponent>(agent.entity))
            if (rb->type == RigidBodyComponent::Type::Dynamic) { body = rb->runtimeBodyID; }
        const bool dynamic = physics && body != PhysicsWorld::InvalidBody;
        const std::vector<float>& a = agent.action;
        auto value = [&](size_t i){ return i < a.size() ? std::clamp(a[i], -1.0f, 1.0f) : 0.0f; };

        switch (c.control){
            case AgentComponent::Control::Move:{
                float move = 0.0f, turn = 0.0f;
                if (agent.discrete){
                    int index = a.empty() ? 0 : (int)a[0];
                    move = index == 1 ? 1.0f : index == 2 ? -1.0f : 0.0f;
                    turn = index == 3 ? 1.0f : index == 4 ? -1.0f : 0.0f;
                }
                else { move = value(0); turn = value(1); }
                glm::vec3 velocity = Forward(t.Rotation.y) * move * c.moveSpeed;
                if (dynamic){
                    glm::vec3 v = physics->GetLinearVelocity(body);
                    physics->SetLinearVelocity(body, { velocity.x, v.y, velocity.z });
                    physics->SetAngularVelocity(body, { 0.0f, turn * c.turnSpeed, 0.0f });
                }
                else{
                    t.Translation += velocity * dt;
                    t.Rotation.y += turn * c.turnSpeed * dt;
                }
                break;
            }
            case AgentComponent::Control::Force:{
                glm::vec3 dir(0.0f);
                if (agent.discrete){
                    int index = a.empty() ? 0 : (int)a[0];
                    if (index == 1) { dir = { 0, 0, -1 }; }
                    if (index == 2) { dir = { 0, 0, 1 }; }
                    if (index == 3) { dir = { -1, 0, 0 }; }
                    if (index == 4) { dir = { 1, 0, 0 }; }
                }
                else { dir = { value(0), 0.0f, value(1) }; }
                if (dynamic) { physics->ApplyForce(body, dir * c.force); }
                else         { t.Translation += dir * c.moveSpeed * dt; }
                break;
            }
            case AgentComponent::Control::Custom:
                break; // the script got OnAction(actions) when the decision was made
        }
    }

    void Environment::EvaluateZones(AgentState& agent, float dt){
        auto& registry = m_Scene->m_Registry;
        const auto& c = registry.get<AgentComponent>(agent.entity);
        const glm::vec3 pos = registry.get<TransformComponent>(agent.entity).Translation;
        const std::string& agentTag = agent.name;

        auto zones = registry.view<RewardZoneComponent, TransformComponent>();
        for (auto zone : zones){
            if (zone == agent.entity) { continue; }
            const auto& z = zones.get<RewardZoneComponent>(zone);
            if (!z.agentTag.empty() && z.agentTag != agentTag) { continue; }
            const uint32_t key = (uint32_t)zone;
            bool inside = InsideZone(pos, zones.get<TransformComponent>(zone).GetTransform(), z.halfExtents);
            bool wasInside = agent.zonesInside.count(key) > 0;
            if (inside){
                if (z.trigger == RewardZoneComponent::Trigger::OnEnter && !wasInside){
                    agent.pendingReward += z.reward;
                    if (z.endEpisode) { agent.pendingEnd = true; }
                }
                else if (z.trigger == RewardZoneComponent::Trigger::WhileInside){
                    agent.pendingReward += z.reward * dt;
                    if (z.endEpisode) { agent.pendingEnd = true; }
                }
                agent.zonesInside.insert(key);
            }
            else { agent.zonesInside.erase(key); }
        }

        if (pos.y < c.fallHeight){
            agent.pendingReward += c.fallReward;
            agent.pendingEnd = true;
        }
    }

    void Environment::FinishDecision(AgentState& agent){
        auto& registry = m_Scene->m_Registry;
        const auto& c = registry.get<AgentComponent>(agent.entity);

        float reward = agent.pendingReward + c.stepReward;
        if (c.approachReward != 0.0f){
            float d = TargetDistance(agent);
            if (d >= 0.0f && agent.previousTargetDistance >= 0.0f)
                reward += (agent.previousTargetDistance - d) * c.approachReward;
            agent.previousTargetDistance = d;
        }
        agent.pendingReward = 0.0f;
        agent.lastReward = reward;
        agent.episodeReturn += reward;
        agent.episodeStep++;
        m_Log->totalSteps++;
        agent.returnTrace.push_back(agent.episodeReturn);
        if (agent.returnTrace.size() > 2000) { agent.returnTrace.pop_front(); }

        bool timeUp = c.maxSteps > 0 && agent.episodeStep >= c.maxSteps;
        if (agent.pendingEnd || timeUp){
            agent.truncated = !agent.pendingEnd && timeUp;
            FinishAgentEpisode(agent);
        }
        else{
            CollectObservation(agent);
        }
    }

    void Environment::FinishAgentEpisode(AgentState& agent){
        auto& registry = m_Scene->m_Registry;
        agent.done = true;
        agent.pendingEnd = false;
        agent.episodes++;
        agent.lastEpisodeReturn = agent.episodeReturn;
        CollectObservation(agent); // terminal observation (returned to external clients)

        // Park the agent while the others finish.
        if (PhysicsWorld* physics = m_Scene->GetPhysicsWorld())
            if (auto* rb = registry.try_get<RigidBodyComponent>(agent.entity))
                if (rb->type == RigidBodyComponent::Type::Dynamic && rb->runtimeBodyID != PhysicsWorld::InvalidBody){
                    glm::vec3 v = physics->GetLinearVelocity(rb->runtimeBodyID);
                    physics->SetLinearVelocity(rb->runtimeBodyID, { 0.0f, v.y, 0.0f });
                    physics->SetAngularVelocity(rb->runtimeBodyID, glm::vec3(0.0f));
                }
    }

    bool Environment::AllDone() const{
        if (m_Agents.empty()) { return false; }
        for (const auto& agent : m_Agents)
            if (!agent.done) { return false; }
        return true;
    }

    void Environment::PruneAgents(){
        auto& registry = m_Scene->m_Registry;
        for (size_t i = 0; i < m_Agents.size();){
            entt::entity e = m_Agents[i].entity;
            if (registry.valid(e) && registry.all_of<AgentComponent, TransformComponent>(e)) { i++; continue; }
            m_Agents.erase(m_Agents.begin() + (long)i);
        }
    }

    void Environment::PhysicsStep(float dt){
        // Agents deleted in the editor while playing drop out of the run.
        PruneAgents();

        // Decision boundaries: pick (or take the client's) next action.
        for (auto& agent : m_Agents){
            if (agent.done || agent.stepsUntilDecision > 0) { continue; }
            const auto& c = m_Scene->m_Registry.get<AgentComponent>(agent.entity);
            if (agent.policy == AgentComponent::Policy::External){
                if (!agent.externalActionReady) { continue; } // holds still until the client answers
                agent.externalActionReady = false;
                agent.awaitingAction = false;
            }
            else{
                ChooseAction(agent);
            }
            if (c.control == AgentComponent::Control::Custom)
                for (LuaScript* script : m_Scene->FindScripts(agent.entity)) { script->OnAction(agent.action); }
            agent.stepsUntilDecision = std::max(1, c.decisionPeriod);
        }

        for (auto& agent : m_Agents)
            if (!agent.done && agent.stepsUntilDecision > 0) { ApplyAction(agent, dt); }

        m_Scene->StepSimulation(dt);
        m_PhysicsSteps++;

        // Agents whose decision ends now get their sensor zone hits in one batch.
        // (A decision cut short by a terminal event casts on its own.)
        m_SensorBatch.clear();
        for (auto& agent : m_Agents)
            if (!agent.done && agent.stepsUntilDecision == 1) { m_SensorBatch.push_back(&agent); }
        if (!m_SensorBatch.empty()) { CastSensors(m_SensorBatch); }

        for (auto& agent : m_Agents){
            if (agent.done || agent.stepsUntilDecision <= 0) { continue; }
            EvaluateZones(agent, dt);
            agent.stepsUntilDecision--;
            // A terminal event ends the decision at once rather than after the period.
            if (agent.stepsUntilDecision == 0 || agent.pendingEnd){
                agent.stepsUntilDecision = 0;
                FinishDecision(agent);
                if (!agent.done && agent.policy == AgentComponent::Policy::External) { agent.awaitingAction = true; }
            }
        }

        if (AllDone()){
            EpisodeRecord record;
            record.episode = m_Episode;
            float sum = 0.0f;
            int length = 0;
            for (const auto& agent : m_Agents){ sum += agent.episodeReturn; length = std::max(length, agent.episodeStep); }
            record.meanReturn = sum / (float)m_Agents.size();
            record.length = length;
            record.simTime = m_Scene->GetSimulationTime();
            m_Log->episodes.push_back(record);
            if (m_Log->episodes.size() > 20000) { m_Log->episodes.erase(m_Log->episodes.begin(), m_Log->episodes.begin() + 5000); }
            // A client decides when to reset its episodes.
            if (!ClientDriven()) { ResetEpisode(); }
        }
    }

    void Environment::Update(float frameSeconds){
        const double start = NowSeconds();
        const float fixed = std::clamp(m_Scene->Simulation().fixedTimestep, 1.0f / 1000.0f, 0.1f);
        const double budget = frameBudgetMs / 1000.0;

        if (!paused && !ClientDriven()){
            if (maxSpeed){
                for (int i = 0; i < 200000 && NowSeconds() - start < budget; i++) { PhysicsStep(fixed); }
                m_Accumulator = 0.0f;
            }
            else{
                m_Accumulator += std::min(frameSeconds, 0.25f) * std::max(0.0f, timeScale);
                while (m_Accumulator >= fixed){
                    PhysicsStep(fixed);
                    m_Accumulator -= fixed;
                    if (NowSeconds() - start > budget){ m_Accumulator = 0.0f; break; } // can't keep up: slow down
                }
            }
        }

        // Rolling rates (steps per real second, simulated seconds per real second).
        double now = NowSeconds();
        if (now - m_RateWindowStart >= 0.5){
            float window = (float)(now - m_RateWindowStart);
            m_StepsPerSecond = (float)(m_PhysicsSteps - m_RateSteps) / window;
            m_SimSpeed = (m_Scene->GetSimulationTime() - m_RateSimTime) / window;
            m_RateWindowStart = now;
            m_RateSteps = m_PhysicsSteps;
            m_RateSimTime = m_Scene->GetSimulationTime();
        }
    }

    void Environment::StepDecision(){
        const float fixed = std::clamp(m_Scene->Simulation().fixedTimestep, 1.0f / 1000.0f, 0.1f);
        int episode = m_Episode;
        long long steps = m_Log->totalSteps;
        for (int i = 0; i < 1000; i++){
            PhysicsStep(fixed);
            if (m_Log->totalSteps != steps || m_Episode != episode || m_Agents.empty()) { break; }
        }
    }

    void Environment::Simulate(int steps){
        const float fixed = std::clamp(m_Scene->Simulation().fixedTimestep, 1.0f / 1000.0f, 0.1f);
        for (int i = 0; i < steps; i++) { PhysicsStep(fixed); }
    }

    int Environment::EpisodeStep() const{
        int step = 0;
        for (const auto& agent : m_Agents) { step = std::max(step, agent.episodeStep); }
        return step;
    }

    bool Environment::HasExternalAgents() const{
        for (const auto& agent : m_Agents)
            if (agent.policy == AgentComponent::Policy::External) { return true; }
        return false;
    }

    std::vector<AgentState*> Environment::ExternalAgents(){
        std::vector<AgentState*> out;
        for (auto& agent : m_Agents)
            if (agent.policy == AgentComponent::Policy::External) { out.push_back(&agent); }
        return out;
    }

    void Environment::ExternalReset(){
        PruneAgents();
        ResetEpisode();
        for (AgentState* agent : ExternalAgents()) { agent->awaitingAction = true; }
    }

    void Environment::ExternalStep(const std::vector<std::vector<float>>& actions){
        PruneAgents();
        std::vector<AgentState*> external = ExternalAgents();
        for (size_t i = 0; i < external.size(); i++){
            AgentState& agent = *external[i];
            if (agent.done) { continue; }
            std::vector<float> a = i < actions.size() ? actions[i] : std::vector<float>();
            if (agent.discrete){
                int index = a.empty() ? 0 : (int)std::lround(a[0]);
                agent.action.assign(1, (float)std::clamp(index, 0, std::max(0, agent.actionSize - 1)));
            }
            else{
                a.resize((size_t)agent.actionSize, 0.0f);
                for (float& v : a) { v = std::isfinite(v) ? std::clamp(v, -1.0f, 1.0f) : 0.0f; }
                agent.action = a;
            }
            agent.externalActionReady = true;
        }

        const float fixed = std::clamp(m_Scene->Simulation().fixedTimestep, 1.0f / 1000.0f, 0.1f);
        for (int i = 0; i < 100000; i++){
            bool anyRunning = false;
            for (AgentState* agent : external)
                if (!agent->done && (agent->externalActionReady || agent->stepsUntilDecision > 0)) { anyRunning = true; }
            if (!anyRunning) { break; }
            PhysicsStep(fixed);
        }
    }

    AgentState* Environment::FindAgent(entt::entity entity){
        for (auto& agent : m_Agents)
            if (agent.entity == entity) { return &agent; }
        return nullptr;
    }

    void Environment::AddReward(entt::entity entity, float reward){
        if (AgentState* agent = FindAgent(entity)) { if (!agent->done) { agent->pendingReward += reward; } }
    }

    void Environment::EndEpisode(entt::entity entity){
        if (AgentState* agent = FindAgent(entity)) { if (!agent->done) { agent->pendingEnd = true; } }
    }
}

namespace Phoenix{
    void Environment::AddObservation(entt::entity entity, float value){
        if (AgentState* agent = FindAgent(entity)) { agent->customObservations.push_back(value); }
    }
}
