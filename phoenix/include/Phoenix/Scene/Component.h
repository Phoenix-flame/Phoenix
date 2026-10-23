#pragma once
#include <Phoenix/core/base.h>
#include <Phoenix/core/assert.h>
#include <Phoenix/Scene/SceneCamera.h>
#include <glm/gtc/matrix_transform.hpp>
#include <Phoenix/renderer/shader.h>
#include <Phoenix/renderer/VertexArray.h>
#include <Phoenix/renderer/Mesh.h>
#include <Phoenix/renderer/WaterRipples.h>
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
        // Engine forward is local -Z. Some rigs (e.g. many Mixamo characters) face +Z;
        // set this so the camera sits behind the model's actual back, not its front.
        bool  modelForwardZ = false;
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
        // Optional textures loaded from disk (assigned via the Textures panel). The
        // diffuse map overrides the flat diffuse colour / any mesh-embedded texture; the
        // normal map adds tangent-space surface detail. Stored by path (serialized); the
        // GL texture is resolved/cached from the path at draw time.
        std::string diffusePath;
        std::string normalPath;
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
    // The material/mass fields are applied to the Jolt body on Run. The body's shape
    // is made of EVERY collider component on the entity (box + sphere + ... combine
    // into one compound shape); each collider has its own local offset/rotation.
    struct RigidBodyComponent{
        enum class Type { Static = 0, Dynamic = 1, Kinematic = 2 };
        Type type = Type::Dynamic;

        float friction = 0.5f;
        float restitution = 0.0f;      // bounciness (0 = dead, 1 = superball)
        float density = 1000.0f;       // kg/m3; mass follows from the collider volume
        float mass = 0.0f;             // kg; > 0 overrides the density-derived mass
        glm::vec3 centerOfMassOffset = { 0.0f, 0.0f, 0.0f }; // local; low COM = hard to tip
        float linearDamping = 0.05f;
        float angularDamping = 0.05f;
        float gravityFactor = 1.0f;    // 0 = floats, 1 = normal gravity
        bool  continuousCollision = false; // for fast movers (bullets) so they don't tunnel
        bool  isSensor = false;        // trigger volume: detects contacts, no response
        bool  allowSleeping = true;    // resting bodies stop simulating until disturbed
        bool  lockPosition[3] = { false, false, false }; // freeze world X/Y/Z movement
        bool  lockRotation[3] = { false, false, false }; // freeze world X/Y/Z rotation
        int   layer = 0;               // collision layer (Scene physics settings matrix)
        glm::vec3 initialVelocity = { 0.0f, 0.0f, 0.0f };
        glm::vec3 initialAngularVelocity = { 0.0f, 0.0f, 0.0f }; // rad/s

        uint32_t runtimeBodyID = 0xffffffff;
        RigidBodyComponent() = default;
        RigidBodyComponent(const RigidBodyComponent&) = default;
    };

    // Colliders. Sizes are in the entity's local space and scaled by its Transform on
    // Run; offset/rotation place the shape relative to the entity origin. Several
    // different colliders on one entity form a compound body (e.g. a box handle with
    // a sphere head = a hammer that lands head-first).
    struct BoxColliderComponent{
        glm::vec3 halfExtents = { 0.5f, 0.5f, 0.5f };
        glm::vec3 offset = { 0.0f, 0.0f, 0.0f };
        glm::vec3 rotation = { 0.0f, 0.0f, 0.0f }; // radians
        BoxColliderComponent() = default;
        BoxColliderComponent(const BoxColliderComponent&) = default;
    };

    struct SphereColliderComponent{
        float radius = 0.5f;
        glm::vec3 offset = { 0.0f, 0.0f, 0.0f };
        SphereColliderComponent() = default;
        SphereColliderComponent(const SphereColliderComponent&) = default;
    };

    // A capsule along local Y (rotate it for a horizontal one). Total height =
    // 2 * (halfHeight + radius); halfHeight is the straight cylindrical section only.
    struct CapsuleColliderComponent{
        float radius = 0.3f;
        float halfHeight = 0.5f;
        glm::vec3 offset = { 0.0f, 0.0f, 0.0f };
        glm::vec3 rotation = { 0.0f, 0.0f, 0.0f }; // radians
        CapsuleColliderComponent() = default;
        CapsuleColliderComponent(const CapsuleColliderComponent&) = default;
    };

    struct CylinderColliderComponent{
        float radius = 0.5f;
        float halfHeight = 0.5f;
        glm::vec3 offset = { 0.0f, 0.0f, 0.0f };
        glm::vec3 rotation = { 0.0f, 0.0f, 0.0f }; // radians
        CylinderColliderComponent() = default;
        CylinderColliderComponent(const CylinderColliderComponent&) = default;
    };

    // Collider built from the entity's MeshComponent geometry. convex = convex hull
    // (works for dynamic bodies); otherwise an exact triangle mesh (static only).
    struct MeshColliderComponent{
        bool convex = true;
        MeshColliderComponent() = default;
        MeshColliderComponent(const MeshColliderComponent&) = default;
    };

    // Connects this entity's rigid body to another body (found by Tag; leave the tag
    // empty to anchor to the world) when the simulation starts. Anchor points/axes are
    // given in WORLD space of the edit-time layout.
    //  - Fixed: welds the bodies together (breakable walls, glued parts).
    //  - Point: ball-and-socket at `pivot` (chains, ropes, wrecking balls).
    //  - Distance: keeps own center and the other body's center (or `pivot` when
    //    world-anchored) between min/max distance (-1 = starting distance); a spring
    //    frequency makes it a soft spring/bungee.
    //  - Hinge: rotates around `axis` through `pivot`, optional angle limits and a
    //    motor (doors, seesaws, wheels, windmills).
    //  - Slider: moves along `axis` only, optional position limits and a motor
    //    (pistons, elevators, drawers).
    //  - Cone: ball-and-socket whose swing is limited to a cone around `axis`
    //    (ragdoll shoulders, lamp arms).
    struct JointComponent{
        enum class Type { Point = 0, Distance = 1, Hinge = 2, Fixed = 3, Slider = 4, Cone = 5 };
        enum class Motor { Off = 0, Velocity = 1, Position = 2 };
        Type type = Type::Point;
        std::string connectedTag;          // other rigid-body entity; empty = world
        glm::vec3 pivot = { 0.0f, 0.0f, 0.0f };
        glm::vec3 axis = { 0.0f, 0.0f, 1.0f }; // hinge / slider / cone axis
        float minDistance = -1.0f;         // distance joint; -1 = use starting distance
        float maxDistance = -1.0f;
        float springFrequency = 0.0f;      // distance joint softness in Hz (0 = rigid)
        float springDamping = 0.5f;
        bool  limitAngles = false;         // hinge limits (radians, about the rest pose)
        float minAngle = -1.5708f;
        float maxAngle =  1.5708f;
        bool  limitSlide = false;          // slider limits (meters, about the rest pose)
        float minSlide = -1.0f;
        float maxSlide =  1.0f;
        float coneAngle = 0.7854f;         // cone half angle (radians)
        Motor motor = Motor::Off;          // hinge / slider motor
        float motorTarget = 0.0f;          // rad/s or m/s (velocity), rad or m (position)
        float motorMaxForce = 0.0f;        // N / N·m; 0 = unlimited
        float friction = 0.0f;             // hinge / slider friction torque / force
        float breakForce = 0.0f;           // N; snaps above this force (0 = unbreakable)
        bool  collideConnected = false;    // let the two connected bodies collide
        uint32_t runtimeJointID = 0xffffffff;
        JointComponent() = default;
        JointComponent(const JointComponent&) = default;
    };

    // A kinematic capsule character (player / NPC): walks, climbs steps, slides off
    // too-steep slopes, stands on moving platforms and pushes dynamic bodies. The
    // capsule is centered on the entity position. Drive it from Lua (Move / Jump) or
    // tick keyboardControl for built-in WASD + Space movement relative to the camera
    // (while ticked, keyboard input replaces Move from scripts).
    // Don't add a RigidBodyComponent to the same entity.
    struct CharacterControllerComponent{
        float radius = 0.35f;
        float halfHeight = 0.55f;     // cylindrical part; total height = 2*(halfHeight+radius)
        float maxSlopeAngle = 0.873f; // radians (50 deg); steeper is a wall
        float stepHeight = 0.35f;     // stair height it walks up (0 = off)
        float moveSpeed = 5.0f;       // m/s (keyboard control)
        float jumpSpeed = 6.0f;       // m/s upward
        float gravityFactor = 1.0f;
        float airControl = 0.3f;      // 0..1 steering while airborne
        float mass = 70.0f;           // kg, for pushing bodies
        float maxPushForce = 200.0f;  // N
        int   layer = 0;
        bool  keyboardControl = false; // WASD/arrows + Space, camera relative
        bool  faceMovement = true;     // turn the entity (yaw) toward its movement

        // Runtime (not serialized): the input for the next step and the last state.
        glm::vec3 desiredVelocity = { 0.0f, 0.0f, 0.0f };
        bool jumpRequested = false;
        bool grounded = false;
        glm::vec3 velocity = { 0.0f, 0.0f, 0.0f };
        uint32_t runtimeCharacterID = 0xffffffff;
        CharacterControllerComponent() = default;
        CharacterControllerComponent(const CharacterControllerComponent& o)
            : radius(o.radius), halfHeight(o.halfHeight), maxSlopeAngle(o.maxSlopeAngle),
              stepHeight(o.stepHeight), moveSpeed(o.moveSpeed), jumpSpeed(o.jumpSpeed),
              gravityFactor(o.gravityFactor), airControl(o.airControl), mass(o.mass),
              maxPushForce(o.maxPushForce), layer(o.layer), keyboardControl(o.keyboardControl),
              faceMovement(o.faceMovement) {}
    };

    // A force zone (needs no rigid body): every dynamic body inside the box/sphere
    // volume (local half extents, scaled by the Transform) gets a force each step.
    //  - Directional: constant acceleration along `direction` (wind, conveyor
    //    updraft, anti-gravity lift).
    //  - Radial: toward (strength > 0) or away from (< 0) the entity center
    //    (black hole, explosion field), falling off linearly to the edge.
    // Strength is an acceleration (m/s2), so heavy and light bodies react alike.
    struct ForceFieldComponent{
        enum class Shape { Box = 0, Sphere = 1 };
        enum class Mode { Directional = 0, Radial = 1 };
        Shape shape = Shape::Box;
        Mode mode = Mode::Directional;
        glm::vec3 halfExtents = { 2.0f, 2.0f, 2.0f }; // box; sphere uses halfExtents.x
        glm::vec3 direction = { 0.0f, 1.0f, 0.0f };   // directional, in the entity's local space
        float strength = 15.0f;                       // m/s2
        float drag = 0.0f;                            // extra velocity damping inside (1/s)
        ForceFieldComponent() = default;
        ForceFieldComponent(const ForceFieldComponent&) = default;
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

    // An animated transparent water surface: Gerstner waves in the shader plus an
    // interactive ripple simulation (CPU wave equation) that reacts to physics
    // bodies. While playing, dynamic bodies get buoyancy/drag from the surface and
    // splash ripples when they hit it. Place at a water level over a terrain basin
    // to make a lake. The grid mesh and ripple sim are transient (not serialized).
    struct WaterComponent{
        float size = 40.0f;
        int resolution = 96;
        glm::vec3 color = glm::vec3(0.05f, 0.22f, 0.35f);
        float alpha = 0.7f;
        float amplitude = 0.15f;
        float waveScale = 0.6f;
        float speed = 1.2f;
        float choppiness = 0.6f;   // Gerstner crest sharpness (0 = plain sine waves)
        float foam = 0.5f;         // crest/ripple foam intensity (0 = off)

        bool interactiveRipples = true; // splash/wake heightfield simulation
        int rippleResolution = 128;     // sim grid (per water surface)

        // Physics coupling while playing (0 buoyancy = objects ignore the water).
        // buoyancy = the water's density in units of 1000 kg/m3 (1 = real water):
        // rigid bodies with density below buoyancy*1000 float, denser ones sink.
        float buoyancy = 1.0f;
        float linearDrag = 0.6f;
        float angularDrag = 0.3f;

        Ref<Mesh> mesh;             // flat grid; not serialized
        Ref<WaterRipples> ripples;  // live ripple sim; not serialized
        WaterComponent() = default;
        WaterComponent(const WaterComponent& o)
            : size(o.size), resolution(o.resolution), color(o.color), alpha(o.alpha),
              amplitude(o.amplitude), waveScale(o.waveScale), speed(o.speed),
              choppiness(o.choppiness), foam(o.foam),
              interactiveRipples(o.interactiveRipples), rippleResolution(o.rippleResolution),
              buoyancy(o.buoyancy), linearDrag(o.linearDrag), angularDrag(o.angularDrag) {}
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