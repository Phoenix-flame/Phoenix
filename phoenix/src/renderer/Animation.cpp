#include <Phoenix/renderer/Animation.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>

namespace Phoenix{

    // ---- Bone ----

    Bone::Bone(const std::string& name, int id,
               std::vector<KeyPosition> positions,
               std::vector<KeyRotation> rotations,
               std::vector<KeyScale> scales)
        : m_Positions(std::move(positions)), m_Rotations(std::move(rotations)),
          m_Scales(std::move(scales)), m_Name(name), m_ID(id) {}

    float Bone::GetScaleFactor(float lastTime, float nextTime, float t){
        float midway = t - lastTime;
        float frames = nextTime - lastTime;
        if (frames <= 0.0f) { return 0.0f; }
        return midway / frames;
    }

    int Bone::GetPositionIndex(float t) const{
        for (size_t i = 0; i + 1 < m_Positions.size(); i++)
            if (t < m_Positions[i + 1].timeStamp) { return (int)i; }
        return m_Positions.empty() ? 0 : (int)m_Positions.size() - 1;
    }
    int Bone::GetRotationIndex(float t) const{
        for (size_t i = 0; i + 1 < m_Rotations.size(); i++)
            if (t < m_Rotations[i + 1].timeStamp) { return (int)i; }
        return m_Rotations.empty() ? 0 : (int)m_Rotations.size() - 1;
    }
    int Bone::GetScaleIndex(float t) const{
        for (size_t i = 0; i + 1 < m_Scales.size(); i++)
            if (t < m_Scales[i + 1].timeStamp) { return (int)i; }
        return m_Scales.empty() ? 0 : (int)m_Scales.size() - 1;
    }

    glm::mat4 Bone::InterpolatePosition(float t) const{
        if (m_Positions.empty()) { return glm::mat4(1.0f); }
        if (m_Positions.size() == 1) { return glm::translate(glm::mat4(1.0f), m_Positions[0].position); }
        int p0 = GetPositionIndex(t);
        int p1 = p0 + 1;
        float factor = GetScaleFactor(m_Positions[p0].timeStamp, m_Positions[p1].timeStamp, t);
        glm::vec3 pos = glm::mix(m_Positions[p0].position, m_Positions[p1].position, factor);
        return glm::translate(glm::mat4(1.0f), pos);
    }

    glm::mat4 Bone::InterpolateRotation(float t) const{
        if (m_Rotations.empty()) { return glm::mat4(1.0f); }
        if (m_Rotations.size() == 1) { return glm::mat4_cast(glm::normalize(m_Rotations[0].orientation)); }
        int p0 = GetRotationIndex(t);
        int p1 = p0 + 1;
        float factor = GetScaleFactor(m_Rotations[p0].timeStamp, m_Rotations[p1].timeStamp, t);
        glm::quat q = glm::slerp(m_Rotations[p0].orientation, m_Rotations[p1].orientation, factor);
        return glm::mat4_cast(glm::normalize(q));
    }

    glm::mat4 Bone::InterpolateScaling(float t) const{
        if (m_Scales.empty()) { return glm::mat4(1.0f); }
        if (m_Scales.size() == 1) { return glm::scale(glm::mat4(1.0f), m_Scales[0].scale); }
        int p0 = GetScaleIndex(t);
        int p1 = p0 + 1;
        float factor = GetScaleFactor(m_Scales[p0].timeStamp, m_Scales[p1].timeStamp, t);
        glm::vec3 s = glm::mix(m_Scales[p0].scale, m_Scales[p1].scale, factor);
        return glm::scale(glm::mat4(1.0f), s);
    }

    void Bone::Update(float animationTime){
        m_LocalTransform = InterpolatePosition(animationTime)
                         * InterpolateRotation(animationTime)
                         * InterpolateScaling(animationTime);
    }

    // ---- Animation ----

    Bone* Animation::FindBone(const std::string& name){
        for (auto& bone : m_Bones)
            if (bone.GetBoneName() == name) { return &bone; }
        return nullptr;
    }

    // ---- Animator ----

    Animator::Animator(){
        m_FinalBoneMatrices.resize(MAX_BONES, glm::mat4(1.0f));
    }

    void Animator::PlayAnimation(Animation* animation){
        m_CurrentAnimation = animation;
        m_CurrentTime = 0.0f;
    }

    void Animator::UpdateAnimation(float dt){
        if (!m_CurrentAnimation) { return; }
        m_CurrentTime += m_CurrentAnimation->GetTicksPerSecond() * dt;
        m_CurrentTime = std::fmod(m_CurrentTime, m_CurrentAnimation->GetDuration());
        CalculateBoneTransform(&m_CurrentAnimation->GetRootNode(), glm::mat4(1.0f));
    }

    void Animator::CalculateBoneTransform(const AssimpNodeData* node, const glm::mat4& parentTransform){
        const std::string& nodeName = node->name;
        glm::mat4 nodeTransform = node->transformation;

        Bone* bone = m_CurrentAnimation->FindBone(nodeName);
        if (bone){
            bone->Update(m_CurrentTime);
            nodeTransform = bone->GetLocalTransform();
        }

        glm::mat4 globalTransform = parentTransform * nodeTransform;

        const auto& boneInfoMap = m_CurrentAnimation->GetBoneIDMap();
        auto it = boneInfoMap.find(nodeName);
        if (it != boneInfoMap.end()){
            int index = it->second.id;
            const glm::mat4& offset = it->second.offset;
            if (index >= 0 && index < MAX_BONES)
                m_FinalBoneMatrices[index] = m_CurrentAnimation->m_GlobalInverseTransform * globalTransform * offset;
        }

        for (const auto& child : node->children)
            CalculateBoneTransform(&child, globalTransform);
    }
}
