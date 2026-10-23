#include <Phoenix/Scene/SceneSerializer.h>
#include <Phoenix/Scene/Entity.h>
#include <Phoenix/Scene/Scene.h>
#include <Phoenix/Scene/Component.h>
#include <Phoenix/renderer/Mesh.h>
#include <Phoenix/core/log.h>

#include <yaml-cpp/yaml.h>
#include <fstream>
#include <algorithm>
#include <unordered_set>
#include <vector>

namespace Phoenix{

    static YAML::Emitter& operator<<(YAML::Emitter& out, const glm::vec3& v){
        out << YAML::Flow << YAML::BeginSeq << v.x << v.y << v.z << YAML::EndSeq;
        return out;
    }

    // Readers fall back to the given default when a key is missing or malformed (older
    // or hand-edited scene files), instead of throwing and aborting the whole load.
    static glm::vec3 ReadVec3(const YAML::Node& node, const glm::vec3& fallback = glm::vec3(0.0f)){
        if (!node || !node.IsSequence() || node.size() < 3) { return fallback; }
        return { node[0].as<float>(fallback.x), node[1].as<float>(fallback.y), node[2].as<float>(fallback.z) };
    }

    static void WriteBool3(YAML::Emitter& out, const bool v[3]){
        out << YAML::Flow << YAML::BeginSeq << v[0] << v[1] << v[2] << YAML::EndSeq;
    }

    // Missing/malformed -> all false.
    static void ReadBool3(const YAML::Node& node, bool out[3]){
        for (int i = 0; i < 3; i++)
            out[i] = (node && node.IsSequence() && node.size() == 3) ? node[i].as<bool>(false) : false;
    }

    // Scene-level physics settings (gravity, solver, collision layers).
    static void SerializePhysicsSettings(YAML::Emitter& out, const ScenePhysicsSettings& p){
        out << YAML::Key << "Physics" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "Gravity" << YAML::Value << p.gravity;
        out << YAML::Key << "VelocitySteps" << YAML::Value << p.velocitySteps;
        out << YAML::Key << "PositionSteps" << YAML::Value << p.positionSteps;
        out << YAML::Key << "SubstepRate" << YAML::Value << p.substepRate;
        out << YAML::Key << "Layers" << YAML::Value << YAML::BeginSeq;
        for (int i = 0; i < ScenePhysicsSettings::MaxLayers; i++){
            out << YAML::BeginMap;
            out << YAML::Key << "Name" << YAML::Value << p.layerNames[i];
            out << YAML::Key << "Mask" << YAML::Value << (unsigned)p.layerMasks[i];
            out << YAML::EndMap;
        }
        out << YAML::EndSeq;
        out << YAML::EndMap;
    }

    static void ReadPhysicsSettings(const YAML::Node& node, ScenePhysicsSettings& p){
        const ScenePhysicsSettings def;
        p = def; // a scene without the key gets the defaults, not the previous scene's
        if (!node) { return; }
        p.gravity = ReadVec3(node["Gravity"], def.gravity);
        p.velocitySteps = node["VelocitySteps"].as<int>(def.velocitySteps);
        p.positionSteps = node["PositionSteps"].as<int>(def.positionSteps);
        p.substepRate = node["SubstepRate"].as<float>(def.substepRate);
        const YAML::Node layers = node["Layers"];
        if (layers && layers.IsSequence()){
            for (std::size_t i = 0; i < layers.size() && i < (std::size_t)ScenePhysicsSettings::MaxLayers; i++){
                p.layerNames[i] = layers[i]["Name"].as<std::string>(def.layerNames[i]);
                p.layerMasks[i] = (uint16_t)layers[i]["Mask"].as<unsigned>(def.layerMasks[i]);
            }
        }
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
        out << YAML::Key << "diffusePath" << YAML::Value << material.diffusePath;
        out << YAML::Key << "normalPath"  << YAML::Value << material.normalPath;
        out << YAML::EndMap;
    }

