#include <Phoenix/Scene/Scene.h>
#include <Phoenix/Scene/Entity.h>
#include <Phoenix/Scene/Component.h>
#include <Phoenix/Physics/Physics.h>
#include <Phoenix/Math/Math.h>
#include <Phoenix/renderer/Animation.h>
#include <Phoenix/core/log.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cctype>
#include <string>

namespace Phoenix{

    // Ragdolls for skinned models. On activation, one capsule body is built per
    // humanoid bone segment (bone origin -> its end bone) from the current animated
    // pose, children are joined to parents with swing-twist joints at the bone origin,
    // and every frame the skeleton is posed from the bodies: driven nodes take their
    // body's transform, the others keep their local transform from activation.
    namespace{
        enum class PartKind { Torso, Head, Limb };

        struct PartDef{
            const char* bones[6];   // candidate bone names (lower case, no "rig:" prefix)
            const char* ends[6];    // candidate end bones (segment end point)
            int parent;             // index into s_Parts, -1 = root
            PartKind kind;
            float swing1, swing2, twistMin, twistMax; // degrees
        };

        // Mixamo names first, then common Unreal/Blender/Unity variants.
        const PartDef s_Parts[] = {
            /* 0 pelvis */ { { "hips", "pelvis", "hip", "root_pelvis" }, { "spine1", "spine", "spine_01", "spine_02" }, -1, PartKind::Torso, 0, 0, 0, 0 },
            /* 1 chest  */ { { "spine2", "chest", "spine_03", "upperchest", "spine1" }, { "neck", "neck_01", "head" }, 0, PartKind::Torso, 25, 25, -20, 20 },
            /* 2 head   */ { { "head" }, { "headtop_end", "head_end", "headtop", "head_top" }, 1, PartKind::Head, 40, 40, -45, 45 },
            /* 3 l arm  */ { { "leftarm", "upperarm_l", "l_upperarm", "leftupperarm", "upper_arm.l" }, { "leftforearm", "lowerarm_l", "forearm_l", "l_forearm", "leftlowerarm", "forearm.l" }, 1, PartKind::Limb, 75, 75, -40, 40 },
            /* 4 l fore */ { { "leftforearm", "lowerarm_l", "forearm_l", "l_forearm", "leftlowerarm", "forearm.l" }, { "lefthand", "hand_l", "l_hand", "hand.l" }, 3, PartKind::Limb, 70, 10, -40, 40 },
            /* 5 r arm  */ { { "rightarm", "upperarm_r", "r_upperarm", "rightupperarm", "upper_arm.r" }, { "rightforearm", "lowerarm_r", "forearm_r", "r_forearm", "rightlowerarm", "forearm.r" }, 1, PartKind::Limb, 75, 75, -40, 40 },
            /* 6 r fore */ { { "rightforearm", "lowerarm_r", "forearm_r", "r_forearm", "rightlowerarm", "forearm.r" }, { "righthand", "hand_r", "r_hand", "hand.r" }, 5, PartKind::Limb, 70, 10, -40, 40 },
            /* 7 l thigh*/ { { "leftupleg", "thigh_l", "l_thigh", "leftupperleg", "thigh.l" }, { "leftleg", "calf_l", "l_calf", "leftlowerleg", "shin_l", "shin.l" }, 0, PartKind::Limb, 60, 35, -15, 15 },
            /* 8 l shin */ { { "leftleg", "calf_l", "l_calf", "leftlowerleg", "shin_l", "shin.l" }, { "leftfoot", "foot_l", "l_foot", "foot.l" }, 7, PartKind::Limb, 70, 5, -5, 5 },
            /* 9 r thigh*/ { { "rightupleg", "thigh_r", "r_thigh", "rightupperleg", "thigh.r" }, { "rightleg", "calf_r", "r_calf", "rightlowerleg", "shin_r", "shin.r" }, 0, PartKind::Limb, 60, 35, -15, 15 },
            /*10 r shin */ { { "rightleg", "calf_r", "r_calf", "rightlowerleg", "shin_r", "shin.r" }, { "rightfoot", "foot_r", "r_foot", "foot.r" }, 9, PartKind::Limb, 70, 5, -5, 5 },
        };
        constexpr int s_PartCount = (int)(sizeof(s_Parts) / sizeof(s_Parts[0]));

        // "mixamorig:LeftArm" / "Armature|LeftArm" -> "leftarm".
        std::string NormalizeBoneName(const std::string& name){
            size_t cut = name.find_last_of(":|");
            std::string out = cut == std::string::npos ? name : name.substr(cut + 1);
            for (char& c : out) { c = (char)std::tolower((unsigned char)c); }
            return out;
        }

        int FindNode(const std::vector<std::string>& names, const char* const candidates[6]){
            for (int c = 0; c < 6 && candidates[c]; c++)
                for (int i = 0; i < (int)names.size(); i++)
                    if (names[i] == candidates[c]) { return i; }
            return -1;
        }

        glm::vec3 Origin(const glm::mat4& m) { return glm::vec3(m[3]); }
    }

