#pragma once
#include <Phoenix/core/base.h>
#include <Phoenix/core/assert.h>
#include <Phoenix/Scene/SceneCamera.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
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
        bool  startAsleep = false;     // frozen in place until touched (standing ragdolls, stacks)
        bool  lockPosition[3] = { false, false, false }; // freeze world X/Y/Z movement
        bool  lockRotation[3] = { false, false, false }; // freeze world X/Y/Z rotation
        int   layer = 0;               // collision layer (Scene physics settings matrix)
        glm::vec3 initialVelocity = { 0.0f, 0.0f, 0.0f };
        glm::vec3 initialAngularVelocity = { 0.0f, 0.0f, 0.0f }; // rad/s

        uint32_t runtimeBodyID = 0xffffffff;
        // Runtime (not serialized). The poses of the last two fixed steps, blended
        // for rendering, and the transform the simulation last wrote to the entity:
        // any other change (gizmo drag, SetTranslation) teleports the body there.
        glm::vec3 prevPosition{ 0.0f }, currPosition{ 0.0f };
        glm::quat prevRotation{ 1, 0, 0, 0 }, currRotation{ 1, 0, 0, 0 };
        glm::vec3 syncedTranslation{ 0.0f }, syncedRotation{ 0.0f };
        bool synced = false;
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

    // Any number of box / sphere / capsule / cylinder shapes on one entity (two boxes,
    // a table of five boxes, a dumbbell ...). Combines with the single-shape collider
    // components above into one compound body. Same local-space rules as they use.
    struct CompoundColliderComponent{
        struct Shape{
            enum class Type { Box = 0, Sphere = 1, Capsule = 2, Cylinder = 3 };
            Type type = Type::Box;
            glm::vec3 halfExtents = { 0.5f, 0.5f, 0.5f }; // box
            float radius = 0.5f;                          // sphere / capsule / cylinder
            float halfHeight = 0.5f;                      // capsule / cylinder (local Y)
            glm::vec3 offset = { 0.0f, 0.0f, 0.0f };
            glm::vec3 rotation = { 0.0f, 0.0f, 0.0f };    // radians
        };
        std::vector<Shape> shapes;
        Shape& Add(Shape::Type type) { shapes.push_back({}); shapes.back().type = type; return shapes.back(); }
        CompoundColliderComponent() = default;
        CompoundColliderComponent(const CompoundColliderComponent&) = default;
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
    //    (lamp arms).
    //  - SwingTwist: cone with separate limits on its two swing axes plus a twist
    //    range around `axis` (ragdoll shoulders, hips, knees, neck).
    struct JointComponent{
        enum class Type { Point = 0, Distance = 1, Hinge = 2, Fixed = 3, Slider = 4, Cone = 5, SwingTwist = 6 };
        enum class Motor { Off = 0, Velocity = 1, Position = 2 };
        // One joint. An entity can have any number (a plank hinged at both ends, a
        // ragdoll torso holding the head and both arms ...).
        struct Joint{
            Type type = Type::Point;
            std::string connectedTag;          // other rigid-body entity; empty = world
            glm::vec3 pivot = { 0.0f, 0.0f, 0.0f };
            glm::vec3 axis = { 0.0f, 0.0f, 1.0f }; // hinge / slider / cone / swing-twist axis
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
            // Swing-twist (ragdoll joints): swing cone half angles around the two axes
            // perpendicular to `axis` (planeAxis, and axis x planeAxis), twist range.
            glm::vec3 planeAxis = { 0.0f, 1.0f, 0.0f };
            float swingAngle1 = 0.5f;          // radians
            float swingAngle2 = 0.5f;
            float twistMin = -0.3f;
            float twistMax = 0.3f;
            Motor motor = Motor::Off;          // hinge / slider motor
            float motorTarget = 0.0f;          // rad/s or m/s (velocity), rad or m (position)
            float motorMaxForce = 0.0f;        // N / N·m; 0 = unlimited
            float friction = 0.0f;             // hinge / slider / swing-twist friction
            float breakForce = 0.0f;           // N; snaps above this force (0 = unbreakable)
            bool  collideConnected = false;    // let the two connected bodies collide
            uint32_t runtimeJointID = 0xffffffff;
        };
        std::vector<Joint> joints;
        Joint& Add(Type type = Type::Point) { joints.push_back({}); joints.back().type = type; return joints.back(); }
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
        glm::vec3 prevPosition{ 0.0f }, currPosition{ 0.0f }; // last two fixed steps
        glm::vec3 syncedTranslation{ 0.0f };                  // last written by physics
        bool synced = false;
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

    // Turns the entity's Dynamic rigid body into a four-wheeled car (Jolt wheeled
    // vehicle: suspension, engine, automatic gearbox, differentials, anti-roll bars).
    // The car drives towards local -Z. Wheel layout is in METERS in the entity's
    // local frame (rotated but NOT scaled by the Transform). Optional wheel entities
    // (any visual, e.g. a cylinder shape; no rigid body) are posed every frame.
    // Drive it with keyboardControl (W/S or Up/Down throttle and reverse, A/D or
    // Left/Right steer, Space handbrake) or from Lua with SetVehicleInput.
    struct VehicleComponent{
        enum class Drive { Front = 0, Rear = 1, All = 2 };
        float halfTrack = 0.9f;        // wheel X distance from the center line
        float frontAxle = -1.3f;       // wheel Z (front is -Z)
        float rearAxle = 1.3f;
        float wheelHeight = -0.1f;     // suspension attachment Y
        float wheelRadius = 0.4f;
        float wheelWidth = 0.3f;
        float suspensionMin = 0.2f;    // m
        float suspensionMax = 0.5f;    // m
        float suspensionFrequency = 1.5f; // Hz; higher = stiffer
        float suspensionDamping = 0.5f;
        float maxSteerAngle = 0.52f;   // radians
        float engineTorque = 500.0f;   // N.m
        float maxRPM = 6000.0f;
        float brakeTorque = 1500.0f;   // N.m per wheel
        float handBrakeTorque = 4000.0f;
        Drive drive = Drive::All;
        bool  antiRollBars = true;
        bool  keyboardControl = false;
        std::string wheelTags[4];      // FL, FR, RL, RR visual entities (optional)

        // Runtime (not serialized): driver input (Lua / keyboard) and readouts.
        float forward = 0.0f, right = 0.0f, brake = 0.0f, handBrake = 0.0f;
        float speed = 0.0f, rpm = 0.0f;
        int gear = 0;
        uint32_t runtimeVehicleID = 0xffffffff;
        VehicleComponent() = default;
        VehicleComponent(const VehicleComponent& o)
            : halfTrack(o.halfTrack), frontAxle(o.frontAxle), rearAxle(o.rearAxle), wheelHeight(o.wheelHeight),
              wheelRadius(o.wheelRadius), wheelWidth(o.wheelWidth), suspensionMin(o.suspensionMin),
              suspensionMax(o.suspensionMax), suspensionFrequency(o.suspensionFrequency),
              suspensionDamping(o.suspensionDamping), maxSteerAngle(o.maxSteerAngle), engineTorque(o.engineTorque),
              maxRPM(o.maxRPM), brakeTorque(o.brakeTorque), handBrakeTorque(o.handBrakeTorque), drive(o.drive),
              antiRollBars(o.antiRollBars), keyboardControl(o.keyboardControl){
            for (int i = 0; i < 4; i++) { wheelTags[i] = o.wheelTags[i]; }
        }
    };

    // Physics ragdoll for a skinned, animated model (MeshComponent + Animation). When
    // activated (startActive, or Lua SetRagdoll(true)) capsule bodies are built along
    // the skeleton's humanoid bones from the CURRENT animated pose, joined with
    // swing-twist joints, and the skinned mesh follows the bodies instead of the
    // animation; deactivating removes the bodies and the animation resumes. Bones are
    // matched by name (Mixamo "mixamorig:Hips", "LeftUpLeg", ... and common
    // alternatives such as "pelvis", "thigh", "calf", "upperarm"); missing limbs are
    // skipped, but a hips/pelvis bone is required.
    struct RagdollComponent{
        bool  startActive = false;
        float density = 985.0f;       // kg/m3 (about a human body)
        float friction = 0.6f;
        float limbThickness = 0.22f;  // limb capsule radius as a fraction of bone length
        float torsoThickness = 0.55f;
        float jointFriction = 2.0f;   // N.m; damps flailing
        float jointLimitScale = 1.0f; // scales every joint's angle limits (0.5 = stiff)
        int   layer = 0;
        bool  followHips = true;      // the entity's position follows the pelvis (horizontally)

        // Runtime (not serialized).
        struct Part{
            int node = -1;            // skeleton node driven by this body
            int parentPart = -1;
            uint32_t body = 0xffffffff;
            uint32_t joint = 0xffffffff;
            glm::mat4 bodyToNode{ 1.0f }; // node world = body world * bodyToNode
        };
        std::vector<Part> parts;
        std::vector<glm::mat4> restLocals; // per node, local transform at activation
        std::vector<int> nodePart;         // per node: part index or -1
        glm::vec3 hipsToEntity{ 0.0f };    // world offset entity - pelvis at activation
        bool active = false;
        int request = -1;                  // -1 none, 0 deactivate, 1 activate
        RagdollComponent() = default;
        RagdollComponent(const RagdollComponent& o)
            : startActive(o.startActive), density(o.density), friction(o.friction),
              limbThickness(o.limbThickness), torsoThickness(o.torsoThickness),
              jointFriction(o.jointFriction), jointLimitScale(o.jointLimitScale), layer(o.layer),
              followHips(o.followHips) {}
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