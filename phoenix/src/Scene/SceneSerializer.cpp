#include <Phoenix/Scene/SceneSerializer.h>
#include <Phoenix/Scene/Entity.h>
#include <Phoenix/Scene/Component.h>
#include <Phoenix/renderer/Mesh.h>
#include <Phoenix/core/log.h>

#include <yaml-cpp/yaml.h>
#include <fstream>
#include <unordered_set>
#include <vector>

namespace Phoenix{

    static YAML::Emitter& operator<<(YAML::Emitter& out, const glm::vec3& v){
        out << YAML::Flow << YAML::BeginSeq << v.x << v.y << v.z << YAML::EndSeq;
        return out;
    }

    static glm::vec3 ReadVec3(const YAML::Node& node){
        return { node[0].as<float>(), node[1].as<float>(), node[2].as<float>() };
    }

    static void SerializeMaterial(YAML::Emitter& out, const Material& material){
        out << YAML::Key << "Material" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "ambient"   << YAML::Value << material.ambient;
        out << YAML::Key << "diffuse"   << YAML::Value << material.diffuse;
        out << YAML::Key << "specular"  << YAML::Value << material.specular;
        out << YAML::Key << "shininess" << YAML::Value << material.shininess;
        out << YAML::Key << "emissive"  << YAML::Value << material.emissive;
        out << YAML::Key << "emissiveStrength" << YAML::Value << material.emissiveStrength;
        out << YAML::Key << "reflectivity" << YAML::Value << material.reflectivity;
        out << YAML::EndMap;
    }

    static Material ReadMaterial(const YAML::Node& node){
        Material material;
        material.ambient   = ReadVec3(node["ambient"]);
        material.diffuse   = ReadVec3(node["diffuse"]);
        material.specular  = ReadVec3(node["specular"]);
        material.shininess = node["shininess"].as<float>();
        if (node["emissive"])         material.emissive = ReadVec3(node["emissive"]);
        if (node["emissiveStrength"]) material.emissiveStrength = node["emissiveStrength"].as<float>();
        if (node["reflectivity"])     material.reflectivity = node["reflectivity"].as<float>();
        return material;
    }

    SceneSerializer::SceneSerializer(const Ref<Scene>& scene) : m_Scene(scene) {}

