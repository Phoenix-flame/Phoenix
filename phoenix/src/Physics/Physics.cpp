#include <Phoenix/Physics/Physics.h>
#include <Phoenix/core/log.h>

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/GroupFilterTable.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLockMulti.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/Constraints/ConeConstraint.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>

#include <thread>
#include <mutex>
#include <algorithm>
#include <unordered_set>
#include <unordered_map>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace Phoenix{

    // ---- Jolt collision layers ----
    // Object layer = userLayer * 2 + moving bit. The broad phase only splits moving
    // from non-moving bodies (static vs static is never tested); the user layer
    // matrix is applied in the object-layer pair filter.

    namespace BroadPhaseLayers{
        static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
        static constexpr JPH::BroadPhaseLayer MOVING(1);
        static constexpr JPH::uint NUM_LAYERS(2);
    }

    static constexpr JPH::ObjectLayer NUM_OBJECT_LAYERS = PhysicsWorld::MaxLayers * 2;

    static JPH::ObjectLayer MakeObjectLayer(int userLayer, bool moving){
        userLayer = std::clamp(userLayer, 0, PhysicsWorld::MaxLayers - 1);
        return (JPH::ObjectLayer)(userLayer * 2 + (moving ? 1 : 0));
    }
    static int UserLayer(JPH::ObjectLayer layer) { return (int)(layer >> 1); }
    static bool IsMovingLayer(JPH::ObjectLayer layer) { return (layer & 1) != 0; }

    class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface{
    public:
        virtual JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NUM_LAYERS; }
        virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override {
            return IsMovingLayer(inLayer) ? BroadPhaseLayers::MOVING : BroadPhaseLayers::NON_MOVING;
        }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        virtual const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override {
            return inLayer == BroadPhaseLayers::MOVING ? "Moving" : "NonMoving";
        }
#endif
    };

    class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter{
    public:
        virtual bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override{
            return IsMovingLayer(inLayer1) || inLayer2 == BroadPhaseLayers::MOVING;
        }
    };

    class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter{
    public:
        explicit ObjectLayerPairFilterImpl(const uint16_t* masks) : m_Masks(masks) {}
        virtual bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override{
            if (!IsMovingLayer(inObject1) && !IsMovingLayer(inObject2)) { return false; }
            int a = UserLayer(inObject1), b = UserLayer(inObject2);
            return ((m_Masks[a] >> b) & 1) && ((m_Masks[b] >> a) & 1);
        }
    private:
        const uint16_t* m_Masks;
    };

    // Query filters: user-layer mask, plus skipping one body and (optionally) sensors.
    class LayerMaskFilter : public JPH::ObjectLayerFilter{
    public:
        explicit LayerMaskFilter(uint16_t mask) : m_Mask(mask) {}
        virtual bool ShouldCollide(JPH::ObjectLayer inLayer) const override { return (m_Mask >> UserLayer(inLayer)) & 1; }
    private:
        uint16_t m_Mask;
    };

    class QueryBodyFilter : public JPH::BodyFilter{
    public:
        explicit QueryBodyFilter(const PhysicsWorld::QueryFilter& f)
            : m_Ignore(f.ignoreBody), m_Sensors(f.includeSensors) {}
        virtual bool ShouldCollide(const JPH::BodyID& inBodyID) const override {
            return inBodyID.GetIndexAndSequenceNumber() != m_Ignore;
        }
        virtual bool ShouldCollideLocked(const JPH::Body& inBody) const override {
            return m_Sensors || !inBody.IsSensor();
        }
    private:
        uint32_t m_Ignore;
        bool m_Sensors;
    };

    // Records contact begin/end pairs. Jolt calls these from its worker threads
    // during Step, so the event list is mutex-guarded; the Scene drains it after
    // each step on the main thread. Character bodies are skipped here: their
    // contacts come from the character's own listener (CharacterListenerImpl).
    class ContactListenerImpl : public JPH::ContactListener{
    public:
        virtual void OnContactAdded(const JPH::Body& body1, const JPH::Body& body2,
                                    const JPH::ContactManifold& manifold, JPH::ContactSettings&) override{
            uint32_t a = body1.GetID().GetIndexAndSequenceNumber();
            uint32_t b = body2.GetID().GetIndexAndSequenceNumber();
            if (IsCharacterBody(a) || IsCharacterBody(b)) { return; }

            PhysicsWorld::ContactEvent ev{ a, b, true, body1.IsSensor() || body2.IsSensor() };
            JPH::RVec3 p = manifold.GetWorldSpaceContactPointOn1(0);
            JPH::Vec3 n = manifold.mWorldSpaceNormal;
            ev.point = { (float)p.GetX(), (float)p.GetY(), (float)p.GetZ() };
            ev.normal = { n.GetX(), n.GetY(), n.GetZ() };
            JPH::Vec3 relative = body1.GetPointVelocity(p) - body2.GetPointVelocity(p);
            ev.speed = std::max(0.0f, relative.Dot(n));

            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Events.push_back(ev);
        }
        virtual void OnContactRemoved(const JPH::SubShapeIDPair& pair) override{
            uint32_t a = pair.GetBody1ID().GetIndexAndSequenceNumber();
            uint32_t b = pair.GetBody2ID().GetIndexAndSequenceNumber();
            if (IsCharacterBody(a) || IsCharacterBody(b)) { return; }
            std::lock_guard<std::mutex> lock(m_Mutex);
            // Sensor flag is unknown here (bodies can't be read); the Scene tracks it.
            m_Events.push_back({ a, b, false, false });
        }
        void Push(const PhysicsWorld::ContactEvent& ev){
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Events.push_back(ev);
        }
        std::vector<PhysicsWorld::ContactEvent> Consume(){
            std::lock_guard<std::mutex> lock(m_Mutex);
            std::vector<PhysicsWorld::ContactEvent> out;
            out.swap(m_Events);
            return out;
        }
        // Only modified between steps (character create/remove), read during them.
        std::unordered_set<uint32_t> characterBodies;
    private:
        bool IsCharacterBody(uint32_t id) const { return characterBodies.count(id) > 0; }
        std::mutex m_Mutex;
        std::vector<PhysicsWorld::ContactEvent> m_Events;
    };

    // Character contacts (bodies and sensors it walks into) -> the same event queue.
    class CharacterListenerImpl : public JPH::CharacterContactListener{
    public:
        explicit CharacterListenerImpl(ContactListenerImpl& sink) : m_Sink(sink) {}
        virtual void OnContactAdded(const JPH::CharacterVirtual* inCharacter, const JPH::BodyID& inBodyID2,
                                    const JPH::SubShapeID&, JPH::RVec3Arg inContactPosition, JPH::Vec3Arg inContactNormal,
                                    JPH::CharacterContactSettings&) override{
            if (inCharacter->GetInnerBodyID().IsInvalid()) { return; }
            PhysicsWorld::ContactEvent ev{ inCharacter->GetInnerBodyID().GetIndexAndSequenceNumber(),
                                           inBodyID2.GetIndexAndSequenceNumber(), true, false };
            ev.point = { (float)inContactPosition.GetX(), (float)inContactPosition.GetY(), (float)inContactPosition.GetZ() };
            ev.normal = { -inContactNormal.GetX(), -inContactNormal.GetY(), -inContactNormal.GetZ() };
            ev.speed = std::max(0.0f, inCharacter->GetLinearVelocity().Dot(-inContactNormal));
            m_Sink.Push(ev);
        }
        virtual void OnContactRemoved(const JPH::CharacterVirtual* inCharacter, const JPH::BodyID& inBodyID2,
                                      const JPH::SubShapeID&) override{
            if (inCharacter->GetInnerBodyID().IsInvalid()) { return; }
            m_Sink.Push({ inCharacter->GetInnerBodyID().GetIndexAndSequenceNumber(),
                          inBodyID2.GetIndexAndSequenceNumber(), false, false });
        }
    private:
        ContactListenerImpl& m_Sink;
    };

    static void TraceImpl(const char* inFMT, ...){
        va_list list;
        va_start(list, inFMT);
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), inFMT, list);
        va_end(list);
        PHX_CORE_TRACE("[Jolt] {0}", buffer);
    }

