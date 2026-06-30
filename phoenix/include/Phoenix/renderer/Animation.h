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
        // Compute the pose of `anim` at `timeTicks` into `out` (recursive hierarchy walk).
        void ComputePose(Animation* anim, float timeTicks, std::vector<glm::mat4>& out);
        void ComputeNode(Animation* anim, const AssimpNodeData* node,
                         const glm::mat4& parentTransform, float timeTicks, std::vector<glm::mat4>& out);

        std::vector<glm::mat4> m_FinalBoneMatrices;
        Animation* m_CurrentAnimation = nullptr;
        float m_CurrentTime = 0.0f;        // ticks

        LoopMode m_LoopMode = LoopMode::Loop;
        bool  m_Paused = false;
        bool  m_Finished = false;          // set when a Once clip reaches the end
        float m_Speed = 1.0f;
        int   m_PingPongDir = 1;           // +1 forward, -1 backward (PingPong)
        PlayWindow m_PlayWindow{ 0.0f, 0.0f, false };

        // Crossfade state: while m_PrevAnimation is set, blend prev -> current.
        Animation* m_PrevAnimation = nullptr;
        float m_PrevTime = 0.0f;           // ticks
        float m_BlendDuration = 0.0f;      // seconds
        float m_BlendElapsed = 0.0f;       // seconds
        std::vector<glm::mat4> m_PoseA;    // scratch (prev)
        std::vector<glm::mat4> m_PoseB;    // scratch (current)
    };
}
