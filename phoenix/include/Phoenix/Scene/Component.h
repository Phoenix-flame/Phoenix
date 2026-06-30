#pragma once
#include <Phoenix/core/base.h>
#include <Phoenix/core/assert.h>
#include <Phoenix/Scene/SceneCamera.h>
#include <glm/gtc/matrix_transform.hpp>
#include <Phoenix/renderer/shader.h>
#include <Phoenix/renderer/VertexArray.h>
#include <Phoenix/renderer/Mesh.h>
#include <Phoenix/Scene/ScriptableEntity.h>
namespace Phoenix{

    struct TagComponent{
		std::string Tag;

		TagComponent() = default;
		TagComponent(const TagComponent&) = default;
		TagComponent(const std::string& tag) : Tag(tag) {}
	};

    struct CameraComponent{
        SceneCamera camera;
        bool primary = false;
        CameraComponent() = default;
        CameraComponent(const CameraComponent& other) = default;
    };

    // Third-person follow camera. Add it to a camera entity to make that camera track
    // another entity (named by its Tag). When primary, the Scene positions the view
    // behind+above the target each frame and looks at it. followYaw orbits the camera
    // behind the target's facing (turning the character turns the view).
    struct CameraFollowComponent{
        std::string target;        // Tag of the entity to follow
        float distance = 6.0f;     // metres behind the target
        float height = 2.5f;       // camera height above the target's origin
        float lookHeight = 1.5f;   // height of the look-at point above the target
        bool  followYaw = true;    // stay behind the target's facing
        CameraFollowComponent() = default;
        CameraFollowComponent(const CameraFollowComponent&) = default;
    };

    struct Material
    {
        glm::vec3 ambient = glm::vec3(1.0f, 0.5f, 0.31f);
        glm::vec3 diffuse = glm::vec3(1.0f, 0.5f, 0.31f);
        glm::vec3 specular = glm::vec3(0.5f, 0.5f, 0.5f);
        float shininess = 32.0f;
        // Self-illumination for the bloom/glow effect. emissive * emissiveStrength is
        // added to the lit colour; strength > 1 pushes it into HDR so it blooms.
        glm::vec3 emissive = glm::vec3(0.0f);
        float emissiveStrength = 0.0f;
        // Environment (sky/ground) reflection blended in by this amount (0 = none,
        // 1 = mirror). Reflects a procedural environment, not the scene itself.
        float reflectivity = 0.0f;
    };

    struct MeshComponent{
        Ref<Model> model;
        Material material;
        MeshComponent() = default;
        MeshComponent(const MeshComponent& other) = default;
        MeshComponent(const Ref<Model>& model) : model(model) {}
    };

    struct CubeComponent{
        Material material;
        CubeComponent() = default;
        CubeComponent(const CubeComponent&) = default;
    };

    // A built-in procedural shape (sphere, pillar/cylinder, cone, plane, cube). The
    // unit-sized mesh is generated lazily and shared across all entities of the same
    // type (cached by the Scene); scale it with the entity transform. Add a Rigid Body
    // + Mesh Collider to make it interact physically.
    struct PrimitiveComponent{
        enum class Type { Cube = 0, Sphere = 1, Cylinder = 2, Cone = 3, Plane = 4 };
        Type type = Type::Sphere;
        Material material;
        PrimitiveComponent() = default;
        PrimitiveComponent(const PrimitiveComponent&) = default;
        PrimitiveComponent(Type t) : type(t) {}
    };

    // Physics. Type values match PhysicsWorld::BodyType. runtimeBodyID is filled in
    // when the scene's physics simulation starts (0xffffffff = no body / not running).
    struct RigidBodyComponent{
        enum class Type { Static = 0, Dynamic = 1, Kinematic = 2 };
        Type type = Type::Dynamic;
        uint32_t runtimeBodyID = 0xffffffff;
        RigidBodyComponent() = default;
        RigidBodyComponent(const RigidBodyComponent&) = default;
    };

    struct BoxColliderComponent{
        glm::vec3 halfExtents = { 0.5f, 0.5f, 0.5f };
        BoxColliderComponent() = default;
        BoxColliderComponent(const BoxColliderComponent&) = default;
    };

    // Collider built from the entity's MeshComponent geometry. convex = convex hull
    // (works for dynamic bodies); otherwise an exact triangle mesh (static only).
    struct MeshColliderComponent{
        bool convex = true;
        MeshColliderComponent() = default;
        MeshColliderComponent(const MeshColliderComponent&) = default;
    };

    // When present, the entity is drawn as a wireframe ("mesh network"). The bool
    // exists only so the type is non-empty (entt returns void from emplace<> for
    // empty types, which AddComponent's `T&` return can't bind to).
    struct WireframeComponent{
        bool enabled = true;
        WireframeComponent() = default;
        WireframeComponent(const WireframeComponent&) = default;
    };

    // An editable heightmap terrain (sculpt hills/valleys/riverbeds). `heights` is a
    // resolution×resolution grid; the mesh is regenerated when `dirty`. The Mesh and
    // runtime body are transient (not serialized / reset on copy).
    struct TerrainComponent{
        int resolution = 64;        // grid is resolution×resolution vertices
        float size = 40.0f;         // world width/depth
        std::vector<float> heights; // resolution*resolution, row-major (z*res + x)
        Material material;
        bool generateCollider = true;

        Ref<Mesh> mesh;             // generated; not serialized
        bool dirty = true;
        uint32_t runtimeBodyID = 0xffffffff;

        TerrainComponent(){ heights.resize((size_t)resolution * resolution, 0.0f); }
        TerrainComponent(const TerrainComponent& o)
            : resolution(o.resolution), size(o.size), heights(o.heights),
              material(o.material), generateCollider(o.generateCollider), dirty(true) {}
    };