#ifdef JPH_ENABLE_ASSERTS
    static bool AssertFailedImpl(const char* inExpression, const char* inMessage, const char* inFile, JPH::uint inLine){
        PHX_CORE_ERROR("[Jolt] {0}:{1}: ({2}) {3}", inFile, inLine, inExpression, inMessage ? inMessage : "");
        return true;
    }
#endif

    // ---- Global Jolt setup ----

    static bool s_Initialized = false;

    void PhysicsWorld::Init(){
        if (s_Initialized) { return; }
        JPH::RegisterDefaultAllocator();
        JPH::Trace = TraceImpl;
        JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = AssertFailedImpl;)
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
        s_Initialized = true;
    }

    void PhysicsWorld::Shutdown(){
        if (!s_Initialized) { return; }
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
        s_Initialized = false;
    }

    // ---- PhysicsWorld implementation ----

    struct JointRecord{
        JPH::Ref<JPH::TwoBodyConstraint> constraint;
        PhysicsWorld::JointType type;
        uint32_t bodyA, bodyB;
        float breakForce;
    };

    struct CharacterRecord{
        JPH::Ref<JPH::CharacterVirtual> character;
        PhysicsWorld::CharacterDesc desc;
        JPH::Vec3 input = JPH::Vec3::sZero();
        bool jump = false;
        float jumpSpeed = 0.0f;
    };

    // Collision groups let jointed bodies ignore each other (collideConnected = false).
    // Sub-group ids are handed out on demand from one shared table.
    static constexpr JPH::uint MaxJointGroups = 4096;

    struct PhysicsWorld::Impl{
        PhysicsWorld::Settings settings;
        BPLayerInterfaceImpl broadPhaseLayerInterface;
        ObjectVsBroadPhaseLayerFilterImpl objectVsBroadPhaseLayerFilter;
        ObjectLayerPairFilterImpl objectLayerPairFilter{ settings.layerMasks };
        ContactListenerImpl contactListener;
        CharacterListenerImpl characterListener{ contactListener };
        JPH::TempAllocatorImpl tempAllocator{ 10 * 1024 * 1024 };
        JPH::JobSystemThreadPool jobSystem;
        JPH::PhysicsSystem physicsSystem;
        JPH::EPhysicsUpdateError lastUpdateError = JPH::EPhysicsUpdateError::None;
        float lastSubstep = 1.0f / 60.0f;

        // Declared after physicsSystem so they are destroyed first (characters remove
        // their inner bodies from the system in their destructor).
        std::vector<JointRecord> joints;          // index = joint id; null constraint = removed
        std::vector<uint32_t> brokenJoints;
        std::vector<CharacterRecord> characters;  // index = character id; null = removed
        JPH::CharacterVsCharacterCollisionSimple characterVsCharacter;
        JPH::Ref<JPH::GroupFilterTable> groupTable;
        std::unordered_map<uint32_t, JPH::CollisionGroup::SubGroupID> bodySubGroups;

        explicit Impl(const PhysicsWorld::Settings& s) : settings(s){
            int numThreads = (int)std::thread::hardware_concurrency() - 1;
            if (numThreads < 1) { numThreads = 1; }
            jobSystem.Init(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, numThreads);

            // Max bodies, body mutexes (0 = default), body pairs, contact constraints.
            // Jolt drops contacts once these fill up (objects sink through the floor),
            // so leave headroom for piles of bodies resting on mesh terrain.
            physicsSystem.Init(10240, 0, 65536, 10240,
                broadPhaseLayerInterface, objectVsBroadPhaseLayerFilter, objectLayerPairFilter);
            physicsSystem.SetGravity(JPH::Vec3(settings.gravity.x, settings.gravity.y, settings.gravity.z));
            physicsSystem.SetContactListener(&contactListener);

            JPH::PhysicsSettings physicsSettings = physicsSystem.GetPhysicsSettings();
            physicsSettings.mNumVelocitySteps = (JPH::uint)std::clamp(settings.velocitySteps, 2, 64);
            physicsSettings.mNumPositionSteps = (JPH::uint)std::clamp(settings.positionSteps, 1, 32);
            physicsSystem.SetPhysicsSettings(physicsSettings);
        }

        void UpdateCharacter(CharacterRecord& rec, float dt);

        JPH::BodyInterface& Bodies() { return physicsSystem.GetBodyInterface(); }
        const JPH::BodyInterface& Bodies() const { return physicsSystem.GetBodyInterface(); }

        JointRecord* GetJoint(uint32_t id){
            if (id >= joints.size() || !joints[id].constraint) { return nullptr; }
            return &joints[id];
        }
        CharacterRecord* GetCharacter(uint32_t id){
            if (id >= characters.size() || !characters[id].character) { return nullptr; }
            return &characters[id];
        }
        const CharacterRecord* GetCharacter(uint32_t id) const{
            if (id >= characters.size() || !characters[id].character) { return nullptr; }
            return &characters[id];
        }

        // Stop two jointed bodies colliding with each other.
        void DisableCollision(uint32_t a, uint32_t b){
            if (a == InvalidBody || b == InvalidBody) { return; }
            if (!groupTable) { groupTable = new JPH::GroupFilterTable(MaxJointGroups); }
            auto subGroupOf = [&](uint32_t body, JPH::CollisionGroup::SubGroupID& out){
                auto it = bodySubGroups.find(body);
                if (it != bodySubGroups.end()) { out = it->second; return true; }
                if (bodySubGroups.size() >= MaxJointGroups) { return false; }
                out = (JPH::CollisionGroup::SubGroupID)bodySubGroups.size();
                bodySubGroups[body] = out;
                Bodies().SetCollisionGroup(JPH::BodyID(body), JPH::CollisionGroup(groupTable, 0, out));
                return true;
            };
            JPH::CollisionGroup::SubGroupID sa, sb;
            if (!subGroupOf(a, sa) || !subGroupOf(b, sb)){
                PHX_CORE_WARN("Physics: too many jointed bodies to disable joint collisions");
                return;
            }
            groupTable->DisableCollision(sa, sb);
        }
    };

    PhysicsWorld::PhysicsWorld(const Settings& settings){
        Init(); // idempotent lazy global init
        m_Impl = CreateScope<Impl>(settings);
    }

    PhysicsWorld::~PhysicsWorld() = default;

    const PhysicsWorld::Settings& PhysicsWorld::GetSettings() const { return m_Impl->settings; }

    // Rotations use the same XYZ Euler convention as TransformComponent::GetTransform
    // (Rx * Ry * Rz). glm::quat(euler) / glm::eulerAngles use ZYX instead, which made
    // colliders disagree with the rendered mesh for any multi-axis rotation.
    static glm::quat EulerXYZToQuat(const glm::vec3& e){
        return glm::angleAxis(e.x, glm::vec3(1, 0, 0))
             * glm::angleAxis(e.y, glm::vec3(0, 1, 0))
             * glm::angleAxis(e.z, glm::vec3(0, 0, 1));
    }

    static JPH::Quat ToJolt(const glm::quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
    static JPH::Vec3 ToJolt(const glm::vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
    static JPH::RVec3 ToJoltR(const glm::vec3& v) { return JPH::RVec3(v.x, v.y, v.z); }
    static glm::vec3 ToGlm(JPH::Vec3Arg v) { return { v.GetX(), v.GetY(), v.GetZ() }; }
    static glm::vec3 ToGlmR(JPH::RVec3Arg v) { return { (float)v.GetX(), (float)v.GetY(), (float)v.GetZ() }; }

    // Shift each angle by a multiple of 2*pi so it lies closest to the reference.
    static glm::vec3 UnwrapNear(glm::vec3 angles, const glm::vec3& reference){
        const float twoPi = glm::two_pi<float>();
        for (int i = 0; i < 3; i++)
            angles[i] += twoPi * std::round((reference[i] - angles[i]) / twoPi);
        return angles;
    }

    // XYZ Euler angles for q, picking whichever of the two equivalent solutions
    // ((x, y, z) and (x + pi, pi - y, z + pi)) is closest to the reference, so the
    // angles stay continuous frame to frame instead of flipping once y passes 90 deg.
    static glm::vec3 QuatToEulerXYZ(const glm::quat& q, const glm::vec3& reference){
        glm::vec3 a;
        glm::extractEulerAngleXYZ(glm::mat4_cast(q), a.x, a.y, a.z);
        const float pi = glm::pi<float>();
        glm::vec3 first  = UnwrapNear(a, reference);
        glm::vec3 second = UnwrapNear(glm::vec3(a.x + pi, pi - a.y, a.z + pi), reference);
        glm::vec3 d1 = first - reference, d2 = second - reference;
        return glm::dot(d1, d1) <= glm::dot(d2, d2) ? first : second;
    }

    // ---- Shapes ----

    static JPH::ShapeRefC CreateShape(JPH::ShapeSettings& settings, const char* what){
        JPH::ShapeSettings::ShapeResult result = settings.Create();
        if (result.HasError()){
            PHX_CORE_ERROR("[Jolt] {0} shape error: {1}", what, result.GetError().c_str());
            return nullptr;
        }
        return result.Get();
    }

    // One sub-shape (without its offset). Triangle meshes on non-static bodies fall
    // back to a convex hull of the same points, since Jolt can't simulate them.
    static JPH::ShapeRefC BuildSubShape(const PhysicsWorld::ShapeDesc& desc, bool isStatic, float density){
        using namespace JPH;
        using Type = PhysicsWorld::ShapeDesc::Type;
        auto convex = [density](ConvexShapeSettings& s, const char* what){
            s.SetEmbedded();
            if (density > 0.0f) { s.SetDensity(density); }
            return CreateShape(s, what);
        };
        switch (desc.type){
            case Type::Box:{
                // Jolt requires halfExtent >= convex radius (default 0.05) on every axis.
                BoxShapeSettings s(Vec3(std::max(desc.halfExtents.x, 0.06f), std::max(desc.halfExtents.y, 0.06f),
                                        std::max(desc.halfExtents.z, 0.06f)));
                return convex(s, "box");
            }
            case Type::Sphere:{
                SphereShapeSettings s(std::max(desc.radius, 0.01f));
                return convex(s, "sphere");
            }
            case Type::Capsule:{
                CapsuleShapeSettings s(std::max(desc.halfHeight, 0.01f), std::max(desc.radius, 0.01f));
                return convex(s, "capsule");
            }
            case Type::Cylinder:{
                CylinderShapeSettings s(std::max(desc.halfHeight, 0.06f), std::max(desc.radius, 0.06f));
                return convex(s, "cylinder");
            }
            case Type::TriangleMesh:
                if (isStatic && desc.indices.size() >= 3){
                    VertexList vertices;
                    vertices.reserve(desc.points.size());
                    for (const auto& p : desc.points) { vertices.push_back(Float3(p.x, p.y, p.z)); }
                    IndexedTriangleList triangles;
                    triangles.reserve(desc.indices.size() / 3);
                    for (size_t i = 0; i + 2 < desc.indices.size(); i += 3)
                        triangles.push_back(IndexedTriangle(desc.indices[i], desc.indices[i + 1], desc.indices[i + 2], 0));
                    MeshShapeSettings s(vertices, triangles);
                    s.SetEmbedded();
                    return CreateShape(s, "mesh");
                }
                [[fallthrough]];
            case Type::ConvexHull:{
                if (desc.points.empty()) { return nullptr; }
                Array<Vec3> hull;
                hull.reserve(desc.points.size());
                for (const auto& p : desc.points) { hull.push_back(Vec3(p.x, p.y, p.z)); }
                ConvexHullShapeSettings s(hull);
                return convex(s, "convex hull");
            }
        }
        return nullptr;
    }

    static bool IsIdentityOffset(const PhysicsWorld::ShapeDesc& d){
        return glm::dot(d.offset, d.offset) < 1e-12f && glm::dot(d.rotation, d.rotation) < 1e-12f;
    }

    // All sub-shapes combined: as-is for one centered shape, rotated/translated for one
    // offset shape, a static compound for several; then the optional COM shift.
    static JPH::ShapeRefC BuildBodyShape(const std::vector<PhysicsWorld::ShapeDesc>& shapes,
                                         bool isStatic, const PhysicsWorld::BodyProps& props){
        using namespace JPH;
        std::vector<ShapeRefC> built;
        std::vector<const PhysicsWorld::ShapeDesc*> descs;
        for (const auto& desc : shapes){
            ShapeRefC s = BuildSubShape(desc, isStatic, props.density);
            if (s) { built.push_back(s); descs.push_back(&desc); }
        }
        if (built.empty()) { return nullptr; }

        ShapeRefC shape;
        if (built.size() == 1 && IsIdentityOffset(*descs[0])){
            shape = built[0];
        }
        else if (built.size() == 1){
            RotatedTranslatedShapeSettings s(ToJolt(descs[0]->offset), ToJolt(EulerXYZToQuat(descs[0]->rotation)), built[0]);
            s.SetEmbedded();
            shape = CreateShape(s, "offset");
        }
        else{
            StaticCompoundShapeSettings s;
            s.SetEmbedded();
            for (size_t i = 0; i < built.size(); i++)
                s.AddShape(ToJolt(descs[i]->offset), ToJolt(EulerXYZToQuat(descs[i]->rotation)), built[i]);
            shape = CreateShape(s, "compound");
        }
        if (!shape) { return nullptr; }

        if (!isStatic && glm::dot(props.centerOfMassOffset, props.centerOfMassOffset) > 1e-12f){
            OffsetCenterOfMassShapeSettings s(ToJolt(props.centerOfMassOffset), shape);
            s.SetEmbedded();
            if (ShapeRefC offset = CreateShape(s, "center of mass")) { shape = offset; }
        }
        return shape;
    }

    uint32_t PhysicsWorld::CreateBody(const std::vector<ShapeDesc>& shapes, const glm::vec3& position,
                                      const glm::vec3& rotationEuler, BodyType type, const BodyProps& props){
        using namespace JPH;
        const bool isStatic = type == BodyType::Static;
        ShapeRefC shape = BuildBodyShape(shapes, isStatic, props);
        if (!shape) { return InvalidBody; }

        EMotionType motion = isStatic                       ? EMotionType::Static
                           : (type == BodyType::Kinematic)  ? EMotionType::Kinematic
                                                            : EMotionType::Dynamic;
        BodyCreationSettings settings(shape, ToJoltR(position), ToJolt(EulerXYZToQuat(rotationEuler)),
            motion, MakeObjectLayer(props.layer, !isStatic));

        settings.mFriction = props.friction;
        settings.mRestitution = props.restitution;
        settings.mLinearDamping = props.linearDamping;
        settings.mAngularDamping = props.angularDamping;
        settings.mGravityFactor = props.gravityFactor;
        settings.mIsSensor = props.isSensor;
        settings.mAllowSleeping = props.allowSleeping;
        // Kinematic sensors should also see static/kinematic bodies they sweep over.
        if (props.isSensor && type == BodyType::Kinematic) { settings.mCollideKinematicVsNonDynamic = true; }
        if (props.continuousCollision) { settings.mMotionQuality = EMotionQuality::LinearCast; }
        settings.mLinearVelocity = ToJolt(props.initialVelocity);
        settings.mAngularVelocity = ToJolt(props.initialAngularVelocity);

        if (type == BodyType::Dynamic){
            if (props.mass > 0.0f){
                settings.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
                settings.mMassPropertiesOverride.mMass = props.mass;
            }
            uint8 dofs = (uint8)EAllowedDOFs::All;
            const EAllowedDOFs translation[3] = { EAllowedDOFs::TranslationX, EAllowedDOFs::TranslationY, EAllowedDOFs::TranslationZ };
            const EAllowedDOFs rotation[3] = { EAllowedDOFs::RotationX, EAllowedDOFs::RotationY, EAllowedDOFs::RotationZ };
            for (int i = 0; i < 3; i++){
                if (props.lockPosition[i]) { dofs &= ~(uint8)translation[i]; }
                if (props.lockRotation[i]) { dofs &= ~(uint8)rotation[i]; }
            }
            if (dofs == 0) { PHX_CORE_WARN("Physics: every axis locked on a dynamic body; ignoring the locks (use Static)"); }
            else { settings.mAllowedDOFs = (EAllowedDOFs)dofs; }
        }

        BodyID id = m_Impl->Bodies().CreateAndAddBody(settings, isStatic ? EActivation::DontActivate : EActivation::Activate);
        if (id.IsInvalid()) { PHX_CORE_WARN("Physics body limit reached; body not created"); }
        return id.GetIndexAndSequenceNumber();
    }

    void PhysicsWorld::RemoveBody(uint32_t bodyID){
        if (bodyID == InvalidBody) { return; }
        // Constraints hold raw Body pointers: remove the ones attached to this body
        // first, or the next Step would read the destroyed body.
        for (uint32_t j = 0; j < (uint32_t)m_Impl->joints.size(); j++){
            const JointRecord& rec = m_Impl->joints[j];
            if (rec.constraint && (rec.bodyA == bodyID || rec.bodyB == bodyID)) { RemoveJoint(j); }
        }
        m_Impl->bodySubGroups.erase(bodyID);
        JPH::BodyID id(bodyID);
        m_Impl->Bodies().RemoveBody(id);
        m_Impl->Bodies().DestroyBody(id);
    }

    // ---- Joints ----

    // Locks the two bodies (InvalidBody -> the world's fixed body) and builds the
    // constraint from its settings.
    static JPH::TwoBodyConstraint* CreateTwoBodyConstraint(JPH::PhysicsSystem& system, uint32_t a, uint32_t b,
                                                           JPH::TwoBodyConstraintSettings& settings){
        using namespace JPH;
        BodyID ids[2] = { BodyID(a), BodyID(b) };
        BodyLockMultiWrite lock(system.GetBodyLockInterface(), ids, 2);
        Body* body1 = (a != PhysicsWorld::InvalidBody) ? lock.GetBody(0) : &Body::sFixedToWorld;
        Body* body2 = (b != PhysicsWorld::InvalidBody) ? lock.GetBody(1) : &Body::sFixedToWorld;
        if (!body1 || !body2){
            PHX_CORE_ERROR("[Jolt] constraint references a missing body");
            return nullptr;
        }
        return settings.Create(*body1, *body2);
    }

    static JPH::Vec3 SafeAxis(const glm::vec3& axis){
        JPH::Vec3 a = ToJolt(axis);
        return a.LengthSq() < 1.0e-8f ? JPH::Vec3::sAxisY() : a.Normalized();
    }

    uint32_t PhysicsWorld::AddJoint(uint32_t bodyA, uint32_t bodyB, const JointDesc& d){
        using namespace JPH;
        if (bodyA == InvalidBody) { return InvalidJoint; }
        PhysicsSystem& system = m_Impl->physicsSystem;
        const RVec3 pivot = ToJoltR(d.pointA);
        const Vec3 axis = SafeAxis(d.axis);
        const Vec3 normal = axis.GetNormalizedPerpendicular();
        const float maxForce = d.motorMaxForce > 0.0f ? d.motorMaxForce : FLT_MAX;

        // Jolt measures hinge angles / slider positions (and drives motors) of body 2
        // relative to body 1, so the joint's own body is body 2 and the connected body
        // (or the world) body 1: a positive motor target turns/moves the entity along
        // +axis.
        TwoBodyConstraint* constraint = nullptr;
        switch (d.type){
            case JointType::Fixed:{
                FixedConstraintSettings s;
                s.mSpace = EConstraintSpace::WorldSpace;
                s.mPoint1 = s.mPoint2 = pivot;
                constraint = CreateTwoBodyConstraint(system, bodyB, bodyA, s);
                break;
            }
            case JointType::Point:{
                PointConstraintSettings s;
                s.mSpace = EConstraintSpace::WorldSpace;
                s.mPoint1 = s.mPoint2 = pivot;
                constraint = CreateTwoBodyConstraint(system, bodyB, bodyA, s);
                break;
            }
            case JointType::Distance:{
                DistanceConstraintSettings s;
                s.mSpace = EConstraintSpace::WorldSpace;
                s.mPoint1 = ToJoltR(d.pointB);
                s.mPoint2 = pivot;
                s.mMinDistance = d.minDistance;
                s.mMaxDistance = d.maxDistance;
                if (d.springFrequency > 0.0f){
                    s.mLimitsSpringSettings.mFrequency = d.springFrequency;
                    s.mLimitsSpringSettings.mDamping = std::max(0.0f, d.springDamping);
                }
                constraint = CreateTwoBodyConstraint(system, bodyB, bodyA, s);
                break;
            }
            case JointType::Hinge:{
                HingeConstraintSettings s;
                s.mSpace = EConstraintSpace::WorldSpace;
                s.mPoint1 = s.mPoint2 = pivot;
                s.mHingeAxis1 = s.mHingeAxis2 = axis;
                s.mNormalAxis1 = s.mNormalAxis2 = normal;
                if (d.limited){
                    s.mLimitsMin = std::clamp(d.minLimit, -JPH_PI, 0.0f);
                    s.mLimitsMax = std::clamp(d.maxLimit, 0.0f, JPH_PI);
                }
                s.mMaxFrictionTorque = std::max(0.0f, d.friction);
                s.mMotorSettings.SetTorqueLimit(maxForce);
                constraint = CreateTwoBodyConstraint(system, bodyB, bodyA, s);
                break;
            }
            case JointType::Slider:{
                SliderConstraintSettings s;
                s.mSpace = EConstraintSpace::WorldSpace;
                s.mPoint1 = s.mPoint2 = pivot;
                s.mSliderAxis1 = s.mSliderAxis2 = axis;
                s.mNormalAxis1 = s.mNormalAxis2 = normal;
                if (d.limited){
                    s.mLimitsMin = std::min(d.minLimit, 0.0f);
                    s.mLimitsMax = std::max(d.maxLimit, 0.0f);
                }
                s.mMaxFrictionForce = std::max(0.0f, d.friction);
                s.mMotorSettings.SetForceLimit(maxForce);
                constraint = CreateTwoBodyConstraint(system, bodyB, bodyA, s);
                break;
            }
            case JointType::Cone:{
                ConeConstraintSettings s;
                s.mSpace = EConstraintSpace::WorldSpace;
                s.mPoint1 = s.mPoint2 = pivot;
                s.mTwistAxis1 = s.mTwistAxis2 = axis;
                s.mHalfConeAngle = std::clamp(d.maxLimit, 0.0f, JPH_PI);
                constraint = CreateTwoBodyConstraint(system, bodyB, bodyA, s);
                break;
            }
        }
        if (!constraint) { return InvalidJoint; }

        system.AddConstraint(constraint);
        uint32_t id = (uint32_t)m_Impl->joints.size();
        m_Impl->joints.push_back({ constraint, d.type, bodyA, bodyB, std::max(0.0f, d.breakForce) });
        if (d.motorMode != MotorMode::Off) { SetJointMotor(id, d.motorMode, d.motorTarget); }
        if (!d.collideConnected) { m_Impl->DisableCollision(bodyA, bodyB); }
        return id;
    }

    void PhysicsWorld::RemoveJoint(uint32_t joint){
        JointRecord* rec = m_Impl->GetJoint(joint);
        if (!rec) { return; }
        m_Impl->physicsSystem.RemoveConstraint(rec->constraint);
        rec->constraint = nullptr;
    }

    bool PhysicsWorld::IsJointAlive(uint32_t joint) const{
        return joint < m_Impl->joints.size() && m_Impl->joints[joint].constraint;
    }

    void PhysicsWorld::SetJointMotor(uint32_t joint, MotorMode mode, float target){
        using namespace JPH;
        JointRecord* rec = m_Impl->GetJoint(joint);
        if (!rec) { return; }
        EMotorState state = mode == MotorMode::Velocity ? EMotorState::Velocity
                          : mode == MotorMode::Position ? EMotorState::Position : EMotorState::Off;
        if (rec->type == JointType::Hinge){
            auto* hinge = static_cast<HingeConstraint*>(rec->constraint.GetPtr());
            hinge->SetMotorState(state);
            if (mode == MotorMode::Velocity) { hinge->SetTargetAngularVelocity(target); }
            if (mode == MotorMode::Position) { hinge->SetTargetAngle(target); }
        }
        else if (rec->type == JointType::Slider){
            auto* slider = static_cast<SliderConstraint*>(rec->constraint.GetPtr());
            slider->SetMotorState(state);
            if (mode == MotorMode::Velocity) { slider->SetTargetVelocity(target); }
            if (mode == MotorMode::Position) { slider->SetTargetPosition(target); }
        }
        else { return; }
        // A motor on a sleeping body would otherwise not start until something hits it.
        if (rec->bodyA != InvalidBody) { m_Impl->Bodies().ActivateBody(BodyID(rec->bodyA)); }
        if (rec->bodyB != InvalidBody) { m_Impl->Bodies().ActivateBody(BodyID(rec->bodyB)); }
    }

    float PhysicsWorld::GetJointValue(uint32_t joint) const{
        using namespace JPH;
        const JointRecord* rec = joint < m_Impl->joints.size() ? &m_Impl->joints[joint] : nullptr;
        if (!rec || !rec->constraint) { return 0.0f; }
        switch (rec->type){
            case JointType::Hinge:  return static_cast<const HingeConstraint*>(rec->constraint.GetPtr())->GetCurrentAngle();
            case JointType::Slider: return static_cast<const SliderConstraint*>(rec->constraint.GetPtr())->GetCurrentPosition();
            case JointType::Distance:{
                auto* c = static_cast<const DistanceConstraint*>(rec->constraint.GetPtr());
                RVec3 p1 = c->GetBody1()->GetCenterOfMassTransform() * c->GetConstraintToBody1Matrix().GetTranslation();
                RVec3 p2 = c->GetBody2()->GetCenterOfMassTransform() * c->GetConstraintToBody2Matrix().GetTranslation();
                return (float)(p2 - p1).Length();
            }
            default: return 0.0f;
        }
    }

    // Magnitude of the positional impulse a joint applied in the last collision step.
    static float JointPositionLambda(const JointRecord& rec){
        using namespace JPH;
        const Constraint* c = rec.constraint.GetPtr();
        switch (rec.type){
            case PhysicsWorld::JointType::Fixed:    return static_cast<const FixedConstraint*>(c)->GetTotalLambdaPosition().Length();
            case PhysicsWorld::JointType::Point:    return static_cast<const PointConstraint*>(c)->GetTotalLambdaPosition().Length();
            case PhysicsWorld::JointType::Distance: return std::abs(static_cast<const DistanceConstraint*>(c)->GetTotalLambdaPosition());
            case PhysicsWorld::JointType::Hinge:    return static_cast<const HingeConstraint*>(c)->GetTotalLambdaPosition().Length();
            case PhysicsWorld::JointType::Slider:   return static_cast<const SliderConstraint*>(c)->GetTotalLambdaPosition().Length();
            case PhysicsWorld::JointType::Cone:     return static_cast<const ConeConstraint*>(c)->GetTotalLambdaPosition().Length();
        }
        return 0.0f;
    }

    std::vector<uint32_t> PhysicsWorld::ConsumeBrokenJoints(){
        std::vector<uint32_t> out;
        out.swap(m_Impl->brokenJoints);
        return out;
    }

    uint32_t PhysicsWorld::GetJointCount() const{
        uint32_t n = 0;
        for (const auto& rec : m_Impl->joints) { if (rec.constraint) { n++; } }
        return n;
    }

    // ---- Characters ----

    uint32_t PhysicsWorld::CreateCharacter(const CharacterDesc& desc, const glm::vec3& position, float yaw){
        using namespace JPH;
        const float radius = std::max(desc.radius, 0.05f);
        const float halfHeight = std::max(desc.halfHeight, 0.01f);

        JPH::Ref<CharacterVirtualSettings> settings = new CharacterVirtualSettings();
        settings->mShape = new CapsuleShape(halfHeight, radius);
        settings->mInnerBodyShape = settings->mShape; // a body raycasts/sensors/rigid bodies can see
        settings->mInnerBodyLayer = MakeObjectLayer(desc.layer, true);
        settings->mUp = Vec3::sAxisY();
        settings->mMaxSlopeAngle = std::clamp(desc.maxSlopeAngle, 0.0f, 1.55f);
        settings->mMass = std::max(desc.mass, 1.0f);
        settings->mMaxStrength = std::max(desc.maxStrength, 0.0f);
        // The capsule is centered on the character position (matching the entity
        // transform): contacts below the cylindrical part count as standing support.
        settings->mSupportingVolume = Plane(Vec3::sAxisY(), halfHeight);

        Quat rotation = Quat::sRotation(Vec3::sAxisY(), yaw);
        JPH::Ref<CharacterVirtual> character = new CharacterVirtual(settings, ToJoltR(position), rotation, 0, &m_Impl->physicsSystem);
        character->SetListener(&m_Impl->characterListener);
        character->SetCharacterVsCharacterCollision(&m_Impl->characterVsCharacter);
        m_Impl->characterVsCharacter.Add(character);
        if (!character->GetInnerBodyID().IsInvalid())
            m_Impl->contactListener.characterBodies.insert(character->GetInnerBodyID().GetIndexAndSequenceNumber());

        CharacterRecord rec;
        rec.character = character;
        rec.desc = desc;
        m_Impl->characters.push_back(rec);
        return (uint32_t)m_Impl->characters.size() - 1;
    }

    void PhysicsWorld::RemoveCharacter(uint32_t id){
        CharacterRecord* rec = m_Impl->GetCharacter(id);
        if (!rec) { return; }
        m_Impl->characterVsCharacter.Remove(rec->character);
        if (!rec->character->GetInnerBodyID().IsInvalid())
            m_Impl->contactListener.characterBodies.erase(rec->character->GetInnerBodyID().GetIndexAndSequenceNumber());
        rec->character = nullptr; // destructor removes the inner body
    }

    void PhysicsWorld::SetCharacterInput(uint32_t id, const glm::vec3& velocity, bool jump, float jumpSpeed){
        if (CharacterRecord* rec = m_Impl->GetCharacter(id)){
            rec->input = JPH::Vec3(velocity.x, 0.0f, velocity.z);
            rec->jump = rec->jump || jump; // latch until the next Step consumes it
            rec->jumpSpeed = jumpSpeed;
        }
    }

    void PhysicsWorld::SetCharacterPosition(uint32_t id, const glm::vec3& position){
        if (CharacterRecord* rec = m_Impl->GetCharacter(id)){
            rec->character->SetPosition(ToJoltR(position));
            rec->character->SetLinearVelocity(JPH::Vec3::sZero());
        }
    }

    glm::vec3 PhysicsWorld::GetCharacterPosition(uint32_t id) const{
        const CharacterRecord* rec = m_Impl->GetCharacter(id);
        return rec ? ToGlmR(rec->character->GetPosition()) : glm::vec3(0.0f);
    }

    glm::vec3 PhysicsWorld::GetCharacterVelocity(uint32_t id) const{
        const CharacterRecord* rec = m_Impl->GetCharacter(id);
        return rec ? ToGlm(rec->character->GetLinearVelocity()) : glm::vec3(0.0f);
    }

    bool PhysicsWorld::IsCharacterGrounded(uint32_t id) const{
        const CharacterRecord* rec = m_Impl->GetCharacter(id);
        return rec && rec->character->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
    }

    glm::vec3 PhysicsWorld::GetCharacterGroundNormal(uint32_t id) const{
        const CharacterRecord* rec = m_Impl->GetCharacter(id);
        return (rec && rec->character->IsSupported()) ? ToGlm(rec->character->GetGroundNormal()) : glm::vec3(0.0f, 1.0f, 0.0f);
    }

    uint32_t PhysicsWorld::GetCharacterBody(uint32_t id) const{
        const CharacterRecord* rec = m_Impl->GetCharacter(id);
        return rec ? rec->character->GetInnerBodyID().GetIndexAndSequenceNumber() : InvalidBody;
    }

    // Velocity update in the style of Jolt's CharacterVirtual sample: keep ground
    // velocity when standing (moving platforms), keep vertical velocity in the air,
    // add gravity, then steer horizontally (fully on ground, partially in the air).
    void PhysicsWorld::Impl::UpdateCharacter(CharacterRecord& rec, float dt){
        using namespace JPH;
        CharacterVirtual& c = *rec.character;
        const PhysicsWorld::CharacterDesc& desc = rec.desc;
        const Vec3 up = c.GetUp();
        const Vec3 linear = c.GetLinearVelocity();
        const Vec3 vertical = linear.Dot(up) * up;
        const Vec3 groundVelocity = c.GetGroundVelocity();
        const bool movingTowardsGround = (vertical.GetY() - groundVelocity.GetY()) < 0.1f;
        const bool onGround = c.GetGroundState() == CharacterBase::EGroundState::OnGround;

        Vec3 velocity;
        Vec3 horizontal;
        if (onGround && movingTowardsGround){
            velocity = groundVelocity;
            if (rec.jump) { velocity += rec.jumpSpeed * up; }
            horizontal = rec.input;
        }
        else{
            velocity = vertical;
            Vec3 current = linear - vertical;
            float control = 1.0f - std::pow(1.0f - std::clamp(desc.airControl, 0.0f, 1.0f), dt * 60.0f);
            horizontal = current + (rec.input - current) * control;
        }
        if (c.IsSupported()) { horizontal = c.CancelVelocityTowardsSteepSlopes(horizontal); }
        velocity += physicsSystem.GetGravity() * desc.gravityFactor * dt;
        velocity += horizontal;
        c.SetLinearVelocity(velocity);
        rec.jump = false;

        CharacterVirtual::ExtendedUpdateSettings update;
        update.mStickToFloorStepDown = Vec3(0.0f, -std::max(0.0f, desc.stickToFloor), 0.0f);
        update.mWalkStairsStepUp = Vec3(0.0f, std::max(0.0f, desc.stepHeight), 0.0f);
        ObjectLayer layer = MakeObjectLayer(desc.layer, true);
        c.ExtendedUpdate(dt, physicsSystem.GetGravity() * desc.gravityFactor, update,
            physicsSystem.GetDefaultBroadPhaseLayerFilter(layer),
            physicsSystem.GetDefaultLayerFilter(layer),
            {}, {}, tempAllocator);
    }

    // ---- Dynamics ----

    void PhysicsWorld::ApplyImpulse(uint32_t bodyID, const glm::vec3& impulse){
        if (bodyID == InvalidBody) { return; }
        m_Impl->Bodies().AddImpulse(JPH::BodyID(bodyID), ToJolt(impulse));
    }

    void PhysicsWorld::ApplyImpulseAtPoint(uint32_t bodyID, const glm::vec3& impulse, const glm::vec3& worldPoint){
        if (bodyID == InvalidBody) { return; }
        m_Impl->Bodies().AddImpulse(JPH::BodyID(bodyID), ToJolt(impulse), ToJoltR(worldPoint));
    }

    void PhysicsWorld::ApplyAngularImpulse(uint32_t bodyID, const glm::vec3& impulse){
        if (bodyID == InvalidBody) { return; }
        m_Impl->Bodies().AddAngularImpulse(JPH::BodyID(bodyID), ToJolt(impulse));
    }

    void PhysicsWorld::ApplyForce(uint32_t bodyID, const glm::vec3& force){
        if (bodyID == InvalidBody) { return; }
        m_Impl->Bodies().AddForce(JPH::BodyID(bodyID), ToJolt(force));
    }

    void PhysicsWorld::ApplyForceAtPoint(uint32_t bodyID, const glm::vec3& force, const glm::vec3& worldPoint){
        if (bodyID == InvalidBody) { return; }
        m_Impl->Bodies().AddForce(JPH::BodyID(bodyID), ToJolt(force), ToJoltR(worldPoint));
    }

    void PhysicsWorld::ApplyTorque(uint32_t bodyID, const glm::vec3& torque){
        if (bodyID == InvalidBody) { return; }
        m_Impl->Bodies().AddTorque(JPH::BodyID(bodyID), ToJolt(torque));
    }

    void PhysicsWorld::SetLinearVelocity(uint32_t bodyID, const glm::vec3& velocity){
        if (bodyID == InvalidBody) { return; }
        m_Impl->Bodies().SetLinearVelocity(JPH::BodyID(bodyID), ToJolt(velocity));
    }

    glm::vec3 PhysicsWorld::GetLinearVelocity(uint32_t bodyID) const{
        if (bodyID == InvalidBody) { return glm::vec3(0.0f); }
        return ToGlm(m_Impl->Bodies().GetLinearVelocity(JPH::BodyID(bodyID)));
    }

    void PhysicsWorld::SetAngularVelocity(uint32_t bodyID, const glm::vec3& velocity){
        if (bodyID == InvalidBody) { return; }
        m_Impl->Bodies().SetAngularVelocity(JPH::BodyID(bodyID), ToJolt(velocity));
    }

    glm::vec3 PhysicsWorld::GetAngularVelocity(uint32_t bodyID) const{
        if (bodyID == InvalidBody) { return glm::vec3(0.0f); }
        return ToGlm(m_Impl->Bodies().GetAngularVelocity(JPH::BodyID(bodyID)));
    }

    float PhysicsWorld::GetMass(uint32_t bodyID) const{
        if (bodyID == InvalidBody) { return 0.0f; }
        JPH::BodyLockRead lock(m_Impl->physicsSystem.GetBodyLockInterface(), JPH::BodyID(bodyID));
        if (!lock.Succeeded() || !lock.GetBody().IsDynamic()) { return 0.0f; }
        float inverse = lock.GetBody().GetMotionProperties()->GetInverseMass();
        return inverse > 0.0f ? 1.0f / inverse : 0.0f;
    }

    void PhysicsWorld::SetGravityFactor(uint32_t bodyID, float factor){
        if (bodyID == InvalidBody) { return; }
        m_Impl->Bodies().SetGravityFactor(JPH::BodyID(bodyID), factor);
        m_Impl->Bodies().ActivateBody(JPH::BodyID(bodyID));
    }

    void PhysicsWorld::SetFriction(uint32_t bodyID, float friction){
        if (bodyID == InvalidBody) { return; }
        m_Impl->Bodies().SetFriction(JPH::BodyID(bodyID), friction);
    }

    void PhysicsWorld::SetRestitution(uint32_t bodyID, float restitution){
        if (bodyID == InvalidBody) { return; }
        m_Impl->Bodies().SetRestitution(JPH::BodyID(bodyID), restitution);
    }

    void PhysicsWorld::SetBodyTransform(uint32_t bodyID, const glm::vec3& position, const glm::vec3& rotationEuler){
        if (bodyID == InvalidBody) { return; }
        m_Impl->Bodies().SetPositionAndRotation(JPH::BodyID(bodyID), ToJoltR(position),
            ToJolt(EulerXYZToQuat(rotationEuler)), JPH::EActivation::Activate);
    }

    void PhysicsWorld::WakeUp(uint32_t bodyID){
        if (bodyID == InvalidBody) { return; }
        m_Impl->Bodies().ActivateBody(JPH::BodyID(bodyID));
    }

    bool PhysicsWorld::IsAwake(uint32_t bodyID) const{
        if (bodyID == InvalidBody) { return false; }
        return m_Impl->Bodies().IsActive(JPH::BodyID(bodyID));
    }

    bool PhysicsWorld::IsSensor(uint32_t bodyID) const{
        if (bodyID == InvalidBody) { return false; }
        JPH::BodyLockRead lock(m_Impl->physicsSystem.GetBodyLockInterface(), JPH::BodyID(bodyID));
        return lock.Succeeded() && lock.GetBody().IsSensor();
    }

    void PhysicsWorld::MoveKinematic(uint32_t bodyID, const glm::vec3& position, const glm::vec3& rotationEuler, float dt){
        if (bodyID == InvalidBody || dt <= 0.0f) { return; }
        m_Impl->Bodies().MoveKinematic(JPH::BodyID(bodyID), ToJoltR(position), ToJolt(EulerXYZToQuat(rotationEuler)), dt);
    }

    bool PhysicsWorld::ApplyBuoyancy(uint32_t bodyID, const glm::vec3& surfacePoint, const glm::vec3& surfaceNormal,
                                     float buoyancy, float linearDrag, float angularDrag, float dt){
        if (bodyID == InvalidBody || dt <= 0.0f) { return false; }
        return m_Impl->Bodies().ApplyBuoyancyImpulse(JPH::BodyID(bodyID), ToJoltR(surfacePoint), ToJolt(surfaceNormal),
            buoyancy, linearDrag, angularDrag, JPH::Vec3::sZero(), m_Impl->physicsSystem.GetGravity(), dt);
    }

    // ---- Queries ----

    // World-space surface normal of a body at a point (falls back to -direction).
    static glm::vec3 SurfaceNormal(const JPH::PhysicsSystem& system, const JPH::BodyID& id,
                                   const JPH::SubShapeID& subShape, JPH::RVec3Arg point, const glm::vec3& fallback){
        JPH::BodyLockRead lock(system.GetBodyLockInterface(), id);
        if (!lock.Succeeded()) { return fallback; }
        return ToGlm(lock.GetBody().GetWorldSpaceSurfaceNormal(subShape, point));
    }

    bool PhysicsWorld::RayCast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
                               Hit& outHit, const QueryFilter& filter) const{
        using namespace JPH;
        float len = glm::length(direction);
        if (len < 1e-6f || maxDistance <= 0.0f) { return false; }
        glm::vec3 d = direction / len * maxDistance;
        RRayCast ray{ ToJoltR(origin), ToJolt(d) };
        RayCastResult hit;
        LayerMaskFilter layers(filter.layerMask);
        QueryBodyFilter bodies(filter);
        if (!m_Impl->physicsSystem.GetNarrowPhaseQuery().CastRay(ray, hit, {}, layers, bodies)) { return false; }
        outHit.body = hit.mBodyID.GetIndexAndSequenceNumber();
        outHit.point = origin + d * hit.mFraction;
        outHit.distance = maxDistance * hit.mFraction;
        outHit.normal = SurfaceNormal(m_Impl->physicsSystem, hit.mBodyID, hit.mSubShapeID2,
            ToJoltR(outHit.point), -direction / len);
        return true;
    }

    bool PhysicsWorld::SphereCast(const glm::vec3& origin, const glm::vec3& direction, float radius, float maxDistance,
                                  Hit& outHit, const QueryFilter& filter) const{
        using namespace JPH;
        float len = glm::length(direction);
        if (len < 1e-6f || maxDistance <= 0.0f) { return false; }
        glm::vec3 d = direction / len * maxDistance;
        SphereShape sphere(std::max(radius, 0.01f));
        sphere.SetEmbedded();
        RShapeCast cast = RShapeCast::sFromWorldTransform(&sphere, Vec3::sOne(), RMat44::sTranslation(ToJoltR(origin)), ToJolt(d));
        ShapeCastSettings settings;
        ClosestHitCollisionCollector<CastShapeCollector> collector;
        LayerMaskFilter layers(filter.layerMask);
        QueryBodyFilter bodies(filter);
        m_Impl->physicsSystem.GetNarrowPhaseQuery().CastShape(cast, settings, RVec3::sZero(), collector, {}, layers, bodies);
        if (!collector.HadHit()) { return false; }
        const ShapeCastResult& r = collector.mHit;
        outHit.body = r.mBodyID2.GetIndexAndSequenceNumber();
        outHit.point = ToGlm(r.mContactPointOn2);
        outHit.distance = maxDistance * r.mFraction;
        Vec3 axis = r.mPenetrationAxis;
        outHit.normal = axis.LengthSq() > 1e-12f ? ToGlm(-axis.Normalized()) : -direction / len;
        return true;
    }

    static std::vector<uint32_t> Overlap(const JPH::PhysicsSystem& system, const JPH::Shape& shape, JPH::RMat44Arg transform,
                                         const PhysicsWorld::QueryFilter& filter){
        using namespace JPH;
        CollideShapeSettings settings;
        AllHitCollisionCollector<CollideShapeCollector> collector;
        LayerMaskFilter layers(filter.layerMask);
        QueryBodyFilter bodies(filter);
        system.GetNarrowPhaseQuery().CollideShape(&shape, Vec3::sOne(), transform, settings, RVec3::sZero(),
            collector, {}, layers, bodies);
        std::vector<uint32_t> out;
        for (const auto& hit : collector.mHits){
            uint32_t id = hit.mBodyID2.GetIndexAndSequenceNumber();
            if (std::find(out.begin(), out.end(), id) == out.end()) { out.push_back(id); }
        }
        return out;
    }

    std::vector<uint32_t> PhysicsWorld::OverlapSphere(const glm::vec3& center, float radius, const QueryFilter& filter) const{
        JPH::SphereShape sphere(std::max(radius, 0.01f));
        sphere.SetEmbedded();
        return Overlap(m_Impl->physicsSystem, sphere, JPH::RMat44::sTranslation(ToJoltR(center)), filter);
    }

    std::vector<uint32_t> PhysicsWorld::OverlapBox(const glm::vec3& center, const glm::vec3& halfExtents,
                                                   const glm::vec3& rotationEuler, const QueryFilter& filter) const{
        JPH::BoxShape box(JPH::Vec3(std::max(halfExtents.x, 0.06f), std::max(halfExtents.y, 0.06f), std::max(halfExtents.z, 0.06f)));
        box.SetEmbedded();
        return Overlap(m_Impl->physicsSystem, box,
            JPH::RMat44::sRotationTranslation(ToJolt(EulerXYZToQuat(rotationEuler)), ToJoltR(center)), filter);
    }

    std::vector<PhysicsWorld::ContactEvent> PhysicsWorld::ConsumeContactEvents(){
        return m_Impl->contactListener.Consume();
    }

    void PhysicsWorld::OptimizeBroadPhase(){
        m_Impl->physicsSystem.OptimizeBroadPhase();
    }

    void PhysicsWorld::Step(float dt){
        if (dt <= 0.0f) { return; }
        dt = std::min(dt, MaxStep);

        // Characters move first (Jolt's recommended order) so the bodies they push
        // respond in this same step.
        for (auto& rec : m_Impl->characters)
            if (rec.character) { m_Impl->UpdateCharacter(rec, dt); }

        const float rate = std::max(m_Impl->settings.maxSubstepRate, 1.0f);
        const int collisionSteps = std::max(1, (int)std::ceil(dt * rate - 1e-3f));
        m_Impl->lastSubstep = dt / collisionSteps;
        JPH::EPhysicsUpdateError error = m_Impl->physicsSystem.Update(dt, collisionSteps,
            &m_Impl->tempAllocator, &m_Impl->jobSystem);

        // Log when the error state changes rather than every frame it persists.
        if (error != m_Impl->lastUpdateError && error != JPH::EPhysicsUpdateError::None){
            auto has = [error](JPH::EPhysicsUpdateError flag){ return (error & flag) != JPH::EPhysicsUpdateError::None; };
            PHX_CORE_WARN("Physics update dropped contacts:{0}{1}{2}",
                has(JPH::EPhysicsUpdateError::ManifoldCacheFull) ? " manifold cache full;" : "",
                has(JPH::EPhysicsUpdateError::BodyPairCacheFull) ? " body pair cache full;" : "",
                has(JPH::EPhysicsUpdateError::ContactConstraintsFull) ? " contact constraints full;" : "");
        }
        m_Impl->lastUpdateError = error;

        // Breakable joints: the positional impulse of the last collision step over its
        // duration is the force the joint had to hold.
        for (uint32_t j = 0; j < (uint32_t)m_Impl->joints.size(); j++){
            JointRecord& rec = m_Impl->joints[j];
            if (!rec.constraint || rec.breakForce <= 0.0f) { continue; }
            if (JointPositionLambda(rec) / m_Impl->lastSubstep > rec.breakForce){
                RemoveJoint(j);
                m_Impl->brokenJoints.push_back(j);
            }
        }
    }

    void PhysicsWorld::GetBodyTransform(uint32_t bodyID, glm::vec3& outPosition, glm::vec3& inOutRotationEuler) const{
        if (bodyID == InvalidBody) { return; }
        JPH::BodyID id(bodyID);
        JPH::RVec3 pos = m_Impl->Bodies().GetPosition(id);
        JPH::Quat rot = m_Impl->Bodies().GetRotation(id);
        outPosition = ToGlmR(pos);
        glm::quat q(rot.GetW(), rot.GetX(), rot.GetY(), rot.GetZ());
        inOutRotationEuler = QuatToEulerXYZ(q, inOutRotationEuler);
    }

    uint32_t PhysicsWorld::GetBodyCount() const { return m_Impl->physicsSystem.GetNumBodies(); }

    uint32_t PhysicsWorld::GetActiveBodyCount() const{
        return m_Impl->physicsSystem.GetNumActiveBodies(JPH::EBodyType::RigidBody);
    }
}
