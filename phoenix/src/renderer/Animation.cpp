#include <Phoenix/renderer/Animation.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace Phoenix{

    // ---- JointPose ----

    glm::mat4 JointPose::ToMatrix() const{
        // T * R * S without the three matrix products.
        glm::mat4 m = glm::mat4_cast(rotation);
        m[0] *= scale.x;
        m[1] *= scale.y;
        m[2] *= scale.z;
        m[3] = glm::vec4(translation, 1.0f);
        return m;
    }

    JointPose JointPose::FromMatrix(const glm::mat4& m){
        JointPose p;
        p.translation = glm::vec3(m[3]);
        glm::vec3 c0(m[0]), c1(m[1]), c2(m[2]);
        p.scale = glm::vec3(glm::length(c0), glm::length(c1), glm::length(c2));
        // A mirrored basis: fold the reflection into one scale axis so the rest is a rotation.
        if (glm::dot(glm::cross(c0, c1), c2) < 0.0f) { p.scale.x = -p.scale.x; }
        if (p.scale.x == 0.0f || p.scale.y == 0.0f || p.scale.z == 0.0f) { return p; }
        glm::mat3 r(c0 / p.scale.x, c1 / p.scale.y, c2 / p.scale.z);
        p.rotation = glm::normalize(glm::quat_cast(r));
        return p;
    }

    JointPose JointPose::Blend(const JointPose& a, const JointPose& b, float t){
        JointPose p;
        p.translation = glm::mix(a.translation, b.translation, t);
        p.rotation = glm::normalize(glm::slerp(a.rotation, b.rotation, t)); // shortest arc
        p.scale = glm::mix(a.scale, b.scale, t);
        return p;
    }

    // ---- Bone ----

    glm::vec3 Bone::RemoveDrift(){
        if (m_Positions.size() < 2) { return glm::vec3(0.0f); }
        const KeyPosition first = m_Positions.front();
        const KeyPosition last = m_Positions.back();
        const float span = last.timeStamp - first.timeStamp;
        const glm::vec3 drift = last.position - first.position;
        if (span <= 0.0f || glm::length(drift) < 1e-4f) { return glm::vec3(0.0f); }
        for (KeyPosition& k : m_Positions)
            k.position -= drift * ((k.timeStamp - first.timeStamp) / span);
        return drift;
    }

    glm::vec3 Animation::RemoveRootDrift(){
        // Mixamo's root motion lives on the hips; otherwise use the topmost animated
        // node with position keys.
        Bone* root = nullptr;
        for (Bone& b : m_Bones){
            const std::string& n = b.GetBoneName();
            if (n == "Hips" || (n.size() > 5 && n.compare(n.size() - 5, 5, ":Hips") == 0)) { root = &b; break; }
        }
        if (!root){
            for (const Node& node : m_Nodes)
                if (node.channel >= 0) { root = &m_Bones[node.channel]; break; }
        }
        return root ? root->RemoveDrift() : glm::vec3(0.0f);
    }

    Bone::Bone(const std::string& name, int id,
               std::vector<KeyPosition> positions,
               std::vector<KeyRotation> rotations,
               std::vector<KeyScale> scales)
        : m_Positions(std::move(positions)), m_Rotations(std::move(rotations)),
          m_Scales(std::move(scales)), m_Name(name), m_ID(id) {}

    // Index of the last key at or before t (0 when t precedes the first key), and the
    // [0, 1] factor towards the next key. Binary search: long clips have hundreds of keys.
    template<typename Key>
    static size_t FindKey(const std::vector<Key>& keys, float t, float& factor){
        auto it = std::upper_bound(keys.begin(), keys.end(), t,
            [](float value, const Key& key){ return value < key.timeStamp; });
        size_t i = (it == keys.begin()) ? 0 : (size_t)(it - keys.begin()) - 1;
        factor = 0.0f;
        if (i + 1 < keys.size()){
            float span = keys[i + 1].timeStamp - keys[i].timeStamp;
            if (span > 0.0f) { factor = glm::clamp((t - keys[i].timeStamp) / span, 0.0f, 1.0f); }
        }
        return i;
    }

    JointPose Bone::Sample(float t) const{
        JointPose p;
        float f;
        if (!m_Positions.empty()){
            size_t i = FindKey(m_Positions, t, f);
            p.translation = (i + 1 < m_Positions.size())
                ? glm::mix(m_Positions[i].position, m_Positions[i + 1].position, f)
                : m_Positions[i].position;
        }
        if (!m_Rotations.empty()){
            size_t i = FindKey(m_Rotations, t, f);
            p.rotation = (i + 1 < m_Rotations.size())
                ? glm::slerp(m_Rotations[i].orientation, m_Rotations[i + 1].orientation, f)
                : m_Rotations[i].orientation;
            p.rotation = glm::normalize(p.rotation);
        }
        if (!m_Scales.empty()){
            size_t i = FindKey(m_Scales, t, f);
            p.scale = (i + 1 < m_Scales.size())
                ? glm::mix(m_Scales[i].scale, m_Scales[i + 1].scale, f)
                : m_Scales[i].scale;
        }
        return p;
    }

    // ---- Animation ----

    void Animation::BuildNodeCache(){
        m_Nodes.clear();
        m_NodeIndex.clear();

        std::unordered_map<std::string, int> channelByName;
        for (size_t i = 0; i < m_Bones.size(); i++)
            channelByName.emplace(m_Bones[i].GetBoneName(), (int)i);

        // Iterative pre-order walk, so every parent lands before its children.
        struct Item { const AssimpNodeData* node; int parent; };
        std::vector<Item> stack{ { &m_RootNode, -1 } };
        while (!stack.empty()){
            Item item = stack.back();
            stack.pop_back();

            Node n;
            n.name = item.node->name;
            n.parent = item.parent;
            n.bindLocal = item.node->transformation;
            n.bindPose = JointPose::FromMatrix(n.bindLocal);
            auto ch = channelByName.find(n.name);
            if (ch != channelByName.end()) { n.channel = ch->second; }
            auto bi = m_BoneInfoMap.find(n.name);
            if (bi != m_BoneInfoMap.end() && bi->second.id >= 0 && bi->second.id < MAX_BONES){
                n.boneIndex = bi->second.id;
                n.offset = bi->second.offset;
            }

            int index = (int)m_Nodes.size();
            m_NodeIndex.emplace(n.name, index); // first wins on duplicate names
            m_Nodes.push_back(std::move(n));

            // Push children reversed so they pop in their original order.
            for (auto c = item.node->children.rbegin(); c != item.node->children.rend(); ++c)
                stack.push_back({ &*c, index });
        }
    }

    int Animation::FindNode(const std::string& name) const{
        auto it = m_NodeIndex.find(name);
        return it != m_NodeIndex.end() ? it->second : -1;
    }

    Bone* Animation::FindBone(const std::string& name){
        for (auto& bone : m_Bones)
            if (bone.GetBoneName() == name) { return &bone; }
        return nullptr;
    }

    // ---- Animator ----

    Animator::Animator(){
        m_FinalBoneMatrices.resize(MAX_BONES, glm::mat4(1.0f));
    }

    void Animator::CancelBlend(){
        m_PrevAnimation = nullptr;
        m_BlendElapsed = m_BlendDuration = 0.0f;
        m_SourceFrozen = false;
        m_LastBlendedValid = false;
    }

    void Animator::PlayAnimation(Animation* animation){
        m_CurrentAnimation = animation;
        m_CurrentTime = 0.0f;
        m_Finished = false;
        m_PingPongDir = 1;
        CancelBlend(); // cancel any in-flight crossfade
    }

    void Animator::CrossFade(Animation* next, float fadeSeconds){
        if (!next) { return; }
        if (!m_CurrentAnimation || fadeSeconds <= 0.0f){ PlayAnimation(next); return; }

        if (m_PrevAnimation && m_LastBlendedValid){
            // Interrupting a crossfade: start from the pose actually on screen rather
            // than snapping back to the outgoing clip.
            m_FrozenPose = m_LastBlended;
            m_SourceFrozen = true;
        }
        else{
            m_PrevTime = m_CurrentTime;
            m_SourceFrozen = false;
        }
        m_PrevAnimation = m_CurrentAnimation;
        m_LastBlendedValid = false;

        m_CurrentAnimation = next;
        m_CurrentTime = 0.0f;
        m_Finished = false;
        m_PingPongDir = 1;
        m_BlendDuration = fadeSeconds;
        m_BlendElapsed = 0.0f;
        BuildBlendMap();
    }

    void Animator::BuildBlendMap(){
        const auto& nodes = m_CurrentAnimation->GetNodes();
        m_BlendMap.resize(nodes.size());
        if (m_PrevAnimation == m_CurrentAnimation){
            for (size_t i = 0; i < nodes.size(); i++) { m_BlendMap[i] = (int)i; }
            return;
        }
        for (size_t i = 0; i < nodes.size(); i++)
            m_BlendMap[i] = m_PrevAnimation->FindNode(nodes[i].name);
    }

    void Animator::Stop(){
        m_CurrentTime = 0.0f;
        m_Paused = true;
        m_Finished = false;
        m_PingPongDir = 1;
        CancelBlend();
    }

    void Animator::Seek(float seconds){
        if (!m_CurrentAnimation) { return; }
        float tps = m_CurrentAnimation->GetTicksPerSecond();
        float dur = m_CurrentAnimation->GetDuration();
        m_CurrentTime = glm::clamp(seconds * tps, 0.0f, dur);
        m_Finished = false;
        CancelBlend(); // a manual seek cancels any crossfade
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
                if (!m_SourceFrozen && pdur > 0.0f){
                    m_PrevTime += m_PrevAnimation->GetTicksPerSecond() * dt * std::abs(m_Speed);
                    m_PrevTime = (m_LoopMode == LoopMode::Loop) ? std::fmod(m_PrevTime, pdur)
                                                                : std::min(m_PrevTime, pdur);
                }
            }
            float t = (m_BlendDuration > 0.0f) ? glm::clamp(m_BlendElapsed / m_BlendDuration, 0.0f, 1.0f) : 1.0f;
            if (t >= 1.0f){
                CancelBlend();
                ComputePose();
            }
            else{
                ComputeBlendedPose(t);
            }
        }
        else{
            ComputePose();
        }
    }

    void Animator::ComputePose(){
        Animation* anim = m_CurrentAnimation;
        const auto& nodes = anim->GetNodes();
        m_Globals.resize(nodes.size());
        for (size_t i = 0; i < nodes.size(); i++){
            const Animation::Node& n = nodes[i];
            glm::mat4 local = (n.channel >= 0) ? anim->m_Bones[n.channel].Sample(m_CurrentTime).ToMatrix()
                                               : n.bindLocal;
            m_Globals[i] = (n.parent >= 0) ? m_Globals[n.parent] * local : local;
            if (n.boneIndex >= 0)
                m_FinalBoneMatrices[n.boneIndex] = anim->m_GlobalInverseTransform * m_Globals[i] * n.offset;
        }
    }

    void Animator::ComputeBlendedPose(float t){
        Animation* anim = m_CurrentAnimation;
        Animation* src = m_PrevAnimation;
        const auto& nodes = anim->GetNodes();
        const auto& srcNodes = src->GetNodes();
        m_Globals.resize(nodes.size());
        m_LastBlended.resize(nodes.size());

        for (size_t i = 0; i < nodes.size(); i++){
            const Animation::Node& n = nodes[i];
            JointPose pose = (n.channel >= 0) ? anim->m_Bones[n.channel].Sample(m_CurrentTime) : n.bindPose;

            int j = m_BlendMap[i];
            if (j >= 0){
                JointPose from;
                if (m_SourceFrozen && (size_t)j < m_FrozenPose.size()) { from = m_FrozenPose[j]; }
                else if (srcNodes[j].channel >= 0) { from = src->m_Bones[srcNodes[j].channel].Sample(m_PrevTime); }
                else { from = srcNodes[j].bindPose; }
                pose = JointPose::Blend(from, pose, t);
            }
            m_LastBlended[i] = pose;

            glm::mat4 local = pose.ToMatrix();
            m_Globals[i] = (n.parent >= 0) ? m_Globals[n.parent] * local : local;
            if (n.boneIndex >= 0)
                m_FinalBoneMatrices[n.boneIndex] = anim->m_GlobalInverseTransform * m_Globals[i] * n.offset;
        }
        m_LastBlendedValid = true;
    }
}
