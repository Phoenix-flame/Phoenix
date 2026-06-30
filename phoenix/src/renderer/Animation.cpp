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
        int p0 = GetPositionIndex(t);
        if (p0 + 1 >= (int)m_Positions.size()) { return glm::translate(glm::mat4(1.0f), m_Positions[p0].position); }
        int p1 = p0 + 1;
        float factor = GetScaleFactor(m_Positions[p0].timeStamp, m_Positions[p1].timeStamp, t);
        glm::vec3 pos = glm::mix(m_Positions[p0].position, m_Positions[p1].position, factor);
        return glm::translate(glm::mat4(1.0f), pos);
    }

    glm::mat4 Bone::InterpolateRotation(float t) const{
        if (m_Rotations.empty()) { return glm::mat4(1.0f); }
        int p0 = GetRotationIndex(t);
        if (p0 + 1 >= (int)m_Rotations.size()) { return glm::mat4_cast(glm::normalize(m_Rotations[p0].orientation)); }
        int p1 = p0 + 1;
        float factor = GetScaleFactor(m_Rotations[p0].timeStamp, m_Rotations[p1].timeStamp, t);
        glm::quat q = glm::slerp(m_Rotations[p0].orientation, m_Rotations[p1].orientation, factor);
        return glm::mat4_cast(glm::normalize(q));
    }

    glm::mat4 Bone::InterpolateScaling(float t) const{
        if (m_Scales.empty()) { return glm::mat4(1.0f); }
        int p0 = GetScaleIndex(t);
        if (p0 + 1 >= (int)m_Scales.size()) { return glm::scale(glm::mat4(1.0f), m_Scales[p0].scale); }
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
        m_PoseA.resize(MAX_BONES, glm::mat4(1.0f));
        m_PoseB.resize(MAX_BONES, glm::mat4(1.0f));
    }

    void Animator::PlayAnimation(Animation* animation){
        m_CurrentAnimation = animation;
        m_CurrentTime = 0.0f;
        m_Finished = false;
        m_PingPongDir = 1;
        m_PrevAnimation = nullptr; // cancel any in-flight crossfade
        m_BlendElapsed = m_BlendDuration = 0.0f;
    }

    void Animator::CrossFade(Animation* next, float fadeSeconds){
        if (!next) { return; }
        if (!m_CurrentAnimation || fadeSeconds <= 0.0f){ PlayAnimation(next); return; }
        // Snapshot the outgoing clip as the blend source.
        m_PrevAnimation = m_CurrentAnimation;
        m_PrevTime = m_CurrentTime;
        m_CurrentAnimation = next;
        m_CurrentTime = 0.0f;
        m_Finished = false;
        m_PingPongDir = 1;
        m_BlendDuration = fadeSeconds;
        m_BlendElapsed = 0.0f;
    }

    void Animator::Stop(){
        m_CurrentTime = 0.0f;
        m_Paused = true;
        m_Finished = false;
        m_PingPongDir = 1;
        m_PrevAnimation = nullptr;
        m_BlendElapsed = m_BlendDuration = 0.0f;
    }

    void Animator::Seek(float seconds){
        if (!m_CurrentAnimation) { return; }
        float tps = m_CurrentAnimation->GetTicksPerSecond();
        float dur = m_CurrentAnimation->GetDuration();
        m_CurrentTime = glm::clamp(seconds * tps, 0.0f, dur);
        m_Finished = false;
        m_PrevAnimation = nullptr; // a manual seek cancels any crossfade
        m_BlendElapsed = m_BlendDuration = 0.0f;
    }

    float Animator::GetCurrentSeconds() const{
        if (!m_CurrentAnimation) { return 0.0f; }
        float tps = m_CurrentAnimation->GetTicksPerSecond();
        return tps > 0.0f ? m_CurrentTime / tps : 0.0f;
    }

    float Animator::GetDurationSeconds() const{
        if (!m_CurrentAnimation) { return 0.0f; }
        float tps = m_CurrentAnimation->GetTicksPerSecond();
        return tps > 0.0f ? m_CurrentAnimation->GetDuration() / tps : 0.0f;
    }

    void Animator::UpdateAnimation(float dt){
        if (!m_CurrentAnimation) { return; }

        float tps = m_CurrentAnimation->GetTicksPerSecond();
        float dur = m_CurrentAnimation->GetDuration();
        float prevTicks = m_CurrentTime;
        bool wrapped = false;

        if (!m_Paused && dt > 0.0f && dur > 0.0f){
            float advance = tps * dt * m_Speed * (float)m_PingPongDir;
            m_CurrentTime += advance;
            switch (m_LoopMode){
                case LoopMode::Loop:
                    if (m_CurrentTime >= dur){ m_CurrentTime = std::fmod(m_CurrentTime, dur); wrapped = true; }
                    else if (m_CurrentTime < 0.0f){ m_CurrentTime = std::fmod(m_CurrentTime, dur) + dur; wrapped = true; }
                    break;
                case LoopMode::Once:
                    if (m_CurrentTime >= dur){ m_CurrentTime = dur; m_Finished = true; }
                    else if (m_CurrentTime < 0.0f){ m_CurrentTime = 0.0f; m_Finished = true; }
                    break;
                case LoopMode::PingPong:
                    if (m_CurrentTime >= dur){ m_CurrentTime = dur; m_PingPongDir = -1; }
                    else if (m_CurrentTime <= 0.0f){ m_CurrentTime = 0.0f; m_PingPongDir = 1; }
                    break;
            }
        }

        // Report the playhead window in seconds for event firing.
        float invTps = tps > 0.0f ? 1.0f / tps : 0.0f;
        m_PlayWindow = { prevTicks * invTps, m_CurrentTime * invTps, wrapped };

        // Advance + apply the crossfade, if any.
        if (m_PrevAnimation){
            if (!m_Paused && dt > 0.0f){
                m_BlendElapsed += dt;
                float pdur = m_PrevAnimation->GetDuration();
                if (pdur > 0.0f){
                    m_PrevTime += m_PrevAnimation->GetTicksPerSecond() * dt * m_Speed;
                    m_PrevTime = std::fmod(m_PrevTime, pdur);
                }
            }
            float t = (m_BlendDuration > 0.0f) ? glm::clamp(m_BlendElapsed / m_BlendDuration, 0.0f, 1.0f) : 1.0f;
            ComputePose(m_PrevAnimation, m_PrevTime, m_PoseA);
            ComputePose(m_CurrentAnimation, m_CurrentTime, m_PoseB);
            for (int i = 0; i < MAX_BONES; i++)
                m_FinalBoneMatrices[i] = m_PoseA[i] * (1.0f - t) + m_PoseB[i] * t; // matrix lerp (ok for short fades)
            if (t >= 1.0f){ m_PrevAnimation = nullptr; m_BlendElapsed = m_BlendDuration = 0.0f; }
        }
        else{
            ComputePose(m_CurrentAnimation, m_CurrentTime, m_FinalBoneMatrices);
        }
    }

    void Animator::ComputePose(Animation* anim, float timeTicks, std::vector<glm::mat4>& out){
        ComputeNode(anim, &anim->GetRootNode(), glm::mat4(1.0f), timeTicks, out);
    }

    void Animator::ComputeNode(Animation* anim, const AssimpNodeData* node,
            const glm::mat4& parentTransform, float timeTicks, std::vector<glm::mat4>& out){
        const std::string& nodeName = node->name;
        glm::mat4 nodeTransform = node->transformation;

        Bone* bone = anim->FindBone(nodeName);
        if (bone){
            bone->Update(timeTicks);
            nodeTransform = bone->GetLocalTransform();
        }

        glm::mat4 globalTransform = parentTransform * nodeTransform;

        const auto& boneInfoMap = anim->GetBoneIDMap();
        auto it = boneInfoMap.find(nodeName);
        if (it != boneInfoMap.end()){
            int index = it->second.id;
            const glm::mat4& offset = it->second.offset;
            if (index >= 0 && index < MAX_BONES)
                out[index] = anim->m_GlobalInverseTransform * globalTransform * offset;
        }

        for (const auto& child : node->children)
            ComputeNode(anim, &child, globalTransform, timeTicks, out);
    }
}
