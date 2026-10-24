// Checks the batch ray caster: OpenCL against the CPU (must match bit for bit), and
// physics bodies mirrored from Jolt against Jolt's own RayCast. Prints timings.
//   ray_batch_bench [arenas=16] [raysPerArena=64] [bodiesPerArena=24] [iterations=20]
// Each arena holds random boxes, spheres, capsules, cylinders, a compound with an
// offset centre of mass and a convex hull (left to Jolt), plus Reward Zones.
#include <Phoenix/AI/RayBatchCaster.h>
#include <Phoenix/Physics/Physics.h>
#include <Phoenix/core/log.h>
#include <glm/gtc/matrix_transform.hpp>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>

using namespace Phoenix;
using Clock = std::chrono::steady_clock;

// As in the Environment: only rays through a non-mirrored body's bounds go to Jolt.
static bool SegmentTouchesBounds(const glm::vec3& origin, const glm::vec3& delta, const glm::vec3& lo, const glm::vec3& hi){
    float tMin = 0.0f, tMax = 1.0f;
    for (int i = 0; i < 3; i++){
        const float a = lo[i] - 1e-3f, b = hi[i] + 1e-3f;
        if (std::abs(delta[i]) < 1e-12f){
            if (origin[i] < a || origin[i] > b) { return false; }
            continue;
        }
        float t1 = (a - origin[i]) / delta[i], t2 = (b - origin[i]) / delta[i];
        if (t1 > t2) { std::swap(t1, t2); }
        tMin = std::max(tMin, t1);
        tMax = std::min(tMax, t2);
        if (tMin > tMax) { return false; }
    }
    return true;
}

static double Microseconds(Clock::time_point start){
    return std::chrono::duration<double, std::micro>(Clock::now() - start).count();
}