    static void SerializeEntity(YAML::Emitter& out, Entity entity){
        out << YAML::BeginMap;
        out << YAML::Key << "Entity" << YAML::Value << (uint32_t)entity;

        if (entity.HasComponent<TagComponent>()){
            out << YAML::Key << "TagComponent" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "Tag" << YAML::Value << entity.GetComponent<TagComponent>().Tag;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<TransformComponent>()){
            auto& tc = entity.GetComponent<TransformComponent>();
            out << YAML::Key << "TransformComponent" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "Translation" << YAML::Value << tc.Translation;
            out << YAML::Key << "Rotation"    << YAML::Value << tc.Rotation;
            out << YAML::Key << "Scale"       << YAML::Value << tc.Scale;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<CameraComponent>()){
            auto& cc = entity.GetComponent<CameraComponent>();
            auto& camera = cc.camera;
            out << YAML::Key << "CameraComponent" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "Primary" << YAML::Value << cc.primary;
            out << YAML::Key << "ProjectionType" << YAML::Value << (int)camera.GetProjectionType();
            out << YAML::Key << "PerspectiveFOV"  << YAML::Value << camera.GetPerspectiveVerticalFOV();
            out << YAML::Key << "PerspectiveNear" << YAML::Value << camera.GetPerspectiveNearClip();
            out << YAML::Key << "PerspectiveFar"  << YAML::Value << camera.GetPerspectiveFarClip();
            out << YAML::Key << "OrthographicSize" << YAML::Value << camera.GetOrthographicSize();
            out << YAML::Key << "OrthographicNear" << YAML::Value << camera.GetOrthographicNearClip();
            out << YAML::Key << "OrthographicFar"  << YAML::Value << camera.GetOrthographicFarClip();
            out << YAML::EndMap;
        }

        if (entity.HasComponent<CubeComponent>()){
            out << YAML::Key << "CubeComponent" << YAML::Value << YAML::BeginMap;
            SerializeMaterial(out, entity.GetComponent<CubeComponent>().material);
            out << YAML::EndMap;
        }

        // PointLightComponent derives from DirLightComponent, so check it first and
        // serialize only one of the two for a given entity.
        if (entity.HasComponent<PointLightComponent>()){
            auto& pl = entity.GetComponent<PointLightComponent>();
            out << YAML::Key << "PointLightComponent" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "ambient"   << YAML::Value << pl.ambient;
            out << YAML::Key << "diffuse"   << YAML::Value << pl.diffuse;
            out << YAML::Key << "specular"  << YAML::Value << pl.specular;
            out << YAML::Key << "isActive"  << YAML::Value << pl.isActive;
            out << YAML::Key << "constant"  << YAML::Value << pl.constant;
            out << YAML::Key << "linear"    << YAML::Value << pl.linear;
            out << YAML::Key << "quadratic" << YAML::Value << pl.quadratic;
            out << YAML::EndMap;
        }
        else if (entity.HasComponent<DirLightComponent>()){
            auto& dl = entity.GetComponent<DirLightComponent>();
            out << YAML::Key << "DirLightComponent" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "ambient"  << YAML::Value << dl.ambient;
            out << YAML::Key << "diffuse"  << YAML::Value << dl.diffuse;
            out << YAML::Key << "specular" << YAML::Value << dl.specular;
            out << YAML::Key << "isActive" << YAML::Value << dl.isActive;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<MeshComponent>()){
            auto& mc = entity.GetComponent<MeshComponent>();
            out << YAML::Key << "MeshComponent" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "Path" << YAML::Value << (mc.model ? mc.model->GetPath() : std::string());
            SerializeMaterial(out, mc.material);
            out << YAML::EndMap;
        }

        if (entity.HasComponent<RigidBodyComponent>()){
            auto& rb = entity.GetComponent<RigidBodyComponent>();
            out << YAML::Key << "RigidBodyComponent" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "Type" << YAML::Value << (int)rb.type;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<BoxColliderComponent>()){
            auto& bc = entity.GetComponent<BoxColliderComponent>();
            out << YAML::Key << "BoxColliderComponent" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "HalfExtents" << YAML::Value << bc.halfExtents;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<MeshColliderComponent>()){
            out << YAML::Key << "MeshColliderComponent" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "Convex" << YAML::Value << entity.GetComponent<MeshColliderComponent>().convex;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<WireframeComponent>()){
            out << YAML::Key << "WireframeComponent" << YAML::Value << YAML::BeginMap << YAML::EndMap;
        }

        if (entity.HasComponent<TerrainComponent>()){
            auto& t = entity.GetComponent<TerrainComponent>();
            out << YAML::Key << "TerrainComponent" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "Resolution" << YAML::Value << t.resolution;
            out << YAML::Key << "Size" << YAML::Value << t.size;
            out << YAML::Key << "GenerateCollider" << YAML::Value << t.generateCollider;
            SerializeMaterial(out, t.material);
            out << YAML::Key << "Heights" << YAML::Value << YAML::Flow << YAML::BeginSeq;
            for (float h : t.heights) { out << h; }
            out << YAML::EndSeq;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<WaterComponent>()){
            auto& w = entity.GetComponent<WaterComponent>();
            out << YAML::Key << "WaterComponent" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "Size" << YAML::Value << w.size;
            out << YAML::Key << "Resolution" << YAML::Value << w.resolution;
            out << YAML::Key << "Color" << YAML::Value << w.color;
            out << YAML::Key << "Alpha" << YAML::Value << w.alpha;
            out << YAML::Key << "Amplitude" << YAML::Value << w.amplitude;
            out << YAML::Key << "WaveScale" << YAML::Value << w.waveScale;
            out << YAML::Key << "Speed" << YAML::Value << w.speed;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<AnimationComponent>()){
            auto& a = entity.GetComponent<AnimationComponent>();
            out << YAML::Key << "AnimationComponent" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "Clip" << YAML::Value << a.clip;
            out << YAML::Key << "Playing" << YAML::Value << a.playing;
            out << YAML::Key << "Speed" << YAML::Value << a.speed;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<PrimitiveComponent>()){
            auto& p = entity.GetComponent<PrimitiveComponent>();
            out << YAML::Key << "PrimitiveComponent" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "Type" << YAML::Value << (int)p.type;
            SerializeMaterial(out, p.material);
            out << YAML::EndMap;
        }

        if (entity.HasComponent<LuaScriptComponent>()){
            out << YAML::Key << "LuaScriptComponent" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "Source" << YAML::Value << entity.GetComponent<LuaScriptComponent>().source;
            out << YAML::EndMap;
        }

        out << YAML::EndMap;
    }

