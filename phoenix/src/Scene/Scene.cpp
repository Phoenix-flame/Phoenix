
#include <Phoenix/Scene/Scene.h>
#include <Phoenix/Scene/Entity.h>
#include <Phoenix/Scene/Component.h>
#include <Phoenix/renderer/renderer.h>
#include <Phoenix/renderer/Primitives.h>
#include <Phoenix/core/log.h>
#include <Phoenix/core/Profiler.h>
#include <Phoenix/Physics/Physics.h>
#include <Phoenix/Scene/LuaScript.h>
#include <Phoenix/core/Input.h>
#include <Phoenix/core/keyCodes.h>
#include <limits>
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <set>
#include <tuple>
namespace Phoenix{

    // Built-in primitive meshes are unit-sized and identical across all entities of a
    // given type, so generate each once and share it. Generated lazily on the main
    // (render) thread the first time a primitive of that type is drawn.
    static Ref<Mesh> GetPrimitiveMesh(PrimitiveComponent::Type type){
        static std::unordered_map<int, Ref<Mesh>> cache;
        auto it = cache.find((int)type);
        if (it != cache.end()) { return it->second; }
        Ref<Mesh> mesh;
        switch (type){
            case PrimitiveComponent::Type::Cube:     mesh = Primitives::Cube();     break;
            case PrimitiveComponent::Type::Sphere:   mesh = Primitives::Sphere();   break;
            case PrimitiveComponent::Type::Cylinder: mesh = Primitives::Cylinder(); break;
            case PrimitiveComponent::Type::Cone:     mesh = Primitives::Cone();     break;
            case PrimitiveComponent::Type::Plane:    mesh = Primitives::Plane();    break;
        }
        cache[(int)type] = mesh;
        return mesh;
    }

    // Build a renderable mesh from a terrain heightmap (positions + analytic normals).
    static Ref<Mesh> BuildTerrainMesh(const TerrainComponent& terrain){
        const int N = terrain.resolution;
        const float halfSize = terrain.size * 0.5f;
        const float cell = (N > 1) ? terrain.size / (float)(N - 1) : terrain.size;

        auto heightAt = [&](int x, int z) -> float {
            x = std::max(0, std::min(N - 1, x));
            z = std::max(0, std::min(N - 1, z));
            return terrain.heights[(size_t)z * N + x];
        };

        std::vector<Vertex> vertices;
        vertices.reserve((size_t)N * N);
        for (int z = 0; z < N; z++){
            for (int x = 0; x < N; x++){
                float fx = (N > 1) ? (float)x / (N - 1) : 0.0f;
                float fz = (N > 1) ? (float)z / (N - 1) : 0.0f;
                glm::vec3 pos(fx * terrain.size - halfSize, heightAt(x, z), fz * terrain.size - halfSize);
                // central-difference normal
                float hl = heightAt(x - 1, z), hr = heightAt(x + 1, z);
                float hd = heightAt(x, z - 1), hu = heightAt(x, z + 1);
                glm::vec3 normal = glm::normalize(glm::vec3(hl - hr, 2.0f * cell, hd - hu));
                vertices.push_back({ pos, normal, glm::vec2(fx * 8.0f, fz * 8.0f) });
            }
        }

        std::vector<uint32_t> indices;
        indices.reserve((size_t)(N - 1) * (N - 1) * 6);
        for (int z = 0; z < N - 1; z++){
            for (int x = 0; x < N - 1; x++){
                uint32_t i0 = (uint32_t)(z * N + x);
                uint32_t i1 = (uint32_t)(z * N + x + 1);
                uint32_t i2 = (uint32_t)((z + 1) * N + x);
                uint32_t i3 = (uint32_t)((z + 1) * N + x + 1);
                indices.push_back(i0); indices.push_back(i2); indices.push_back(i1);
                indices.push_back(i1); indices.push_back(i2); indices.push_back(i3);
            }
        }
        return CreateRef<Mesh>(vertices, indices);
    }

    // Flat XZ grid mesh (y=0, normals up) used by the water surface; waves are applied
    // in the water shader.
    static Ref<Mesh> BuildGridMesh(float size, int N){
        if (N < 2) { N = 2; }
        float half = size * 0.5f;
        std::vector<Vertex> vertices;
        vertices.reserve((size_t)N * N);
        for (int z = 0; z < N; z++){
            for (int x = 0; x < N; x++){
                float fx = (float)x / (N - 1), fz = (float)z / (N - 1);
                vertices.push_back({ glm::vec3(fx * size - half, 0.0f, fz * size - half),
                                     glm::vec3(0.0f, 1.0f, 0.0f), glm::vec2(fx, fz) });
            }
        }
        std::vector<uint32_t> indices;
        indices.reserve((size_t)(N - 1) * (N - 1) * 6);
        for (int z = 0; z < N - 1; z++){
            for (int x = 0; x < N - 1; x++){
                uint32_t i0 = (uint32_t)(z * N + x), i1 = (uint32_t)(z * N + x + 1);
                uint32_t i2 = (uint32_t)((z + 1) * N + x), i3 = (uint32_t)((z + 1) * N + x + 1);
                indices.push_back(i0); indices.push_back(i2); indices.push_back(i1);
                indices.push_back(i1); indices.push_back(i2); indices.push_back(i3);
            }
        }
        return CreateRef<Mesh>(vertices, indices);
    }

    Scene::Scene() = default;
    Scene::~Scene() = default;

    // Vertical height of the procedural Gerstner wave set at world position p —
    // the CPU mirror used for buoyancy and splash detection. The wave constants
    // MUST match the ones in water.glsl.
    static float WaterWaveHeight(const WaterComponent& water, const glm::vec2& p, float time){
        static const glm::vec2 DIR[4] = { { 0.9438798f, 0.3303579f }, { -0.4103913f, 0.9119215f },
                                          { 0.7071068f, -0.7071068f }, { -0.9048187f, -0.4258407f } };
        static const float FRQ[4] = { 1.0f, 1.9f, 3.1f, 5.3f };
        static const float AMP[4] = { 1.0f, 0.5f, 0.25f, 0.12f };
        static const float SPD[4] = { 1.0f, 1.25f, 1.7f, 2.3f };
        float h = 0.0f;
        for (int i = 0; i < 4; i++){
            float w = water.waveScale * FRQ[i];
            float a = water.amplitude * AMP[i];
            h += a * std::sin(glm::dot(p, DIR[i]) * w + time * water.speed * SPD[i]);
        }
        return h;
    }

    // Collect the rigid body's material/mass settings for body creation.
    static PhysicsWorld::BodyProps MakeBodyProps(const RigidBodyComponent& rb){
        PhysicsWorld::BodyProps props;
        props.friction = rb.friction;
        props.restitution = rb.restitution;
        props.density = rb.density;
        props.mass = rb.mass;
        props.centerOfMassOffset = rb.centerOfMassOffset;
        props.linearDamping = rb.linearDamping;
        props.angularDamping = rb.angularDamping;
        props.gravityFactor = rb.gravityFactor;
        props.continuousCollision = rb.continuousCollision;
        props.isSensor = rb.isSensor;
        props.allowSleeping = rb.allowSleeping;
        for (int i = 0; i < 3; i++){
            props.lockPosition[i] = rb.lockPosition[i];
            props.lockRotation[i] = rb.lockRotation[i];
        }
        props.layer = rb.layer;
        props.initialVelocity = rb.initialVelocity;
        props.initialAngularVelocity = rb.initialAngularVelocity;
        return props;
    }

