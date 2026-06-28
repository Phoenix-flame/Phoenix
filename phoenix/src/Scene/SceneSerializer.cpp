#include <Phoenix/Scene/SceneSerializer.h>
#include <Phoenix/Scene/Entity.h>
#include <Phoenix/Scene/Component.h>
#include <Phoenix/renderer/Mesh.h>
#include <Phoenix/core/log.h>

#include <yaml-cpp/yaml.h>
#include <fstream>

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
        out << YAML::EndMap;
    }

    static Material ReadMaterial(const YAML::Node& node){
        Material material;
        material.ambient   = ReadVec3(node["ambient"]);
        material.diffuse   = ReadVec3(node["diffuse"]);
        material.specular  = ReadVec3(node["specular"]);
        material.shininess = node["shininess"].as<float>();
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

        out << YAML::EndMap;
    }

    void SceneSerializer::Serialize(const std::string& filepath){
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "Scene" << YAML::Value << "Untitled";
        out << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;

        for (auto entityID : m_Scene->m_Registry.storage<entt::entity>()){
            if (!m_Scene->m_Registry.valid(entityID)) { continue; }
            Entity entity{ entityID, m_Scene.get() };
            SerializeEntity(out, entity);
        }

        out << YAML::EndSeq;
        out << YAML::EndMap;

        std::ofstream fout(filepath);
        fout << out.c_str();
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

        if (!data["Entities"]){
            PHX_CORE_ERROR("Scene file '{0}' has no entities node", filepath);
            return false;
        }

        for (auto entityNode : data["Entities"]){
            std::string name;
            if (entityNode["TagComponent"])
                name = entityNode["TagComponent"]["Tag"].as<std::string>();

            // CreateEntity already attaches a TransformComponent and a TagComponent.
            Entity entity = m_Scene->CreateEntity(name);

            if (auto tn = entityNode["TransformComponent"]){
                auto& tc = entity.GetComponent<TransformComponent>();
                tc.Translation = ReadVec3(tn["Translation"]);
                tc.Rotation    = ReadVec3(tn["Rotation"]);
                tc.Scale       = ReadVec3(tn["Scale"]);
            }

            if (auto cn = entityNode["CameraComponent"]){
                auto& cc = entity.AddComponent<CameraComponent>();
                cc.primary = cn["Primary"].as<bool>();
                auto& camera = cc.camera;
                camera.SetProjectionType((SceneCamera::ProjectionType)cn["ProjectionType"].as<int>());
                camera.SetPerspectiveVerticalFOV(cn["PerspectiveFOV"].as<float>());
                camera.SetPerspectiveNearClip(cn["PerspectiveNear"].as<float>());
                camera.SetPerspectiveFarClip(cn["PerspectiveFar"].as<float>());
                camera.SetOrthographicSize(cn["OrthographicSize"].as<float>());
                camera.SetOrthographicNearClip(cn["OrthographicNear"].as<float>());
                camera.SetOrthographicFarClip(cn["OrthographicFar"].as<float>());
            }

            if (auto cn = entityNode["CubeComponent"]){
                auto& cube = entity.AddComponent<CubeComponent>();
                cube.material = ReadMaterial(cn["Material"]);
            }

            if (auto pn = entityNode["PointLightComponent"]){
                auto& pl = entity.AddComponent<PointLightComponent>();
                pl.ambient   = ReadVec3(pn["ambient"]);
                pl.diffuse   = ReadVec3(pn["diffuse"]);
                pl.specular  = ReadVec3(pn["specular"]);
                pl.isActive  = pn["isActive"].as<bool>();
                pl.constant  = pn["constant"].as<float>();
                pl.linear    = pn["linear"].as<float>();
                pl.quadratic = pn["quadratic"].as<float>();
                m_Scene->m_NumPointLights++;
            }
            else if (auto dn = entityNode["DirLightComponent"]){
                auto& dl = entity.AddComponent<DirLightComponent>();
                dl.ambient  = ReadVec3(dn["ambient"]);
                dl.diffuse  = ReadVec3(dn["diffuse"]);
                dl.specular = ReadVec3(dn["specular"]);
                dl.isActive = dn["isActive"].as<bool>();
            }

            if (auto mn = entityNode["MeshComponent"]){
                auto& mc = entity.AddComponent<MeshComponent>();
                std::string path = mn["Path"].as<std::string>();
                if (!path.empty())
                    mc.model = CreateRef<Model>(path);
                mc.material = ReadMaterial(mn["Material"]);
            }
        }

        PHX_CORE_INFO("Deserialized scene from '{0}'", filepath);
        return true;
    }
}