    // An animated transparent water surface (a flat grid displaced by waves in the
    // water shader). Place it at a water level over a sculpted terrain basin to make
    // a lake. The grid mesh is transient (regenerated, not serialized).
    struct WaterComponent{
        float size = 40.0f;
        int resolution = 96;
        glm::vec3 color = glm::vec3(0.05f, 0.22f, 0.35f);
        float alpha = 0.7f;
        float amplitude = 0.15f;
        float waveScale = 0.6f;
        float speed = 1.2f;

        Ref<Mesh> mesh; // flat grid; not serialized
        WaterComponent() = default;
        WaterComponent(const WaterComponent& o)
            : size(o.size), resolution(o.resolution), color(o.color), alpha(o.alpha),
              amplitude(o.amplitude), waveScale(o.waveScale), speed(o.speed) {}
    };

    // An animation event: fire OnAnimationEvent(name) in the entity's Lua script when the
    // playhead of clip `clip` crosses `time` (seconds) while playing.
    struct AnimEvent{
        int clip = 0;
        float time = 0.0f;       // seconds
        std::string name;
    };

    // Plays a skeletal animation on the entity's MeshComponent model. `clip` selects which
    // animation in the model. The Timeline panel and Lua API only WRITE the config/request
    // fields here; the Scene animation loop is the single place that drives the live
    // Animator. The Animator + transient flags are not serialized / reset on copy.
    struct AnimationComponent{
        int   clip = 0;          // index into the model's animations (embedded + extra)
        bool  playing = true;    // advance the animation in edit + play
        float speed = 1.0f;
        int   loopMode = 0;      // 0 Loop, 1 Once, 2 PingPong (Animator::LoopMode)
        float crossfade = 0.2f;  // seconds; blend used when the clip changes

        std::vector<std::string> extraClips; // extra animation files merged onto the rig
        std::vector<AnimEvent> events;        // fired while playing

        // Requests set by Lua / the timeline, consumed (and reset) by the Scene loop.
        float pendingSeek = -1.0f;      // >= 0 -> seek to this many seconds
        int   pendingCrossfade = -1;    // >= 0 -> crossfade to this clip index

        // Transient runtime state (not serialized, not copied).
        Ref<Animator> animator;         // created lazily once the model is ready
        int  activeClip = -1;           // which clip the animator is currently playing
        bool extraClipsLoaded = false;  // extraClips merged into the model yet?

        AnimationComponent() = default;
        AnimationComponent(const AnimationComponent& o)
            : clip(o.clip), playing(o.playing), speed(o.speed), loopMode(o.loopMode),
              crossfade(o.crossfade), extraClips(o.extraClips), events(o.events) {}
    };

    // A Lua script (edited inline, serialized). It runs while the scene is playing;
    // the live runtime is owned by the Scene, not stored here.
    struct LuaScriptComponent{
        std::string source =
            "-- runs while playing (press Run)\n"
            "local t = 0.0\n"
            "function OnUpdate(dt)\n"
            "    t = t + dt\n"
            "    local x, y, z = GetTranslation()\n"
            "    SetTranslation(x, y, z)\n"
            "end\n";
        LuaScriptComponent() = default;
        LuaScriptComponent(const LuaScriptComponent&) = default;
    };


    struct DirLightComponent{
        // diffuse is the dominant term so the light is actually directional; its own
        // ambient is small since the scene has a separate global ambient. (Previously
        // ambient=1.0 flooded everything flat, making the light look like it did nothing.)
        glm::vec3 ambient = glm::vec3(0.05f);
        glm::vec3 diffuse = glm::vec3(0.8f);
        glm::vec3 specular = glm::vec3(1.0f);
        bool isActive = true;
        bool castsShadow = true; // render a shadow map for this light
        DirLightComponent() = default;
        DirLightComponent(const DirLightComponent&) = default;
    };

    struct PointLightComponent: public DirLightComponent{
        float constant = 1.0f;
        float linear = 0.02f;
        float quadratic = 0.099f; 
        void SetConstant(float constant) { this->constant = constant;} 
        void SetLinear(float linear) { this->linear = linear;} 
        void SetQuadratic(float quadratic) { this->quadratic = quadratic;} 
        PointLightComponent() = default;
        PointLightComponent(const PointLightComponent&) = default;
    };


    struct TransformComponent{
        glm::vec3 Translation = {0.0f, 0.0f, 0.0f};
        glm::vec3 Rotation = {0.0f, 0.0f, 0.0f};
        glm::vec3 Scale = {1.0f, 1.0f, 1.0f};
        TransformComponent() = default;
		TransformComponent(const TransformComponent&) = default;
		TransformComponent(const glm::vec3& translation) : Translation(translation) {}

		glm::mat4 GetTransform() const{
			glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), Rotation.x, { 1, 0, 0 })
				* glm::rotate(glm::mat4(1.0f), Rotation.y, { 0, 1, 0 })
				* glm::rotate(glm::mat4(1.0f), Rotation.z, { 0, 0, 1 });

			return glm::translate(glm::mat4(1.0f), Translation)
				* rotation
				* glm::scale(glm::mat4(1.0f), Scale);
		}
    };


    struct NativeScriptComponent{
        ScriptableEntity* Instance = nullptr;

		ScriptableEntity*(*InstantiateScript)();
		void (*DestroyScript)(NativeScriptComponent*);

		template<typename T>
		void Bind(){
			InstantiateScript = []() { return static_cast<ScriptableEntity*>(new T()); };
			DestroyScript = [](NativeScriptComponent* nsc) { delete nsc->Instance; nsc->Instance = nullptr; };
		}
    };
}