    // Every collider component on the entity, scaled into its local space by the
    // transform scale (spheres by the largest axis, capsules/cylinders radially by
    // the larger of X/Z). Returns false when a mesh collider's model isn't loaded yet.
    static bool CollectColliderShapes(entt::registry& registry, entt::entity entity, const glm::vec3& s,
                                      std::vector<PhysicsWorld::ShapeDesc>& out){
        using ShapeType = PhysicsWorld::ShapeDesc::Type;
        if (auto* box = registry.try_get<BoxColliderComponent>(entity)){
            PhysicsWorld::ShapeDesc d;
            d.type = ShapeType::Box;
            d.halfExtents = box->halfExtents * s;
            d.offset = box->offset * s;
            d.rotation = box->rotation;
            out.push_back(d);
        }
        if (auto* sphere = registry.try_get<SphereColliderComponent>(entity)){
            PhysicsWorld::ShapeDesc d;
            d.type = ShapeType::Sphere;
            d.radius = sphere->radius * std::max(s.x, std::max(s.y, s.z));
            d.offset = sphere->offset * s;
            out.push_back(d);
        }
        if (auto* capsule = registry.try_get<CapsuleColliderComponent>(entity)){
            PhysicsWorld::ShapeDesc d;
            d.type = ShapeType::Capsule;
            d.radius = capsule->radius * std::max(s.x, s.z);
            d.halfHeight = capsule->halfHeight * s.y;
            d.offset = capsule->offset * s;
            d.rotation = capsule->rotation;
            out.push_back(d);
        }
        if (auto* cylinder = registry.try_get<CylinderColliderComponent>(entity)){
            PhysicsWorld::ShapeDesc d;
            d.type = ShapeType::Cylinder;
            d.radius = cylinder->radius * std::max(s.x, s.z);
            d.halfHeight = cylinder->halfHeight * s.y;
            d.offset = cylinder->offset * s;
            d.rotation = cylinder->rotation;
            out.push_back(d);
        }
        if (auto* meshCollider = registry.try_get<MeshColliderComponent>(entity)){
            // Geometry (scaled into local space) from the entity's model, or from its
            // generated primitive-shape mesh.
            PhysicsWorld::ShapeDesc d;
            d.type = meshCollider->convex ? ShapeType::ConvexHull : ShapeType::TriangleMesh;
            if (auto* mesh = registry.try_get<MeshComponent>(entity)){
                if (!mesh->model || mesh->model->GetMeshes().empty()) { return false; } // not loaded yet
                for (const auto& sub : mesh->model->GetMeshes()){
                    uint32_t base = (uint32_t)d.points.size();
                    for (const auto& p : sub->GetPositions()) { d.points.push_back(p * s); }
                    for (uint32_t idx : sub->GetIndices())     { d.indices.push_back(base + idx); }
                }
            }
            else if (auto* primitive = registry.try_get<PrimitiveComponent>(entity)){
                if (Ref<Mesh> mesh = GetPrimitiveMesh(primitive->type)){
                    d.points.reserve(mesh->GetPositions().size());
                    for (const auto& p : mesh->GetPositions()) { d.points.push_back(p * s); }
                    d.indices = mesh->GetIndices();
                }
            }
            if (!d.points.empty()) { out.push_back(std::move(d)); }
        }
        return true;
    }

    static PhysicsWorld::JointDesc MakeJointDesc(const JointComponent& joint, const glm::vec3& ownCenter,
                                                 const glm::vec3& otherCenter){
        PhysicsWorld::JointDesc d;
        switch (joint.type){
            case JointComponent::Type::Point:    d.type = PhysicsWorld::JointType::Point; break;
            case JointComponent::Type::Distance: d.type = PhysicsWorld::JointType::Distance; break;
            case JointComponent::Type::Hinge:    d.type = PhysicsWorld::JointType::Hinge; break;
            case JointComponent::Type::Fixed:    d.type = PhysicsWorld::JointType::Fixed; break;
            case JointComponent::Type::Slider:   d.type = PhysicsWorld::JointType::Slider; break;
            case JointComponent::Type::Cone:     d.type = PhysicsWorld::JointType::Cone; break;
        }
        d.axis = joint.axis;
        if (joint.type == JointComponent::Type::Distance){
            d.pointA = ownCenter;
            d.pointB = otherCenter;
        }
        else{
            d.pointA = d.pointB = joint.pivot;
        }
        d.minDistance = joint.minDistance;
        d.maxDistance = joint.maxDistance;
        d.springFrequency = joint.springFrequency;
        d.springDamping = joint.springDamping;
        if (joint.type == JointComponent::Type::Hinge){
            d.limited = joint.limitAngles;
            d.minLimit = joint.minAngle;
            d.maxLimit = joint.maxAngle;
        }
        else if (joint.type == JointComponent::Type::Slider){
            d.limited = joint.limitSlide;
            d.minLimit = joint.minSlide;
            d.maxLimit = joint.maxSlide;
        }
        else if (joint.type == JointComponent::Type::Cone){
            d.maxLimit = joint.coneAngle;
        }
        d.motorMode = (PhysicsWorld::MotorMode)(int)joint.motor;
        d.motorTarget = joint.motorTarget;
        d.motorMaxForce = joint.motorMaxForce;
        d.friction = joint.friction;
        d.breakForce = joint.breakForce;
        d.collideConnected = joint.collideConnected;
        return d;
    }

    void Scene::OnRuntimeStart(){
        PhysicsWorld::Settings settings;
        settings.gravity = m_PhysicsSettings.gravity;
        settings.velocitySteps = m_PhysicsSettings.velocitySteps;
        settings.positionSteps = m_PhysicsSettings.positionSteps;
        settings.maxSubstepRate = m_PhysicsSettings.substepRate;
        for (int i = 0; i < PhysicsWorld::MaxLayers; i++) { settings.layerMasks[i] = m_PhysicsSettings.layerMasks[i]; }
        m_PhysicsWorld = CreateScope<PhysicsWorld>(settings);
        m_BodyToEntity.clear();
        m_JointToEntity.clear();
        m_SensorPairs.clear();
        m_DebugContacts.clear();

        // One body per rigid-body entity, shaped by all of its collider components.
        auto rbView = m_Registry.view<RigidBodyComponent, TransformComponent>();
        for (auto entity : rbView){
            auto& rb = rbView.get<RigidBodyComponent>(entity);
            auto& transform = rbView.get<TransformComponent>(entity);
            std::vector<PhysicsWorld::ShapeDesc> shapes;
            if (!CollectColliderShapes(m_Registry, entity, transform.Scale, shapes) || shapes.empty()) { continue; }
            rb.runtimeBodyID = m_PhysicsWorld->CreateBody(shapes, transform.Translation, transform.Rotation,
                (PhysicsWorld::BodyType)(int)rb.type, MakeBodyProps(rb));
            if (rb.runtimeBodyID != PhysicsWorld::InvalidBody) { m_BodyToEntity[rb.runtimeBodyID] = entity; }
        }

        // Terrain: static triangle-mesh collider from the heightfield.
        auto terrainView = m_Registry.view<TerrainComponent, TransformComponent>();
        for (auto entity : terrainView){
            auto& terrain = terrainView.get<TerrainComponent>(entity);
            if (!terrain.generateCollider) { continue; }
            if (terrain.dirty || !terrain.mesh){
                terrain.mesh = BuildTerrainMesh(terrain);
                terrain.dirty = false;
            }
            auto& transform = terrainView.get<TransformComponent>(entity);
            PhysicsWorld::ShapeDesc d;
            d.type = PhysicsWorld::ShapeDesc::Type::TriangleMesh;
            d.points.reserve(terrain.mesh->GetPositions().size());
            for (const auto& p : terrain.mesh->GetPositions()) { d.points.push_back(p * transform.Scale); }
            d.indices = terrain.mesh->GetIndices();
            PhysicsWorld::BodyProps props;
            props.friction = 0.8f;
            terrain.runtimeBodyID = m_PhysicsWorld->CreateBody({ d }, transform.Translation, transform.Rotation,
                PhysicsWorld::BodyType::Static, props);
            if (terrain.runtimeBodyID != PhysicsWorld::InvalidBody) { m_BodyToEntity[terrain.runtimeBodyID] = entity; }
        }

        // Characters: kinematic capsules driven by input, not by the solver.
        auto characterView = m_Registry.view<CharacterControllerComponent, TransformComponent>();
        for (auto entity : characterView){
            auto& cc = characterView.get<CharacterControllerComponent>(entity);
            auto& transform = characterView.get<TransformComponent>(entity);
            if (m_Registry.any_of<RigidBodyComponent>(entity))
                PHX_CORE_WARN("'{0}' has both a Character Controller and a Rigid Body; remove one",
                    m_Registry.any_of<TagComponent>(entity) ? m_Registry.get<TagComponent>(entity).Tag : "?");
            PhysicsWorld::CharacterDesc desc;
            desc.radius = cc.radius * std::max(transform.Scale.x, transform.Scale.z);
            desc.halfHeight = cc.halfHeight * transform.Scale.y;
            desc.maxSlopeAngle = cc.maxSlopeAngle;
            desc.stepHeight = cc.stepHeight;
            desc.mass = cc.mass;
            desc.maxStrength = cc.maxPushForce;
            desc.gravityFactor = cc.gravityFactor;
            desc.airControl = cc.airControl;
            desc.layer = cc.layer;
            cc.runtimeCharacterID = m_PhysicsWorld->CreateCharacter(desc, transform.Translation, transform.Rotation.y);
            cc.desiredVelocity = glm::vec3(0.0f);
            cc.jumpRequested = false;
            uint32_t body = m_PhysicsWorld->GetCharacterBody(cc.runtimeCharacterID);
            if (body != PhysicsWorld::InvalidBody) { m_BodyToEntity[body] = entity; }
        }

        // Joints: connect bodies after they all exist (creation order independent).
        // The connected entity is found by Tag; an empty tag anchors to the world.
        {
            auto jointView = m_Registry.view<JointComponent, RigidBodyComponent>();
            for (auto entity : jointView){
                auto& joint = jointView.get<JointComponent>(entity);
                auto& rb = jointView.get<RigidBodyComponent>(entity);
                joint.runtimeJointID = PhysicsWorld::InvalidJoint;
                if (rb.runtimeBodyID == PhysicsWorld::InvalidBody) { continue; } // no body (missing collider?)

                uint32_t other = PhysicsWorld::InvalidBody; // world
                glm::vec3 otherCenter = joint.pivot;
                if (!joint.connectedTag.empty()){
                    Entity target = FindEntityByTag(joint.connectedTag);
                    if (target && target.HasComponent<RigidBodyComponent>())
                        other = target.GetComponent<RigidBodyComponent>().runtimeBodyID;
                    if (other == PhysicsWorld::InvalidBody || (entt::entity)target == entity){
                        PHX_CORE_WARN("Joint on '{0}': connected body '{1}' not found or has no collider",
                            m_Registry.any_of<TagComponent>(entity) ? m_Registry.get<TagComponent>(entity).Tag : "?",
                            joint.connectedTag);
                        continue;
                    }
                    otherCenter = target.GetComponent<TransformComponent>().Translation;
                }

                glm::vec3 own = m_Registry.get<TransformComponent>(entity).Translation;
                joint.runtimeJointID = m_PhysicsWorld->AddJoint(rb.runtimeBodyID, other, MakeJointDesc(joint, own, otherCenter));
                if (joint.runtimeJointID != PhysicsWorld::InvalidJoint) { m_JointToEntity[joint.runtimeJointID] = entity; }
            }
        }

        m_PhysicsWorld->OptimizeBroadPhase();

        // Instantiate Lua scripts for the runtime.
        auto scriptView = m_Registry.view<LuaScriptComponent>();
        for (auto entity : scriptView){
            Entity e{ entity, this };
            m_Scripts.push_back(CreateRef<LuaScript>(scriptView.get<LuaScriptComponent>(entity).source, e));
        }
    }

