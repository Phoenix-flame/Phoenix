#pragma once
#include <Phoenix/core/base.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>

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

    // A node's local transform split into translation / rotation / scale. Poses are
    // blended in this form: lerping composed matrices shears and shrinks joints.
    struct JointPose{
        glm::vec3 translation = glm::vec3(0.0f);
        glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3 scale = glm::vec3(1.0f);

        glm::mat4 ToMatrix() const;
        static JointPose FromMatrix(const glm::mat4& m); // assumes no shear
        static JointPose Blend(const JointPose& a, const JointPose& b, float t);
    };

    // One bone's animation channel: keyframes for translation, rotation and scale.
    // Sample(time) interpolates them into a local pose. Sampling is const so a clip
    // can be shared by every entity using the model.
    class Bone{
    public:
        Bone(const std::string& name, int id,
             std::vector<KeyPosition> positions,
             std::vector<KeyRotation> rotations,
             std::vector<KeyScale> scales);

        JointPose Sample(float animationTime) const;
        const std::string& GetBoneName() const { return m_Name; }
        int GetBoneID() const { return m_ID; }

    private:
        std::vector<KeyPosition> m_Positions;
        std::vector<KeyRotation> m_Rotations;
        std::vector<KeyScale>    m_Scales;
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
        // The node hierarchy flattened parent-before-child, with each node's channel
        // and bone slot resolved once so posing does no string lookups per frame.
        struct Node{
            std::string name;
            int parent = -1;              // index into GetNodes(), -1 for the root
            int channel = -1;             // index into m_Bones, -1 if not animated
            int boneIndex = -1;           // slot in the final bone matrices, -1 if none
            glm::mat4 offset = glm::mat4(1.0f);
            glm::mat4 bindLocal = glm::mat4(1.0f);
            JointPose bindPose;           // bindLocal decomposed, for blending
        };

        // Build GetNodes() from m_RootNode / m_Bones / m_BoneInfoMap. The loader calls
        // this once after filling those fields.
        void BuildNodeCache();
        const std::vector<Node>& GetNodes() const { return m_Nodes; }
        int FindNode(const std::string& name) const; // -1 if absent

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

    private:
        std::vector<Node> m_Nodes;
        std::unordered_map<std::string, int> m_NodeIndex;
    };

    enum class LoopMode { Loop = 0, Once = 1, PingPong = 2 };

    // Advances a playing animation and produces the final bone matrices for skinning.
    // Supports pause/seek, loop modes, and crossfade blending between two clips. Times
    // in the public API are in SECONDS; internally the clip is sampled in ticks.
    class Animator{
    public:
        Animator();

        // Switch immediately to `animation` (resets time to 0).
        void PlayAnimation(Animation* animation);
        // Blend from the current clip to `next` over `fadeSeconds`.
        void CrossFade(Animation* next, float fadeSeconds);

        // Advance the playing clip by dt seconds and recompute the bone matrices. Pass
        // dt = 0 to re-pose at the current time without advancing (used when paused or
        // scrubbing). Does nothing structural when paused except still posing at rest.
        void UpdateAnimation(float dt);

        void Pause()  { m_Paused = true; }
        void Resume() { m_Paused = false; }
        void Stop();                       // reset to t=0 and pause
        void Seek(float seconds);          // jump to an absolute time (seconds), clamped
        void SetLoopMode(LoopMode mode) { m_LoopMode = mode; }
        void SetSpeed(float speed) { m_Speed = speed; }

        const std::vector<glm::mat4>& GetFinalBoneMatrices() const { return m_FinalBoneMatrices; }
        Animation* GetCurrentAnimation() const { return m_CurrentAnimation; }
        float GetCurrentTime() const { return m_CurrentTime; }      // ticks (legacy)
        float GetCurrentSeconds() const;
        float GetDurationSeconds() const;
        bool  IsPaused() const { return m_Paused; }
        bool  IsFinished() const { return m_Finished; }

        // The [previous, current] playhead window in SECONDS for the clip just advanced,
        // so the caller can fire events crossed this frame. When a Loop wrapped, `wrapped`
        // is true and the window is [prevSeconds, duration] + [0, curSeconds].
        struct PlayWindow { float prevSeconds; float curSeconds; bool wrapped; };
        const PlayWindow& GetPlayWindow() const { return m_PlayWindow; }

    private:
        // Pose the current clip at m_CurrentTime into m_FinalBoneMatrices.
        void ComputePose();
        // Same, blending from the crossfade source by weight t in [0, 1].
        void ComputeBlendedPose(float t);
        // Point m_BlendMap at the source clip's nodes, matched by name (clips merged
        // from other files can have different hierarchies).
        void BuildBlendMap();
        void CancelBlend();

        std::vector<glm::mat4> m_FinalBoneMatrices;
        Animation* m_CurrentAnimation = nullptr;
        float m_CurrentTime = 0.0f;        // ticks

        LoopMode m_LoopMode = LoopMode::Loop;
        bool  m_Paused = false;
        bool  m_Finished = false;          // set when a Once clip reaches the end
        float m_Speed = 1.0f;
        int   m_PingPongDir = 1;           // +1 forward, -1 backward (PingPong)
        PlayWindow m_PlayWindow{ 0.0f, 0.0f, false };

        // Crossfade state: while m_PrevAnimation is set, blend prev -> current. When a
        // crossfade interrupts another, the half-blended pose is frozen as the source
        // (m_SourceFrozen) so the switch doesn't pop.
        Animation* m_PrevAnimation = nullptr;
        float m_PrevTime = 0.0f;           // ticks
        float m_BlendDuration = 0.0f;      // seconds
        float m_BlendElapsed = 0.0f;       // seconds
        bool  m_SourceFrozen = false;
        std::vector<JointPose> m_FrozenPose;   // per m_PrevAnimation node
        std::vector<int> m_BlendMap;           // current node -> source node, or -1

        // Scratch, reused every frame.
        std::vector<glm::mat4> m_Globals;      // per node of the clip being posed
        std::vector<JointPose> m_LastBlended;  // last blended local pose (current layout)
        bool m_LastBlendedValid = false;
    };
}
