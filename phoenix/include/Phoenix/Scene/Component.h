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
#include <Phoenix/Scene/TerrainGenerator.h>
#include <Phoenix/renderer/PoolWater.h>
#include <Phoenix/Physics/ParticleFluid.h>
#include <unordered_set>
#include <Phoenix/renderer/WaterRipples.h>
#include <Phoenix/Scene/ScriptableEntity.h>
#include <string>
#include <vector>
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
        // How fast the camera swings round behind a turning target (1/s); 0 = snaps.
        float yawSmoothing = 8.0f;
        // Transient: the yaw the camera currently trails at.
        float smoothedYaw = 0.0f;
        bool  smoothedYawValid = false;
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
        // Shadow participation: castShadows puts the object into the lights' shadow maps;
        // receiveShadows lets shadows fall on it. (Geometry on a light entity, its marker
        // or bulb, never casts: it would block its own light.)
        bool castShadows = true;
        bool receiveShadows = true;
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
        enum class Type { Cube = 0, Sphere = 1, Cylinder = 2, Cone = 3, Plane = 4, Capsule = 5 };
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
        bool  lockTilt = false;        // only rotate about Y (agents and characters stay upright)
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
    // Keeps this entity on a bone of another entity's skinned model (a gun in a
    // hand, a hat on a head). Every frame its Translation/Rotation are set from the
    // animated bone; its own Scale is kept. Offset and rotation are relative to the
    // bone, in world units / radians (the target's scale doesn't apply).
    struct BoneAttachmentComponent{
        std::string target;            // Tag of the entity with the skinned model
        std::string bone = "RightHand"; // bone name; "RightHand" also finds "mixamorig:RightHand"
        glm::vec3 offset{ 0.0f };
        glm::vec3 rotation{ 0.0f };
        BoneAttachmentComponent() = default;
        BoneAttachmentComponent(const BoneAttachmentComponent&) = default;
    };

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

        // Noise generation (Generate in the inspector), and domain randomization: with
        // randomizeEachEpisode, every RL episode regenerates the heights from these
        // settings with a fresh seed (Stop restores the edit-time heights).
        TerrainNoise noise;
        bool randomizeEachEpisode = false;

        Ref<Mesh> mesh;             // generated; not serialized
        bool dirty = true;
        uint32_t runtimeBodyID = 0xffffffff;

        TerrainComponent(){ heights.resize((size_t)resolution * resolution, 0.0f); }
        TerrainComponent(const TerrainComponent& o)
            : resolution(o.resolution), size(o.size), heights(o.heights),
              material(o.material), generateCollider(o.generateCollider),
              noise(o.noise), randomizeEachEpisode(o.randomizeEachEpisode), dirty(true) {}
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

    // A rectangular swimming pool: a tiled basin (with its own static collider while
    // playing) holding simulated water. The entity's origin is the centre of the rim;
    // the pool turns with the entity's yaw (rotation about X/Z and scale are ignored).
    // While playing, bodies in it push the water aside and make waves, float or sink by
    // density, and are slowed by drag. Drawn with refraction, caustics, depth-dependent
    // colour, reflections and an underwater view. The water mesh, simulation and
    // collider are transient (not serialized / reset on copy).
    struct PoolComponent{
        glm::vec2 size = { 8.0f, 5.0f };    // inner width (x) and length (z), metres
        float depth = 2.0f;                 // rim down to the floor
        float freeboard = 0.2f;             // rim down to the still water surface
        float coping = 0.35f;               // tiled border around the rim (also the wall thickness)

        // Waves.
        float cellSize = 0.05f;             // simulation cell: smaller = finer waves, costlier
        float waveSpeed = 1.2f;             // m/s (pool ripples of a few dm travel ~0.5-1.5 m/s)
        float waveDamping = 0.6f;           // wave height lost per second (fraction)
        // Share of the water a body pushes aside that shows up as waves. The surface model
        // can't form a crater and crown, so the full volume would make metre-high swells;
        // ~0.1 gives splashes and wakes of realistic height.
        float waveStrength = 0.08f;
        bool interactive = true;            // bodies make waves

        // Look.
        glm::vec3 tileColor = glm::vec3(0.86f, 0.93f, 0.97f);
        glm::vec3 groutColor = glm::vec3(0.55f, 0.66f, 0.72f);
        float tileSize = 0.25f;
        glm::vec3 waterColor = glm::vec3(0.12f, 0.55f, 0.68f);  // light scattered back by deep water
        float clarity = 25.0f;              // metres one can see under water (clean pool: 20-30)
        float causticStrength = 0.8f;

        // Physics coupling while playing (as WaterComponent's).
        float buoyancy = 1.0f;
        float linearDrag = 0.8f;
        float angularDrag = 0.4f;
        bool generateCollider = true;

        // Spray: bodies hitting the water fast throw up droplets (particle fluid) that rain
        // back as ripples.
        bool spray = true;
        float sprayAmount = 1.0f;

        Ref<PoolWater> water;               // simulation + textures; not serialized
        Ref<ParticleFluid> sprayFluid;      // live droplets; not serialized
        Ref<Mesh> shellMesh, surfaceMesh;   // generated; not serialized
        bool dirty = true;                  // meshes need rebuilding
        uint32_t runtimeBodyID = 0xffffffff;
        std::vector<float> columns;         // scratch: body displacement per grid vertex
        std::unordered_set<uint32_t> wetBodies; // bodies in the water last step (for spray on entry)
        uint32_t splashes = 0;              // splash counter (seeds the spray pattern)

        PoolComponent() = default;
        PoolComponent(const PoolComponent& o)
            : size(o.size), depth(o.depth), freeboard(o.freeboard), coping(o.coping),
              cellSize(o.cellSize), waveSpeed(o.waveSpeed), waveDamping(o.waveDamping), waveStrength(o.waveStrength),
              interactive(o.interactive),
              tileColor(o.tileColor), groutColor(o.groutColor), tileSize(o.tileSize),
              waterColor(o.waterColor), clarity(o.clarity), causticStrength(o.causticStrength),
              buoyancy(o.buoyancy), linearDrag(o.linearDrag), angularDrag(o.angularDrag),
              generateCollider(o.generateCollider), spray(o.spray), sprayAmount(o.sprayAmount) {}

        // Still water surface and floor heights in the pool's local frame.
        float WaterLevel() const { return -freeboard; }
        float Floor() const { return -depth; }
    };

    // A particle liquid (Position Based Fluids, see ParticleFluid): a block of it at the
    // entity when Play starts (and every RL episode), and/or a stream poured from the
    // entity along its local -Y. It flows around and pushes physics bodies (their box /
    // sphere / capsule / cylinder colliders), and particles that fall into a pool join
    // its water as ripples. Drawn as a smooth liquid surface (screen-space fluid).
    struct FluidComponent{
        bool fillBlock = true;
        glm::vec3 blockSize = glm::vec3(0.8f, 1.0f, 0.8f);   // metres, centred on the entity
        // Pouring: a disc of particles (nozzleRadius, at the rest spacing) leaves the
        // entity along its local -Y each time the last one moved a spacing away.
        bool pour = false;
        float pourSpeed = 2.0f;             // m/s
        float nozzleRadius = 0.08f;

        float spacing = 0.05f;              // particle size: smaller = finer, many more particles
        int maxParticles = 12000;
        int iterations = 4;
        float viscosity = 0.02f;
        float cohesion = 0.1f;
        float friction = 0.1f;
        float surfaceTension = 1.0f;        // keeps streams and drops together
        float lifetime = 0.0f;              // seconds (0 = forever)

        glm::vec3 color = glm::vec3(0.35f, 0.72f, 0.88f);   // tint of thick liquid
        float clarity = 1.2f;               // metres of liquid before most light is absorbed

        Ref<ParticleFluid> fluid;           // live particles; not serialized
        float pourAccumulator = 0.0f;       // seconds since the last disc

        FluidComponent() = default;
        FluidComponent(const FluidComponent& o)
            : fillBlock(o.fillBlock), blockSize(o.blockSize), pour(o.pour), pourSpeed(o.pourSpeed),
              nozzleRadius(o.nozzleRadius), spacing(o.spacing), maxParticles(o.maxParticles), iterations(o.iterations),
              viscosity(o.viscosity), cohesion(o.cohesion), friction(o.friction), surfaceTension(o.surfaceTension),
              lifetime(o.lifetime),
              color(o.color), clarity(o.clarity) {}

        FluidSettings Settings() const{
            FluidSettings s;
            s.spacing = spacing;
            s.iterations = iterations;
            s.viscosity = viscosity;
            s.cohesion = cohesion;
            s.friction = friction;
            s.surfaceTension = surfaceTension;
            s.lifetime = lifetime;
            return s;
        }
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
        bool  inPlace = true;    // strip root travel from clips (scripts move the entity)

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
              crossfade(o.crossfade), inPlace(o.inPlace), extraClips(o.extraClips), events(o.events) {}
    };

    // A Lua script (edited inline, serialized). It runs while the scene is playing;
    // the live runtime is owned by the Scene, not stored here.
    // One Lua script on an entity. Each script runs in its own Lua state, so several
    // scripts on the same entity can all define OnUpdate (etc.) without colliding.
    struct LuaScriptEntry{
        std::string name = "Script";
        std::string source =
            "-- runs while playing (press Play)\n"
            "local t = 0.0\n"
            "function OnUpdate(dt)\n"
            "    t = t + dt\n"
            "    local x, y, z = GetTranslation()\n"
            "    SetTranslation(x, y, z)\n"
            "end\n";
        bool enabled = true; // disabled scripts are kept but not run
    };

    struct LuaScriptComponent{
        std::vector<LuaScriptEntry> scripts;

        LuaScriptComponent() : scripts(1) {}
        LuaScriptComponent(const std::string& name, const std::string& source) { Add(name, source); }
        LuaScriptComponent(const LuaScriptComponent&) = default;

        LuaScriptEntry& Add(const std::string& name, const std::string& source){
            scripts.push_back({ name, source, true });
            return scripts.back();
        }
    };


    // ---- AI / reinforcement learning ----

    // A learning agent. While playing, the Environment asks its policy for an action
    // every `decisionPeriod` physics steps, applies it through the chosen control
    // scheme, and scores the result. Observations are assembled from the toggles
    // below, any Ray Sensor on the same entity, and the script's CollectObservations().
    // Runtime state (rewards, episode counters) lives in the Environment, not here.
    struct AgentComponent{
        // How actions move the agent.
        //  Move:   walk forward/back + turn (velocity control; upright rigid body or transform)
        //  Force:  push in world X/Z with a force (rolling balls, sliding pucks)
        //  Custom: actions are handed to the Lua script's OnAction(actions)
        enum class Control { Move = 0, Force = 1, Custom = 2 };
        enum class ActionType { Discrete = 0, Continuous = 1 };
        // Who chooses actions. Learning happens outside the engine: the editor only
        // exposes the environment API (see Environment / Bridge).
        //  Heuristic: keyboard (WASD / arrows, or the script's Heuristic())
        //  Random:    uniform random actions (baseline / smoke test)
        //  External:  a client connected over the bridge (Python Gymnasium API)
        enum class Policy { Heuristic = 0, Random = 1, External = 2 };

        std::string behavior = "Agent"; // name reported to clients for this agent
        Policy policy = Policy::External;
        Control control = Control::Move;
        ActionType actionType = ActionType::Discrete;
        int customActionSize = 2;      // Custom control: choices (discrete) or dimensions
        float moveSpeed = 4.0f;        // m/s (Move; Force without a body)
        float turnSpeed = 3.0f;        // rad/s (Move)
        float force = 25.0f;           // N (Force)

        int decisionPeriod = 5;        // physics steps an action is held for
        int maxSteps = 400;            // decisions per episode (0 = no limit)

        // Observations (all scaled to roughly [-1, 1]).
        bool observePosition = false;  // own position / observationScale
        bool observeVelocity = true;   // own velocity / moveSpeed
        bool observeHeading = true;    // sin/cos of yaw
        bool observeTarget = true;     // target offset in the agent's frame + distance
        std::string targetTag = "Goal";
        float observationScale = 10.0f; // metres that map to 1.0
        int customObservationSize = 0; // values the script adds in CollectObservations()

        // Rewards and termination.
        float stepReward = -0.002f;    // every decision (negative = hurry up)
        float approachReward = 0.1f;   // per metre moved toward the target (shaping)
        float fallHeight = -3.0f;      // below this the episode ends...
        float fallReward = -1.0f;      // ...with this reward

        int ActionSize() const{
            if (control == Control::Custom) { return customActionSize < 1 ? 1 : customActionSize; }
            return actionType == ActionType::Discrete ? 5 : 2;
        }
        AgentComponent() = default;
        AgentComponent(const AgentComponent&) = default;
    };

    // Fan of rays in the agent's horizontal plane, centred on its facing (-Z). Each
    // ray contributes [hit distance / length (1 = nothing), one-hot of the hit tag].
    // Rays hit physics bodies (except the agent's own) and Reward Zones.
    struct RaySensorComponent{
        int rays = 7;
        float fov = 140.0f;          // degrees, total spread
        float length = 12.0f;
        float heightOffset = 0.0f;   // ray origin above the entity origin
        std::vector<std::string> detectableTags = { "Goal", "Wall", "Hazard" };
        int ValuesPerRay() const { return 1 + (int)detectableTags.size(); }
        RaySensorComponent() = default;
        RaySensorComponent(const RaySensorComponent&) = default;
    };

    // A box volume (halfExtents in local space, scaled by the transform) that rewards
    // or penalises agents entering / staying inside it, optionally ending the episode.
    // Purely logical: it needs no collider and is drawn as an overlay in the editor.
    struct RewardZoneComponent{
        enum class Trigger { OnEnter = 0, WhileInside = 1 };
        glm::vec3 halfExtents = { 1.0f, 1.0f, 1.0f };
        float reward = 1.0f;         // per entry, or per second while inside
        Trigger trigger = Trigger::OnEnter;
        bool endEpisode = true;
        std::string agentTag;        // only this agent (empty = any agent)
        bool visibleToRays = true;   // Ray Sensors can see it
        RewardZoneComponent() = default;
        RewardZoneComponent(const RewardZoneComponent&) = default;
    };

    // Domain randomization: on every episode reset the entity is moved to its
    // edit-time pose plus a random offset in [-positionRange, positionRange] and,
    // optionally, a random heading.
    struct RandomizerComponent{
        glm::vec3 positionRange = { 3.0f, 0.0f, 3.0f };
        bool randomYaw = true;
        RandomizerComponent() = default;
        RandomizerComponent(const RandomizerComponent&) = default;
    };

    // How a light's shadow looks. Softness is the size of the light source: a bigger
    // source casts a wider penumbra that still hardens where the caster touches the
    // receiver (percentage-closer soft shadows).
    struct ShadowSettings{
        float softness = 1.0f;   // directional: angular radius in degrees; point: radius in world units; 0 = hard
        float strength = 1.0f;   // 0 = no shadow, 1 = shadowed areas get only ambient light
        float bias = 1.0f;       // depth bias in shadow-map texels: raise against acne, lower against gaps at contacts
        float normalBias = 1.5f; // receivers are offset along their normal by this many texels
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
        ShadowSettings shadow;
        // Directional only: shadows cover the view out to this distance from the camera
        // (or the whole scene, if that is smaller). Shorter = sharper shadow texels.
        float shadowDistance = 40.0f;
        DirLightComponent() = default;
        DirLightComponent(const DirLightComponent&) = default;
    };

    // Point lights cast omnidirectional shadows (six shadow-map faces each).
    struct PointLightComponent: public DirLightComponent{
        float constant = 1.0f;
        float linear = 0.02f;
        float quadratic = 0.099f; 
        void SetConstant(float constant) { this->constant = constant;} 
        void SetLinear(float linear) { this->linear = linear;} 
        void SetQuadratic(float quadratic) { this->quadratic = quadratic;} 
        PointLightComponent() { shadow.softness = 0.1f; } // a 10 cm bulb
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