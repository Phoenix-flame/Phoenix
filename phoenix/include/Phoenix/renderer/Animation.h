#pragma once
#include <Phoenix/core/base.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <map>

// Skeletal animation. A rigged model (loaded via Assimp) carries per-vertex bone
// indices/weights and a bone hierarchy; an Animation stores the keyframes; an
// Animator walks the hierarchy each frame and produces the final bone matrices the
// vertex shader uses to skin the mesh.

namespace Phoenix{

    // Max bones a skinned mesh may reference; must match u_BoneMatrices[] in the
    // lighting / shadow shaders. Mixamo rigs use ~65 bones, so 100 is plenty.
    static const int MAX_BONES = 100;

    // Per-bone bind-pose data extracted while loading the mesh. `id` indexes into the
    // shader's bone-matrix array; `offset` transforms a vertex from model space into
    // the bone's local space (the inverse bind matrix).
    struct BoneInfo{
        int id = 0;
        glm::mat4 offset = glm::mat4(1.0f);
    };

    struct KeyPosition{ glm::vec3 position; float timeStamp; };
    struct KeyRotation{ glm::quat orientation; float timeStamp; };
    struct KeyScale   { glm::vec3 scale; float timeStamp; };

    // One bone's animation channel: keyframes for translation, rotation and scale.
    // Update(time) interpolates them into a local transform.
    class Bone{
    public:
        Bone(const std::string& name, int id,
             std::vector<KeyPosition> positions,
             std::vector<KeyRotation> rotations,
             std::vector<KeyScale> scales);

        void Update(float animationTime);
        const glm::mat4& GetLocalTransform() const { return m_LocalTransform; }
        const std::string& GetBoneName() const { return m_Name; }
        int GetBoneID() const { return m_ID; }

    private:
        int GetPositionIndex(float t) const;
        int GetRotationIndex(float t) const;
        int GetScaleIndex(float t) const;
        static float GetScaleFactor(float lastTime, float nextTime, float t);
        glm::mat4 InterpolatePosition(float t) const;
        glm::mat4 InterpolateRotation(float t) const;
        glm::mat4 InterpolateScaling(float t) const;

        std::vector<KeyPosition> m_Positions;
        std::vector<KeyRotation> m_Rotations;
        std::vector<KeyScale>    m_Scales;
        glm::mat4 m_LocalTransform = glm::mat4(1.0f);
        std::string m_Name;
        int m_ID = 0;
    };

    // A copy of the Assimp node hierarchy (the importer/scene is freed after loading).
    struct AssimpNodeData{
        glm::mat4 transformation = glm::mat4(1.0f);
        std::string name;
        std::vector<AssimpNodeData> children;
    };

    // A single animation clip. All fields are filled by the model loader (Mesh.cpp);
    // it owns its own bone list, node hierarchy and bone-info map.
    class Animation{
    public:
        Bone* FindBone(const std::string& name);

        float GetTicksPerSecond() const { return m_TicksPerSecond; }
        float GetDuration() const { return m_Duration; }
        const AssimpNodeData& GetRootNode() const { return m_RootNode; }
        const std::map<std::string, BoneInfo>& GetBoneIDMap() const { return m_BoneInfoMap; }
        const std::string& GetName() const { return m_Name; }

        std::string m_Name;
        float m_Duration = 0.0f;
        float m_TicksPerSecond = 25.0f;
        std::vector<Bone> m_Bones;
        AssimpNodeData m_RootNode;
        std::map<std::string, BoneInfo> m_BoneInfoMap;
        glm::mat4 m_GlobalInverseTransform = glm::mat4(1.0f);
    };

    // Advances a playing animation and produces the final bone matrices for skinning.
    class Animator{
    public:
        Animator();

        void PlayAnimation(Animation* animation);
        void UpdateAnimation(float dt);
        void CalculateBoneTransform(const AssimpNodeData* node, const glm::mat4& parentTransform);

        const std::vector<glm::mat4>& GetFinalBoneMatrices() const { return m_FinalBoneMatrices; }
        Animation* GetCurrentAnimation() const { return m_CurrentAnimation; }
        float GetCurrentTime() const { return m_CurrentTime; }

    private:
        std::vector<glm::mat4> m_FinalBoneMatrices;
        Animation* m_CurrentAnimation = nullptr;
        float m_CurrentTime = 0.0f;
    };
}