    void Scene::ProcessRagdollRequests(){
        auto view = m_Registry.view<RagdollComponent>();
        for (auto entity : view){
            auto& rd = view.get<RagdollComponent>(entity);
            if (rd.request < 0) { continue; }
            if (rd.request == 0){
                DeactivateRagdoll({ entity, this }, rd);
                rd.request = -1;
                continue;
            }
            if (rd.active) { rd.request = -1; continue; }
            // Activation needs a posed skeleton: wait until the animator has run once.
            auto* anim = m_Registry.try_get<AnimationComponent>(entity);
            if (!anim || !anim->animator || anim->animator->GetNodeGlobals().empty()) { continue; }
            if (!ActivateRagdoll({ entity, this }, rd, *anim->animator)) { rd.request = -1; continue; }
            rd.request = -1;
        }
    }

    bool Scene::ActivateRagdoll(Entity entity, RagdollComponent& rd, Animator& animator){
        Animation* clip = animator.GetCurrentAnimation();
        const auto& globals = animator.GetNodeGlobals();
        if (!clip || globals.size() != clip->GetNodes().size()) { return false; }
        const auto& nodes = clip->GetNodes();

        std::vector<std::string> names;
        names.reserve(nodes.size());
        for (const auto& n : nodes) { names.push_back(NormalizeBoneName(n.name)); }

        const auto& transform = entity.GetComponent<TransformComponent>();
        const glm::mat4 entityWorld = transform.GetTransform();
        const glm::mat4 modelToWorld = entityWorld * clip->m_GlobalInverseTransform;
        auto nodeWorld = [&](int i){ return modelToWorld * globals[i]; };

        // Resolve parts (and their nearest existing parent part).
        int partNode[s_PartCount], partEnd[s_PartCount], partIndex[s_PartCount];
        for (int p = 0; p < s_PartCount; p++){
            partNode[p] = FindNode(names, s_Parts[p].bones);
            partEnd[p] = FindNode(names, s_Parts[p].ends);
            partIndex[p] = -1;
        }
        if (partNode[0] < 0){
            PHX_CORE_WARN("Ragdoll on '{0}': no hips/pelvis bone found", entity.GetComponent<TagComponent>().Tag);
            return false;
        }

        rd.parts.clear();
        rd.nodePart.assign(nodes.size(), -1);
        PhysicsWorld::BodyProps props;
        props.density = rd.density;
        props.friction = rd.friction;
        props.layer = rd.layer;
        props.angularDamping = 0.6f;
        props.linearDamping = 0.1f;

        glm::vec3 chestDir(0.0f, 1.0f, 0.0f);
        float chestLength = 0.3f;
        for (int p = 0; p < s_PartCount; p++){
            if (partNode[p] < 0) { continue; }
            const PartDef& def = s_Parts[p];
            glm::vec3 p0 = Origin(nodeWorld(partNode[p]));
            glm::vec3 p1;
            if (partEnd[p] >= 0 && partEnd[p] != partNode[p]) { p1 = Origin(nodeWorld(partEnd[p])); }
            else if (def.kind == PartKind::Head) { p1 = p0 + chestDir * chestLength * 0.8f; } // no head-top bone
            else { continue; }
            glm::vec3 segment = p1 - p0;
            float length = glm::length(segment);
            if (length < 1e-3f) { continue; }
            glm::vec3 dir = segment / length;
            if (p == 1) { chestDir = dir; chestLength = length; }

            float thickness = def.kind == PartKind::Limb ? rd.limbThickness : def.kind == PartKind::Head ? 0.5f : rd.torsoThickness;
            float radius = std::clamp(thickness * length, 0.01f, 0.5f * length + 0.05f);
            PhysicsWorld::ShapeDesc shape;
            shape.type = PhysicsWorld::ShapeDesc::Type::Capsule;
            shape.radius = radius;
            shape.halfHeight = std::max(0.01f, 0.5f * length - radius);

            glm::quat rotation = glm::rotation(glm::vec3(0.0f, 1.0f, 0.0f), dir);
            glm::vec3 center = 0.5f * (p0 + p1);
            uint32_t body = m_PhysicsWorld->CreateBody({ shape }, center, Math::QuatToEulerXYZ(rotation, glm::vec3(0.0f)),
                PhysicsWorld::BodyType::Dynamic, props);
            if (body == PhysicsWorld::InvalidBody) { continue; }

            RagdollComponent::Part part;
            part.node = partNode[p];
            part.body = body;
            glm::mat4 bodyWorld = glm::translate(glm::mat4(1.0f), center) * glm::mat4_cast(rotation);
            part.bodyToNode = glm::inverse(bodyWorld) * nodeWorld(partNode[p]);

            // Joint to the nearest existing ancestor part, at this bone's origin.
            int parent = def.parent;
            while (parent >= 0 && partIndex[parent] < 0) { parent = s_Parts[parent].parent; }
            if (parent >= 0){
                part.parentPart = partIndex[parent];
                PhysicsWorld::JointDesc joint;
                joint.type = PhysicsWorld::JointType::SwingTwist;
                joint.pointA = joint.pointB = p0;
                joint.axis = dir;
                joint.planeAxis = rotation * glm::vec3(1.0f, 0.0f, 0.0f);
                const float scale = glm::radians(std::max(0.0f, rd.jointLimitScale));
                joint.normalHalfCone = def.swing1 * scale;
                joint.planeHalfCone = def.swing2 * scale;
                joint.twistMin = def.twistMin * scale;
                joint.twistMax = def.twistMax * scale;
                joint.friction = rd.jointFriction;
                part.joint = m_PhysicsWorld->AddJoint(body, rd.parts[part.parentPart].body, joint);
            }
            partIndex[p] = (int)rd.parts.size();
            rd.nodePart[partNode[p]] = partIndex[p];
            rd.parts.push_back(part);
        }

        // Parts overlap at the joints: keep the whole ragdoll from self-colliding.
        for (size_t a = 0; a < rd.parts.size(); a++)
            for (size_t b = a + 1; b < rd.parts.size(); b++)
                m_PhysicsWorld->DisableCollisionBetween(rd.parts[a].body, rd.parts[b].body);

        // A ragdolled character keeps its momentum.
        if (auto* cc = m_Registry.try_get<CharacterControllerComponent>(entity))
            for (const auto& part : rd.parts) { m_PhysicsWorld->SetLinearVelocity(part.body, cc->velocity); }

        // Local transforms at activation pose the nodes the bodies don't drive.
        rd.restLocals.resize(nodes.size());
        for (size_t i = 0; i < nodes.size(); i++)
            rd.restLocals[i] = nodes[i].parent >= 0 ? glm::inverse(globals[nodes[i].parent]) * globals[i] : globals[i];

        glm::vec3 pelvis; glm::quat unused;
        m_PhysicsWorld->GetBodyPose(rd.parts[0].body, pelvis, unused);
        rd.hipsToEntity = transform.Translation - pelvis;
        rd.active = true;
        return true;
    }