    std::string SceneSerializer::SerializeToString(){
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "Scene" << YAML::Value << "Untitled";
        out << YAML::Key << "Ambient" << YAML::Value << m_Scene->AmbientColor();
        out << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;

        for (auto entityID : m_Scene->m_Registry.storage<entt::entity>()){
            if (!m_Scene->m_Registry.valid(entityID)) { continue; }
            Entity entity{ entityID, m_Scene.get() };
            SerializeEntity(out, entity);
        }

        out << YAML::EndSeq;
        out << YAML::EndMap;
        return std::string(out.c_str());
    }

    void SceneSerializer::Serialize(const std::string& filepath){
        std::ofstream fout(filepath);
        fout << SerializeToString();
        PHX_CORE_INFO("Serialized scene to '{0}'", filepath);
    }

    bool SceneSerializer::Deserialize(const std::string& filepath){
        YAML::Node data;
        try{
            data = YAML::LoadFile(filepath);
        }
        catch (const std::exception& e){
            PHX_CORE_ERROR("Failed to load scene '{0}': {1}", filepath, e.what());
            return false;
        }
        return DeserializeNode(data);
    }

    bool SceneSerializer::DeserializeFromString(const std::string& data){
        YAML::Node root;
        try{
            root = YAML::Load(data);
        }
        catch (const std::exception& e){
            PHX_CORE_ERROR("Failed to parse scene snapshot: {0}", e.what());
            return false;
        }
        return DeserializeNode(root);
    }

