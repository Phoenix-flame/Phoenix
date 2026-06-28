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
#include <Jolt/Physics/Body/BodyCreationSettings.h>

#include <glm/gtc/quaternion.hpp>

#include <thread>
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
        }
    };

    PhysicsWorld::PhysicsWorld(){
        Init(); // idempotent lazy global init
        m_Impl = CreateScope<Impl>();
    }

    PhysicsWorld::~PhysicsWorld() = default;

    uint32_t PhysicsWorld::CreateBox(const glm::vec3& position, const glm::vec3& rotationEuler,
                                     const glm::vec3& halfExtents, BodyType type){
        using namespace JPH;
        BodyInterface& bodyInterface = m_Impl->physicsSystem.GetBodyInterface();

        BoxShapeSettings shapeSettings(Vec3(halfExtents.x, halfExtents.y, halfExtents.z));
        shapeSettings.SetEmbedded();
        ShapeSettings::ShapeResult shapeResult = shapeSettings.Create();
        if (shapeResult.HasError()){
            PHX_CORE_ERROR("[Jolt] box shape error: {0}", shapeResult.GetError().c_str());
            return InvalidBody;
        }
        ShapeRefC shape = shapeResult.Get();

        glm::quat q = glm::quat(rotationEuler); // euler (radians) -> quaternion

        EMotionType motion = (type == BodyType::Static)    ? EMotionType::Static
                           : (type == BodyType::Kinematic) ? EMotionType::Kinematic
                                                           : EMotionType::Dynamic;
        ObjectLayer layer = (type == BodyType::Static) ? Layers::NON_MOVING : Layers::MOVING;

        BodyCreationSettings settings(shape,
            RVec3(position.x, position.y, position.z),
            Quat(q.x, q.y, q.z, q.w),
            motion, layer);

        BodyID id = bodyInterface.CreateAndAddBody(settings,
            type == BodyType::Static ? EActivation::DontActivate : EActivation::Activate);
        return id.GetIndexAndSequenceNumber();
    }

    void PhysicsWorld::RemoveBody(uint32_t bodyID){
        if (bodyID == InvalidBody) { return; }
        JPH::BodyID id(bodyID);
        JPH::BodyInterface& bodyInterface = m_Impl->physicsSystem.GetBodyInterface();
        bodyInterface.RemoveBody(id);
        bodyInterface.DestroyBody(id);
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