    void Scene::DeactivateRagdoll(Entity entity, RagdollComponent& rd){
        (void)entity;
        if (m_PhysicsWorld)
            for (const auto& part : rd.parts) { m_PhysicsWorld->RemoveBody(part.body); } // joints go with them
        rd.parts.clear();
        rd.active = false;
    }

    void Scene::UpdateRagdollFollow(){
        auto view = m_Registry.view<RagdollComponent, TransformComponent>();
        for (auto entity : view){
            auto& rd = view.get<RagdollComponent>(entity);
            if (!rd.active || !rd.followHips || rd.parts.empty()) { continue; }
            glm::vec3 pelvis; glm::quat unused;
            m_PhysicsWorld->GetBodyPose(rd.parts[0].body, pelvis, unused);
            // Horizontal only: the origin of a rig is usually at its feet, and a fallen
            // body would otherwise drag it under the floor.
            auto& t = view.get<TransformComponent>(entity);
            t.Translation.x = pelvis.x + rd.hipsToEntity.x;
            t.Translation.z = pelvis.z + rd.hipsToEntity.z;
            // A character controller on the same entity follows the ragdoll instead of
            // fighting it (the external-edit sync would teleport it every frame).
            if (auto* cc = m_Registry.try_get<CharacterControllerComponent>(entity)) { cc->synced = false; }
        }
    }

    void Scene::PoseRagdoll(Entity entity, RagdollComponent& rd, Animator& animator){
        Animation* clip = animator.GetCurrentAnimation();
        if (!clip || !m_PhysicsWorld || rd.parts.empty()) { return; }
        const auto& nodes = clip->GetNodes();
        if (rd.restLocals.size() != nodes.size() || rd.nodePart.size() != nodes.size()) { return; }

        const glm::mat4 entityWorld = entity.GetComponent<TransformComponent>().GetTransform();
        const glm::mat4 worldToEntity = glm::inverse(entityWorld);
        const glm::mat4 modelToWorld = entityWorld * clip->m_GlobalInverseTransform;

        // Skinning: entityWorld * final * v must equal nodeWorld * offset * v.
        std::vector<glm::mat4> world(nodes.size());
        auto& finals = animator.GetFinalBoneMatricesMutable();
        for (size_t i = 0; i < nodes.size(); i++){
            int part = rd.nodePart[i];
            if (part >= 0){
                glm::vec3 position; glm::quat rotation;
                m_PhysicsWorld->GetBodyPose(rd.parts[part].body, position, rotation);
                world[i] = glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation) * rd.parts[part].bodyToNode;
            }
            else{
                world[i] = (nodes[i].parent >= 0 ? world[nodes[i].parent] : modelToWorld) * rd.restLocals[i];
            }
            if (nodes[i].boneIndex >= 0 && nodes[i].boneIndex < (int)finals.size())
                finals[nodes[i].boneIndex] = worldToEntity * world[i] * nodes[i].offset;
        }
    }
}