int main(int argc, char** argv){
    const int arenas = argc > 1 ? std::atoi(argv[1]) : 16;
    const int raysPerArena = argc > 2 ? std::atoi(argv[2]) : 64;
    const int bodiesPerArena = argc > 3 ? std::atoi(argv[3]) : 24;
    const int iterations = argc > 4 ? std::atoi(argv[4]) : 20;
    Log::Init();

    std::mt19937 rng(7);
    auto uniform = [&](float a, float b){ return std::uniform_real_distribution<float>(a, b)(rng); };
    const int side = (int)std::ceil(std::sqrt((float)arenas));
    const float spacing = 20.0f;
    auto arenaCenter = [&](int a){ return glm::vec3((a % side) * spacing, 0.0f, (a / side) * spacing); };

    PhysicsWorld world;
    using ShapeType = PhysicsWorld::ShapeDesc::Type;
    std::vector<RayBatchCaster::Shape> zones;
    for (int a = 0; a < arenas; a++){
        const glm::vec3 c = arenaCenter(a);
        for (int b = 0; b < bodiesPerArena; b++){
            PhysicsWorld::ShapeDesc shape;
            PhysicsWorld::BodyProps props;
            std::vector<PhysicsWorld::ShapeDesc> shapes;
            switch (b % 6){
                case 0: shape.type = ShapeType::Box; shape.halfExtents = { uniform(0.2f, 1.5f), uniform(0.2f, 1.5f), uniform(0.2f, 1.5f) }; break;
                case 1: shape.type = ShapeType::Sphere; shape.radius = uniform(0.2f, 1.2f); break;
                case 2: shape.type = ShapeType::Capsule; shape.radius = uniform(0.2f, 0.8f); shape.halfHeight = uniform(0.2f, 1.0f); break;
                case 3: shape.type = ShapeType::Cylinder; shape.radius = uniform(0.2f, 1.0f); shape.halfHeight = uniform(0.2f, 1.0f); break;
                case 4:{
                    // Two offset boxes and a shifted centre of mass: a compound in Jolt.
                    PhysicsWorld::ShapeDesc second;
                    shape.type = second.type = ShapeType::Box;
                    shape.halfExtents = { 0.6f, 0.3f, 0.3f };
                    shape.offset = { -0.5f, 0.0f, 0.0f };
                    second.halfExtents = { 0.3f, 0.6f, 0.3f };
                    second.offset = { 0.6f, 0.2f, 0.0f };
                    second.rotation = { 0.0f, 0.0f, 0.4f };
                    shapes.push_back(second);
                    props.centerOfMassOffset = { 0.1f, -0.2f, 0.05f };
                    break;
                }
                case 5:
                    shape.type = ShapeType::ConvexHull;
                    for (int p = 0; p < 12; p++) { shape.points.push_back({ uniform(-0.8f, 0.8f), uniform(-0.8f, 0.8f), uniform(-0.8f, 0.8f) }); }
                    break;
            }
            shapes.push_back(shape);
            glm::vec3 position = c + glm::vec3(uniform(-8.0f, 8.0f), uniform(-0.3f, 0.6f), uniform(-8.0f, 8.0f));
            glm::vec3 rotation(uniform(-0.5f, 0.5f), uniform(0.0f, 6.283f), uniform(-0.5f, 0.5f));
            auto type = (b % 2) ? PhysicsWorld::BodyType::Dynamic : PhysicsWorld::BodyType::Static;
            world.CreateBody(shapes, position, rotation, type, props);
        }
        for (int z = 0; z < 4; z++){
            glm::mat4 model = glm::translate(glm::mat4(1.0f), c + glm::vec3(uniform(-7.0f, 7.0f), 0.0f, uniform(-7.0f, 7.0f)));
            model = glm::rotate(model, uniform(0.0f, 6.283f), glm::vec3(0, 1, 0));
            zones.push_back({ RayBatchCaster::Shape::Type::Zone, glm::inverse(model), glm::vec3(uniform(0.5f, 2.0f), 1.0f, uniform(0.5f, 2.0f)), 1000000 + (int)zones.size() });
        }
    }
    world.OptimizeBroadPhase();

    // Agent-like sensors: fans of 16 rays over 140 degrees from random points, slightly
    // tilted. Each fan is one culling group.
    struct SensorRay{ glm::vec3 origin, dir; };
    std::vector<SensorRay> sensorRays;
    const float length = 12.0f;
    const int fan = 16;
    std::vector<RayBatchCaster::Ray> rays;
    std::vector<RayBatchCaster::Group> groups;
    glm::vec3 boundsMin(std::numeric_limits<float>::max()), boundsMax(-std::numeric_limits<float>::max());
    for (int a = 0; a < arenas; a++){
        for (int f = 0; f < std::max(1, raysPerArena / fan); f++){
            glm::vec3 origin = arenaCenter(a) + glm::vec3(uniform(-8.0f, 8.0f), uniform(0.0f, 0.8f), uniform(-8.0f, 8.0f));
            float yaw = uniform(0.0f, 6.283f), pitch = uniform(-0.15f, 0.15f);
            groups.push_back({ (int)rays.size(), fan });
            for (int r = 0; r < fan; r++){
                float angle = yaw + 1.22f - 2.44f * (float)r / (float)(fan - 1);
                glm::vec3 dir(-std::sin(angle) * std::cos(pitch), std::sin(pitch), -std::cos(angle) * std::cos(pitch));
                sensorRays.push_back({ origin, dir });
                rays.push_back({ origin, dir * length, -1 });
                boundsMin = glm::min(boundsMin, glm::min(origin, origin + dir * length));
                boundsMax = glm::max(boundsMax, glm::max(origin, origin + dir * length));
            }
        }
    }

    RayBatchCaster caster;
    caster.minDeviceWork = 0;
    if (!caster.UseOpenCL(true)){
        std::printf("OpenCL unavailable: %s\n", caster.Status().c_str());
        return 1;
    }
    std::printf("device: %s\n%d rays, %d bodies, %d zones\n", caster.Status().c_str(),
        (int)rays.size(), arenas * bodiesPerArena, (int)zones.size());

    // ---- Mirror the bodies, then cast with Jolt for reference ----
    std::vector<PhysicsWorld::RayPrimitive> primitives;
    std::vector<PhysicsWorld::RayBounds> others;
    auto start = Clock::now();
    world.CollectRayPrimitives(boundsMin - glm::vec3(0.01f), boundsMax + glm::vec3(0.01f), PhysicsWorld::QueryFilter(), primitives, others);
    double collectUs = Microseconds(start);
    std::vector<RayBatchCaster::Shape> bodies;
    using PType = PhysicsWorld::RayPrimitive::Type;
    using SType = RayBatchCaster::Shape::Type;
    for (const auto& p : primitives){
        RayBatchCaster::Shape s;
        s.type = p.type == PType::Box ? SType::Box : p.type == PType::Sphere ? SType::Sphere
               : p.type == PType::Capsule ? SType::Capsule : SType::Cylinder;
        s.size = p.type == PType::Box ? p.halfExtents : p.type == PType::Sphere ? glm::vec3(p.radius, 0, 0) : glm::vec3(p.halfHeight, p.radius, 0);
        s.inverse = glm::inverse(glm::translate(glm::mat4(1.0f), p.position) * glm::mat4_cast(p.rotation));
        s.owner = (int)p.body;
        s.center = p.position;
        s.radius = p.type == PType::Box ? glm::length(p.halfExtents) : p.type == PType::Sphere ? p.radius
                 : p.type == PType::Capsule ? p.halfHeight + p.radius : std::sqrt(p.halfHeight * p.halfHeight + p.radius * p.radius);
        bodies.push_back(s);
    }
    for (auto& z : zones){
        glm::mat4 model = glm::inverse(z.inverse);
        z.center = glm::vec3(model[3]);
        z.radius = z.size.x * glm::length(glm::vec3(model[0])) + z.size.y * glm::length(glm::vec3(model[1])) + z.size.z * glm::length(glm::vec3(model[2]));
    }
    std::printf("mirrored: %d primitives, %d bodies left to Jolt (collect %.1f us)\n", (int)bodies.size(), (int)others.size(), collectUs);

    const float maxT = 1.0f + std::numeric_limits<float>::epsilon();
    std::vector<RayBatchCaster::Hit> cpu, gpu, cpuZones, gpuZones, unculled;
    const std::vector<RayBatchCaster::Group> noGroups;
    auto time = [&](auto&& run){
        run(); // warm-up (kernel and buffer setup)
        auto t0 = Clock::now();
        for (int i = 0; i < iterations; i++) { run(); }
        return Microseconds(t0) / iterations;
    };
    double cpuUs = time([&]{ caster.CastCPU(bodies, rays, groups, maxT, cpu); });
    double gpuUs = time([&]{ caster.Cast(bodies, rays, groups, maxT, gpu); });
    if (!caster.LastCastOnDevice()){ std::printf("OpenCL launch failed: %s\n", caster.Status().c_str()); return 1; }
    const size_t culledTests = caster.LastTests();
    double unculledUs = time([&]{ caster.Cast(bodies, rays, noGroups, maxT, unculled); });
    caster.CastCPU(zones, rays, groups, 1.0f, cpuZones);
    caster.Cast(zones, rays, groups, 1.0f, gpuZones);

    // Full merge as the Environment does it: mirrored hit, then Jolt for the others.
    std::vector<PhysicsWorld::Hit> joltHits(rays.size());
    std::vector<bool> joltHit(rays.size());
    double joltUs = time([&]{
        for (size_t r = 0; r < rays.size(); r++)
            joltHit[r] = world.RayCast(sensorRays[r].origin, sensorRays[r].dir, length, joltHits[r]);
    });
    // Jolt for the bodies that weren't mirrored: per fan, the ones whose bounds touch
    // it; per ray, only those it passes through.
    std::vector<std::vector<PhysicsWorld::RayBounds>> fanOthers(groups.size());
    auto filterFans = [&]{
        for (size_t g = 0; g < groups.size(); g++){
            glm::vec3 lo(3.0e38f), hi(-3.0e38f);
            for (int r = groups[g].firstRay; r < groups[g].firstRay + groups[g].rayCount; r++){
                lo = glm::min(lo, glm::min(rays[r].origin, rays[r].origin + rays[r].delta));
                hi = glm::max(hi, glm::max(rays[r].origin, rays[r].origin + rays[r].delta));
            }
            fanOthers[g].clear();
            for (const auto& o : others)
                if (glm::all(glm::lessThanEqual(o.boundsMin, hi)) && glm::all(glm::lessThanEqual(lo, o.boundsMax))) { fanOthers[g].push_back(o); }
        }
    };
    std::vector<uint32_t> rayOthers;
    PhysicsWorld::QueryFilter onlyOthers;
    onlyOthers.onlyBodies = &rayOthers;
    auto castOthers = [&](size_t r, PhysicsWorld::Hit& h){
        rayOthers.clear();
        for (const auto& o : fanOthers[r / fan])
            if (SegmentTouchesBounds(rays[r].origin, rays[r].delta, o.boundsMin, o.boundsMax)) { rayOthers.push_back(o.body); }
        return !rayOthers.empty() && world.RayCast(sensorRays[r].origin, sensorRays[r].dir, length, h, onlyOthers);
    };
    int joltCalls = 0;
    double fallbackUs = time([&]{
        PhysicsWorld::Hit h;
        filterFans();
        joltCalls = 0;
        for (size_t r = 0; r < rays.size(); r++) { castOthers(r, h); joltCalls += !rayOthers.empty(); }
    });

    int deviceMismatch = 0, zoneMismatch = 0;
    for (size_t r = 0; r < rays.size(); r++){
        if (cpu[r].shape != gpu[r].shape || cpu[r].t != gpu[r].t) { deviceMismatch++; }
        if (unculled[r].shape != gpu[r].shape || unculled[r].t != gpu[r].t) { deviceMismatch++; }
        if (cpuZones[r].shape != gpuZones[r].shape || cpuZones[r].t != gpuZones[r].t) { zoneMismatch++; }
    }

    int hits = 0, bodyMismatch = 0, nearTie = 0;
    float maxDiff = 0.0f;
    for (size_t r = 0; r < rays.size(); r++){
        float best = maxT;
        uint32_t body = PhysicsWorld::InvalidBody;
        if (gpu[r].shape >= 0) { best = gpu[r].t; body = (uint32_t)bodies[(size_t)gpu[r].shape].owner; }
        PhysicsWorld::Hit h;
        if (castOthers(r, h) && h.distance / length < best){
            best = h.distance / length;
            body = h.body;
        }
        uint32_t joltBody = joltHit[r] ? joltHits[r].body : PhysicsWorld::InvalidBody;
        float joltT = joltHit[r] ? joltHits[r].distance / length : maxT;
        hits += joltHit[r];
        if (body != joltBody){
            // Two bodies (or a grazing hit) within float noise of each other is a tie, not an error.
            if (std::abs(best - joltT) < 1e-4f) { nearTie++; } else { bodyMismatch++; }
            continue;
        }
        if (body != PhysicsWorld::InvalidBody) { maxDiff = std::max(maxDiff, std::abs(best - joltT)); }
    }

    std::printf("OpenCL vs CPU (and vs unculled): %d body-ray mismatches, %d zone-ray mismatches\n", deviceMismatch, zoneMismatch);
    std::printf("culling: %zu of %zu ray-shape tests left (%.1f per ray); unculled OpenCL: %.1f us\n",
        culledTests, rays.size() * bodies.size(), (double)culledTests / (double)rays.size(), unculledUs);
    std::printf("mirror vs Jolt: %d / %d rays hit; %d different body, %d near ties; max |dt| on the same body %g\n",
        hits, (int)rays.size(), bodyMismatch, nearTie, maxDiff);
    std::printf("Jolt: %.1f us   mirror CPU: %.1f us   mirror OpenCL: %.1f us   (+ Jolt for the %d other bodies, %d rays: %.1f us)\n",
        joltUs, cpuUs, gpuUs, (int)others.size(), joltCalls, fallbackUs);
    return (deviceMismatch == 0 && zoneMismatch == 0 && bodyMismatch == 0) ? 0 : 2;
}