    void Scene::OnRuntimeStop(){
        for (auto entity : m_Registry.view<RigidBodyComponent>())
            m_Registry.get<RigidBodyComponent>(entity).runtimeBodyID = PhysicsWorld::InvalidBody;
        for (auto entity : m_Registry.view<TerrainComponent>())
            m_Registry.get<TerrainComponent>(entity).runtimeBodyID = PhysicsWorld::InvalidBody;
        for (auto entity : m_Registry.view<JointComponent>())
            m_Registry.get<JointComponent>(entity).runtimeJointID = PhysicsWorld::InvalidJoint;
        for (auto entity : m_Registry.view<CharacterControllerComponent>()){
            auto& cc = m_Registry.get<CharacterControllerComponent>(entity);
            cc.runtimeCharacterID = PhysicsWorld::InvalidCharacter;
            cc.grounded = false;
            cc.velocity = glm::vec3(0.0f);
        }
        m_Scripts.clear(); // scripts may reference bodies; drop them before the world
        m_PhysicsWorld.reset();
        m_BodyToEntity.clear();
        m_JointToEntity.clear();
        m_SensorPairs.clear();
        m_SubmergedBodies.clear();
        m_DebugContacts.clear();
    }

    Entity Scene::FindEntityByBodyID(uint32_t bodyID){
        auto it = m_BodyToEntity.find(bodyID);
        if (it == m_BodyToEntity.end() || !m_Registry.valid(it->second)) { return {}; }
        return { it->second, this };
    }

    Entity Scene::FindEntityByTag(const std::string& tag){
        auto view = m_Registry.view<TagComponent>();
        for (auto entity : view)
            if (view.get<TagComponent>(entity).Tag == tag) { return { entity, this }; }
        return {};
    }

    uint32_t Scene::GetEntityBody(Entity entity){
        if (!m_PhysicsWorld || !entity) { return PhysicsWorld::InvalidBody; }
        if (auto* rb = m_Registry.try_get<RigidBodyComponent>(entity); rb && rb->runtimeBodyID != PhysicsWorld::InvalidBody)
            return rb->runtimeBodyID;
        if (auto* cc = m_Registry.try_get<CharacterControllerComponent>(entity))
            return m_PhysicsWorld->GetCharacterBody(cc->runtimeCharacterID);
        if (auto* terrain = m_Registry.try_get<TerrainComponent>(entity))
            return terrain->runtimeBodyID;
        return PhysicsWorld::InvalidBody;
    }

    // ---- Per-frame physics helpers ----