    // Sync a single entity's components to match `node`: update existing, add
    // missing, remove ones no longer present. A MeshComponent's Model is reused
    // when the path is unchanged, so undo/redo of unrelated edits never reloads it.
    void SceneSerializer::ApplyEntity(Entity entity, const YAML::Node& node){
        if (node["TagComponent"])
            entity.GetComponent<TagComponent>().Tag = node["TagComponent"]["Tag"].as<std::string>();

        if (auto n = node["TransformComponent"]){
            auto& tc = entity.GetComponent<TransformComponent>();
            tc.Translation = ReadVec3(n["Translation"]);
            tc.Rotation    = ReadVec3(n["Rotation"]);
            tc.Scale       = ReadVec3(n["Scale"]);
        }

        if (auto n = node["CameraComponent"]){
            auto& cc = entity.HasComponent<CameraComponent>() ? entity.GetComponent<CameraComponent>() : entity.AddComponent<CameraComponent>();
            cc.primary = n["Primary"].as<bool>();
            auto& camera = cc.camera;
            camera.SetProjectionType((SceneCamera::ProjectionType)n["ProjectionType"].as<int>());
            camera.SetPerspectiveVerticalFOV(n["PerspectiveFOV"].as<float>());
            camera.SetPerspectiveNearClip(n["PerspectiveNear"].as<float>());
            camera.SetPerspectiveFarClip(n["PerspectiveFar"].as<float>());
            camera.SetOrthographicSize(n["OrthographicSize"].as<float>());
            camera.SetOrthographicNearClip(n["OrthographicNear"].as<float>());
            camera.SetOrthographicFarClip(n["OrthographicFar"].as<float>());
        }
        else if (entity.HasComponent<CameraComponent>()) entity.RemoveComponent<CameraComponent>();

        if (auto n = node["CubeComponent"]){
            auto& cube = entity.HasComponent<CubeComponent>() ? entity.GetComponent<CubeComponent>() : entity.AddComponent<CubeComponent>();
            cube.material = ReadMaterial(n["Material"]);
        }
        else if (entity.HasComponent<CubeComponent>()) entity.RemoveComponent<CubeComponent>();

        if (auto n = node["PointLightComponent"]){
            auto& pl = entity.HasComponent<PointLightComponent>() ? entity.GetComponent<PointLightComponent>() : entity.AddComponent<PointLightComponent>();
            pl.ambient   = ReadVec3(n["ambient"]);
            pl.diffuse   = ReadVec3(n["diffuse"]);
            pl.specular  = ReadVec3(n["specular"]);
            pl.isActive  = n["isActive"].as<bool>();
            pl.constant  = n["constant"].as<float>();
            pl.linear    = n["linear"].as<float>();
            pl.quadratic = n["quadratic"].as<float>();
        }
        else if (entity.HasComponent<PointLightComponent>()) entity.RemoveComponent<PointLightComponent>();

        if (auto n = node["DirLightComponent"]){
            auto& dl = entity.HasComponent<DirLightComponent>() ? entity.GetComponent<DirLightComponent>() : entity.AddComponent<DirLightComponent>();
            dl.ambient  = ReadVec3(n["ambient"]);
            dl.diffuse  = ReadVec3(n["diffuse"]);
            dl.specular = ReadVec3(n["specular"]);
            dl.isActive = n["isActive"].as<bool>();
        }
        else if (entity.HasComponent<DirLightComponent>()) entity.RemoveComponent<DirLightComponent>();

        if (auto n = node["MeshComponent"]){
            auto& mc = entity.HasComponent<MeshComponent>() ? entity.GetComponent<MeshComponent>() : entity.AddComponent<MeshComponent>();
            std::string path = n["Path"].as<std::string>();
            if (path.empty())
                mc.model = nullptr;
            else if (!mc.model || mc.model->GetPath() != path)
                mc.model = CreateRef<Model>(path); // only (re)load when the path actually changed
            mc.material = ReadMaterial(n["Material"]);
        }
        else if (entity.HasComponent<MeshComponent>()) entity.RemoveComponent<MeshComponent>();

        if (auto n = node["RigidBodyComponent"]){
            auto& rb = entity.HasComponent<RigidBodyComponent>() ? entity.GetComponent<RigidBodyComponent>() : entity.AddComponent<RigidBodyComponent>();
            rb.type = (RigidBodyComponent::Type)n["Type"].as<int>();
        }
        else if (entity.HasComponent<RigidBodyComponent>()) entity.RemoveComponent<RigidBodyComponent>();

        if (auto n = node["BoxColliderComponent"]){
            auto& bc = entity.HasComponent<BoxColliderComponent>() ? entity.GetComponent<BoxColliderComponent>() : entity.AddComponent<BoxColliderComponent>();
            bc.halfExtents = ReadVec3(n["HalfExtents"]);
        }
        else if (entity.HasComponent<BoxColliderComponent>()) entity.RemoveComponent<BoxColliderComponent>();

        if (auto n = node["MeshColliderComponent"]){
            auto& mc = entity.HasComponent<MeshColliderComponent>() ? entity.GetComponent<MeshColliderComponent>() : entity.AddComponent<MeshColliderComponent>();
            mc.convex = n["Convex"].as<bool>();
        }
        else if (entity.HasComponent<MeshColliderComponent>()) entity.RemoveComponent<MeshColliderComponent>();

        if (node["WireframeComponent"]){
            if (!entity.HasComponent<WireframeComponent>()) entity.AddComponent<WireframeComponent>();
        }
        else if (entity.HasComponent<WireframeComponent>()) entity.RemoveComponent<WireframeComponent>();

        if (auto n = node["TerrainComponent"]){
            auto& t = entity.HasComponent<TerrainComponent>() ? entity.GetComponent<TerrainComponent>() : entity.AddComponent<TerrainComponent>();
            t.resolution = n["Resolution"].as<int>();
            t.size = n["Size"].as<float>();
            t.generateCollider = n["GenerateCollider"].as<bool>();
            t.material = ReadMaterial(n["Material"]);
            t.heights.clear();
            for (auto h : n["Heights"]) { t.heights.push_back(h.as<float>()); }
            if ((int)t.heights.size() != t.resolution * t.resolution) { t.heights.resize((size_t)t.resolution * t.resolution, 0.0f); }
            t.dirty = true;
            t.mesh = nullptr;
        }
        else if (entity.HasComponent<TerrainComponent>()) entity.RemoveComponent<TerrainComponent>();

        if (auto n = node["WaterComponent"]){
            auto& w = entity.HasComponent<WaterComponent>() ? entity.GetComponent<WaterComponent>() : entity.AddComponent<WaterComponent>();
            w.size = n["Size"].as<float>();
            w.resolution = n["Resolution"].as<int>();
            w.color = ReadVec3(n["Color"]);
            w.alpha = n["Alpha"].as<float>();
            w.amplitude = n["Amplitude"].as<float>();
            w.waveScale = n["WaveScale"].as<float>();
            w.speed = n["Speed"].as<float>();
            w.mesh = nullptr;
        }
        else if (entity.HasComponent<WaterComponent>()) entity.RemoveComponent<WaterComponent>();

        if (auto n = node["AnimationComponent"]){
            auto& a = entity.HasComponent<AnimationComponent>() ? entity.GetComponent<AnimationComponent>() : entity.AddComponent<AnimationComponent>();
            a.clip = n["Clip"].as<int>();
            a.playing = n["Playing"].as<bool>();
            a.speed = n["Speed"].as<float>();
            a.animator = nullptr; // transient; recreated at runtime
            a.activeClip = -1;
        }
        else if (entity.HasComponent<AnimationComponent>()) entity.RemoveComponent<AnimationComponent>();

        if (auto n = node["PrimitiveComponent"]){
            auto& p = entity.HasComponent<PrimitiveComponent>() ? entity.GetComponent<PrimitiveComponent>() : entity.AddComponent<PrimitiveComponent>();
            p.type = (PrimitiveComponent::Type)n["Type"].as<int>();
            p.material = ReadMaterial(n["Material"]);
        }
        else if (entity.HasComponent<PrimitiveComponent>()) entity.RemoveComponent<PrimitiveComponent>();

        if (auto n = node["LuaScriptComponent"]){
            auto& s = entity.HasComponent<LuaScriptComponent>() ? entity.GetComponent<LuaScriptComponent>() : entity.AddComponent<LuaScriptComponent>();
            s.source = n["Source"].as<std::string>();
        }
        else if (entity.HasComponent<LuaScriptComponent>()) entity.RemoveComponent<LuaScriptComponent>();
    }

