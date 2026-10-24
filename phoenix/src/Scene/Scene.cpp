
#include <Phoenix/Scene/Scene.h>
#include <Phoenix/Scene/Entity.h>
#include <Phoenix/Scene/Component.h>
#include <Phoenix/renderer/renderer.h>
#include <Phoenix/renderer/Primitives.h>
#include <Phoenix/core/log.h>
#include <Phoenix/core/Profiler.h>
#include <Phoenix/Physics/Physics.h>
#include <Phoenix/Scene/LuaScript.h>
#include <Phoenix/Math/Math.h>
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
            case PrimitiveComponent::Type::Capsule:  mesh = Primitives::Capsule();  break;
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

    // ---- Shadow helpers ------------------------------------------------------------

    // World AABB of the local box lo..hi under transform m.
    static void TransformBounds(const glm::mat4& m, const glm::vec3& lo, const glm::vec3& hi,
                                glm::vec3& outLo, glm::vec3& outHi){
        glm::vec3 center = glm::vec3(m * glm::vec4((lo + hi) * 0.5f, 1.0f));
        glm::vec3 half = (hi - lo) * 0.5f;
        glm::vec3 extent = glm::abs(glm::vec3(m[0])) * half.x + glm::abs(glm::vec3(m[1])) * half.y
                         + glm::abs(glm::vec3(m[2])) * half.z;
        outLo = center - extent;
        outHi = center + extent;
    }

    // Is the sphere at least partly inside the frustum of view-projection m?
    // (Planes extracted from the matrix rows, Gribb/Hartmann.)
    static bool SphereInFrustum(const glm::mat4& m, const glm::vec3& center, float radius){
        glm::vec4 rows[4];
        for (int i = 0; i < 4; i++) { rows[i] = glm::vec4(m[0][i], m[1][i], m[2][i], m[3][i]); }
        for (int i = 0; i < 3; i++){
            for (float sign : { 1.0f, -1.0f }){
                glm::vec4 plane = rows[3] + sign * rows[i];
                if (glm::dot(glm::vec3(plane), center) + plane.w < -radius * glm::length(glm::vec3(plane))) { return false; }
            }
        }
        return true;
    }

    // Bounding sphere of the part of the view frustum within maxDistance of the camera.
    // Its radius only depends on the projection, so it doesn't change as the camera moves
    // or turns (a stable size keeps directional shadows from shimmering).
    static void ViewSliceSphere(const glm::mat4& projection, const glm::mat4& view, float maxDistance,
                                glm::vec3& outCenter, float& outRadius){
        glm::mat4 invViewProj = glm::inverse(projection * view);
        glm::mat4 camera = glm::inverse(view);
        glm::vec3 camPos = glm::vec3(camera[3]);
        glm::vec3 forward = -glm::normalize(glm::vec3(camera[2]));
        glm::vec3 corners[8];
        int n = 0;
        for (float x : { -1.0f, 1.0f }){
            for (float y : { -1.0f, 1.0f }){
                glm::vec4 a = invViewProj * glm::vec4(x, y, -1.0f, 1.0f);
                glm::vec4 b = invViewProj * glm::vec4(x, y, 1.0f, 1.0f);
                glm::vec3 nearP = glm::vec3(a) / a.w, farP = glm::vec3(b) / b.w;
                float dn = glm::dot(nearP - camPos, forward), df = glm::dot(farP - camPos, forward);
                if (df > maxDistance && df > dn)
                    farP = nearP + (farP - nearP) * ((maxDistance - dn) / (df - dn));
                corners[n++] = nearP;
                corners[n++] = farP;
            }
        }
        glm::vec3 center(0.0f);
        for (const auto& c : corners) { center += c / 8.0f; }
        float radius = 0.0f;
        for (const auto& c : corners) { radius = std::max(radius, glm::length(c - center)); }
        outCenter = center;
        outRadius = std::ceil(radius * 16.0f) / 16.0f; // absorb float noise
    }

    // Orthographic light-space matrix for a directional light covering the sphere
    // (center, radius), deep enough to include every caster in sceneLo..sceneHi. The box
    // moves in whole shadow-map texels so shadow edges don't crawl as it follows the
    // camera.
    static glm::mat4 DirLightMatrix(const glm::vec3& dir, const glm::vec3& center, float radius,
                                    const glm::vec3& sceneLo, const glm::vec3& sceneHi, uint32_t resolution){
        glm::vec3 up = (std::abs(dir.y) > 0.99f) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
        glm::mat4 lightView = glm::lookAt(glm::vec3(0.0f), dir, up); // rotation only

        glm::vec3 c = glm::vec3(lightView * glm::vec4(center, 1.0f));
        float texel = 2.0f * radius / (float)resolution;
        c.x = std::floor(c.x / texel) * texel;
        c.y = std::floor(c.y / texel) * texel;

        // Depth range (view looks down -Z): the sphere plus all casters, with some slack.
        float zNear = -c.z - radius, zFar = -c.z + radius;
        for (int i = 0; i < 8; i++){
            glm::vec3 corner((i & 1) ? sceneHi.x : sceneLo.x, (i & 2) ? sceneHi.y : sceneLo.y, (i & 4) ? sceneHi.z : sceneLo.z);
            float depth = -(lightView * glm::vec4(corner, 1.0f)).z;
            zNear = std::min(zNear, depth);
            zFar = std::max(zFar, depth);
        }
        glm::mat4 projection = glm::ortho(c.x - radius, c.x + radius, c.y - radius, c.y + radius, zNear - 1.0f, zFar + 1.0f);
        return projection * lightView;
    }

    // Distance at which a point light's contribution fades below ~1/64 of its peak;
    // its shadow map doesn't need to reach further.
    static float PointLightRange(const PointLightComponent& light){
        float intensity = std::max({ light.diffuse.r, light.diffuse.g, light.diffuse.b, light.specular.r, light.specular.g, light.specular.b });
        float c = light.constant - 64.0f * intensity, b = light.linear, a = light.quadratic;
        float range = 50.0f;
        if (a > 1e-5f)       { range = (-b + std::sqrt(std::max(b * b - 4.0f * a * c, 0.0f))) / (2.0f * a); }
        else if (b > 1e-5f)  { range = -c / b; }
        return glm::clamp(range, 1.0f, 50.0f);
    }

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
        props.lockTilt = rb.lockTilt;
        props.allowSleeping = rb.allowSleeping;
        props.startAsleep = rb.startAsleep;
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
        if (auto* compound = registry.try_get<CompoundColliderComponent>(entity)){
            for (const auto& shape : compound->shapes){
                PhysicsWorld::ShapeDesc d;
                d.type = (ShapeType)(int)shape.type; // Box/Sphere/Capsule/Cylinder share values
                d.halfExtents = shape.halfExtents * s;
                const float radial = shape.type == CompoundColliderComponent::Shape::Type::Sphere
                    ? std::max(s.x, std::max(s.y, s.z)) : std::max(s.x, s.z);
                d.radius = shape.radius * radial;
                d.halfHeight = shape.halfHeight * s.y;
                d.offset = shape.offset * s;
                d.rotation = shape.rotation;
                out.push_back(d);
            }
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

    static PhysicsWorld::JointDesc MakeJointDesc(const JointComponent::Joint& joint, const glm::vec3& ownCenter,
                                                 const glm::vec3& otherCenter){
        PhysicsWorld::JointDesc d;
        switch (joint.type){
            case JointComponent::Type::Point:    d.type = PhysicsWorld::JointType::Point; break;
            case JointComponent::Type::Distance: d.type = PhysicsWorld::JointType::Distance; break;
            case JointComponent::Type::Hinge:    d.type = PhysicsWorld::JointType::Hinge; break;
            case JointComponent::Type::Fixed:    d.type = PhysicsWorld::JointType::Fixed; break;
            case JointComponent::Type::Slider:   d.type = PhysicsWorld::JointType::Slider; break;
            case JointComponent::Type::Cone:     d.type = PhysicsWorld::JointType::Cone; break;
            case JointComponent::Type::SwingTwist: d.type = PhysicsWorld::JointType::SwingTwist; break;
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
        d.planeAxis = joint.planeAxis;
        d.normalHalfCone = joint.swingAngle1;
        d.planeHalfCone = joint.swingAngle2;
        d.twistMin = joint.twistMin;
        d.twistMax = joint.twistMax;
        d.motorMode = (PhysicsWorld::MotorMode)(int)joint.motor;
        d.motorTarget = joint.motorTarget;
        d.motorMaxForce = joint.motorMaxForce;
        d.friction = joint.friction;
        d.breakForce = joint.breakForce;
        d.collideConnected = joint.collideConnected;
        return d;
    }

    // One body for a rigid-body entity, shaped by all of its collider components
    // (also used for entities spawned while playing).
    void Scene::CreateRuntimeBody(entt::entity entity){
        if (!m_PhysicsWorld || !m_Registry.all_of<RigidBodyComponent, TransformComponent>(entity)) { return; }
        auto& rb = m_Registry.get<RigidBodyComponent>(entity);
        auto& transform = m_Registry.get<TransformComponent>(entity);
        std::vector<PhysicsWorld::ShapeDesc> shapes;
        if (!CollectColliderShapes(m_Registry, entity, transform.Scale, shapes) || shapes.empty()) { return; }
        rb.runtimeBodyID = m_PhysicsWorld->CreateBody(shapes, transform.Translation, transform.Rotation,
            (PhysicsWorld::BodyType)(int)rb.type, MakeBodyProps(rb));
        if (rb.runtimeBodyID != PhysicsWorld::InvalidBody) { m_BodyToEntity[rb.runtimeBodyID] = entity; }
        // Interpolation and external-edit detection start from the current transform.
        rb.prevPosition = rb.currPosition = transform.Translation;
        rb.prevRotation = rb.currRotation = Math::EulerXYZToQuat(transform.Rotation);
        rb.syncedTranslation = transform.Translation;
        rb.syncedRotation = transform.Rotation;
        rb.synced = rb.runtimeBodyID != PhysicsWorld::InvalidBody;
    }

    void Scene::OnRuntimeStart(){
        PhysicsWorld::Settings settings;
        settings.gravity = m_Simulation.gravity;
        settings.velocitySteps = m_PhysicsSettings.velocitySteps;
        settings.positionSteps = m_PhysicsSettings.positionSteps;
        settings.maxSubstepRate = m_PhysicsSettings.substepRate;
        for (int i = 0; i < PhysicsWorld::MaxLayers; i++) { settings.layerMasks[i] = m_PhysicsSettings.layerMasks[i]; }
        m_PhysicsWorld = CreateScope<PhysicsWorld>(settings);
        m_SimTime = 0.0f;
        m_ContactLog.clear();
        m_BodyToEntity.clear();
        m_JointToEntity.clear();
        m_SensorPairs.clear();
        m_DebugContacts.clear();

        // One body per rigid-body entity, shaped by all of its collider components.
        std::vector<entt::entity> bodies;
        for (auto entity : m_Registry.view<RigidBodyComponent, TransformComponent>()) { bodies.push_back(entity); }
        for (auto entity : bodies) { CreateRuntimeBody(entity); }

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
                auto& jc = jointView.get<JointComponent>(entity);
                auto& rb = jointView.get<RigidBodyComponent>(entity);
                for (int index = 0; index < (int)jc.joints.size(); index++){
                    auto& joint = jc.joints[index];
                    joint.runtimeJointID = PhysicsWorld::InvalidJoint;
                    if (rb.runtimeBodyID == PhysicsWorld::InvalidBody) { continue; } // no body (missing collider?)

                    uint32_t other = PhysicsWorld::InvalidBody; // world
                    glm::vec3 otherCenter = joint.pivot;
                    if (!joint.connectedTag.empty()){
                        Entity target = FindEntityByTag(joint.connectedTag);
                        if (target && target.HasComponent<RigidBodyComponent>())
                            other = target.GetComponent<RigidBodyComponent>().runtimeBodyID;
                        if (other == PhysicsWorld::InvalidBody || (entt::entity)target == entity){
                            PHX_CORE_WARN("Joint {0} on '{1}': connected body '{2}' not found or has no collider", index + 1,
                                m_Registry.any_of<TagComponent>(entity) ? m_Registry.get<TagComponent>(entity).Tag : "?",
                                joint.connectedTag);
                            continue;
                        }
                        otherCenter = target.GetComponent<TransformComponent>().Translation;
                    }

                    glm::vec3 own = m_Registry.get<TransformComponent>(entity).Translation;
                    joint.runtimeJointID = m_PhysicsWorld->AddJoint(rb.runtimeBodyID, other, MakeJointDesc(joint, own, otherCenter));
                    if (joint.runtimeJointID != PhysicsWorld::InvalidJoint) { m_JointToEntity[joint.runtimeJointID] = { entity, index }; }
                }
            }
        }

        CreateVehicles();

        // Fixed-step interpolation and external-edit detection start from the
        // edit-time layout.
        m_PhysicsAccumulator = 0.0f;
        for (auto entity : m_Registry.view<RigidBodyComponent, TransformComponent>()){
            auto& rb = m_Registry.get<RigidBodyComponent>(entity);
            auto& t = m_Registry.get<TransformComponent>(entity);
            rb.prevPosition = rb.currPosition = t.Translation;
            rb.prevRotation = rb.currRotation = Math::EulerXYZToQuat(t.Rotation);
            rb.syncedTranslation = t.Translation;
            rb.syncedRotation = t.Rotation;
            rb.synced = rb.runtimeBodyID != PhysicsWorld::InvalidBody;
        }
        for (auto entity : m_Registry.view<CharacterControllerComponent, TransformComponent>()){
            auto& cc = m_Registry.get<CharacterControllerComponent>(entity);
            auto& t = m_Registry.get<TransformComponent>(entity);
            cc.prevPosition = cc.currPosition = cc.syncedTranslation = t.Translation;
            cc.synced = cc.runtimeCharacterID != PhysicsWorld::InvalidCharacter;
        }
        for (auto entity : m_Registry.view<RagdollComponent>()){
            auto& rd = m_Registry.get<RagdollComponent>(entity);
            rd.parts.clear();
            rd.active = false;
            rd.request = rd.startActive ? 1 : -1;
        }

        m_PhysicsWorld->OptimizeBroadPhase();

        // Instantiate Lua scripts for the runtime.
        // Snapshot the ids first: a script's top-level code could create entities.
        m_ScriptLog.clear();
        m_Shared.clear();
        m_PendingDestroy.clear();
        m_Hud.clear();
        m_Banner.clear();
        auto scriptView = m_Registry.view<LuaScriptComponent>();
        std::vector<entt::entity> scripted(scriptView.begin(), scriptView.end());
        for (auto entity : scripted)
            ReloadScripts(Entity{ entity, this });
    }

    LuaScript* Scene::FindScript(Entity entity, int index){
        for (auto& script : m_Scripts)
            if ((entt::entity)script->GetEntity() == (entt::entity)entity && script->GetIndex() == index)
                return script.get();
        return nullptr;
    }

    void Scene::ReloadScripts(Entity entity){
        if (!IsRunning()) { return; }
        m_Scripts.erase(std::remove_if(m_Scripts.begin(), m_Scripts.end(),
            [&](const Ref<LuaScript>& script){ return (entt::entity)script->GetEntity() == (entt::entity)entity; }),
            m_Scripts.end());
        StartScripts(entity, nullptr);
    }

    void Scene::StartScripts(Entity entity, const ScriptArgs* args){
        if (!m_Registry.valid((entt::entity)entity) || !entity.HasComponent<LuaScriptComponent>()) { return; }
        // Copy: a script's top-level code runs in the constructor and may add
        // components or spawn entities, which can move the component in memory.
        std::vector<LuaScriptEntry> entries = entity.GetComponent<LuaScriptComponent>().scripts;
        for (int i = 0; i < (int)entries.size(); i++)
            if (entries[i].enabled)
                m_Scripts.push_back(CreateRef<LuaScript>(entries[i].source, entity, i, entries[i].name, args));
    }

    Entity Scene::FindEntityByTag(const std::string& tag){
        for (auto entity : m_Registry.view<TagComponent>())
            if (m_Registry.get<TagComponent>(entity).Tag == tag && !IsPendingDestroy(entity))
                return Entity{ entity, this };
        return {};
    }

    // Component types a spawned copy takes from its template.
    template<typename... Components>
    static void CopyComponents(entt::registry& registry, entt::entity from, entt::entity to){
        ([&]{
            if (auto* c = registry.try_get<Components>(from)) { registry.emplace_or_replace<Components>(to, *c); }
        }(), ...);
    }

    Entity Scene::Spawn(const std::string& templateTag, const glm::vec3& position, const ScriptArgs& args){
        if (!IsRunning()) { return {}; }
        Entity source = FindEntityByTag(templateTag);
        if (!source) { return {}; }

        Entity e = CreateEntity(templateTag);
        CopyComponents<TransformComponent, MeshComponent, CubeComponent, PrimitiveComponent, WireframeComponent,
                       RigidBodyComponent, BoxColliderComponent, SphereColliderComponent, CapsuleColliderComponent,
                       CylinderColliderComponent, MeshColliderComponent, CompoundColliderComponent, AnimationComponent,
                       LuaScriptComponent, BoneAttachmentComponent>(
            m_Registry, source, e);
        auto& t = e.GetComponent<TransformComponent>();
        t.Translation = position;
        auto yaw = args.find("yaw");
        if (yaw != args.end())
            if (const double* d = std::get_if<double>(&yaw->second)) { t.Rotation.y = (float)*d; }
        if (auto* rb = m_Registry.try_get<RigidBodyComponent>(e)){
            rb->runtimeBodyID = 0xffffffff; // the template's body isn't ours
            CreateRuntimeBody(e);
        }
        StartScripts(e, &args);
        return e;
    }

    void Scene::UpdateBoneAttachments(){
        auto view = m_Registry.view<BoneAttachmentComponent, TransformComponent>();
        for (auto entity : view){
            const auto& att = view.get<BoneAttachmentComponent>(entity);
            Entity target = FindEntityByTag(att.target);
            if (!target || (entt::entity)target == entity || !target.HasComponent<MeshComponent>()) { continue; }
            const auto& mesh = target.GetComponent<MeshComponent>();
            BoneInfo bone;
            if (!mesh.model || !mesh.model->FindBone(att.bone, bone)) { continue; }

            // Bone pose in model space: the skinning matrix undoes the bind pose
            // (offset), so multiply it back. Without an animator: the bind pose.
            glm::mat4 boneModel = glm::inverse(bone.offset);
            if (auto* anim = m_Registry.try_get<AnimationComponent>((entt::entity)target)){
                if (anim->animator){
                    const auto& finals = anim->animator->GetFinalBoneMatrices();
                    if (bone.id >= 0 && bone.id < (int)finals.size()) { boneModel = finals[bone.id] * boneModel; }
                }
            }
            glm::mat4 world = target.GetComponent<TransformComponent>().GetTransform() * boneModel;

            // Keep the bone's position and orientation but drop its scale (the
            // target's 0.01-ish import scale), then apply the attachment offset.
            glm::mat4 basis(1.0f);
            for (int c = 0; c < 3; c++) { basis[c] = glm::vec4(glm::normalize(glm::vec3(world[c])), 0.0f); }
            basis[3] = world[3];
            glm::mat4 local = glm::translate(glm::mat4(1.0f), att.offset)
                * glm::rotate(glm::mat4(1.0f), att.rotation.x, { 1, 0, 0 })
                * glm::rotate(glm::mat4(1.0f), att.rotation.y, { 0, 1, 0 })
                * glm::rotate(glm::mat4(1.0f), att.rotation.z, { 0, 0, 1 });
            glm::vec3 translation, rotation, scale;
            if (Math::DecomposeTransform(basis * local, translation, rotation, scale)){
                auto& t = view.get<TransformComponent>(entity);
                t.Translation = translation;
                t.Rotation = rotation;
            }
        }
    }

    void Scene::QueueDestroy(Entity entity){
        if (m_Registry.valid((entt::entity)entity)) { m_PendingDestroy.insert((entt::entity)entity); }
    }

    void Scene::FlushPendingDestroy(){
        if (m_PendingDestroy.empty()) { return; }
        std::vector<entt::entity> doomed(m_PendingDestroy.begin(), m_PendingDestroy.end());
        m_PendingDestroy.clear();
        for (auto entity : doomed)
            if (m_Registry.valid(entity)) { DestroyEntity(Entity{ entity, this }); }
    }

    void Scene::SendScriptMessage(Entity target, const std::string& name, const std::vector<ScriptValue>& args, Entity sender){
        if (!m_Registry.valid((entt::entity)target) || IsPendingDestroy((entt::entity)target)) { return; }
        if (m_MessageDepth >= 16) { return; } // a message loop between scripts
        m_MessageDepth++;
        // Snapshot: a handler may spawn (adding runtimes) while we deliver.
        std::vector<Ref<LuaScript>> receivers;
        for (auto& script : m_Scripts)
            if ((entt::entity)script->GetEntity() == (entt::entity)target) { receivers.push_back(script); }
        for (auto& script : receivers) { script->OnMessage(name, args, sender); }
        m_MessageDepth--;
    }

    void Scene::ShowHudText(const std::string& key, const std::string& text, const glm::vec3& color){
        auto it = m_Hud.find(key);
        if (it == m_Hud.end()) { m_Hud[key] = { text, color, m_HudOrder++ }; }
        else { it->second.text = text; it->second.color = color; }
    }

    bool Scene::IsValid(Entity entity) const{
        return m_Registry.valid((entt::entity)entity);
    }

    void Scene::LogScript(Entity entity, int script, const std::string& scriptName, bool error,
                          const std::string& text, int line){
        if (!m_ScriptLog.empty()){
            ScriptLogEntry& last = m_ScriptLog.back();
            if (last.entity == (entt::entity)entity && last.script == script && last.error == error && last.text == text){
                last.count++;
                last.time = m_Time;
                return;
            }
        }
        ScriptLogEntry entry;
        entry.entity = (entt::entity)entity;
        if (entity.HasComponent<TagComponent>()) { entry.entityName = entity.GetComponent<TagComponent>().Tag; }
        entry.script = script;
        entry.scriptName = scriptName;
        entry.error = error;
        entry.line = line;
        entry.time = m_Time;
        entry.text = text;
        m_ScriptLog.push_back(std::move(entry));
        while (m_ScriptLog.size() > 2000) { m_ScriptLog.pop_front(); }
    }

    void Scene::OnRuntimeStop(){
        for (auto entity : m_Registry.view<RigidBodyComponent>())
            m_Registry.get<RigidBodyComponent>(entity).runtimeBodyID = PhysicsWorld::InvalidBody;
        for (auto entity : m_Registry.view<TerrainComponent>())
            m_Registry.get<TerrainComponent>(entity).runtimeBodyID = PhysicsWorld::InvalidBody;
        for (auto entity : m_Registry.view<RigidBodyComponent>())
            m_Registry.get<RigidBodyComponent>(entity).synced = false;
        for (auto entity : m_Registry.view<JointComponent>())
            for (auto& joint : m_Registry.get<JointComponent>(entity).joints) { joint.runtimeJointID = PhysicsWorld::InvalidJoint; }
        for (auto entity : m_Registry.view<VehicleComponent>()){
            auto& v = m_Registry.get<VehicleComponent>(entity);
            v.runtimeVehicleID = PhysicsWorld::InvalidVehicle;
            v.speed = v.rpm = 0.0f;
            v.gear = 0;
        }
        for (auto entity : m_Registry.view<RagdollComponent>()){
            auto& rd = m_Registry.get<RagdollComponent>(entity);
            rd.parts.clear();
            rd.active = false;
            rd.request = -1;
        }
        for (auto entity : m_Registry.view<CharacterControllerComponent>()){
            auto& cc = m_Registry.get<CharacterControllerComponent>(entity);
            cc.runtimeCharacterID = PhysicsWorld::InvalidCharacter;
            cc.synced = false;
            cc.grounded = false;
            cc.velocity = glm::vec3(0.0f);
        }
        m_Scripts.clear(); // scripts may reference bodies; drop them before the world
        m_PhysicsWorld.reset();
        m_PendingDestroy.clear();
        m_Hud.clear();
        m_Banner.clear();
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

    // Characters after a step: record the position (written back interpolated) and
    // state, and turn toward the movement.
    void Scene::UpdateCharactersPostStep(float dt){
        auto view = m_Registry.view<CharacterControllerComponent, TransformComponent>();
        for (auto entity : view){
            auto& cc = view.get<CharacterControllerComponent>(entity);
            if (cc.runtimeCharacterID == PhysicsWorld::InvalidCharacter) { continue; }
            auto& transform = view.get<TransformComponent>(entity);
            cc.prevPosition = cc.currPosition;
            cc.currPosition = m_PhysicsWorld->GetCharacterPosition(cc.runtimeCharacterID);
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

            Entity entityA = FindEntityByBodyID(ev.bodyA);
            Entity entityB = FindEntityByBodyID(ev.bodyB);
            if (!entityA || !entityB) { continue; }

            // Contact log for the Python physics API. Bounded: a client that never
            // drains it only keeps the latest events.
            if (m_ContactLog.size() >= 4096) { m_ContactLog.erase(m_ContactLog.begin(), m_ContactLog.begin() + 2048); }
            m_ContactLog.push_back({ (entt::entity)entityA, (entt::entity)entityB, ev.entered, m_SimTime });

            // By index with a held reference: callbacks may spawn (appending runtimes).
            for (std::size_t i = 0; i < m_Scripts.size(); i++){
                Ref<LuaScript> script = m_Scripts[i];
                entt::entity se = (entt::entity)script->GetEntity();
                if (IsPendingDestroy(se)) { continue; }
                bool isA = se == (entt::entity)entityA, isB = se == (entt::entity)entityB;
                if (!isA && !isB) { continue; }
                Entity other = isA ? entityB : entityA;
                if (sensor) { script->OnTrigger(tagOf(other), ev.entered, other); }
                else{
                    // The normal points from A to B; flip it for B so it always points
                    // from the script's entity towards the other one.
                    script->OnCollision(tagOf(other), ev.entered, other, ev.point, isA ? ev.normal : -ev.normal, ev.speed);
                }
            }
        }

        for (uint32_t joint : broken){
            auto it = m_JointToEntity.find(joint);
            if (it == m_JointToEntity.end() || !m_Registry.valid(it->second.first)) { continue; }
            Entity owner{ it->second.first, this };
            auto& jc = owner.GetComponent<JointComponent>();
            if (it->second.second >= (int)jc.joints.size()) { m_JointToEntity.erase(it); continue; }
            auto& j = jc.joints[it->second.second];
            j.runtimeJointID = PhysicsWorld::InvalidJoint;
            Entity other = j.connectedTag.empty() ? Entity{} : FindEntityByTag(j.connectedTag);
            for (std::size_t i = 0; i < m_Scripts.size(); i++){
                Ref<LuaScript> script = m_Scripts[i];
                entt::entity se = (entt::entity)script->GetEntity();
                if (IsPendingDestroy(se)) { continue; }
                if (se == (entt::entity)owner)       { script->OnJointBreak(j.connectedTag); }
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
                it = (it->second.first == (entt::entity)entity) ? m_JointToEntity.erase(it) : std::next(it);
            if (auto* rd = m_Registry.try_get<RagdollComponent>(entity)) { DeactivateRagdoll(entity, *rd); }
            m_Scripts.erase(std::remove_if(m_Scripts.begin(), m_Scripts.end(),
                [&](const Ref<LuaScript>& script){ return (entt::entity)script->GetEntity() == (entt::entity)entity; }),
                m_Scripts.end());
        }
        m_Registry.destroy(entity);
    }

    void Scene::ApplyWaterCoupling(float dt){
        auto view = m_Registry.view<RigidBodyComponent, TransformComponent>();
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
                    const glm::vec3& bodyPos = rb.currPosition; // this step's pose (not the interpolated one)
                    float lx = bodyPos.x - wTransform.Translation.x;
                    float lz = bodyPos.z - wTransform.Translation.z;
                    if (std::abs(lx) > half || std::abs(lz) > half) { continue; }

                    float u = lx / water.size + 0.5f;
                    float v = lz / water.size + 0.5f;
                    float surfaceY = wTransform.Translation.y
                        + WaterWaveHeight(water, { bodyPos.x, bodyPos.z }, m_Time)
                        + (water.ripples ? water.ripples->SampleHeight(u, v) : 0.0f);
                    if (bodyPos.y - surfaceY > 4.0f){ // far above: can't touch
                        m_SubmergedBodies.erase(rb.runtimeBodyID);
                        continue;
                    }

                    // Jolt's buoyancy factor is fluid density RELATIVE TO THE BODY
                    // (>1 floats, <1 sinks), so derive it from the body's density:
                    // water.buoyancy is the water's density in units of 1000 kg/m3.
                    float factor = std::min(8.0f, water.buoyancy * 1000.0f / std::max(1.0f, rb.density));
                    bool touching = m_PhysicsWorld->ApplyBuoyancy(rb.runtimeBodyID,
                        { bodyPos.x, surfaceY, bodyPos.z }, { 0.0f, 1.0f, 0.0f },
                        factor, water.linearDrag, water.angularDrag, dt);

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
                                    std::min(0.05f, 0.25f * speed * dt));
                            }
                        }
                    }
                    if (touching) { m_SubmergedBodies.insert(rb.runtimeBodyID); }
                    else          { m_SubmergedBodies.erase(rb.runtimeBodyID); }
                }
            }
        }

    }

    // One frame of physics for callers that drive the scene themselves (tools, tests):
    // fixed steps of Simulation().fixedTimestep from an accumulator, then transforms
    // interpolated between the last two steps. Play mode steps through the RL
    // Environment instead, which calls StepSimulation once per fixed step.
    void Scene::UpdatePhysics(float ts){
        if (!m_PhysicsWorld) { return; }
        const float frameDt = std::min(std::max(ts, 0.0f), PhysicsWorld::MaxStep);
        const float h = std::clamp(m_Simulation.fixedTimestep, 1.0f / 1000.0f, 0.1f);
        m_PhysicsAccumulator += frameDt;
        int steps = (int)std::floor(m_PhysicsAccumulator / h + 1e-4f);
        m_PhysicsAccumulator = std::max(0.0f, m_PhysicsAccumulator - steps * h);
        RunPhysics(steps, h, std::clamp(m_PhysicsAccumulator / h, 0.0f, 1.0f));
    }

    // `steps` physics steps of h seconds with everything that has to happen per step
    // (kinematic targets, water, characters, force fields, events), then the poses
    // written back blended by alpha between the last two steps.
    void Scene::RunPhysics(int steps, float h, float alpha){
        if (!m_PhysicsWorld) { return; }
        ProcessRagdollRequests();
        SyncExternalEdits();

        UpdateVehiclesPreStep();
        auto view = m_Registry.view<RigidBodyComponent, TransformComponent>();
        for (int step = 0; step < steps; step++){
            // Kinematic bodies follow their entity transform (scripts/animation move
            // it); MoveKinematic over the frame's remaining steps gives them a steady
            // velocity that pushes dynamic bodies properly.
            for (auto entity : view){
                auto& rb = view.get<RigidBodyComponent>(entity);
                if (rb.runtimeBodyID == PhysicsWorld::InvalidBody || rb.type != RigidBodyComponent::Type::Kinematic) { continue; }
                auto& transform = view.get<TransformComponent>(entity);
                m_PhysicsWorld->MoveKinematic(rb.runtimeBodyID, transform.Translation, transform.Rotation, h * (steps - step));
            }
            ApplyWaterCoupling(h);
            UpdateCharactersPreStep(h);
            ApplyForceFields();

            m_PhysicsWorld->Step(h);

            // Keep the last two poses of every dynamic body for interpolation.
            for (auto entity : view){
                auto& rb = view.get<RigidBodyComponent>(entity);
                if (rb.runtimeBodyID == PhysicsWorld::InvalidBody || rb.type != RigidBodyComponent::Type::Dynamic) { continue; }
                rb.prevPosition = rb.currPosition;
                rb.prevRotation = rb.currRotation;
                m_PhysicsWorld->GetBodyPose(rb.runtimeBodyID, rb.currPosition, rb.currRotation);
            }
            UpdateCharactersPostStep(h);
            DispatchPhysicsEvents();
        }

        // Write back. Only dynamic bodies are moved by the simulation: static ones never
        // move and kinematic ones follow their entity transform.
        for (auto entity : view){
            auto& rb = view.get<RigidBodyComponent>(entity);
            if (rb.runtimeBodyID == PhysicsWorld::InvalidBody || rb.type != RigidBodyComponent::Type::Dynamic) { continue; }
            auto& transform = view.get<TransformComponent>(entity);
            transform.Translation = glm::mix(rb.prevPosition, rb.currPosition, alpha);
            transform.Rotation = Math::QuatToEulerXYZ(glm::slerp(rb.prevRotation, rb.currRotation, alpha), transform.Rotation);
            rb.syncedTranslation = transform.Translation;
            rb.syncedRotation = transform.Rotation;
        }
        for (auto entity : m_Registry.view<CharacterControllerComponent, TransformComponent>()){
            auto& cc = m_Registry.get<CharacterControllerComponent>(entity);
            if (cc.runtimeCharacterID == PhysicsWorld::InvalidCharacter) { continue; }
            auto& transform = m_Registry.get<TransformComponent>(entity);
            transform.Translation = glm::mix(cc.prevPosition, cc.currPosition, alpha);
            cc.syncedTranslation = transform.Translation;
        }
        UpdateVehiclesPostStep();
        UpdateRagdollFollow();
    }

    // Anything that moved a body's entity since physics last wrote it (the gizmo while
    // playing, SetTranslation from Lua, undo...) teleports the body there; dynamic
    // bodies also lose their velocity, so a dragged object hangs where it is dropped.
    void Scene::SyncExternalEdits(){
        for (auto entity : m_Registry.view<RigidBodyComponent, TransformComponent>()){
            auto& rb = m_Registry.get<RigidBodyComponent>(entity);
            if (!rb.synced || rb.type == RigidBodyComponent::Type::Kinematic) { continue; }
            auto& t = m_Registry.get<TransformComponent>(entity);
            if (t.Translation == rb.syncedTranslation && t.Rotation == rb.syncedRotation) { continue; }
            m_PhysicsWorld->SetBodyTransform(rb.runtimeBodyID, t.Translation, t.Rotation);
            if (rb.type == RigidBodyComponent::Type::Dynamic){
                m_PhysicsWorld->SetLinearVelocity(rb.runtimeBodyID, glm::vec3(0.0f));
                m_PhysicsWorld->SetAngularVelocity(rb.runtimeBodyID, glm::vec3(0.0f));
            }
            rb.prevPosition = rb.currPosition = t.Translation;
            rb.prevRotation = rb.currRotation = Math::EulerXYZToQuat(t.Rotation);
            rb.syncedTranslation = t.Translation;
            rb.syncedRotation = t.Rotation;
        }
        for (auto entity : m_Registry.view<CharacterControllerComponent, TransformComponent>()){
            auto& cc = m_Registry.get<CharacterControllerComponent>(entity);
            if (!cc.synced) { continue; }
            auto& t = m_Registry.get<TransformComponent>(entity);
            if (t.Translation == cc.syncedTranslation) { continue; }
            m_PhysicsWorld->SetCharacterPosition(cc.runtimeCharacterID, t.Translation);
            cc.prevPosition = cc.currPosition = cc.syncedTranslation = t.Translation;
        }
    }

    void Scene::OnUpdate(EditorCamera& editorCamera, Timestep ts, Entity selectedEntity){
        if (IsRunning())
            StepSimulation((float)ts);
        OnRender(editorCamera, ts, selectedEntity);
    }

    std::vector<LuaScript*> Scene::FindScripts(entt::entity entity){
        std::vector<LuaScript*> scripts;
        for (auto& script : m_Scripts)
            if ((entt::entity)script->GetEntity() == entity) { scripts.push_back(script.get()); }
        return scripts;
    }

    void Scene::StepSimulation(float dt){
        Timestep ts = dt;
        m_SimTime += dt;

        // Lua scripts run while playing (they may move/recolour their entities).
        if (!m_Scripts.empty()){
            PHX_PROFILE("Lua Scripts");
            // By index with a held reference: scripts may spawn (appending runtimes).
            for (std::size_t i = 0; i < m_Scripts.size(); i++){
                Ref<LuaScript> script = m_Scripts[i];
                if (!IsPendingDestroy((entt::entity)script->GetEntity())) { script->OnUpdate((float)ts); }
            }
        }

        // Physics: one step of dt (the Environment already runs fixed steps).
        if (m_PhysicsWorld){
            PHX_PROFILE("Physics Step");
            RunPhysics(1, dt, 1.0f);
        }

    }

    void Scene::OnRender(EditorCamera& editorCamera, Timestep ts, Entity selectedEntity){
        m_Time += (float)ts; // drives water animation (advances in edit + play)

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
                    if (anim.inPlace) { mesh.model->RemoveRootMotion(); }
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

                // A live ragdoll poses the skeleton from its bodies instead.
                if (auto* ragdoll = m_Registry.try_get<RagdollComponent>(entity); ragdoll && ragdoll->active){
                    PoseRagdoll(Entity{ entity, this }, *ragdoll, animator);
                    continue;
                }

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
                        for (std::size_t i = 0; i < m_Scripts.size(); i++){
                            Ref<LuaScript> script = m_Scripts[i];
                            if ((entt::entity)script->GetEntity() == entity) { script->OnAnimationEvent(ev.name); }
                        }
                    }
                }
            }
        }


        // Bone attachments follow their (now posed) bones.
        UpdateBoneAttachments();

        // Entities scripts destroyed this frame go now, before anything is drawn.
        FlushPendingDestroy();

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
                        // Trail the target's yaw (shortest way round) so turns and
                        // step turns swing the camera instead of snapping it.
                        if (follow.followYaw && follow.yawSmoothing > 0.0f){
                            if (!follow.smoothedYawValid) { follow.smoothedYaw = yaw; follow.smoothedYawValid = true; }
                            float diff = std::remainder(yaw - follow.smoothedYaw, glm::two_pi<float>());
                            follow.smoothedYaw += diff * (1.0f - std::exp(-follow.yawSmoothing * (float)ts));
                            yaw = follow.smoothedYaw;
                        }
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
        int numRealPointLights = 0; // the entity point lights come first; emissive ones after
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
        numRealPointLights = numPointLight;

        // Emissive (glowing) objects also cast light on their surroundings: add each
        // as a point light coloured by its emissive, until the 4-light cap is reached.
        auto addEmissiveLight = [&](const Material& material, const glm::vec3& position){
            if (numPointLight >= MAX_NUM_POINT_LIGHTS) { return; }
            if (material.emissiveStrength <= 0.0f) { return; }
            if (material.emissive.r <= 0.0f && material.emissive.g <= 0.0f && material.emissive.b <= 0.0f) { return; }
            // A glowing object right at a point light is that light's bulb: the light
            // already lights the scene (and casts the shadows), so don't add a second,
            // unshadowed one that would wash its shadows out.
            for (int i = 0; i < numRealPointLights; i++)
                if (glm::length(pointLightPos[i] - position) < 0.75f) { return; }
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
            light.castsShadow = false; // it sits inside its own object
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

        // Shadow passes: render scene depth from each shadow-casting light into its tile
        // of the shadow atlases (directional: one tile; point: six cube-face tiles), all
        // before the main lighting pass.
        {
            PHX_PROFILE("Shadow Pass");

            // Gather every caster once with a world bounding sphere (for per-pass culling),
            // and the bounds of everything drawn (to size the directional shadow boxes).
            struct ShadowCaster{
                Ref<VertexArray> vertexArray;
                glm::mat4 transform;
                const std::vector<glm::mat4>* bones; // skinned when non-null
                glm::vec3 center;
                float radius;
                Ref<Texture2D> alphaMap; // cut-out casters: only opaque texels cast
                float alphaCutoff;
            };
            std::vector<ShadowCaster> casters;
            glm::vec3 sceneLo(std::numeric_limits<float>::max()), sceneHi(-std::numeric_limits<float>::max());
            auto addCaster = [&](const Ref<VertexArray>& va, const glm::mat4& transform, const std::vector<glm::mat4>* bones,
                                 const glm::vec3& lo, const glm::vec3& hi,
                                 const Ref<Texture2D>& alphaMap = nullptr, float alphaCutoff = 0.0f){
                casters.push_back({ va, transform, bones, (lo + hi) * 0.5f, glm::length(hi - lo) * 0.5f, alphaMap, alphaCutoff });
            };
            auto addBounds = [&](const glm::mat4& transform, const glm::vec3& localLo, const glm::vec3& localHi,
                                 glm::vec3& lo, glm::vec3& hi){
                TransformBounds(transform, localLo, localHi, lo, hi);
                sceneLo = glm::min(sceneLo, lo);
                sceneHi = glm::max(sceneHi, hi);
            };
            const glm::vec3 unitLo(-0.5f), unitHi(0.5f);
            // Geometry on a light entity is its marker / bulb: it sits at (or around) the
            // light and would block it, so it never casts.
            auto isLight = [&](entt::entity entity){
                return m_Registry.any_of<DirLightComponent, PointLightComponent>(entity);
            };

            if (Renderer::AreShadowsEnabled()){
                auto cubes = m_Registry.view<CubeComponent, TransformComponent>();
                Ref<Mesh> cubeMesh = GetPrimitiveMesh(PrimitiveComponent::Type::Cube);
                for (auto entity : cubes){
                    glm::mat4 transform = cubes.get<TransformComponent>(entity).GetTransform();
                    glm::vec3 lo, hi;
                    addBounds(transform, unitLo, unitHi, lo, hi);
                    if (cubes.get<CubeComponent>(entity).material.castShadows && !isLight(entity))
                        addCaster(cubeMesh->GetVertexArray(), transform, nullptr, lo, hi);
                }

                auto primitives = m_Registry.view<PrimitiveComponent, TransformComponent>();
                for (auto entity : primitives){
                    auto& primitive = primitives.get<PrimitiveComponent>(entity);
                    Ref<Mesh> mesh = GetPrimitiveMesh(primitive.type);
                    if (!mesh) { continue; }
                    glm::mat4 transform = primitives.get<TransformComponent>(entity).GetTransform();
                    glm::vec3 lo, hi;
                    addBounds(transform, unitLo, unitHi, lo, hi);
                    if (primitive.material.castShadows && !isLight(entity))
                        addCaster(mesh->GetVertexArray(), transform, nullptr, lo, hi);
                }

                auto meshes = m_Registry.view<MeshComponent, TransformComponent>();
                for (auto entity : meshes){
                    auto& mesh = meshes.get<MeshComponent>(entity);
                    if (!mesh.model) { continue; }
                    mesh.model->Update(); // completes async GPU upload once parsing finishes
                    glm::vec3 localLo, localHi;
                    if (!mesh.model->GetLocalBounds(localLo, localHi)) { continue; }
                    auto* anim = m_Registry.try_get<AnimationComponent>(entity);
                    bool animated = anim && anim->animator && mesh.model->HasAnimations();
                    if (animated){
                        // Bind-pose bounds; leave room for the animated pose.
                        glm::vec3 pad = (localHi - localLo) * 0.25f;
                        localLo -= pad; localHi += pad;
                    }
                    glm::mat4 transform = meshes.get<TransformComponent>(entity).GetTransform();
                    glm::vec3 lo, hi;
                    addBounds(transform, localLo, localHi, lo, hi);
                    if (!mesh.material.castShadows || isLight(entity)) { continue; }
                    for (const auto& subMesh : mesh.model->GetMeshes())
                        addCaster(subMesh->GetVertexArray(), transform, animated ? &anim->animator->GetFinalBoneMatrices() : nullptr, lo, hi,
                                  subMesh->GetDiffuseMap(), subMesh->GetAlphaCutoff());
                }

                auto terrains = m_Registry.view<TerrainComponent, TransformComponent>();
                for (auto entity : terrains){
                    auto& terrain = terrains.get<TerrainComponent>(entity);
                    if (terrain.dirty || !terrain.mesh){
                        terrain.mesh = BuildTerrainMesh(terrain);
                        terrain.dirty = false;
                    }
                    float minH = 0.0f, maxH = 0.0f;
                    if (!terrain.heights.empty()){
                        auto range = std::minmax_element(terrain.heights.begin(), terrain.heights.end());
                        minH = *range.first; maxH = *range.second;
                    }
                    float half = terrain.size * 0.5f;
                    glm::mat4 transform = terrains.get<TransformComponent>(entity).GetTransform();
                    glm::vec3 lo, hi;
                    addBounds(transform, { -half, minH, -half }, { half, maxH, half }, lo, hi);
                    if (terrain.material.castShadows)
                        addCaster(terrain.mesh->GetVertexArray(), transform, nullptr, lo, hi);
                }
            }

            auto submitCasters = [&](const glm::mat4& lightSpace){
                for (const auto& caster : casters){
                    if (!SphereInFrustum(lightSpace, caster.center, caster.radius)) { continue; }
                    if (caster.bones) { Renderer::SubmitShadowAnimated(caster.vertexArray, caster.transform, *caster.bones, caster.alphaMap, caster.alphaCutoff); }
                    else              { Renderer::SubmitShadow(caster.vertexArray, caster.transform, caster.alphaMap, caster.alphaCutoff); }
                }
            };

            const bool haveScene = !casters.empty();
            int numDirShadows = 0, numPointShadows = 0;
            for (int i = 0; i < numDirLights; i++)
                if (haveScene && dirLightComp[i].castsShadow) { numDirShadows++; }
            for (int i = 0; i < numRealPointLights; i++)
                if (haveScene && pLightComponent[i].castsShadow) { numPointShadows++; }

            Renderer::BeginShadows(numDirShadows, numPointShadows);
            if (numDirShadows > 0){
                const uint32_t resolution = Renderer::GetDirShadowResolution(numDirShadows);
                glm::vec3 sceneCenter = (sceneLo + sceneHi) * 0.5f;
                float sceneRadius = std::ceil(glm::length(sceneHi - sceneLo) * 0.5f * 2.0f) / 2.0f;
                for (int i = 0; i < numDirLights; i++){
                    if (!dirLightComp[i].castsShadow) { continue; }
                    // Cover the view out to the shadow distance, or the whole scene when
                    // that's smaller (sharper shadows for compact scenes).
                    glm::vec3 center = sceneCenter;
                    float radius = sceneRadius;
                    glm::vec3 viewCenter;
                    float viewRadius;
                    ViewSliceSphere(sceneCameraProjection, sceneCameraView, std::max(dirLightComp[i].shadowDistance, 1.0f), viewCenter, viewRadius);
                    if (viewRadius < radius) { center = viewCenter; radius = viewRadius; }
                    radius = std::max(radius, 0.5f);

                    glm::mat4 lightSpace = DirLightMatrix(dirLightDir[i], center, radius, sceneLo, sceneHi, resolution);
                    Renderer::BeginShadowPass(i, lightSpace, dirLightComp[i].shadow);
                    submitCasters(lightSpace);
                    Renderer::EndShadowPass();
                }
            }
            for (int i = 0; i < numRealPointLights && numPointShadows > 0; i++){
                if (!pLightComponent[i].castsShadow) { continue; }
                float range = PointLightRange(pLightComponent[i]);
                for (int face = 0; face < 6; face++){
                    Renderer::BeginPointShadowPass(i, face, pointLightPos[i], range, pLightComponent[i].shadow);
                    submitCasters(Renderer::GetPointShadowMatrix(pointLightPos[i], face, range));
                    Renderer::EndShadowPass();
                }
            }
        }

        {
            PHX_PROFILE("Scene Components");
            // The sun disc sits where the first directional light shines from.
            Renderer::SetSky(m_Sky, numDirLights > 0, -lightDir, numDirLights > 0 ? dirLightComp[0].diffuse : glm::vec3(0.0f));
            Renderer::BeginScene(sceneCameraProjection, sceneCameraView, cameraPos);
            Renderer::SetLights(m_AmbientColor, dirLightComp, dirLightDir, numDirLights, pLightComponent, pointLightPos, numPointLight);
            Renderer::DrawSky();
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
                            anim->animator->GetFinalBoneMatrices(), subMesh->GetDiffuseMap(),
                            subMesh->GetNormalMap(), subMesh->GetAlphaCutoff());
                    else
                        Renderer::Submit(subMesh->GetVertexArray(), mesh.material, transform.GetTransform(), subMesh->GetDiffuseMap(),
                            subMesh->GetNormalMap(), subMesh->GetAlphaCutoff());
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
	void Scene::OnComponentAdded<AgentComponent>(Entity entity, AgentComponent& component){
	}

    template<>
	void Scene::OnComponentAdded<RaySensorComponent>(Entity entity, RaySensorComponent& component){
	}

    template<>
	void Scene::OnComponentAdded<RewardZoneComponent>(Entity entity, RewardZoneComponent& component){
	}

    template<>
	void Scene::OnComponentAdded<RandomizerComponent>(Entity entity, RandomizerComponent& component){
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
	void Scene::OnComponentAdded<CompoundColliderComponent>(Entity entity, CompoundColliderComponent& component){
	}

    template<>
	void Scene::OnComponentAdded<VehicleComponent>(Entity entity, VehicleComponent& component){
	}

    template<>
	void Scene::OnComponentAdded<RagdollComponent>(Entity entity, RagdollComponent& component){
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
	void Scene::OnComponentAdded<BoneAttachmentComponent>(Entity entity, BoneAttachmentComponent& component){
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