    // Characters: turn keyboard (or Lua-set) input into the character's desired
    // velocity before the step.
    void Scene::UpdateCharactersPreStep(float /*dt*/){
        auto view = m_Registry.view<CharacterControllerComponent, TransformComponent>();
        for (auto entity : view){
            auto& cc = view.get<CharacterControllerComponent>(entity);
            if (cc.runtimeCharacterID == PhysicsWorld::InvalidCharacter) { continue; }
            if (cc.keyboardControl){
                // Camera-relative WASD/arrows on the ground plane.
                glm::mat4 cameraWorld = glm::inverse(m_ActiveView);
                glm::vec3 forward = -glm::vec3(cameraWorld[2]);
                forward.y = 0.0f;
                forward = glm::dot(forward, forward) > 1e-6f ? glm::normalize(forward) : glm::vec3(0.0f, 0.0f, -1.0f);
                glm::vec3 right = glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f));
                glm::vec3 move(0.0f);
                if (Input::IsKeyPressed(Key::W) || Input::IsKeyPressed(Key::Up))    { move += forward; }
                if (Input::IsKeyPressed(Key::S) || Input::IsKeyPressed(Key::Down))  { move -= forward; }
                if (Input::IsKeyPressed(Key::D) || Input::IsKeyPressed(Key::Right)) { move += right; }
                if (Input::IsKeyPressed(Key::A) || Input::IsKeyPressed(Key::Left))  { move -= right; }
                if (glm::dot(move, move) > 1e-6f) { move = glm::normalize(move); }
                float speed = cc.moveSpeed * (Input::IsKeyPressed(Key::LeftShift) ? 1.8f : 1.0f);
                cc.desiredVelocity = move * speed;
                if (Input::IsKeyPressed(Key::Space)) { cc.jumpRequested = true; }
            }
            m_PhysicsWorld->SetCharacterInput(cc.runtimeCharacterID, cc.desiredVelocity, cc.jumpRequested, cc.jumpSpeed);
            cc.jumpRequested = false;
        }
    }

    // Characters after the step: copy position/state back and face the movement.
    void Scene::UpdateCharactersPostStep(float dt){
        auto view = m_Registry.view<CharacterControllerComponent, TransformComponent>();
        for (auto entity : view){
            auto& cc = view.get<CharacterControllerComponent>(entity);
            if (cc.runtimeCharacterID == PhysicsWorld::InvalidCharacter) { continue; }
            auto& transform = view.get<TransformComponent>(entity);
            transform.Translation = m_PhysicsWorld->GetCharacterPosition(cc.runtimeCharacterID);
            cc.velocity = m_PhysicsWorld->GetCharacterVelocity(cc.runtimeCharacterID);
            cc.grounded = m_PhysicsWorld->IsCharacterGrounded(cc.runtimeCharacterID);
            glm::vec2 planar(cc.desiredVelocity.x, cc.desiredVelocity.z);
            if (cc.faceMovement && glm::dot(planar, planar) > 0.01f){
                // Entities face local -Z (see MoveForward), so yaw = atan2(-x, -z).
                float target = std::atan2(-planar.x, -planar.y);
                float delta = std::remainder(target - transform.Rotation.y, glm::two_pi<float>());
                transform.Rotation.y += delta * std::min(1.0f, 12.0f * dt);
            }
        }
    }

    // Force fields: push every dynamic body inside each field's volume.
    void Scene::ApplyForceFields(){
        auto view = m_Registry.view<ForceFieldComponent, TransformComponent>();
        for (auto fieldEntity : view){
            auto& field = view.get<ForceFieldComponent>(fieldEntity);
            auto& t = view.get<TransformComponent>(fieldEntity);
            glm::vec3 half = field.halfExtents * t.Scale;
            float radius = field.halfExtents.x * std::max(t.Scale.x, std::max(t.Scale.y, t.Scale.z));
            std::vector<uint32_t> bodies = field.shape == ForceFieldComponent::Shape::Sphere
                ? m_PhysicsWorld->OverlapSphere(t.Translation, radius)
                : m_PhysicsWorld->OverlapBox(t.Translation, half, t.Rotation);
            glm::vec3 direction = glm::mat3(t.GetTransform()) * field.direction;
            if (glm::dot(direction, direction) > 1e-8f) { direction = glm::normalize(direction); }
            float extent = field.shape == ForceFieldComponent::Shape::Sphere ? radius : glm::length(half);

            for (uint32_t body : bodies){
                Entity e = FindEntityByBodyID(body);
                if (!e || !e.HasComponent<RigidBodyComponent>()) { continue; }
                if (e.GetComponent<RigidBodyComponent>().type != RigidBodyComponent::Type::Dynamic) { continue; }
                float mass = m_PhysicsWorld->GetMass(body);
                if (mass <= 0.0f) { continue; }
                glm::vec3 accel(0.0f);
                if (field.mode == ForceFieldComponent::Mode::Directional){
                    accel = direction * field.strength;
                }
                else{
                    glm::vec3 toCenter = t.Translation - e.GetComponent<TransformComponent>().Translation;
                    float dist = glm::length(toCenter);
                    if (dist > 1e-4f){
                        float falloff = std::clamp(1.0f - dist / std::max(extent, 1e-3f), 0.0f, 1.0f);
                        accel = toCenter / dist * field.strength * falloff;
                    }
                }
                if (field.drag > 0.0f) { accel -= m_PhysicsWorld->GetLinearVelocity(body) * field.drag; }
                m_PhysicsWorld->ApplyForce(body, accel * mass);
                m_PhysicsWorld->WakeUp(body);
            }
        }
    }

    // Contact + joint-break events -> Lua callbacks on both entities' scripts.
    // Contacts through a sensor are triggers (OnTriggerEnter/Exit); others are
    // collisions (OnCollisionEnter/Exit with contact point, normal and speed).
    void Scene::DispatchPhysicsEvents(){
        auto events = m_PhysicsWorld->ConsumeContactEvents();
        auto broken = m_PhysicsWorld->ConsumeBrokenJoints();
        if (m_PhysicsDebug.showContacts){
            // Keep the most recent impacts on screen (enter events last one frame).
            for (const auto& ev : events)
                if (ev.entered && !ev.sensor) { m_DebugContacts.push_back({ ev.point, ev.normal }); }
            const size_t maxContacts = 256;
            if (m_DebugContacts.size() > maxContacts)
                m_DebugContacts.erase(m_DebugContacts.begin(), m_DebugContacts.end() - maxContacts);
        }
        auto tagOf = [](Entity e){
            return (e && e.HasComponent<TagComponent>()) ? e.GetComponent<TagComponent>().Tag : std::string();
        };

        // Deduped per frame (Jolt can report one pair per sub-shape).
        std::set<std::tuple<uint32_t, uint32_t, bool>> seen;
        for (const auto& ev : events){
            uint32_t a = std::min(ev.bodyA, ev.bodyB), b = std::max(ev.bodyA, ev.bodyB);
            if (!seen.insert({ a, b, ev.entered }).second) { continue; }

            // Sensor flag: known on enter (also for character contacts, which only
            // know the other body); exits are matched against the recorded pairs.
            bool sensor = ev.sensor || (ev.entered && (m_PhysicsWorld->IsSensor(ev.bodyA) || m_PhysicsWorld->IsSensor(ev.bodyB)));
            if (ev.entered && sensor) { m_SensorPairs.insert({ a, b }); }
            if (!ev.entered) { sensor = m_SensorPairs.erase({ a, b }) > 0; }

            if (m_Scripts.empty()) { continue; }
            Entity entityA = FindEntityByBodyID(ev.bodyA);
            Entity entityB = FindEntityByBodyID(ev.bodyB);
            if (!entityA || !entityB) { continue; }
            for (auto& script : m_Scripts){
                entt::entity se = (entt::entity)script->GetEntity();
                bool isA = se == (entt::entity)entityA, isB = se == (entt::entity)entityB;
                if (!isA && !isB) { continue; }
                const std::string other = tagOf(isA ? entityB : entityA);
                if (sensor) { script->OnTrigger(other, ev.entered); }
                else{
                    // The normal points from A to B; flip it for B so it always points
                    // from the script's entity towards the other one.
                    script->OnCollision(other, ev.entered, ev.point, isA ? ev.normal : -ev.normal, ev.speed);
                }
            }
        }

        for (uint32_t joint : broken){
            auto it = m_JointToEntity.find(joint);
            if (it == m_JointToEntity.end() || !m_Registry.valid(it->second)) { continue; }
            Entity owner{ it->second, this };
            auto& jc = owner.GetComponent<JointComponent>();
            jc.runtimeJointID = PhysicsWorld::InvalidJoint;
            Entity other = jc.connectedTag.empty() ? Entity{} : FindEntityByTag(jc.connectedTag);
            for (auto& script : m_Scripts){
                entt::entity se = (entt::entity)script->GetEntity();
                if (se == (entt::entity)owner)       { script->OnJointBreak(jc.connectedTag); }
                else if (other && se == (entt::entity)other) { script->OnJointBreak(tagOf(owner)); }
            }
            m_JointToEntity.erase(it);
        }
    }

    // Slab-method ray vs axis-aligned box. Returns true and the hit distance: the entry
    // point, or the exit point when the ray starts inside the box (so a large box
    // around the camera, e.g. terrain, doesn't win every click at distance 0).
    static bool RayAABB(const glm::vec3& origin, const glm::vec3& dir,
                        const glm::vec3& boxMin, const glm::vec3& boxMax, float& outT){
        float tMin = -std::numeric_limits<float>::max();
        float tMax = std::numeric_limits<float>::max();
        for (int i = 0; i < 3; i++){
            if (std::abs(dir[i]) < 1e-8f){
                if (origin[i] < boxMin[i] || origin[i] > boxMax[i]) { return false; }
            }
            else{
                float inv = 1.0f / dir[i];
                float t1 = (boxMin[i] - origin[i]) * inv;
                float t2 = (boxMax[i] - origin[i]) * inv;
                if (t1 > t2) { std::swap(t1, t2); }
                tMin = std::max(tMin, t1);
                tMax = std::min(tMax, t2);
                if (tMin > tMax) { return false; }
            }
        }
        if (tMax < 0.0f) { return false; } // box is behind the ray
        outT = (tMin >= 0.0f) ? tMin : tMax;
        return true;
    }

    // Local-space bounds of what an entity draws, or false if it draws nothing.
    static bool GetPickBounds(entt::registry& registry, entt::entity entity, glm::vec3& outMin, glm::vec3& outMax){
        glm::vec3 lo(std::numeric_limits<float>::max()), hi(-std::numeric_limits<float>::max());
        auto include = [&](const glm::vec3& a, const glm::vec3& b){ lo = glm::min(lo, a); hi = glm::max(hi, b); };
        const glm::vec3 unitHalf(0.5f);

        // Cubes, primitives (all built inside the unit cube) and the camera / light
        // gizmos, which are drawn at about unit size.
        if (registry.any_of<CubeComponent, PrimitiveComponent, CameraComponent, DirLightComponent>(entity))
            include(-unitHalf, unitHalf);
        if (auto* mesh = registry.try_get<MeshComponent>(entity)){
            glm::vec3 a, b;
            if (mesh->model && mesh->model->GetLocalBounds(a, b)) { include(a, b); }
        }
        if (auto* terrain = registry.try_get<TerrainComponent>(entity)){
            float half = terrain->size * 0.5f;
            float minH = 0.0f, maxH = 0.0f;
            if (!terrain->heights.empty()){
                auto [mn, mx] = std::minmax_element(terrain->heights.begin(), terrain->heights.end());
                minH = *mn;
                maxH = *mx;
            }
            include({ -half, minH, -half }, { half, maxH, half });
        }
        if (auto* water = registry.try_get<WaterComponent>(entity)){
            float half = water->size * 0.5f;
            float waves = std::max(water->amplitude * 2.0f, 0.05f);
            include({ -half, -waves, -half }, { half, waves, half });
        }

        if (lo.x > hi.x) { return false; }
        // Pad flat shapes (planes, flat terrain) so the slab test stays robust.
        const glm::vec3 pad(1e-3f);
        outMin = lo - pad;
        outMax = hi + pad;
        return true;
    }

    Entity Scene::PickEntity(const glm::vec3& rayOrigin, const glm::vec3& rayDirection){
        float nearest = std::numeric_limits<float>::max();
        Entity result;

        auto view = m_Registry.view<TransformComponent>();
        for (auto entity : view){
            glm::vec3 localMin, localMax;
            if (!GetPickBounds(m_Registry, entity, localMin, localMax)) { continue; }

            // World-space AABB enclosing the local bounds under this transform.
            glm::mat4 model = view.get<TransformComponent>(entity).GetTransform();
            glm::vec3 boxMin(std::numeric_limits<float>::max());
            glm::vec3 boxMax(-std::numeric_limits<float>::max());
            for (int i = 0; i < 8; i++){
                glm::vec3 corner((i & 1) ? localMax.x : localMin.x,
                                 (i & 2) ? localMax.y : localMin.y,
                                 (i & 4) ? localMax.z : localMin.z);
                glm::vec3 world = glm::vec3(model * glm::vec4(corner, 1.0f));
                boxMin = glm::min(boxMin, world);
                boxMax = glm::max(boxMax, world);
            }

            float t;
            if (RayAABB(rayOrigin, rayDirection, boxMin, boxMax, t) && t < nearest){
                nearest = t;
                result = Entity{ entity, this };
            }
        }
        return result;
    }

    Entity Scene::CreateEntity(const std::string& name){
        Entity entity = { m_Registry.create(), this };
        entity.AddComponent<TransformComponent>();
        auto& tag = entity.AddComponent<TagComponent>();
		tag.Tag = name.empty() ? "Entity" : name;
        return entity;
    }

    Entity Scene::CreatePointLightEntity(const std::string& name)
    {
        if (GetNumberOfPointLights() >= MAX_NUM_POINT_LIGHTS)
        {
            throw std::runtime_error("Reached maximum number of point lights");
        }
        Entity entity = { m_Registry.create(), this };
        entity.AddComponent<TransformComponent>();
        entity.GetComponent<TransformComponent>().Scale.x = 0.1;
        entity.GetComponent<TransformComponent>().Scale.y = 0.1;
        entity.GetComponent<TransformComponent>().Scale.z = 0.1;
        entity.AddComponent<CubeComponent>();
        entity.GetComponent<CubeComponent>().material.ambient = glm::vec3(1.0f, 1.0f, 1.0f);
        entity.GetComponent<CubeComponent>().material.diffuse = glm::vec3(1.0f, 1.0f, 1.0f);

        entity.AddComponent<PointLightComponent>();
        auto& tag = entity.AddComponent<TagComponent>();
		tag.Tag = name.empty() ? "Entity" : name;
        return entity;
    }

    Entity Scene::CreateDirLightEntity(const std::string& name)
    {
        Entity entity = { m_Registry.create(), this };
        entity.AddComponent<TransformComponent>();
        entity.AddComponent<CubeComponent>();
        entity.GetComponent<TransformComponent>().Scale.x = 0.2;
        entity.GetComponent<TransformComponent>().Scale.y = 0.2;
        entity.GetComponent<TransformComponent>().Scale.z = 0.2;
        entity.AddComponent<DirLightComponent>();
        auto& tag = entity.AddComponent<TagComponent>();
		tag.Tag = name.empty() ? "Entity" : name;
        return entity;
    }

    int Scene::GetNumberOfPointLights(){
        return (int)m_Registry.view<PointLightComponent>().size();
    }

    void Scene::DestroyEntity(Entity entity){
        // While playing, the entity may own a physics body and Lua scripts; tear those
        // down too, or the body keeps colliding invisibly and the script keeps running.
        if (IsRunning()){
            uint32_t bodies[2] = { PhysicsWorld::InvalidBody, PhysicsWorld::InvalidBody };
            if (auto* rb = m_Registry.try_get<RigidBodyComponent>(entity)) { bodies[0] = rb->runtimeBodyID; }
            if (auto* terrain = m_Registry.try_get<TerrainComponent>(entity)) { bodies[1] = terrain->runtimeBodyID; }
            for (uint32_t body : bodies){
                if (body == PhysicsWorld::InvalidBody) { continue; }
                m_PhysicsWorld->RemoveBody(body); // also removes its joints
                m_BodyToEntity.erase(body);
                m_SubmergedBodies.erase(body);
            }
            if (auto* cc = m_Registry.try_get<CharacterControllerComponent>(entity)){
                m_BodyToEntity.erase(m_PhysicsWorld->GetCharacterBody(cc->runtimeCharacterID));
                m_PhysicsWorld->RemoveCharacter(cc->runtimeCharacterID);
            }
            for (auto it = m_JointToEntity.begin(); it != m_JointToEntity.end();)
                it = (it->second == (entt::entity)entity) ? m_JointToEntity.erase(it) : std::next(it);
            m_Scripts.erase(std::remove_if(m_Scripts.begin(), m_Scripts.end(),
                [&](const Ref<LuaScript>& script){ return (entt::entity)script->GetEntity() == (entt::entity)entity; }),
                m_Scripts.end());
        }
        m_Registry.destroy(entity);
    }

    void Scene::OnUpdate(EditorCamera& editorCamera, Timestep ts, Entity selectedEntity){

        m_Time += (float)ts; // drives water animation (advances in edit + play)

        // Lua scripts run while playing (they may move/recolour their entities).
        if (!m_Scripts.empty()){
            PHX_PROFILE("Lua Scripts");
            for (auto& script : m_Scripts){ script->OnUpdate((float)ts); }
        }

        // Physics: step the simulation and copy body transforms back to entities.
        if (m_PhysicsWorld){
            PHX_PROFILE("Physics Step");
            auto view = m_Registry.view<RigidBodyComponent, TransformComponent>();
            // Kinematic targets, buoyancy impulses and the step must all use the same
            // (clamped) dt, or a long frame would over-apply impulses.
            const float physicsDt = std::min((float)ts, PhysicsWorld::MaxStep);

            // Kinematic bodies follow their entity transform (scripts/animation move
            // it); MoveKinematic gives them the velocities to push dynamic bodies.
            for (auto entity : view){
                auto& rb = view.get<RigidBodyComponent>(entity);
                if (rb.runtimeBodyID == 0xffffffff || rb.type != RigidBodyComponent::Type::Kinematic) { continue; }
                auto& transform = view.get<TransformComponent>(entity);
                m_PhysicsWorld->MoveKinematic(rb.runtimeBodyID, transform.Translation, transform.Rotation, physicsDt);
            }

            // Water coupling: buoyancy + fluid drag on dynamic bodies over a water
            // surface, splash ripples on entry, and a wake while moving in it. The
            // surface height includes the Gerstner waves AND the live ripples, so
            // floating bodies bob on both.
            {
                auto waterView = m_Registry.view<WaterComponent, TransformComponent>();
                for (auto wEntity : waterView){
                    auto& water = waterView.get<WaterComponent>(wEntity);
                    if (water.buoyancy <= 0.0f) { continue; }
                    auto& wTransform = waterView.get<TransformComponent>(wEntity);
                    float half = water.size * 0.5f;

                    for (auto entity : view){
                        auto& rb = view.get<RigidBodyComponent>(entity);
                        if (rb.runtimeBodyID == 0xffffffff || rb.type != RigidBodyComponent::Type::Dynamic) { continue; }
                        auto& t = view.get<TransformComponent>(entity);
                        float lx = t.Translation.x - wTransform.Translation.x;
                        float lz = t.Translation.z - wTransform.Translation.z;
                        if (std::abs(lx) > half || std::abs(lz) > half) { continue; }

                        float u = lx / water.size + 0.5f;
                        float v = lz / water.size + 0.5f;
                        float surfaceY = wTransform.Translation.y
                            + WaterWaveHeight(water, { t.Translation.x, t.Translation.z }, m_Time)
                            + (water.ripples ? water.ripples->SampleHeight(u, v) : 0.0f);
                        if (t.Translation.y - surfaceY > 4.0f){ // far above: can't touch
                            m_SubmergedBodies.erase(rb.runtimeBodyID);
                            continue;
                        }

                        // Jolt's buoyancy factor is fluid density RELATIVE TO THE BODY
                        // (>1 floats, <1 sinks), so derive it from the body's density:
                        // water.buoyancy is the water's density in units of 1000 kg/m3.
                        float factor = std::min(8.0f, water.buoyancy * 1000.0f / std::max(1.0f, rb.density));
                        bool touching = m_PhysicsWorld->ApplyBuoyancy(rb.runtimeBodyID,
                            { t.Translation.x, surfaceY, t.Translation.z }, { 0.0f, 1.0f, 0.0f },
                            factor, water.linearDrag, water.angularDrag, physicsDt);

                        if (water.ripples && touching){
                            glm::vec3 vel = m_PhysicsWorld->GetLinearVelocity(rb.runtimeBodyID);
                            bool wasTouching = m_SubmergedBodies.count(rb.runtimeBodyID) > 0;
                            if (!wasTouching && vel.y < -1.0f){
                                // Splash: radius/strength grow with impact speed.
                                float strength = std::min(0.4f, 0.06f * -vel.y);
                                water.ripples->AddImpulse(u, v, 0.03f + 0.002f * -vel.y, strength);
                            }
                            else{
                                float speed = glm::length(glm::vec2(vel.x, vel.z));
                                if (speed > 0.5f){ // wake behind moving bodies
                                    water.ripples->AddImpulse(u, v, 0.02f,
                                        std::min(0.05f, 0.25f * speed * physicsDt));
                                }
                            }
                        }
                        if (touching) { m_SubmergedBodies.insert(rb.runtimeBodyID); }
                        else          { m_SubmergedBodies.erase(rb.runtimeBodyID); }
                    }
                }
            }

            UpdateCharactersPreStep(physicsDt);
            ApplyForceFields();
            m_PhysicsWorld->Step(physicsDt);
            UpdateCharactersPostStep(physicsDt);

            // Only dynamic bodies are moved by the simulation: static ones never move and
            // kinematic ones follow their entity transform, so reading those back would
            // just overwrite the authored/scripted rotation with an equivalent one.
            for (auto entity : view){
                auto& rb = view.get<RigidBodyComponent>(entity);
                if (rb.runtimeBodyID == PhysicsWorld::InvalidBody || rb.type != RigidBodyComponent::Type::Dynamic) { continue; }
                auto& transform = view.get<TransformComponent>(entity);
                m_PhysicsWorld->GetBodyTransform(rb.runtimeBodyID, transform.Translation, transform.Rotation);
            }

            DispatchPhysicsEvents();
        }

        {
			m_Registry.view<NativeScriptComponent>().each([=](auto entity, auto& nsc)
			{
				if (!nsc.Instance)
				{
					nsc.Instance = nsc.InstantiateScript();
					nsc.Instance->m_Entity = Entity{ entity, this };
					nsc.Instance->OnCreate();
				}

				nsc.Instance->OnUpdate(ts);
			});
		}

        // Skeletal animation: this is the SINGLE place that drives each Animator (Lua and
        // the timeline only write the component's config/request fields). Done before the
        // shadow + main passes so both can read the posed skeleton.
        {
            PHX_PROFILE("Animation");
            auto animView = m_Registry.view<MeshComponent, AnimationComponent>();
            for (auto entity : animView){
                auto& mesh = animView.get<MeshComponent>(entity);
                auto& anim = animView.get<AnimationComponent>(entity);
                if (!mesh.model) { continue; }
                mesh.model->Update(); // ensure GPU upload + animations are available
                if (!mesh.model->IsReady() || !mesh.model->HasAnimations()) { continue; }

                // Merge any extra clip files once, now that the model (skeleton) is ready.
                if (!anim.extraClipsLoaded){
                    for (const auto& path : anim.extraClips)
                        if (!path.empty()) { mesh.model->AddAnimationsFromFile(path); }
                    anim.extraClipsLoaded = true;
                }

                if (!anim.animator){ anim.animator = CreateRef<Animator>(); anim.activeClip = -1; }
                Animator& animator = *anim.animator;
                const int clipCount = (int)mesh.model->GetAnimationCount();
                anim.clip = std::max(0, std::min(clipCount - 1, anim.clip));

                // Push playback config every frame.
                animator.SetSpeed(anim.speed);
                animator.SetLoopMode((LoopMode)anim.loopMode);
                if (anim.playing) { animator.Resume(); } else { animator.Pause(); }

                // Clip changes: explicit crossfade request, then a plain clip-index change
                // (first assignment hard-cuts; later changes crossfade).
                if (anim.pendingCrossfade >= 0){
                    int c = std::max(0, std::min(clipCount - 1, anim.pendingCrossfade));
                    animator.CrossFade(mesh.model->GetAnimation(c), anim.crossfade);
                    anim.clip = c; anim.activeClip = c; anim.pendingCrossfade = -1;
                }
                else if (anim.activeClip != anim.clip){
                    if (anim.activeClip < 0) { animator.PlayAnimation(mesh.model->GetAnimation(anim.clip)); }
                    else                     { animator.CrossFade(mesh.model->GetAnimation(anim.clip), anim.crossfade); }
                    anim.activeClip = anim.clip;
                }

                // Seek request (timeline scrub / Lua). Negative means "no request".
                if (anim.pendingSeek >= 0.0f){ animator.Seek(anim.pendingSeek); anim.pendingSeek = -1.0f; }

                // Advance (0 when paused -> re-pose at rest); speed is applied inside.
                animator.UpdateAnimation(anim.playing ? (float)ts : 0.0f);

                // Fire animation events crossed this frame to the entity's running script.
                if (anim.playing && !anim.events.empty() && !m_Scripts.empty()){
                    const auto& w = animator.GetPlayWindow();
                    for (const auto& ev : anim.events){
                        if (ev.clip != anim.activeClip) { continue; }
                        bool crossed = w.wrapped
                            ? (ev.time > w.prevSeconds || ev.time <= w.curSeconds)
                            : (ev.time > w.prevSeconds && ev.time <= w.curSeconds);
                        if (!crossed) { continue; }
                        for (auto& script : m_Scripts)
                            if ((entt::entity)script->GetEntity() == entity) { script->OnAnimationEvent(ev.name); }
                    }
                }
            }
        }


        auto cameras = m_Registry.view<TransformComponent,CameraComponent>();
        glm::mat4 sceneCameraProjection = editorCamera.GetProjection();
        glm::mat4 sceneCameraView = editorCamera.GetView();
        glm::vec3 cameraPos = editorCamera.GetPosition();
        bool anyActiveCamera = false;
        entt::entity activeCamera = entt::null; // the camera we're viewing THROUGH
        for (auto cam:cameras){
            auto camera = cameras.get<CameraComponent>(cam);
            auto transform = cameras.get<TransformComponent>(cam);
            if (camera.primary){
                editorCamera.SetState(false);
                anyActiveCamera = true;
                activeCamera = cam;
                sceneCameraProjection = camera.camera.GetProjection();

                glm::vec3 camPos = transform.Translation;
                glm::mat4 camView = glm::inverse(transform.GetTransform());

                // Third-person follow: place the camera behind+above the target and look
                // at it, instead of using the camera entity's own transform.
                if (m_Registry.any_of<CameraFollowComponent>(cam)){
                    auto& follow = m_Registry.get<CameraFollowComponent>(cam);
                    TransformComponent* targetT = nullptr;
                    auto tagged = m_Registry.view<TagComponent, TransformComponent>();
                    for (auto te : tagged){
                        if (tagged.get<TagComponent>(te).Tag == follow.target){
                            targetT = &tagged.get<TransformComponent>(te);
                            break;
                        }
                    }
                    if (targetT){
                        float yaw = follow.followYaw ? targetT->Rotation.y : 0.0f;
                        // The target faces local -Z = (-sin,0,-cos); "behind" is the opposite.
                        // For a +Z-facing model, behind is the other way.
                        glm::vec3 behind(std::sin(yaw), 0.0f, std::cos(yaw));
                        if (follow.modelForwardZ) { behind = -behind; }
                        camPos = targetT->Translation + behind * follow.distance + glm::vec3(0.0f, follow.height, 0.0f);
                        glm::vec3 lookAt = targetT->Translation + glm::vec3(0.0f, follow.lookHeight, 0.0f);
                        camView = glm::lookAt(camPos, lookAt, glm::vec3(0.0f, 1.0f, 0.0f));
                    }
                }

                sceneCameraView = camView;
                cameraPos = camPos;
                break;
            }
        }
        if (!anyActiveCamera)
        {
            editorCamera.SetState(true);
        }
        m_ActiveView = sceneCameraView;
        m_ActiveProjection = sceneCameraProjection;
        // Collect up to MAX_DIR_LIGHTS active directional lights. Each can cast its own
        // shadow (shadow map i for directional light i). PointLightComponent derives from
        // DirLightComponent, so exclude point-light entities here.
        const int MAX_DIR = Renderer::MAX_DIR_LIGHTS;
        DirLightComponent dirLightComp[Renderer::MAX_DIR_LIGHTS];
        glm::vec3 dirLightDir[Renderer::MAX_DIR_LIGHTS];
        int numDirLights = 0;
        auto lightsView = m_Registry.view<TransformComponent,DirLightComponent>();
        for(auto entity:lightsView){
            if (numDirLights >= MAX_DIR) { break; }
            if (m_Registry.any_of<PointLightComponent>(entity)) { continue; }
            auto light = lightsView.get<DirLightComponent>(entity);
            if (!light.isActive) { continue; }
            auto transform = lightsView.get<TransformComponent>(entity);
            dirLightComp[numDirLights] = light;
            // A directional light points along its local forward (-Z), rotated by the
            // entity's orientation. The shader uses lightDir = -direction, so the lit
            // faces are the ones facing into the arrow (the arrow shows where the light
            // travels; surfaces facing the source light up).
            dirLightDir[numDirLights] = glm::normalize(glm::mat3(transform.GetTransform()) * glm::vec3(0.0f, 0.0f, -1.0f));
            numDirLights++;
        }
        // Representative direction for the water surface (uses one sun).
        glm::vec3 lightDir = numDirLights > 0 ? dirLightDir[0] : glm::vec3(0.0f, -1.0f, 0.0f);

        glm::vec3 pointLightPos[4] = {glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(0.0f)};
        PointLightComponent pLightComponent[4];
        int numPointLight = 0;
        auto pLightsView = m_Registry.view<TransformComponent,PointLightComponent>();
        for(auto entity:pLightsView){
            if (numPointLight >= MAX_NUM_POINT_LIGHTS) { break; } // shader supports up to 4
            auto light = pLightsView.get<PointLightComponent>(entity);
            if (!light.isActive) { continue; }
            auto transform = pLightsView.get<TransformComponent>(entity);
            pLightComponent[numPointLight] = light;
            pointLightPos[numPointLight] = transform.Translation;
            numPointLight ++;
        }

        // Emissive (glowing) objects also cast light on their surroundings: add each
        // as a point light coloured by its emissive, until the 4-light cap is reached.
        auto addEmissiveLight = [&](const Material& material, const glm::vec3& position){
            if (numPointLight >= MAX_NUM_POINT_LIGHTS) { return; }
            if (material.emissiveStrength <= 0.0f) { return; }
            if (material.emissive.r <= 0.0f && material.emissive.g <= 0.0f && material.emissive.b <= 0.0f) { return; }
            // The emitted light is GENTLE and decoupled from the (often large) glow
            // strength, and falls off quickly, so several emissive objects don't blow
            // the scene out to white. The strength still drives the bloom in the shader.
            glm::vec3 color = material.emissive * 0.6f;
            PointLightComponent light;
            light.ambient   = glm::vec3(0.0f);
            light.diffuse   = color;
            light.specular  = color;
            light.constant  = 1.0f;
            light.linear    = 0.22f;
            light.quadratic = 0.20f;
            pLightComponent[numPointLight] = light;
            pointLightPos[numPointLight] = position;
            numPointLight ++;
        };
        auto emissiveCubes = m_Registry.view<TransformComponent, CubeComponent>();
        for (auto entity : emissiveCubes){
            addEmissiveLight(emissiveCubes.get<CubeComponent>(entity).material,
                             emissiveCubes.get<TransformComponent>(entity).Translation);
        }
        auto emissiveMeshes = m_Registry.view<TransformComponent, MeshComponent>();
        for (auto entity : emissiveMeshes){
            addEmissiveLight(emissiveMeshes.get<MeshComponent>(entity).material,
                             emissiveMeshes.get<TransformComponent>(entity).Translation);
        }
        auto emissivePrimitives = m_Registry.view<TransformComponent, PrimitiveComponent>();
        for (auto entity : emissivePrimitives){
            addEmissiveLight(emissivePrimitives.get<PrimitiveComponent>(entity).material,
                             emissivePrimitives.get<TransformComponent>(entity).Translation);
        }

        // Directional shadow pass: render scene depth from each shadow-casting light into
        // its own shadow map (all before the main lighting pass). One map per light.
        if (numDirLights > 0){
            PHX_PROFILE("Shadow Pass");

            // Submit every shadow caster once (shared by all lights this frame).
            auto submitCasters = [&](){
                auto cubeCasters = m_Registry.view<CubeComponent, TransformComponent>();
                for (auto entity : cubeCasters)
                    Renderer::SubmitShadowCube(cubeCasters.get<TransformComponent>(entity).GetTransform());

                auto meshCasters = m_Registry.view<MeshComponent, TransformComponent>();
                for (auto entity : meshCasters){
                    auto& mesh = meshCasters.get<MeshComponent>(entity);
                    if (!mesh.model) { continue; }
                    glm::mat4 transform = meshCasters.get<TransformComponent>(entity).GetTransform();
                    auto* anim = m_Registry.try_get<AnimationComponent>(entity);
                    bool animated = anim && anim->animator && mesh.model->HasAnimations();
                    for (const auto& subMesh : mesh.model->GetMeshes()){
                        if (animated)
                            Renderer::SubmitShadowAnimated(subMesh->GetVertexArray(), transform, anim->animator->GetFinalBoneMatrices());
                        else
                            Renderer::SubmitShadow(subMesh->GetVertexArray(), transform);
                    }
                }
                auto terrainCasters = m_Registry.view<TerrainComponent, TransformComponent>();
                for (auto entity : terrainCasters){
                    auto& terrain = terrainCasters.get<TerrainComponent>(entity);
                    if (!terrain.mesh) { continue; } // built lazily in the main pass
                    Renderer::SubmitShadow(terrain.mesh->GetVertexArray(),
                        terrainCasters.get<TransformComponent>(entity).GetTransform());
                }
                auto primitiveCasters = m_Registry.view<PrimitiveComponent, TransformComponent>();
                for (auto entity : primitiveCasters){
                    Ref<Mesh> mesh = GetPrimitiveMesh(primitiveCasters.get<PrimitiveComponent>(entity).type);
                    if (!mesh) { continue; }
                    Renderer::SubmitShadow(mesh->GetVertexArray(),
                        primitiveCasters.get<TransformComponent>(entity).GetTransform());
                }
            };

            const float orthoHalf = 15.0f;
            glm::mat4 lightProj = glm::ortho(-orthoHalf, orthoHalf, -orthoHalf, orthoHalf, 0.1f, 50.0f);
            for (int i = 0; i < numDirLights; i++){
                glm::vec3 dir = dirLightDir[i];
                glm::vec3 up = (std::abs(dir.y) > 0.99f) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
                glm::vec3 center(0.0f);
                glm::mat4 lightView = glm::lookAt(center - dir * 20.0f, center, up);
                glm::mat4 lightSpace = lightProj * lightView;

                // A non-casting light still gets a (cleared = fully lit) map so the 1:1
                // light->map mapping holds and it contributes no shadow.
                Renderer::BeginShadowPass(i, lightSpace);
                if (dirLightComp[i].castsShadow) { submitCasters(); }
                Renderer::EndShadowPass();
            }
        }

        {
            PHX_PROFILE("Scene Components");
            Renderer::BeginScene(sceneCameraProjection, sceneCameraView, cameraPos);
            Renderer::SetLights(m_AmbientColor, dirLightComp, dirLightDir, numDirLights, pLightComponent, pointLightPos, numPointLight);
            auto view = m_Registry.view<CubeComponent, TransformComponent>();
            for (auto entity : view) {
                auto cube = view.get<CubeComponent>(entity);
                auto transform = view.get<TransformComponent>(entity);
                Renderer::SetWireframe(m_Registry.any_of<WireframeComponent>(entity));
                Renderer::SubmitCube(cube.material, transform.GetTransform());
            }
            Renderer::SetWireframe(false);

            auto meshView = m_Registry.view<MeshComponent, TransformComponent>();
            for (auto entity : meshView) {
                auto& mesh = meshView.get<MeshComponent>(entity);
                auto transform = meshView.get<TransformComponent>(entity);
                if (!mesh.model) { continue; }
                mesh.model->Update(); // completes async GPU upload once parsing finishes
                Renderer::SetWireframe(m_Registry.any_of<WireframeComponent>(entity));
                auto* anim = m_Registry.try_get<AnimationComponent>(entity);
                bool animated = anim && anim->animator && mesh.model->HasAnimations();
                for (const auto& subMesh : mesh.model->GetMeshes()) {
                    if (animated)
                        Renderer::SubmitAnimated(subMesh->GetVertexArray(), mesh.material, transform.GetTransform(),
                            anim->animator->GetFinalBoneMatrices(), subMesh->GetDiffuseMap());
                    else
                        Renderer::Submit(subMesh->GetVertexArray(), mesh.material, transform.GetTransform(), subMesh->GetDiffuseMap());
                }
            }
            Renderer::SetWireframe(false);

            auto terrainView = m_Registry.view<TerrainComponent, TransformComponent>();
            for (auto entity : terrainView){
                auto& terrain = terrainView.get<TerrainComponent>(entity);
                auto transform = terrainView.get<TransformComponent>(entity);
                if (terrain.dirty || !terrain.mesh){
                    terrain.mesh = BuildTerrainMesh(terrain);
                    terrain.dirty = false;
                }
                Renderer::SetWireframe(m_Registry.any_of<WireframeComponent>(entity));
                Renderer::Submit(terrain.mesh->GetVertexArray(), terrain.material, transform.GetTransform());
            }
            Renderer::SetWireframe(false);

            auto primitiveView = m_Registry.view<PrimitiveComponent, TransformComponent>();
            for (auto entity : primitiveView){
                auto& primitive = primitiveView.get<PrimitiveComponent>(entity);
                auto transform = primitiveView.get<TransformComponent>(entity);
                Ref<Mesh> mesh = GetPrimitiveMesh(primitive.type);
                if (!mesh) { continue; }
                Renderer::SetWireframe(m_Registry.any_of<WireframeComponent>(entity));
                Renderer::Submit(mesh->GetVertexArray(), primitive.material, transform.GetTransform());
            }
            Renderer::SetWireframe(false);
            Renderer::EndScene();
        }

        // Transparent water surfaces (drawn after the opaque scene). The ripple sim
        // steps in edit AND play so leftover splashes keep propagating and decay.
        {
            auto waterView = m_Registry.view<WaterComponent, TransformComponent>();
            for (auto entity : waterView){
                auto& water = waterView.get<WaterComponent>(entity);
                auto transform = waterView.get<TransformComponent>(entity);
                if (!water.mesh) { water.mesh = BuildGridMesh(water.size, water.resolution); }
                if (water.interactiveRipples && !water.ripples){
                    water.ripples = CreateRef<WaterRipples>(water.rippleResolution);
                }
                else if (!water.interactiveRipples && water.ripples){
                    water.ripples.reset();
                }
                if (water.ripples){
                    water.ripples->Step((float)ts);
                    water.ripples->Upload();
                }

                Renderer::WaterParams params;
                params.color = water.color;
                params.alpha = water.alpha;
                params.lightDir = lightDir;
                params.time = m_Time;
                params.amplitude = water.amplitude;
                params.waveScale = water.waveScale;
                params.speed = water.speed;
                params.choppiness = water.choppiness;
                params.foam = water.foam;
                params.size = water.size;
                params.rippleTexture = water.ripples ? water.ripples->GetTextureID() : 0;
                Renderer::SubmitWater(water.mesh->GetVertexArray(), transform.GetTransform(), params);
            }
        }

        // Selection outline (drawn over the scene, using this frame's camera state).
        if (selectedEntity && selectedEntity.HasComponent<TransformComponent>()){
            const glm::vec3 outlineColor = { 1.0f, 0.5f, 0.1f };
            glm::mat4 transform = selectedEntity.GetComponent<TransformComponent>().GetTransform();

            if (selectedEntity.HasComponent<CubeComponent>()){
                Renderer::DrawOutlineCube(transform, outlineColor);
            }
            if (selectedEntity.HasComponent<MeshComponent>()){
                auto& mesh = selectedEntity.GetComponent<MeshComponent>();
                if (mesh.model){
                    std::vector<Ref<VertexArray>> vertexArrays;
                    for (const auto& subMesh : mesh.model->GetMeshes()){
                        vertexArrays.push_back(subMesh->GetVertexArray());
                    }
                    auto* anim = selectedEntity.HasComponent<AnimationComponent>() ? &selectedEntity.GetComponent<AnimationComponent>() : nullptr;
                    const std::vector<glm::mat4>* bones = (anim && anim->animator && mesh.model->HasAnimations())
                        ? &anim->animator->GetFinalBoneMatrices() : nullptr;
                    Renderer::DrawOutline(vertexArrays, transform, outlineColor, bones);
                }
            }
            if (selectedEntity.HasComponent<PrimitiveComponent>()){
                Ref<Mesh> mesh = GetPrimitiveMesh(selectedEntity.GetComponent<PrimitiveComponent>().type);
                if (mesh){
                    Renderer::DrawOutline({ mesh->GetVertexArray() }, transform, outlineColor);
                }
            }
        }

        // Camera frustum gizmos so camera entities show their position and view angle.
        // Skip the camera we're looking THROUGH — its own frustum would sit in the view.
        {
            auto cameraView = m_Registry.view<CameraComponent, TransformComponent>();
            for (auto entity : cameraView){
                if (entity == activeCamera) { continue; }
                auto& cameraComponent = cameraView.get<CameraComponent>(entity);
                auto& transform = cameraView.get<TransformComponent>(entity);
                float fov = cameraComponent.camera.GetPerspectiveVerticalFOV();
                float aspect = cameraComponent.camera.GetOrthographicAcpectRatio();
                if (aspect <= 0.0f) { aspect = 16.0f / 9.0f; }
                Renderer::DrawCameraGizmo(transform.GetTransform(), fov, aspect, glm::vec3(0.3f, 0.7f, 1.0f));
            }
        }

        // Directional light arrows showing their aim.
        {
            auto dirView = m_Registry.view<DirLightComponent, TransformComponent>();
            for (auto entity : dirView){
                auto& transform = dirView.get<TransformComponent>(entity);
                Renderer::DrawDirLightGizmo(transform.GetTransform(), glm::vec3(1.0f, 0.95f, 0.5f));
            }
        }

        if (m_PhysicsDebug.showColliders) { DrawPhysicsDebug(); }
    }



    void Scene::OnResize(float width, float height){
        m_ViewportWidth = width;
		m_ViewportHeight = height;
        // Update camera component
        auto view = m_Registry.view<CameraComponent>();
        for (auto& entity:view){
            auto& cameraComponent = view.get<CameraComponent>(entity);
            cameraComponent.camera.SetViewportSize(m_ViewportWidth, m_ViewportHeight);
        }
    }

    template<typename T>
	void Scene::OnComponentAdded(Entity entity, T& component){
	}

    template<>
	void Scene::OnComponentAdded<TransformComponent>(Entity entity, TransformComponent& component){
	}

    template<>
	void Scene::OnComponentAdded<CubeComponent>(Entity entity, CubeComponent& component){
	}

    template<>
	void Scene::OnComponentAdded<MeshComponent>(Entity entity, MeshComponent& component){
	}

    template<>
	void Scene::OnComponentAdded<RigidBodyComponent>(Entity entity, RigidBodyComponent& component){
	}

    template<>
	void Scene::OnComponentAdded<BoxColliderComponent>(Entity entity, BoxColliderComponent& component){
	}

    template<>
	void Scene::OnComponentAdded<MeshColliderComponent>(Entity entity, MeshColliderComponent& component){
	}

    template<>
	void Scene::OnComponentAdded<SphereColliderComponent>(Entity entity, SphereColliderComponent& component){
	}

    template<>
	void Scene::OnComponentAdded<CapsuleColliderComponent>(Entity entity, CapsuleColliderComponent& component){
	}

    template<>
	void Scene::OnComponentAdded<CylinderColliderComponent>(Entity entity, CylinderColliderComponent& component){
	}

    template<>
	void Scene::OnComponentAdded<JointComponent>(Entity entity, JointComponent& component){
	}

    template<>
	void Scene::OnComponentAdded<CharacterControllerComponent>(Entity entity, CharacterControllerComponent& component){
	}

    template<>
	void Scene::OnComponentAdded<ForceFieldComponent>(Entity entity, ForceFieldComponent& component){
	}

    template<>
	void Scene::OnComponentAdded<TerrainComponent>(Entity entity, TerrainComponent& component){
	}

    template<>
	void Scene::OnComponentAdded<WaterComponent>(Entity entity, WaterComponent& component){
	}

    template<>
	void Scene::OnComponentAdded<PrimitiveComponent>(Entity entity, PrimitiveComponent& component){
	}

    template<>
	void Scene::OnComponentAdded<AnimationComponent>(Entity entity, AnimationComponent& component){
	}

    template<>
	void Scene::OnComponentAdded<CameraFollowComponent>(Entity entity, CameraFollowComponent& component){
	}

    template<>
	void Scene::OnComponentAdded<WireframeComponent>(Entity entity, WireframeComponent& component){
	}

    template<>
	void Scene::OnComponentAdded<LuaScriptComponent>(Entity entity, LuaScriptComponent& component){
	}



    template<>
	void Scene::OnComponentAdded<TagComponent>(Entity entity, TagComponent& component){
	}

    template<>
	void Scene::OnComponentAdded<NativeScriptComponent>(Entity entity, NativeScriptComponent& component){
	}

    template<>
	void Scene::OnComponentAdded<DirLightComponent>(Entity entity, DirLightComponent& component){
	}

    template<>
	void Scene::OnComponentAdded<PointLightComponent>(Entity entity, PointLightComponent& component){
	}



    template<>
	void Scene::OnComponentAdded<CameraComponent>(Entity entity, CameraComponent& component){
		component.camera.SetViewportSize(m_ViewportWidth, m_ViewportHeight);
	}
}