    void SceneSerializer::RecountPointLights(){
        int count = 0;
        for (auto e : m_Scene->m_Registry.view<PointLightComponent>()) { (void)e; count++; }
        m_Scene->m_NumPointLights = count;
    }

    bool SceneSerializer::DeserializeNode(const YAML::Node& data){
        if (!data["Entities"]){
            PHX_CORE_ERROR("Scene data has no entities node");
            return false;
        }

        if (data["Ambient"]) m_Scene->AmbientColor() = ReadVec3(data["Ambient"]);

        for (auto entityNode : data["Entities"]){
            std::string name;
            if (entityNode["TagComponent"])
                name = entityNode["TagComponent"]["Tag"].as<std::string>();

            // CreateEntity already attaches a TransformComponent and a TagComponent.
            Entity entity = m_Scene->CreateEntity(name);
            ApplyEntity(entity, entityNode);
        }

        RecountPointLights();
        return true;
    }

    bool SceneSerializer::ApplyFromString(const std::string& data){
        YAML::Node root;
        try{
            root = YAML::Load(data);
        }
        catch (const std::exception& e){
            PHX_CORE_ERROR("Failed to parse scene snapshot: {0}", e.what());
            return false;
        }
        if (!root["Entities"]){
            PHX_CORE_ERROR("Scene data has no entities node");
            return false;
        }

        if (root["Ambient"]) m_Scene->AmbientColor() = ReadVec3(root["Ambient"]);

        auto& registry = m_Scene->m_Registry;
        const YAML::Node entities = root["Entities"];

        // Collect the snapshot's entity ids.
        std::unordered_set<uint32_t> targetIds;
        for (std::size_t i = 0; i < entities.size(); i++)
            targetIds.insert(entities[i]["Entity"].as<uint32_t>());

        // Destroy live entities not present in the snapshot (collect first to avoid
        // mutating the storage while iterating it).
        std::vector<entt::entity> toDestroy;
        for (auto handle : registry.storage<entt::entity>()){
            if (!registry.valid(handle)) { continue; }
            if (targetIds.find((uint32_t)handle) == targetIds.end())
                toDestroy.push_back(handle);
        }
        for (auto handle : toDestroy)
            m_Scene->DestroyEntity(Entity{ handle, m_Scene.get() });

        // Update matching entities in place; create any that are missing.
        for (std::size_t i = 0; i < entities.size(); i++){
            YAML::Node node = entities[i];
            entt::entity handle = (entt::entity)node["Entity"].as<uint32_t>();
            Entity entity;
            if (registry.valid(handle))
                entity = Entity{ handle, m_Scene.get() };
            else{
                std::string name;
                if (node["TagComponent"]) name = node["TagComponent"]["Tag"].as<std::string>();
                entity = m_Scene->CreateEntity(name);
            }
            ApplyEntity(entity, node);
        }

        RecountPointLights();
        return true;
    }
}