    static Material ReadMaterial(const YAML::Node& node){
        Material material;
        if (!node || !node.IsMap()) { return material; }
        material.ambient   = ReadVec3(node["ambient"], material.ambient);
        material.diffuse   = ReadVec3(node["diffuse"], material.diffuse);
        material.specular  = ReadVec3(node["specular"], material.specular);
        material.shininess = node["shininess"].as<float>(material.shininess);
        material.emissive  = ReadVec3(node["emissive"], material.emissive);
        material.emissiveStrength = node["emissiveStrength"].as<float>(material.emissiveStrength);
        material.reflectivity = node["reflectivity"].as<float>(material.reflectivity);
        material.diffusePath = node["diffusePath"].as<std::string>(material.diffusePath);
        material.normalPath  = node["normalPath"].as<std::string>(material.normalPath);
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

        if (entity.HasComponent<CameraFollowComponent>()){
            auto& f = entity.GetComponent<CameraFollowComponent>();
            out << YAML::Key << "CameraFollowComponent" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "Target" << YAML::Value << f.target;
            out << YAML::Key << "Distance" << YAML::Value << f.distance;
            out << YAML::Key << "Height" << YAML::Value << f.height;
            out << YAML::Key << "LookHeight" << YAML::Value << f.lookHeight;
            out << YAML::Key << "FollowYaw" << YAML::Value << f.followYaw;
            out << YAML::Key << "ModelForwardZ" << YAML::Value << f.modelForwardZ;
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
            out << YAML::Key << "castsShadow" << YAML::Value << dl.castsShadow;
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
            out << YAML::Key << "Friction" << YAML::Value << rb.friction;
            out << YAML::Key << "Restitution" << YAML::Value << rb.restitution;
            out << YAML::Key << "Density" << YAML::Value << rb.density;
            out << YAML::Key << "Mass" << YAML::Value << rb.mass;
            out << YAML::Key << "CenterOfMassOffset" << YAML::Value << rb.centerOfMassOffset;
            out << YAML::Key << "LinearDamping" << YAML::Value << rb.linearDamping;
            out << YAML::Key << "AngularDamping" << YAML::Value << rb.angularDamping;
            out << YAML::Key << "GravityFactor" << YAML::Value << rb.gravityFactor;
            out << YAML::Key << "ContinuousCollision" << YAML::Value << rb.continuousCollision;
            out << YAML::Key << "IsSensor" << YAML::Value << rb.isSensor;
            out << YAML::Key << "AllowSleeping" << YAML::Value << rb.allowSleeping;
            out << YAML::Key << "LockPosition" << YAML::Value;
            WriteBool3(out, rb.lockPosition);
            out << YAML::Key << "LockRotation" << YAML::Value;
            WriteBool3(out, rb.lockRotation);
            out << YAML::Key << "Layer" << YAML::Value << rb.layer;
            out << YAML::Key << "InitialVelocity" << YAML::Value << rb.initialVelocity;
            out << YAML::Key << "InitialAngularVelocity" << YAML::Value << rb.initialAngularVelocity;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<BoxColliderComponent>()){
            auto& bc = entity.GetComponent<BoxColliderComponent>();
            out << YAML::Key << "BoxColliderComponent" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "HalfExtents" << YAML::Value << bc.halfExtents;
            out << YAML::Key << "Offset" << YAML::Value << bc.offset;
            out << YAML::Key << "Rotation" << YAML::Value << bc.rotation;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<SphereColliderComponent>()){
            auto& sc = entity.GetComponent<SphereColliderComponent>();
            out << YAML::Key << "SphereColliderComponent" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "Radius" << YAML::Value << sc.radius;
            out << YAML::Key << "Offset" << YAML::Value << sc.offset;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<CapsuleColliderComponent>()){
            auto& cc = entity.GetComponent<CapsuleColliderComponent>();
            out << YAML::Key << "CapsuleColliderComponent" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "Radius" << YAML::Value << cc.radius;
            out << YAML::Key << "HalfHeight" << YAML::Value << cc.halfHeight;
            out << YAML::Key << "Offset" << YAML::Value << cc.offset;
            out << YAML::Key << "Rotation" << YAML::Value << cc.rotation;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<CylinderColliderComponent>()){
            auto& cc = entity.GetComponent<CylinderColliderComponent>();
            out << YAML::Key << "CylinderColliderComponent" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "Radius" << YAML::Value << cc.radius;
            out << YAML::Key << "HalfHeight" << YAML::Value << cc.halfHeight;
            out << YAML::Key << "Offset" << YAML::Value << cc.offset;
            out << YAML::Key << "Rotation" << YAML::Value << cc.rotation;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<MeshColliderComponent>()){
            out << YAML::Key << "MeshColliderComponent" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "Convex" << YAML::Value << entity.GetComponent<MeshColliderComponent>().convex;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<JointComponent>()){
            auto& j = entity.GetComponent<JointComponent>();
            out << YAML::Key << "JointComponent" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "Type" << YAML::Value << (int)j.type;
            out << YAML::Key << "ConnectedTag" << YAML::Value << j.connectedTag;
            out << YAML::Key << "Pivot" << YAML::Value << j.pivot;
            out << YAML::Key << "Axis" << YAML::Value << j.axis;
            out << YAML::Key << "MinDistance" << YAML::Value << j.minDistance;
            out << YAML::Key << "MaxDistance" << YAML::Value << j.maxDistance;
            out << YAML::Key << "SpringFrequency" << YAML::Value << j.springFrequency;
            out << YAML::Key << "SpringDamping" << YAML::Value << j.springDamping;
            out << YAML::Key << "LimitAngles" << YAML::Value << j.limitAngles;
            out << YAML::Key << "MinAngle" << YAML::Value << j.minAngle;
            out << YAML::Key << "MaxAngle" << YAML::Value << j.maxAngle;
            out << YAML::Key << "LimitSlide" << YAML::Value << j.limitSlide;
            out << YAML::Key << "MinSlide" << YAML::Value << j.minSlide;
            out << YAML::Key << "MaxSlide" << YAML::Value << j.maxSlide;
            out << YAML::Key << "ConeAngle" << YAML::Value << j.coneAngle;
            out << YAML::Key << "Motor" << YAML::Value << (int)j.motor;
            out << YAML::Key << "MotorTarget" << YAML::Value << j.motorTarget;
            out << YAML::Key << "MotorMaxForce" << YAML::Value << j.motorMaxForce;
            out << YAML::Key << "Friction" << YAML::Value << j.friction;
            out << YAML::Key << "BreakForce" << YAML::Value << j.breakForce;
            out << YAML::Key << "CollideConnected" << YAML::Value << j.collideConnected;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<CharacterControllerComponent>()){
            auto& cc = entity.GetComponent<CharacterControllerComponent>();
            out << YAML::Key << "CharacterControllerComponent" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "Radius" << YAML::Value << cc.radius;
            out << YAML::Key << "HalfHeight" << YAML::Value << cc.halfHeight;
            out << YAML::Key << "MaxSlopeAngle" << YAML::Value << cc.maxSlopeAngle;
            out << YAML::Key << "StepHeight" << YAML::Value << cc.stepHeight;
            out << YAML::Key << "MoveSpeed" << YAML::Value << cc.moveSpeed;
            out << YAML::Key << "JumpSpeed" << YAML::Value << cc.jumpSpeed;
            out << YAML::Key << "GravityFactor" << YAML::Value << cc.gravityFactor;
            out << YAML::Key << "AirControl" << YAML::Value << cc.airControl;
            out << YAML::Key << "Mass" << YAML::Value << cc.mass;
            out << YAML::Key << "MaxPushForce" << YAML::Value << cc.maxPushForce;
            out << YAML::Key << "Layer" << YAML::Value << cc.layer;
            out << YAML::Key << "KeyboardControl" << YAML::Value << cc.keyboardControl;
            out << YAML::Key << "FaceMovement" << YAML::Value << cc.faceMovement;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<ForceFieldComponent>()){
            auto& f = entity.GetComponent<ForceFieldComponent>();
            out << YAML::Key << "ForceFieldComponent" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "Shape" << YAML::Value << (int)f.shape;
            out << YAML::Key << "Mode" << YAML::Value << (int)f.mode;
            out << YAML::Key << "HalfExtents" << YAML::Value << f.halfExtents;
            out << YAML::Key << "Direction" << YAML::Value << f.direction;
            out << YAML::Key << "Strength" << YAML::Value << f.strength;
            out << YAML::Key << "Drag" << YAML::Value << f.drag;
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
            out << YAML::Key << "Choppiness" << YAML::Value << w.choppiness;
            out << YAML::Key << "Foam" << YAML::Value << w.foam;
            out << YAML::Key << "InteractiveRipples" << YAML::Value << w.interactiveRipples;
            out << YAML::Key << "RippleResolution" << YAML::Value << w.rippleResolution;
            out << YAML::Key << "Buoyancy" << YAML::Value << w.buoyancy;
            out << YAML::Key << "LinearDrag" << YAML::Value << w.linearDrag;
            out << YAML::Key << "AngularDrag" << YAML::Value << w.angularDrag;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<AnimationComponent>()){
            auto& a = entity.GetComponent<AnimationComponent>();
            out << YAML::Key << "AnimationComponent" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "Clip" << YAML::Value << a.clip;
            out << YAML::Key << "Playing" << YAML::Value << a.playing;
            out << YAML::Key << "Speed" << YAML::Value << a.speed;
            out << YAML::Key << "LoopMode" << YAML::Value << a.loopMode;
            out << YAML::Key << "Crossfade" << YAML::Value << a.crossfade;
            out << YAML::Key << "ExtraClips" << YAML::Value << YAML::Flow << YAML::BeginSeq;
            for (const auto& p : a.extraClips) { out << p; }
            out << YAML::EndSeq;
            out << YAML::Key << "Events" << YAML::Value << YAML::BeginSeq;
            for (const auto& ev : a.events){
                out << YAML::BeginMap;
                out << YAML::Key << "clip" << YAML::Value << ev.clip;
                out << YAML::Key << "time" << YAML::Value << ev.time;
                out << YAML::Key << "name" << YAML::Value << ev.name;
                out << YAML::EndMap;
            }
            out << YAML::EndSeq;
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
        SerializePhysicsSettings(out, m_Scene->PhysicsSettings());
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
            entity.GetComponent<TagComponent>().Tag = node["TagComponent"]["Tag"].as<std::string>("Entity");

        if (auto n = node["TransformComponent"]){
            auto& tc = entity.GetComponent<TransformComponent>();
            const TransformComponent def{}; // fallbacks for keys missing from the file
            tc.Translation = ReadVec3(n["Translation"], def.Translation);
            tc.Rotation    = ReadVec3(n["Rotation"], def.Rotation);
            tc.Scale       = ReadVec3(n["Scale"], def.Scale);
        }

        if (auto n = node["CameraComponent"]){
            auto& cc = entity.HasComponent<CameraComponent>() ? entity.GetComponent<CameraComponent>() : entity.AddComponent<CameraComponent>();
            const CameraComponent def{}; // fallbacks for keys missing from the file
            cc.primary = n["Primary"].as<bool>(def.primary);
            auto& camera = cc.camera;
            const SceneCamera& dc = def.camera;
            camera.SetProjectionType((SceneCamera::ProjectionType)n["ProjectionType"].as<int>((int)dc.GetProjectionType()));
            camera.SetPerspectiveVerticalFOV(n["PerspectiveFOV"].as<float>(dc.GetPerspectiveVerticalFOV()));
            camera.SetPerspectiveNearClip(n["PerspectiveNear"].as<float>(dc.GetPerspectiveNearClip()));
            camera.SetPerspectiveFarClip(n["PerspectiveFar"].as<float>(dc.GetPerspectiveFarClip()));
            camera.SetOrthographicSize(n["OrthographicSize"].as<float>(dc.GetOrthographicSize()));
            camera.SetOrthographicNearClip(n["OrthographicNear"].as<float>(dc.GetOrthographicNearClip()));
            camera.SetOrthographicFarClip(n["OrthographicFar"].as<float>(dc.GetOrthographicFarClip()));
        }
        else if (entity.HasComponent<CameraComponent>()) entity.RemoveComponent<CameraComponent>();

        if (auto n = node["CameraFollowComponent"]){
            auto& f = entity.HasComponent<CameraFollowComponent>() ? entity.GetComponent<CameraFollowComponent>() : entity.AddComponent<CameraFollowComponent>();
            const CameraFollowComponent def{}; // fallbacks for keys missing from the file
            f.target = n["Target"].as<std::string>(def.target);
            f.distance = n["Distance"].as<float>(def.distance);
            f.height = n["Height"].as<float>(def.height);
            f.lookHeight = n["LookHeight"].as<float>(def.lookHeight);
            f.followYaw = n["FollowYaw"].as<bool>(def.followYaw);
            if (n["ModelForwardZ"]) f.modelForwardZ = n["ModelForwardZ"].as<bool>(def.modelForwardZ);
        }
        else if (entity.HasComponent<CameraFollowComponent>()) entity.RemoveComponent<CameraFollowComponent>();

        if (auto n = node["CubeComponent"]){
            auto& cube = entity.HasComponent<CubeComponent>() ? entity.GetComponent<CubeComponent>() : entity.AddComponent<CubeComponent>();
            cube.material = ReadMaterial(n["Material"]);
        }
        else if (entity.HasComponent<CubeComponent>()) entity.RemoveComponent<CubeComponent>();

        if (auto n = node["PointLightComponent"]){
            auto& pl = entity.HasComponent<PointLightComponent>() ? entity.GetComponent<PointLightComponent>() : entity.AddComponent<PointLightComponent>();
            const PointLightComponent def{}; // fallbacks for keys missing from the file
            pl.ambient   = ReadVec3(n["ambient"], def.ambient);
            pl.diffuse   = ReadVec3(n["diffuse"], def.diffuse);
            pl.specular  = ReadVec3(n["specular"], def.specular);
            pl.isActive  = n["isActive"].as<bool>(def.isActive);
            pl.constant  = n["constant"].as<float>(def.constant);
            pl.linear    = n["linear"].as<float>(def.linear);
            pl.quadratic = n["quadratic"].as<float>(def.quadratic);
        }
        else if (entity.HasComponent<PointLightComponent>()) entity.RemoveComponent<PointLightComponent>();

        if (auto n = node["DirLightComponent"]){
            auto& dl = entity.HasComponent<DirLightComponent>() ? entity.GetComponent<DirLightComponent>() : entity.AddComponent<DirLightComponent>();
            const DirLightComponent def{}; // fallbacks for keys missing from the file
            dl.ambient  = ReadVec3(n["ambient"], def.ambient);
            dl.diffuse  = ReadVec3(n["diffuse"], def.diffuse);
            dl.specular = ReadVec3(n["specular"], def.specular);
            dl.isActive = n["isActive"].as<bool>(def.isActive);
            if (n["castsShadow"]) dl.castsShadow = n["castsShadow"].as<bool>(def.castsShadow);
        }
        else if (entity.HasComponent<DirLightComponent>()) entity.RemoveComponent<DirLightComponent>();

        if (auto n = node["MeshComponent"]){
            auto& mc = entity.HasComponent<MeshComponent>() ? entity.GetComponent<MeshComponent>() : entity.AddComponent<MeshComponent>();
            std::string path = n["Path"].as<std::string>("");
            if (path.empty())
                mc.model = nullptr;
            else if (!mc.model || mc.model->GetPath() != path)
                mc.model = CreateRef<Model>(path); // only (re)load when the path actually changed
            mc.material = ReadMaterial(n["Material"]);
        }
        else if (entity.HasComponent<MeshComponent>()) entity.RemoveComponent<MeshComponent>();

        if (auto n = node["RigidBodyComponent"]){
            auto& rb = entity.HasComponent<RigidBodyComponent>() ? entity.GetComponent<RigidBodyComponent>() : entity.AddComponent<RigidBodyComponent>();
            // Assign every field (with its default when absent): the component may be
            // reused from the live scene, so stale values must not survive.
            const RigidBodyComponent def{}; // fallbacks for keys missing from the file
            rb.type = (RigidBodyComponent::Type)n["Type"].as<int>((int)def.type);
            rb.friction = n["Friction"].as<float>(def.friction);
            rb.restitution = n["Restitution"].as<float>(def.restitution);
            rb.density = n["Density"].as<float>(def.density);
            rb.mass = n["Mass"].as<float>(def.mass);
            rb.centerOfMassOffset = ReadVec3(n["CenterOfMassOffset"], def.centerOfMassOffset);
            rb.linearDamping = n["LinearDamping"].as<float>(def.linearDamping);
            rb.angularDamping = n["AngularDamping"].as<float>(def.angularDamping);
            rb.gravityFactor = n["GravityFactor"].as<float>(def.gravityFactor);
            rb.continuousCollision = n["ContinuousCollision"].as<bool>(def.continuousCollision);
            rb.isSensor = n["IsSensor"].as<bool>(def.isSensor);
            rb.allowSleeping = n["AllowSleeping"].as<bool>(def.allowSleeping);
            ReadBool3(n["LockPosition"], rb.lockPosition);
            ReadBool3(n["LockRotation"], rb.lockRotation);
            rb.layer = std::clamp(n["Layer"].as<int>(def.layer), 0, ScenePhysicsSettings::MaxLayers - 1);
            rb.initialVelocity = ReadVec3(n["InitialVelocity"], def.initialVelocity);
            rb.initialAngularVelocity = ReadVec3(n["InitialAngularVelocity"], def.initialAngularVelocity);
        }
        else if (entity.HasComponent<RigidBodyComponent>()) entity.RemoveComponent<RigidBodyComponent>();

        if (auto n = node["SphereColliderComponent"]){
            auto& sc = entity.HasComponent<SphereColliderComponent>() ? entity.GetComponent<SphereColliderComponent>() : entity.AddComponent<SphereColliderComponent>();
            const SphereColliderComponent def{}; // fallbacks for keys missing from the file
            sc.radius = n["Radius"].as<float>(def.radius);
            sc.offset = ReadVec3(n["Offset"], def.offset);
        }
        else if (entity.HasComponent<SphereColliderComponent>()) entity.RemoveComponent<SphereColliderComponent>();

        if (auto n = node["CapsuleColliderComponent"]){
            auto& cc = entity.HasComponent<CapsuleColliderComponent>() ? entity.GetComponent<CapsuleColliderComponent>() : entity.AddComponent<CapsuleColliderComponent>();
            const CapsuleColliderComponent def{}; // fallbacks for keys missing from the file
            cc.radius = n["Radius"].as<float>(def.radius);
            cc.halfHeight = n["HalfHeight"].as<float>(def.halfHeight);
            cc.offset = ReadVec3(n["Offset"], def.offset);
            cc.rotation = ReadVec3(n["Rotation"], def.rotation);
        }
        else if (entity.HasComponent<CapsuleColliderComponent>()) entity.RemoveComponent<CapsuleColliderComponent>();

        if (auto n = node["CylinderColliderComponent"]){
            auto& cc = entity.HasComponent<CylinderColliderComponent>() ? entity.GetComponent<CylinderColliderComponent>() : entity.AddComponent<CylinderColliderComponent>();
            const CylinderColliderComponent def{}; // fallbacks for keys missing from the file
            cc.radius = n["Radius"].as<float>(def.radius);
            cc.halfHeight = n["HalfHeight"].as<float>(def.halfHeight);
            cc.offset = ReadVec3(n["Offset"], def.offset);
            cc.rotation = ReadVec3(n["Rotation"], def.rotation);
        }
        else if (entity.HasComponent<CylinderColliderComponent>()) entity.RemoveComponent<CylinderColliderComponent>();

        if (auto n = node["JointComponent"]){
            auto& j = entity.HasComponent<JointComponent>() ? entity.GetComponent<JointComponent>() : entity.AddComponent<JointComponent>();
            const JointComponent def{}; // fallbacks for keys missing from the file
            j.type = (JointComponent::Type)std::clamp(n["Type"].as<int>((int)def.type), 0, 5);
            j.connectedTag = n["ConnectedTag"].as<std::string>(def.connectedTag);
            j.pivot = ReadVec3(n["Pivot"], def.pivot);
            j.axis = ReadVec3(n["Axis"], def.axis);
            j.minDistance = n["MinDistance"].as<float>(def.minDistance);
            j.maxDistance = n["MaxDistance"].as<float>(def.maxDistance);
            j.springFrequency = n["SpringFrequency"].as<float>(def.springFrequency);
            j.springDamping = n["SpringDamping"].as<float>(def.springDamping);
            j.limitAngles = n["LimitAngles"].as<bool>(def.limitAngles);
            j.minAngle = n["MinAngle"].as<float>(def.minAngle);
            j.maxAngle = n["MaxAngle"].as<float>(def.maxAngle);
            j.limitSlide = n["LimitSlide"].as<bool>(def.limitSlide);
            j.minSlide = n["MinSlide"].as<float>(def.minSlide);
            j.maxSlide = n["MaxSlide"].as<float>(def.maxSlide);
            j.coneAngle = n["ConeAngle"].as<float>(def.coneAngle);
            j.motor = (JointComponent::Motor)std::clamp(n["Motor"].as<int>((int)def.motor), 0, 2);
            j.motorTarget = n["MotorTarget"].as<float>(def.motorTarget);
            j.motorMaxForce = n["MotorMaxForce"].as<float>(def.motorMaxForce);
            j.friction = n["Friction"].as<float>(def.friction);
            j.breakForce = n["BreakForce"].as<float>(def.breakForce);
            j.collideConnected = n["CollideConnected"].as<bool>(def.collideConnected);
        }
        else if (entity.HasComponent<JointComponent>()) entity.RemoveComponent<JointComponent>();

        if (auto n = node["CharacterControllerComponent"]){
            auto& cc = entity.HasComponent<CharacterControllerComponent>() ? entity.GetComponent<CharacterControllerComponent>() : entity.AddComponent<CharacterControllerComponent>();
            const CharacterControllerComponent def{}; // fallbacks for keys missing from the file
            cc.radius = n["Radius"].as<float>(def.radius);
            cc.halfHeight = n["HalfHeight"].as<float>(def.halfHeight);
            cc.maxSlopeAngle = n["MaxSlopeAngle"].as<float>(def.maxSlopeAngle);
            cc.stepHeight = n["StepHeight"].as<float>(def.stepHeight);
            cc.moveSpeed = n["MoveSpeed"].as<float>(def.moveSpeed);
            cc.jumpSpeed = n["JumpSpeed"].as<float>(def.jumpSpeed);
            cc.gravityFactor = n["GravityFactor"].as<float>(def.gravityFactor);
            cc.airControl = n["AirControl"].as<float>(def.airControl);
            cc.mass = n["Mass"].as<float>(def.mass);
            cc.maxPushForce = n["MaxPushForce"].as<float>(def.maxPushForce);
            cc.layer = std::clamp(n["Layer"].as<int>(def.layer), 0, ScenePhysicsSettings::MaxLayers - 1);
            cc.keyboardControl = n["KeyboardControl"].as<bool>(def.keyboardControl);
            cc.faceMovement = n["FaceMovement"].as<bool>(def.faceMovement);
        }
        else if (entity.HasComponent<CharacterControllerComponent>()) entity.RemoveComponent<CharacterControllerComponent>();

        if (auto n = node["ForceFieldComponent"]){
            auto& f = entity.HasComponent<ForceFieldComponent>() ? entity.GetComponent<ForceFieldComponent>() : entity.AddComponent<ForceFieldComponent>();
            const ForceFieldComponent def{}; // fallbacks for keys missing from the file
            f.shape = (ForceFieldComponent::Shape)std::clamp(n["Shape"].as<int>((int)def.shape), 0, 1);
            f.mode = (ForceFieldComponent::Mode)std::clamp(n["Mode"].as<int>((int)def.mode), 0, 1);
            f.halfExtents = ReadVec3(n["HalfExtents"], def.halfExtents);
            f.direction = ReadVec3(n["Direction"], def.direction);
            f.strength = n["Strength"].as<float>(def.strength);
            f.drag = n["Drag"].as<float>(def.drag);
        }
        else if (entity.HasComponent<ForceFieldComponent>()) entity.RemoveComponent<ForceFieldComponent>();

        if (auto n = node["BoxColliderComponent"]){
            auto& bc = entity.HasComponent<BoxColliderComponent>() ? entity.GetComponent<BoxColliderComponent>() : entity.AddComponent<BoxColliderComponent>();
            const BoxColliderComponent def{}; // fallbacks for keys missing from the file
            bc.halfExtents = ReadVec3(n["HalfExtents"], def.halfExtents);
            bc.offset = ReadVec3(n["Offset"], def.offset);
            bc.rotation = ReadVec3(n["Rotation"], def.rotation);
        }
        else if (entity.HasComponent<BoxColliderComponent>()) entity.RemoveComponent<BoxColliderComponent>();

        if (auto n = node["MeshColliderComponent"]){
            auto& mc = entity.HasComponent<MeshColliderComponent>() ? entity.GetComponent<MeshColliderComponent>() : entity.AddComponent<MeshColliderComponent>();
            const MeshColliderComponent def{}; // fallbacks for keys missing from the file
            mc.convex = n["Convex"].as<bool>(def.convex);
        }
        else if (entity.HasComponent<MeshColliderComponent>()) entity.RemoveComponent<MeshColliderComponent>();

        if (node["WireframeComponent"]){
            if (!entity.HasComponent<WireframeComponent>()) entity.AddComponent<WireframeComponent>();
        }
        else if (entity.HasComponent<WireframeComponent>()) entity.RemoveComponent<WireframeComponent>();

        if (auto n = node["TerrainComponent"]){
            auto& t = entity.HasComponent<TerrainComponent>() ? entity.GetComponent<TerrainComponent>() : entity.AddComponent<TerrainComponent>();
            const TerrainComponent def{}; // fallbacks for keys missing from the file
            t.resolution = n["Resolution"].as<int>(def.resolution);
            t.size = n["Size"].as<float>(def.size);
            t.generateCollider = n["GenerateCollider"].as<bool>(def.generateCollider);
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
            const WaterComponent def{}; // fallbacks for keys missing from the file
            w.size = n["Size"].as<float>(def.size);
            w.resolution = n["Resolution"].as<int>(def.resolution);
            w.color = ReadVec3(n["Color"], def.color);
            w.alpha = n["Alpha"].as<float>(def.alpha);
            w.amplitude = n["Amplitude"].as<float>(def.amplitude);
            w.waveScale = n["WaveScale"].as<float>(def.waveScale);
            w.speed = n["Speed"].as<float>(def.speed);
            w.choppiness = n["Choppiness"] ? n["Choppiness"].as<float>() : 0.6f;
            w.foam = n["Foam"] ? n["Foam"].as<float>() : 0.5f;
            w.interactiveRipples = n["InteractiveRipples"] ? n["InteractiveRipples"].as<bool>() : true;
            w.rippleResolution = n["RippleResolution"] ? n["RippleResolution"].as<int>() : 128;
            w.buoyancy = n["Buoyancy"] ? n["Buoyancy"].as<float>() : 1.0f;
            w.linearDrag = n["LinearDrag"] ? n["LinearDrag"].as<float>() : 0.6f;
            w.angularDrag = n["AngularDrag"] ? n["AngularDrag"].as<float>() : 0.3f;
            w.mesh = nullptr;
        }
        else if (entity.HasComponent<WaterComponent>()) entity.RemoveComponent<WaterComponent>();

        if (auto n = node["AnimationComponent"]){
            auto& a = entity.HasComponent<AnimationComponent>() ? entity.GetComponent<AnimationComponent>() : entity.AddComponent<AnimationComponent>();
            const AnimationComponent def{}; // fallbacks for keys missing from the file
            a.clip = n["Clip"].as<int>(def.clip);
            a.playing = n["Playing"].as<bool>(def.playing);
            a.speed = n["Speed"].as<float>(def.speed);
            if (n["LoopMode"])  a.loopMode = n["LoopMode"].as<int>(def.loopMode);
            if (n["Crossfade"]) a.crossfade = n["Crossfade"].as<float>(def.crossfade);
            a.extraClips.clear();
            if (n["ExtraClips"]) { for (auto p : n["ExtraClips"]) a.extraClips.push_back(p.as<std::string>()); }
            a.events.clear();
            if (n["Events"]){
                for (auto en : n["Events"]){
                    AnimEvent ev;
                    ev.clip = en["clip"].as<int>();
                    ev.time = en["time"].as<float>();
                    ev.name = en["name"].as<std::string>();
                    a.events.push_back(ev);
                }
            }
            a.animator = nullptr; // transient; recreated at runtime
            a.activeClip = -1;
            a.extraClipsLoaded = false;
        }
        else if (entity.HasComponent<AnimationComponent>()) entity.RemoveComponent<AnimationComponent>();

        if (auto n = node["PrimitiveComponent"]){
            auto& p = entity.HasComponent<PrimitiveComponent>() ? entity.GetComponent<PrimitiveComponent>() : entity.AddComponent<PrimitiveComponent>();
            const PrimitiveComponent def{}; // fallbacks for keys missing from the file
            p.type = (PrimitiveComponent::Type)n["Type"].as<int>((int)def.type);
            p.material = ReadMaterial(n["Material"]);
        }
        else if (entity.HasComponent<PrimitiveComponent>()) entity.RemoveComponent<PrimitiveComponent>();

        if (auto n = node["LuaScriptComponent"]){
            auto& s = entity.HasComponent<LuaScriptComponent>() ? entity.GetComponent<LuaScriptComponent>() : entity.AddComponent<LuaScriptComponent>();
            const LuaScriptComponent def{}; // fallbacks for keys missing from the file
            s.source = n["Source"].as<std::string>(def.source);
        }
        else if (entity.HasComponent<LuaScriptComponent>()) entity.RemoveComponent<LuaScriptComponent>();
    }

    // Missing keys fall back to defaults inside ApplyEntity; this catches what's left
    // (e.g. a list element of the wrong type) so one bad entity doesn't abort the load
    // or escape as an uncaught exception.
    void SceneSerializer::TryApplyEntity(Entity entity, const YAML::Node& node){
        try{
            ApplyEntity(entity, node);
        }
        catch (const YAML::Exception& e){
            PHX_CORE_ERROR("Scene entity '{0}' partially loaded: {1}",
                entity.HasComponent<TagComponent>() ? entity.GetComponent<TagComponent>().Tag : std::string(),
                e.what());
        }
    }

    bool SceneSerializer::DeserializeNode(const YAML::Node& data){
        if (!data["Entities"]){
            PHX_CORE_ERROR("Scene data has no entities node");
            return false;
        }

        if (data["Ambient"]) m_Scene->AmbientColor() = ReadVec3(data["Ambient"]);
        ReadPhysicsSettings(data["Physics"], m_Scene->PhysicsSettings());

        for (auto entityNode : data["Entities"]){
            std::string name;
            if (entityNode["TagComponent"])
                name = entityNode["TagComponent"]["Tag"].as<std::string>("Entity");

            // CreateEntity already attaches a TransformComponent and a TagComponent.
            Entity entity = m_Scene->CreateEntity(name);
            TryApplyEntity(entity, entityNode);
        }

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
        ReadPhysicsSettings(root["Physics"], m_Scene->PhysicsSettings());

        auto& registry = m_Scene->m_Registry;
        const YAML::Node entities = root["Entities"];

        // Collect the snapshot's entity ids.
        std::unordered_set<uint32_t> targetIds;
        const uint32_t noId = (uint32_t)entt::entity{ entt::null };
        for (std::size_t i = 0; i < entities.size(); i++){
            uint32_t id = entities[i]["Entity"].as<uint32_t>(noId);
            if (id != noId) { targetIds.insert(id); }
        }

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
            entt::entity handle = (entt::entity)node["Entity"].as<uint32_t>(noId); // missing -> created new
            Entity entity;
            if (registry.valid(handle))
                entity = Entity{ handle, m_Scene.get() };
            else{
                std::string name;
                if (node["TagComponent"]) name = node["TagComponent"]["Tag"].as<std::string>("Entity");
                entity = m_Scene->CreateEntity(name);
            }
            TryApplyEntity(entity, node);
        }

        return true;
    }
}
