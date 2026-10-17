
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
        props.linearDamping = rb.linearDamping;
        props.angularDamping = rb.angularDamping;
        props.gravityFactor = rb.gravityFactor;
        props.continuousCollision = rb.continuousCollision;
        props.isSensor = rb.isSensor;
        props.initialVelocity = rb.initialVelocity;
        return props;
    }

    // One body for a rigid-body entity. Shape priority: explicit primitive colliders
    // (box/sphere/capsule/cylinder) first, then a mesh collider built from the
    // entity's model or primitive-shape geometry.
    void Scene::CreateRuntimeBody(entt::entity entity){
        if (!m_PhysicsWorld || !m_Registry.all_of<RigidBodyComponent, TransformComponent>(entity)) { return; }
        auto& rb = m_Registry.get<RigidBodyComponent>(entity);
        auto& transform = m_Registry.get<TransformComponent>(entity);
        PhysicsWorld::BodyType type = (PhysicsWorld::BodyType)(int)rb.type;
        PhysicsWorld::BodyProps props = MakeBodyProps(rb);
        const glm::vec3& s = transform.Scale;

        if (auto* box = m_Registry.try_get<BoxColliderComponent>(entity)){
            rb.runtimeBodyID = m_PhysicsWorld->CreateBox(transform.Translation, transform.Rotation,
                box->halfExtents * s, type, props);
        }
        else if (auto* sphere = m_Registry.try_get<SphereColliderComponent>(entity)){
            float scale = std::max(s.x, std::max(s.y, s.z));
            rb.runtimeBodyID = m_PhysicsWorld->CreateSphere(transform.Translation, transform.Rotation,
                sphere->radius * scale, type, props);
        }
        else if (auto* capsule = m_Registry.try_get<CapsuleColliderComponent>(entity)){
            float radialScale = std::max(s.x, s.z);
            rb.runtimeBodyID = m_PhysicsWorld->CreateCapsule(transform.Translation, transform.Rotation,
                capsule->halfHeight * s.y, capsule->radius * radialScale, type, props);
        }
        else if (auto* cylinder = m_Registry.try_get<CylinderColliderComponent>(entity)){
            float radialScale = std::max(s.x, s.z);
            rb.runtimeBodyID = m_PhysicsWorld->CreateCylinder(transform.Translation, transform.Rotation,
                cylinder->halfHeight * s.y, cylinder->radius * radialScale, type, props);
        }
        else if (auto* meshCollider = m_Registry.try_get<MeshColliderComponent>(entity)){
            // Gather geometry (scaled into local space) from the entity's model,
            // or from its generated primitive-shape mesh.
            std::vector<glm::vec3> points;
            std::vector<uint32_t> indices;
            if (auto* mesh = m_Registry.try_get<MeshComponent>(entity)){
                if (!mesh->model || mesh->model->GetMeshes().empty()) { return; } // not loaded yet
                for (const auto& sub : mesh->model->GetMeshes()){
                    uint32_t base = (uint32_t)points.size();
                    for (const auto& p : sub->GetPositions()) { points.push_back(p * s); }
                    for (uint32_t idx : sub->GetIndices())     { indices.push_back(base + idx); }
                }
            }
            else if (auto* primitive = m_Registry.try_get<PrimitiveComponent>(entity)){
                Ref<Mesh> mesh = GetPrimitiveMesh(primitive->type);
                if (!mesh) { return; }
                points.reserve(mesh->GetPositions().size());
                for (const auto& p : mesh->GetPositions()) { points.push_back(p * s); }
                indices = mesh->GetIndices();
            }
            if (points.empty()) { return; }

            if (meshCollider->convex || type != PhysicsWorld::BodyType::Static){
                rb.runtimeBodyID = m_PhysicsWorld->CreateConvexHull(points, transform.Translation,
                    transform.Rotation, type, props);
            }
            else{
                rb.runtimeBodyID = m_PhysicsWorld->CreateMesh(points, indices, transform.Translation,
                    transform.Rotation, props);
            }
        }

        if (rb.runtimeBodyID != 0xffffffff) { m_BodyToEntity[rb.runtimeBodyID] = entity; }
    }

    void Scene::OnRuntimeStart(){
        m_PhysicsWorld = CreateScope<PhysicsWorld>();
        m_BodyToEntity.clear();

        // One body per rigid-body entity.
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
            std::vector<glm::vec3> points;
            points.reserve(terrain.mesh->GetPositions().size());
            for (const auto& p : terrain.mesh->GetPositions()) { points.push_back(p * transform.Scale); }
            terrain.runtimeBodyID = m_PhysicsWorld->CreateMesh(points, terrain.mesh->GetIndices(),
                transform.Translation, transform.Rotation, PhysicsWorld::BodyProps());
            if (terrain.runtimeBodyID != 0xffffffff) { m_BodyToEntity[terrain.runtimeBodyID] = entity; }
        }

        // Joints: connect bodies after they all exist (creation order independent).
        // The connected entity is found by Tag; an empty tag anchors to the world.
        {
            auto jointView = m_Registry.view<JointComponent, RigidBodyComponent>();
            for (auto entity : jointView){
                auto& joint = jointView.get<JointComponent>(entity);
                auto& rb = jointView.get<RigidBodyComponent>(entity);
                if (rb.runtimeBodyID == 0xffffffff) { continue; } // no body (missing collider?)

                uint32_t other = 0xffffffff; // world
                glm::vec3 otherCenter = joint.pivot;
                if (!joint.connectedTag.empty()){
                    bool found = false;
                    auto tagged = m_Registry.view<TagComponent, RigidBodyComponent>();
                    for (auto te : tagged){
                        if (tagged.get<TagComponent>(te).Tag != joint.connectedTag) { continue; }
                        other = tagged.get<RigidBodyComponent>(te).runtimeBodyID;
                        if (m_Registry.any_of<TransformComponent>(te))
                            otherCenter = m_Registry.get<TransformComponent>(te).Translation;
                        found = true;
                        break;
                    }
                    if (!found || other == 0xffffffff){
                        PHX_CORE_WARN("Joint on '{0}': connected body '{1}' not found or has no collider",
                            m_Registry.any_of<TagComponent>(entity) ? m_Registry.get<TagComponent>(entity).Tag : "?",
                            joint.connectedTag);
                        continue;
                    }
                }

                switch (joint.type){
                    case JointComponent::Type::Point:
                        m_PhysicsWorld->AddPointConstraint(rb.runtimeBodyID, other, joint.pivot);
                        break;
                    case JointComponent::Type::Distance:{
                        glm::vec3 own = m_Registry.get<TransformComponent>(entity).Translation;
                        m_PhysicsWorld->AddDistanceConstraint(rb.runtimeBodyID, other, own, otherCenter,
                            joint.minDistance, joint.maxDistance);
                        break;
                    }
                    case JointComponent::Type::Hinge:
                        m_PhysicsWorld->AddHingeConstraint(rb.runtimeBodyID, other, joint.pivot, joint.axis,
                            joint.limitAngles, joint.minAngle, joint.maxAngle);
                        break;
                }
            }
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
                       CylinderColliderComponent, MeshColliderComponent, AnimationComponent, LuaScriptComponent,
                       BoneAttachmentComponent>(
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
        auto view = m_Registry.view<RigidBodyComponent>();
        for (auto entity : view){
            view.get<RigidBodyComponent>(entity).runtimeBodyID = 0xffffffff;
        }
        auto terrainView = m_Registry.view<TerrainComponent>();
        for (auto entity : terrainView){
            terrainView.get<TerrainComponent>(entity).runtimeBodyID = 0xffffffff;
        }
        m_PhysicsWorld.reset();
        m_Scripts.clear();
        m_PendingDestroy.clear();
        m_Hud.clear();
        m_Banner.clear();
        m_BodyToEntity.clear();
        m_SubmergedBodies.clear();
    }

    Entity Scene::FindEntityByBodyID(uint32_t bodyID){
        auto it = m_BodyToEntity.find(bodyID);
        if (it == m_BodyToEntity.end() || !m_Registry.valid(it->second)) { return {}; }
        return { it->second, this };
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
                m_PhysicsWorld->RemoveBody(body);
                m_BodyToEntity.erase(body);
                m_SubmergedBodies.erase(body);
            }
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
            // By index with a held reference: scripts may spawn (appending runtimes).
            for (std::size_t i = 0; i < m_Scripts.size(); i++){
                Ref<LuaScript> script = m_Scripts[i];
                if (!IsPendingDestroy((entt::entity)script->GetEntity())) { script->OnUpdate((float)ts); }
            }
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

            m_PhysicsWorld->Step(physicsDt);

            // Only dynamic bodies are moved by the simulation: static ones never move and
            // kinematic ones follow their entity transform, so reading those back would
            // just overwrite the authored/scripted rotation with an equivalent one.
            for (auto entity : view){
                auto& rb = view.get<RigidBodyComponent>(entity);
                if (rb.runtimeBodyID == 0xffffffff || rb.type != RigidBodyComponent::Type::Dynamic) { continue; }
                auto& transform = view.get<TransformComponent>(entity);
                m_PhysicsWorld->GetBodyTransform(rb.runtimeBodyID, transform.Translation, transform.Rotation);
            }

            // Contact events -> Lua OnCollisionEnter/Exit(otherTag) on both entities'
            // scripts. Deduped per frame (Jolt can report one pair per sub-shape).
            if (!m_Scripts.empty()){
                auto events = m_PhysicsWorld->ConsumeContactEvents();
                std::set<std::tuple<uint32_t, uint32_t, bool>> seen;
                for (const auto& ev : events){
                    uint32_t a = std::min(ev.bodyA, ev.bodyB), b = std::max(ev.bodyA, ev.bodyB);
                    if (!seen.insert({ a, b, ev.entered }).second) { continue; }

                    Entity entityA = FindEntityByBodyID(ev.bodyA);
                    Entity entityB = FindEntityByBodyID(ev.bodyB);
                    if (!entityA || !entityB) { continue; }
                    auto tagOf = [](Entity e){
                        return e.HasComponent<TagComponent>() ? e.GetComponent<TagComponent>().Tag : std::string();
                    };
                    for (std::size_t i = 0; i < m_Scripts.size(); i++){
                        Ref<LuaScript> script = m_Scripts[i];
                        entt::entity se = (entt::entity)script->GetEntity();
                        if (IsPendingDestroy(se)) { continue; }
                        if (se == (entt::entity)entityA)      { script->OnCollision(tagOf(entityB), ev.entered, entityB); }
                        else if (se == (entt::entity)entityB) { script->OnCollision(tagOf(entityA), ev.entered, entityA); }
                    }
                }
            }
            else{
                m_PhysicsWorld->ConsumeContactEvents(); // keep the queue drained
            }
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