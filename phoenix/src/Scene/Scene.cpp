
#include <Phoenix/Scene/Scene.h>
#include <Phoenix/Scene/Entity.h>
#include <Phoenix/Scene/Component.h>
#include <Phoenix/renderer/renderer.h>
#include <Phoenix/renderer/Primitives.h>
#include <Phoenix/core/log.h>
#include <Phoenix/core/Profiler.h>
#include <Phoenix/Physics/Physics.h>
#include <Phoenix/Scene/LuaScript.h>
#include <limits>
#include <cmath>
#include <algorithm>
#include <unordered_map>
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

    void Scene::OnRuntimeStart(){
        m_PhysicsWorld = CreateScope<PhysicsWorld>();

        auto view = m_Registry.view<RigidBodyComponent, BoxColliderComponent, TransformComponent>();
        for (auto entity : view){
            auto& rb = view.get<RigidBodyComponent>(entity);
            auto& collider = view.get<BoxColliderComponent>(entity);
            auto& transform = view.get<TransformComponent>(entity);

            glm::vec3 halfExtents = collider.halfExtents * transform.Scale;
            PhysicsWorld::BodyType type = (PhysicsWorld::BodyType)(int)rb.type;
            rb.runtimeBodyID = m_PhysicsWorld->CreateBox(transform.Translation, transform.Rotation, halfExtents, type);
        }

        // Mesh colliders: build the shape from the entity's mesh geometry (scaled into
        // local space), preferring the box collider above if both are present.
        auto meshColliderView = m_Registry.view<RigidBodyComponent, MeshColliderComponent, MeshComponent, TransformComponent>();
        for (auto entity : meshColliderView){
            auto& rb = meshColliderView.get<RigidBodyComponent>(entity);
            if (rb.runtimeBodyID != 0xffffffff) { continue; } // already has a (box) body
            auto& meshCollider = meshColliderView.get<MeshColliderComponent>(entity);
            auto& mesh = meshColliderView.get<MeshComponent>(entity);
            auto& transform = meshColliderView.get<TransformComponent>(entity);
            if (!mesh.model || mesh.model->GetMeshes().empty()) { continue; } // not loaded yet

            // Gather all sub-mesh geometry, scaled by the transform's scale.
            std::vector<glm::vec3> points;
            std::vector<uint32_t> indices;
            for (const auto& sub : mesh.model->GetMeshes()){
                uint32_t base = (uint32_t)points.size();
                for (const auto& p : sub->GetPositions()) { points.push_back(p * transform.Scale); }
                for (uint32_t idx : sub->GetIndices())     { indices.push_back(base + idx); }
            }

            PhysicsWorld::BodyType type = (PhysicsWorld::BodyType)(int)rb.type;
            if (meshCollider.convex || type != PhysicsWorld::BodyType::Static){
                rb.runtimeBodyID = m_PhysicsWorld->CreateConvexHull(points, transform.Translation, transform.Rotation, type);
            }
            else{
                rb.runtimeBodyID = m_PhysicsWorld->CreateMesh(points, indices, transform.Translation, transform.Rotation);
            }
        }
        // Primitive shapes: build a collider from the generated unit mesh. Convex hull
        // for dynamic/kinematic bodies, exact triangle mesh for static ones.
        auto primColliderView = m_Registry.view<RigidBodyComponent, MeshColliderComponent, PrimitiveComponent, TransformComponent>();
        for (auto entity : primColliderView){
            auto& rb = primColliderView.get<RigidBodyComponent>(entity);
            if (rb.runtimeBodyID != 0xffffffff) { continue; } // already has a body
            auto& meshCollider = primColliderView.get<MeshColliderComponent>(entity);
            auto& primitive = primColliderView.get<PrimitiveComponent>(entity);
            auto& transform = primColliderView.get<TransformComponent>(entity);
            Ref<Mesh> mesh = GetPrimitiveMesh(primitive.type);
            if (!mesh) { continue; }

            std::vector<glm::vec3> points;
            points.reserve(mesh->GetPositions().size());
            for (const auto& p : mesh->GetPositions()) { points.push_back(p * transform.Scale); }

            PhysicsWorld::BodyType type = (PhysicsWorld::BodyType)(int)rb.type;
            if (meshCollider.convex || type != PhysicsWorld::BodyType::Static){
                rb.runtimeBodyID = m_PhysicsWorld->CreateConvexHull(points, transform.Translation, transform.Rotation, type);
            }
            else{
                rb.runtimeBodyID = m_PhysicsWorld->CreateMesh(points, mesh->GetIndices(), transform.Translation, transform.Rotation);
            }
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
            std::vector<glm::vec3> points;
            points.reserve(terrain.mesh->GetPositions().size());
            for (const auto& p : terrain.mesh->GetPositions()) { points.push_back(p * transform.Scale); }
            terrain.runtimeBodyID = m_PhysicsWorld->CreateMesh(points, terrain.mesh->GetIndices(),
                transform.Translation, transform.Rotation);
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
    }

    // Slab-method ray vs axis-aligned box. Returns true and the entry distance if hit.
    static bool RayAABB(const glm::vec3& origin, const glm::vec3& dir,
                        const glm::vec3& boxMin, const glm::vec3& boxMax, float& outT){
        float tMin = 0.0f;
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
        outT = tMin;
        return true;
    }

    Entity Scene::PickEntity(const glm::vec3& rayOrigin, const glm::vec3& rayDirection){
        float nearest = std::numeric_limits<float>::max();
        Entity result;

        auto view = m_Registry.view<TransformComponent>();
        for (auto entity : view){
            auto& transform = view.get<TransformComponent>(entity);
            glm::mat4 model = transform.GetTransform();

            // World-space AABB enclosing the unit cube under this transform.
            glm::vec3 boxMin(std::numeric_limits<float>::max());
            glm::vec3 boxMax(-std::numeric_limits<float>::max());
            for (int i = 0; i < 8; i++){
                glm::vec3 corner((i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f);
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
        if (m_NumPointLights == MAX_NUM_POINT_LIGHTS)
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
        m_NumPointLights ++;
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

    void Scene::DestroyEntity(Entity entity){
        if (entity.HasComponent<PointLightComponent>())
        {
            m_NumPointLights --;
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
            m_PhysicsWorld->Step((float)ts);
            auto view = m_Registry.view<RigidBodyComponent, TransformComponent>();
            for (auto entity : view){
                auto& rb = view.get<RigidBodyComponent>(entity);
                if (rb.runtimeBodyID == 0xffffffff) { continue; }
                auto& transform = view.get<TransformComponent>(entity);
                glm::vec3 position, rotation;
                m_PhysicsWorld->GetBodyTransform(rb.runtimeBodyID, position, rotation);
                transform.Translation = position;
                transform.Rotation = rotation;
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

        // Skeletal animation: advance each animator and recompute its bone matrices.
        // Done before the shadow + main passes so both can read the posed skeleton.
        {
            PHX_PROFILE("Animation");
            auto animView = m_Registry.view<MeshComponent, AnimationComponent>();
            for (auto entity : animView){
                auto& mesh = animView.get<MeshComponent>(entity);
                auto& anim = animView.get<AnimationComponent>(entity);
                if (!mesh.model) { continue; }
                mesh.model->Update(); // ensure GPU upload + animations are available
                if (!mesh.model->IsReady() || !mesh.model->HasAnimations()) { continue; }
                if (!anim.animator){ anim.animator = CreateRef<Animator>(); anim.activeClip = -1; }
                int clip = std::max(0, std::min((int)mesh.model->GetAnimationCount() - 1, anim.clip));
                if (anim.activeClip != clip){
                    anim.animator->PlayAnimation(mesh.model->GetAnimation(clip));
                    anim.activeClip = clip;
                }
                // Always pose the skeleton (advance by 0 when paused). A rigged mesh's
                // vertices live in bone space, so the bone matrices must be computed even
                // at rest -- leaving them identity scatters the mesh instead of assembling
                // it into the bind/rest pose.
                anim.animator->UpdateAnimation(anim.playing ? (float)ts * anim.speed : 0.0f);
            }
        }


        auto cameras = m_Registry.view<TransformComponent,CameraComponent>();
        glm::mat4 sceneCameraProjection = editorCamera.GetProjection();
        glm::mat4 sceneCameraView = editorCamera.GetView();
        glm::vec3 cameraPos = editorCamera.GetPosition();
        bool anyActiveCamera = false;
        for (auto cam:cameras){
            auto camera = cameras.get<CameraComponent>(cam);
            auto transform = cameras.get<TransformComponent>(cam);
            if (camera.primary){
                editorCamera.SetState(false);
                anyActiveCamera = true;
                sceneCameraProjection = camera.camera.GetProjection();
                sceneCameraView = glm::inverse(transform.GetTransform());
                cameraPos = transform.Translation;
                break;
            }
        }
        if (!anyActiveCamera)
        {
            editorCamera.SetState(true);
        }
        glm::vec3 lightDir = glm::vec3(0.0f, -1.0f, 0.0f);
        DirLightComponent lightComponent;
        bool dirLightExists = false;
        auto lightsView = m_Registry.view<TransformComponent,DirLightComponent>();
        for(auto entity:lightsView){
            auto light = lightsView.get<DirLightComponent>(entity);
            if (!light.isActive) { continue; }
            auto transform = lightsView.get<TransformComponent>(entity);
            lightComponent = light;
            // A directional light points along its local forward (-Z), rotated by the
            // entity's orientation. The shader uses lightDir = -direction, so the lit
            // faces are the ones facing into the arrow (the arrow shows where the light
            // travels; surfaces facing the source light up).
            lightDir = glm::normalize(glm::mat3(transform.GetTransform()) * glm::vec3(0.0f, 0.0f, -1.0f));
            dirLightExists = true;
        }

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

        // Directional shadow pass: render scene depth from the light's point of view
        // into the shadow map (must happen before the main lighting pass).
        if (dirLightExists){
            PHX_PROFILE("Shadow Pass");
            glm::vec3 up = (std::abs(lightDir.y) > 0.99f) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
            glm::vec3 center(0.0f);
            glm::vec3 lightPos = center - lightDir * 20.0f;
            glm::mat4 lightView = glm::lookAt(lightPos, center, up);
            const float orthoHalf = 15.0f;
            glm::mat4 lightProj = glm::ortho(-orthoHalf, orthoHalf, -orthoHalf, orthoHalf, 0.1f, 50.0f);
            glm::mat4 lightSpace = lightProj * lightView;

            Renderer::BeginShadowPass(lightSpace);
            auto cubeCasters = m_Registry.view<CubeComponent, TransformComponent>();
            for (auto entity : cubeCasters){
                Renderer::SubmitShadowCube(cubeCasters.get<TransformComponent>(entity).GetTransform());
            }
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
            Renderer::EndShadowPass();
        }

        {
            PHX_PROFILE("Scene Components");
            Renderer::BeginScene(sceneCameraProjection, sceneCameraView, cameraPos);
            Renderer::SetLights(m_AmbientColor, dirLightExists, lightComponent, lightDir, pLightComponent, pointLightPos, numPointLight);
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

        // Transparent water surfaces (drawn after the opaque scene).
        {
            auto waterView = m_Registry.view<WaterComponent, TransformComponent>();
            for (auto entity : waterView){
                auto& water = waterView.get<WaterComponent>(entity);
                auto transform = waterView.get<TransformComponent>(entity);
                if (!water.mesh) { water.mesh = BuildGridMesh(water.size, water.resolution); }
                Renderer::SubmitWater(water.mesh->GetVertexArray(), transform.GetTransform(),
                    water.color, water.alpha, lightDir, m_Time, water.amplitude, water.waveScale, water.speed);
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
        {
            auto cameraView = m_Registry.view<CameraComponent, TransformComponent>();
            for (auto entity : cameraView){
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