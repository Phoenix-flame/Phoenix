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
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLockMulti.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>

#include <glm/gtc/quaternion.hpp>

#include <thread>
#include <mutex>
#include <algorithm>
#include <cstdarg>
#include <cstdio>

namespace Phoenix{

    // ---- Jolt collision layers (standard two-layer setup) ----

    namespace Layers{
        static constexpr JPH::ObjectLayer NON_MOVING = 0;
        static constexpr JPH::ObjectLayer MOVING = 1;
        static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
    }

    namespace BroadPhaseLayers{
        static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
        static constexpr JPH::BroadPhaseLayer MOVING(1);
        static constexpr JPH::uint NUM_LAYERS(2);
    }

    class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface{
    public:
        BPLayerInterfaceImpl(){
            m_ObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
            m_ObjectToBroadPhase[Layers::MOVING]     = BroadPhaseLayers::MOVING;
        }
        virtual JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NUM_LAYERS; }
        virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override {
            return m_ObjectToBroadPhase[inLayer];
        }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        virtual const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer) const override { return "Layer"; }
#endif
    private:
        JPH::BroadPhaseLayer m_ObjectToBroadPhase[Layers::NUM_LAYERS];
    };

    class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter{
    public:
        virtual bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override{
            switch (inLayer1){
                case Layers::NON_MOVING: return inLayer2 == BroadPhaseLayers::MOVING;
                case Layers::MOVING:     return true;
                default:                 return false;
            }
        }
    };

    class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter{
    public:
        virtual bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override{
            switch (inObject1){
                case Layers::NON_MOVING: return inObject2 == Layers::MOVING;
                case Layers::MOVING:     return true;
                default:                 return false;
            }
        }
    };

    // Records contact begin/end pairs. Jolt calls these from its worker threads
    // during Step, so the event list is mutex-guarded; the Scene drains it after
    // each step on the main thread.
    class ContactListenerImpl : public JPH::ContactListener{
    public:
        virtual void OnContactAdded(const JPH::Body& body1, const JPH::Body& body2,
                                    const JPH::ContactManifold&, JPH::ContactSettings&) override{
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Events.push_back({ body1.GetID().GetIndexAndSequenceNumber(),
                                 body2.GetID().GetIndexAndSequenceNumber(), true });
        }
        virtual void OnContactRemoved(const JPH::SubShapeIDPair& pair) override{
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Events.push_back({ pair.GetBody1ID().GetIndexAndSequenceNumber(),
                                 pair.GetBody2ID().GetIndexAndSequenceNumber(), false });
        }
        std::vector<PhysicsWorld::ContactEvent> Consume(){
            std::lock_guard<std::mutex> lock(m_Mutex);
            std::vector<PhysicsWorld::ContactEvent> out;
            out.swap(m_Events);
            return out;
        }
    private:
        std::mutex m_Mutex;
        std::vector<PhysicsWorld::ContactEvent> m_Events;
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

    struct PhysicsWorld::Impl{
        BPLayerInterfaceImpl broadPhaseLayerInterface;
        ObjectVsBroadPhaseLayerFilterImpl objectVsBroadPhaseLayerFilter;
        ObjectLayerPairFilterImpl objectLayerPairFilter;
        ContactListenerImpl contactListener;
        JPH::TempAllocatorImpl tempAllocator{ 10 * 1024 * 1024 };
        JPH::JobSystemThreadPool jobSystem;
        JPH::PhysicsSystem physicsSystem;

        Impl(){
            int numThreads = (int)std::thread::hardware_concurrency() - 1;
            if (numThreads < 1) { numThreads = 1; }
            jobSystem.Init(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, numThreads);

            physicsSystem.Init(1024, 0, 1024, 1024,
                broadPhaseLayerInterface, objectVsBroadPhaseLayerFilter, objectLayerPairFilter);
            physicsSystem.SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));
            physicsSystem.SetContactListener(&contactListener);
        }
    };

    PhysicsWorld::PhysicsWorld(){
        Init(); // idempotent lazy global init
        m_Impl = CreateScope<Impl>();
    }

    PhysicsWorld::~PhysicsWorld() = default;

    // Create a body from an already-built shape with the given transform/type/props.
    static uint32_t CreateBodyFromShape(JPH::PhysicsSystem& system, const JPH::ShapeRefC& shape,
                                        const glm::vec3& position, const glm::vec3& rotationEuler,
                                        PhysicsWorld::BodyType type, const PhysicsWorld::BodyProps& props){
        using namespace JPH;
        glm::quat q = glm::quat(rotationEuler);
        EMotionType motion = (type == PhysicsWorld::BodyType::Static)    ? EMotionType::Static
                           : (type == PhysicsWorld::BodyType::Kinematic) ? EMotionType::Kinematic
                                                                         : EMotionType::Dynamic;
        ObjectLayer layer = (type == PhysicsWorld::BodyType::Static) ? Layers::NON_MOVING : Layers::MOVING;
        BodyCreationSettings settings(shape, RVec3(position.x, position.y, position.z),
            Quat(q.x, q.y, q.z, q.w), motion, layer);

        settings.mFriction = props.friction;
        settings.mRestitution = props.restitution;
        settings.mLinearDamping = props.linearDamping;
        settings.mAngularDamping = props.angularDamping;
        settings.mGravityFactor = props.gravityFactor;
        settings.mIsSensor = props.isSensor;
        if (props.continuousCollision) { settings.mMotionQuality = EMotionQuality::LinearCast; }
        settings.mLinearVelocity = Vec3(props.initialVelocity.x, props.initialVelocity.y, props.initialVelocity.z);

        BodyID id = system.GetBodyInterface().CreateAndAddBody(settings,
            type == PhysicsWorld::BodyType::Static ? EActivation::DontActivate : EActivation::Activate);
        return id.GetIndexAndSequenceNumber();
    }

    // Build a convex shape from settings (applying density) and create a body from it.
    static uint32_t CreateConvexBody(JPH::PhysicsSystem& system, JPH::ConvexShapeSettings& shapeSettings,
                                     const glm::vec3& position, const glm::vec3& rotationEuler,
                                     PhysicsWorld::BodyType type, const PhysicsWorld::BodyProps& props,
                                     const char* what){
        shapeSettings.SetEmbedded();
        if (props.density > 0.0f) { shapeSettings.SetDensity(props.density); }
        JPH::ShapeSettings::ShapeResult result = shapeSettings.Create();
        if (result.HasError()){
            PHX_CORE_ERROR("[Jolt] {0} shape error: {1}", what, result.GetError().c_str());
            return PhysicsWorld::InvalidBody;
        }
        return CreateBodyFromShape(system, result.Get(), position, rotationEuler, type, props);
    }

    uint32_t PhysicsWorld::CreateBox(const glm::vec3& position, const glm::vec3& rotationEuler,
                                     const glm::vec3& halfExtents, BodyType type, const BodyProps& props){
        // Jolt requires halfExtent >= convex radius (default 0.05) on every axis.
        JPH::Vec3 he(std::max(halfExtents.x, 0.06f), std::max(halfExtents.y, 0.06f), std::max(halfExtents.z, 0.06f));
        JPH::BoxShapeSettings shapeSettings(he);
        return CreateConvexBody(m_Impl->physicsSystem, shapeSettings, position, rotationEuler, type, props, "box");
    }

    uint32_t PhysicsWorld::CreateSphere(const glm::vec3& position, const glm::vec3& rotationEuler,
                                        float radius, BodyType type, const BodyProps& props){
        JPH::SphereShapeSettings shapeSettings(std::max(radius, 0.01f));
        return CreateConvexBody(m_Impl->physicsSystem, shapeSettings, position, rotationEuler, type, props, "sphere");
    }

    uint32_t PhysicsWorld::CreateCapsule(const glm::vec3& position, const glm::vec3& rotationEuler,
                                         float halfHeight, float radius, BodyType type, const BodyProps& props){
        JPH::CapsuleShapeSettings shapeSettings(std::max(halfHeight, 0.01f), std::max(radius, 0.01f));
        return CreateConvexBody(m_Impl->physicsSystem, shapeSettings, position, rotationEuler, type, props, "capsule");
    }

    uint32_t PhysicsWorld::CreateCylinder(const glm::vec3& position, const glm::vec3& rotationEuler,
                                          float halfHeight, float radius, BodyType type, const BodyProps& props){
        JPH::CylinderShapeSettings shapeSettings(std::max(halfHeight, 0.06f), std::max(radius, 0.06f));
        return CreateConvexBody(m_Impl->physicsSystem, shapeSettings, position, rotationEuler, type, props, "cylinder");
    }

    uint32_t PhysicsWorld::CreateConvexHull(const std::vector<glm::vec3>& points,
                                            const glm::vec3& position, const glm::vec3& rotationEuler,
                                            BodyType type, const BodyProps& props){
        using namespace JPH;
        if (points.empty()) { return InvalidBody; }

        Array<Vec3> hullPoints;
        hullPoints.reserve(points.size());
        for (const auto& p : points) { hullPoints.push_back(Vec3(p.x, p.y, p.z)); }

        ConvexHullShapeSettings shapeSettings(hullPoints);
        return CreateConvexBody(m_Impl->physicsSystem, shapeSettings, position, rotationEuler, type, props, "convex hull");
    }

    uint32_t PhysicsWorld::CreateMesh(const std::vector<glm::vec3>& points, const std::vector<uint32_t>& indices,
                                      const glm::vec3& position, const glm::vec3& rotationEuler,
                                      const BodyProps& props){
        using namespace JPH;
        if (points.empty() || indices.size() < 3) { return InvalidBody; }

        VertexList vertices;
        vertices.reserve(points.size());
        for (const auto& p : points) { vertices.push_back(Float3(p.x, p.y, p.z)); }

        IndexedTriangleList triangles;
        triangles.reserve(indices.size() / 3);
        for (size_t i = 0; i + 2 < indices.size(); i += 3){
            triangles.push_back(IndexedTriangle((uint32)indices[i], (uint32)indices[i + 1], (uint32)indices[i + 2], 0));
        }

        MeshShapeSettings shapeSettings(vertices, triangles);
        shapeSettings.SetEmbedded();
        ShapeSettings::ShapeResult result = shapeSettings.Create();
        if (result.HasError()){
            PHX_CORE_ERROR("[Jolt] mesh shape error: {0}", result.GetError().c_str());
            return InvalidBody;
        }
        // Triangle meshes must be static.
        return CreateBodyFromShape(m_Impl->physicsSystem, result.Get(), position, rotationEuler, BodyType::Static, props);
    }

    void PhysicsWorld::RemoveBody(uint32_t bodyID){
        if (bodyID == InvalidBody) { return; }
        JPH::BodyID id(bodyID);
        JPH::BodyInterface& bodyInterface = m_Impl->physicsSystem.GetBodyInterface();
        bodyInterface.RemoveBody(id);
        bodyInterface.DestroyBody(id);
    }

    // ---- Constraints ----

    // Locks the two bodies (InvalidBody -> the world's fixed body), builds the
    // constraint and registers it with the system.
    template<typename SettingsT>
    static void AddTwoBodyConstraint(JPH::PhysicsSystem& system, uint32_t a, uint32_t b, SettingsT& settings){
        using namespace JPH;
        BodyID ids[2] = { BodyID(a), BodyID(b) };
        BodyLockMultiWrite lock(system.GetBodyLockInterface(), ids, 2);
        Body* body1 = (a != PhysicsWorld::InvalidBody) ? lock.GetBody(0) : &Body::sFixedToWorld;
        Body* body2 = (b != PhysicsWorld::InvalidBody) ? lock.GetBody(1) : &Body::sFixedToWorld;
        if (!body1 || !body2){
            PHX_CORE_ERROR("[Jolt] constraint references a missing body");
            return;
        }
        system.AddConstraint(settings.Create(*body1, *body2));
    }

    void PhysicsWorld::AddPointConstraint(uint32_t body, uint32_t other, const glm::vec3& worldPoint){
        JPH::PointConstraintSettings settings;
        settings.mSpace = JPH::EConstraintSpace::WorldSpace;
        settings.mPoint1 = settings.mPoint2 = JPH::RVec3(worldPoint.x, worldPoint.y, worldPoint.z);
        AddTwoBodyConstraint(m_Impl->physicsSystem, body, other, settings);
    }

    void PhysicsWorld::AddDistanceConstraint(uint32_t body, uint32_t other,
                                             const glm::vec3& worldPointOnBody, const glm::vec3& worldPointOnOther,
                                             float minDistance, float maxDistance){
        JPH::DistanceConstraintSettings settings;
        settings.mSpace = JPH::EConstraintSpace::WorldSpace;
        settings.mPoint1 = JPH::RVec3(worldPointOnBody.x, worldPointOnBody.y, worldPointOnBody.z);
        settings.mPoint2 = JPH::RVec3(worldPointOnOther.x, worldPointOnOther.y, worldPointOnOther.z);
        settings.mMinDistance = minDistance;
        settings.mMaxDistance = maxDistance;
        AddTwoBodyConstraint(m_Impl->physicsSystem, body, other, settings);
    }

    void PhysicsWorld::AddHingeConstraint(uint32_t body, uint32_t other,
                                          const glm::vec3& worldPivot, const glm::vec3& worldAxis,
                                          bool limited, float minAngle, float maxAngle){
        using namespace JPH;
        Vec3 axis = Vec3(worldAxis.x, worldAxis.y, worldAxis.z);
        if (axis.LengthSq() < 1.0e-8f) { axis = Vec3::sAxisY(); }
        axis = axis.Normalized();

        HingeConstraintSettings settings;
        settings.mSpace = EConstraintSpace::WorldSpace;
        settings.mPoint1 = settings.mPoint2 = RVec3(worldPivot.x, worldPivot.y, worldPivot.z);
        settings.mHingeAxis1 = settings.mHingeAxis2 = axis;
        settings.mNormalAxis1 = settings.mNormalAxis2 = axis.GetNormalizedPerpendicular();
        if (limited){
            settings.mLimitsMin = minAngle;
            settings.mLimitsMax = maxAngle;
        }
        AddTwoBodyConstraint(m_Impl->physicsSystem, body, other, settings);
    }

    // ---- Dynamics ----

    void PhysicsWorld::ApplyImpulse(uint32_t bodyID, const glm::vec3& impulse){
        if (bodyID == InvalidBody) { return; }
        m_Impl->physicsSystem.GetBodyInterface().AddImpulse(JPH::BodyID(bodyID),
            JPH::Vec3(impulse.x, impulse.y, impulse.z));
    }

    void PhysicsWorld::ApplyForce(uint32_t bodyID, const glm::vec3& force){
        if (bodyID == InvalidBody) { return; }
        m_Impl->physicsSystem.GetBodyInterface().AddForce(JPH::BodyID(bodyID),
            JPH::Vec3(force.x, force.y, force.z));
    }

    void PhysicsWorld::SetLinearVelocity(uint32_t bodyID, const glm::vec3& velocity){
        if (bodyID == InvalidBody) { return; }
        m_Impl->physicsSystem.GetBodyInterface().SetLinearVelocity(JPH::BodyID(bodyID),
            JPH::Vec3(velocity.x, velocity.y, velocity.z));
    }

    glm::vec3 PhysicsWorld::GetLinearVelocity(uint32_t bodyID) const{
        if (bodyID == InvalidBody) { return glm::vec3(0.0f); }
        JPH::Vec3 v = m_Impl->physicsSystem.GetBodyInterface().GetLinearVelocity(JPH::BodyID(bodyID));
        return { v.GetX(), v.GetY(), v.GetZ() };
    }

    void PhysicsWorld::SetAngularVelocity(uint32_t bodyID, const glm::vec3& velocity){
        if (bodyID == InvalidBody) { return; }
        m_Impl->physicsSystem.GetBodyInterface().SetAngularVelocity(JPH::BodyID(bodyID),
            JPH::Vec3(velocity.x, velocity.y, velocity.z));
    }

    void PhysicsWorld::MoveKinematic(uint32_t bodyID, const glm::vec3& position, const glm::vec3& rotationEuler, float dt){
        if (bodyID == InvalidBody || dt <= 0.0f) { return; }
        glm::quat q = glm::quat(rotationEuler);
        m_Impl->physicsSystem.GetBodyInterface().MoveKinematic(JPH::BodyID(bodyID),
            JPH::RVec3(position.x, position.y, position.z), JPH::Quat(q.x, q.y, q.z, q.w), dt);
    }

    bool PhysicsWorld::RayCast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
                               uint32_t& outBody, glm::vec3& outPoint) const{
        using namespace JPH;
        glm::vec3 d = direction * maxDistance;
        RRayCast ray{ RVec3(origin.x, origin.y, origin.z), Vec3(d.x, d.y, d.z) };
        RayCastResult hit;
        if (!m_Impl->physicsSystem.GetNarrowPhaseQuery().CastRay(ray, hit)) { return false; }
        outBody = hit.mBodyID.GetIndexAndSequenceNumber();
        outPoint = origin + d * hit.mFraction;
        return true;
    }

    std::vector<PhysicsWorld::ContactEvent> PhysicsWorld::ConsumeContactEvents(){
        return m_Impl->contactListener.Consume();
    }

    void PhysicsWorld::OptimizeBroadPhase(){
        m_Impl->physicsSystem.OptimizeBroadPhase();
    }

    void PhysicsWorld::Step(float dt){
        if (dt <= 0.0f) { return; }
        const int collisionSteps = 1;
        m_Impl->physicsSystem.Update(dt, collisionSteps, &m_Impl->tempAllocator, &m_Impl->jobSystem);
    }

    void PhysicsWorld::GetBodyTransform(uint32_t bodyID, glm::vec3& outPosition, glm::vec3& outRotationEuler) const{
        if (bodyID == InvalidBody) { return; }
        JPH::BodyID id(bodyID);
        JPH::BodyInterface& bodyInterface = m_Impl->physicsSystem.GetBodyInterface();
        JPH::RVec3 pos = bodyInterface.GetPosition(id);
        JPH::Quat rot = bodyInterface.GetRotation(id);

        outPosition = glm::vec3((float)pos.GetX(), (float)pos.GetY(), (float)pos.GetZ());
        glm::quat q(rot.GetW(), rot.GetX(), rot.GetY(), rot.GetZ());
        outRotationEuler = glm::eulerAngles(q);
    }
}
