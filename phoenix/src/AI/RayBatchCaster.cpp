#include <Phoenix/AI/RayBatchCaster.h>
#include <Phoenix/Compute/Compute.h>
#include "../Compute/ComputeCL.h"
#include "../Compute/SharedSource.h"
#include <algorithm>
#include <cstring>

namespace Phoenix{

    // Shapes are packed as SHAPE_FLOATS floats: rows 0-2 of the inverse transform
    // (enough for the xyz of a transformed point/vector), the size, then the type and
    // owner as int bits. The CPU path reads the same records as the kernel.
    static constexpr int SHAPE_FLOATS = 18;
    // Rays: origin xyz, delta xyz, then ignore and group index as int bits.
    static constexpr int RAY_FLOATS = 8;

    // The per-shape ray tests, shared with the kernel (see SharedSource.h). The
    // Box/Sphere/Capsule/Cylinder tests follow Jolt's RayAABox, RaySphere, RayCapsule
    // and RayCylinder, so mirrored bodies hit like Jolt's.
    PHX_SHARED_SOURCE(s_SharedSource,
    static int phx_find_root(float a, float b, float c, float* x1, float* x2){
        if (a == 0.0f){
            if (b == 0.0f) { return 0; }
            *x1 = -c / b;
            *x2 = *x1;
            return 1;
        }
        float det = b * b - 4.0f * a * c;
        if (det < 0.0f) { return 0; }
        float q = (b + (b < 0.0f ? -1.0f : 1.0f) * phx_sqrt(det)) / -2.0f;
        *x1 = q / a;
        if (q == 0.0f) { *x2 = *x1; return 1; }
        *x2 = c / q;
        return 2;
    }

    static float phx_ray_sphere(float ox, float oy, float oz, float dx, float dy, float dz, float centerY, float radius){
        float py = oy - centerY;
        float a = dx * dx + dy * dy + dz * dz;
        float b = 2.0f * (dx * ox + dy * py + dz * oz);
        float c = (ox * ox + py * py + oz * oz) - radius * radius;
        float f1, f2;
        if (phx_find_root(a, b, c, &f1, &f2) == 0) { return c <= 0.0f ? 0.0f : FLT_MAX; }
        if (f1 > f2) { float s = f1; f1 = f2; f2 = s; }
        if (f1 >= 0.0f) { return f1; }
        if (f2 >= 0.0f) { return 0.0f; }
        return FLT_MAX;
    }

    static float phx_ray_infinite_cylinder(float ox, float oz, float dx, float dz, float radius){
        float lengthSq = ox * ox + oz * oz;
        float rSq = radius * radius;
        if (lengthSq <= rSq) { return 0.0f; }
        float a = dx * dx + dz * dz;
        float b = 2.0f * (ox * dx + oz * dz);
        float c = lengthSq - rSq;
        float f1, f2;
        if (phx_find_root(a, b, c, &f1, &f2) == 0) { return FLT_MAX; }
        float f = f1 < f2 ? f1 : f2;
        return f >= 0.0f ? f : FLT_MAX;
    }

    static float phx_ray_cylinder(float ox, float oy, float oz, float dx, float dy, float dz, float halfHeight, float radius){
        float f = phx_ray_infinite_cylinder(ox, oz, dx, dz, radius);
        if (f == FLT_MAX) { return FLT_MAX; }
        if (phx_abs(oy + f * dy) <= halfHeight) { return f; }
        if (dy != 0.0f){
            float plane = dy < 0.0f ? (halfHeight - oy) / dy : -(halfHeight + oy) / dy;
            if (plane >= 0.0f){
                float px = ox + plane * dx, pz = oz + plane * dz;
                if (px * px + pz * pz <= radius * radius) { return plane; }
            }
        }
        return FLT_MAX;
    }

    static float phx_ray_capsule(float ox, float oy, float oz, float dx, float dy, float dz, float halfHeight, float radius){
        float f = phx_ray_infinite_cylinder(ox, oz, dx, dz, radius);
        if (f == FLT_MAX) { return FLT_MAX; }
        if (phx_abs(oy + f * dy) <= halfHeight) { return f; }
        float upper = phx_ray_sphere(ox, oy, oz, dx, dy, dz, halfHeight, radius);
        float lower = phx_ray_sphere(ox, oy, oz, dx, dy, dz, -halfHeight, radius);
        return upper < lower ? upper : lower;
    }

    static float phx_ray_box(const float* o, const float* d, const float* h){
        float tMin = -FLT_MAX, tMax = FLT_MAX;
        for (int i = 0; i < 3; i++){
            if (phx_abs(d[i]) <= 1.0e-20f){
                if (o[i] < -h[i] || o[i] > h[i]) { return FLT_MAX; }
                continue;
            }
            float inv = 1.0f / d[i];
            float t1 = (-h[i] - o[i]) * inv;
            float t2 = (h[i] - o[i]) * inv;
            float lo = t1 < t2 ? t1 : t2, hi = t1 < t2 ? t2 : t1;
            tMin = tMin < lo ? lo : tMin;
            tMax = hi < tMax ? hi : tMax;
        }
        if (tMin > tMax || tMax < 0.0f) { return FLT_MAX; }
        return tMin > 0.0f ? tMin : 0.0f;
    }

    static float phx_ray_zone(const float* o, const float* d, const float* h){
        float tMin = 0.0f, tMax = 1.0f;
        for (int i = 0; i < 3; i++){
            if (phx_abs(d[i]) < 1e-8f){
                if (o[i] < -h[i] || o[i] > h[i]) { return FLT_MAX; }
                continue;
            }
            float t1 = (-h[i] - o[i]) / d[i];
            float t2 = ( h[i] - o[i]) / d[i];
            if (t1 > t2) { float s = t1; t1 = t2; t2 = s; }
            tMin = tMin < t1 ? t1 : tMin;
            tMax = t2 < tMax ? t2 : tMax;
            if (tMin > tMax) { return FLT_MAX; }
        }
        return tMin;
    }

    static float phx_cast_shape(const float* m, int type, const float* origin, const float* delta){
        float o[3], d[3];
        for (int i = 0; i < 3; i++){
            const float* r = m + i * 4;
            o[i] = (r[0] * origin[0] + r[1] * origin[1]) + (r[2] * origin[2] + r[3] * 1.0f);
            d[i] = (r[0] * delta[0] + r[1] * delta[1]) + (r[2] * delta[2] + r[3] * 0.0f);
        }
        const float* size = m + 12;
        switch (type){
            case 0: return phx_ray_zone(o, d, size);
            case 1: return phx_ray_box(o, d, size);
            case 2: return phx_ray_sphere(o[0], o[1], o[2], d[0], d[1], d[2], 0.0f, size[0]);
            case 3: return phx_ray_capsule(o[0], o[1], o[2], d[0], d[1], d[2], size[0], size[1]);
            case 4: return phx_ray_cylinder(o[0], o[1], o[2], d[0], d[1], d[2], size[0], size[1]);
        }
        return FLT_MAX;
    }
    )

    static void PackShapes(const std::vector<RayBatchCaster::Shape>& shapes, std::vector<float>& out){
        out.resize(shapes.size() * SHAPE_FLOATS);
        for (size_t s = 0; s < shapes.size(); s++){
            float* dst = &out[s * SHAPE_FLOATS];
            const glm::mat4& m = shapes[s].inverse;
            for (int row = 0; row < 3; row++)
                for (int col = 0; col < 4; col++) { dst[row * 4 + col] = m[col][row]; }
            dst[12] = shapes[s].size.x;
            dst[13] = shapes[s].size.y;
            dst[14] = shapes[s].size.z;
            int type = (int)shapes[s].type, owner = shapes[s].owner;
            std::memcpy(&dst[15], &type, sizeof(int));
            std::memcpy(&dst[16], &owner, sizeof(int));
            dst[17] = 0.0f;
        }
    }

    // A cast packed for either backend: shapes and rays as the kernel reads them, and
    // per group the candidate shapes that survived culling (ascending, so ties resolve
    // as without culling).
    struct RayBatchCaster::Batch{
        std::vector<float> shapes, rays;
        std::vector<int> candidates;   // shape indices, group after group
        std::vector<int> ranges;       // per group: first candidate, count
        size_t tests = 0;              // ray-shape pairs left after culling

        // Culling grid over the XZ plane: shape indices per cell (CSR), plus the shapes
        // that are too big for it (or never culled), which every group checks.
        std::vector<int> cellStart, cellShapes, global;
        std::vector<uint32_t> stamp;   // last group that looked at each shape
        std::vector<glm::vec3> groupMin, groupMax;
    };

    // Whether a shape's bounding sphere reaches the box, with a little slack so
    // rounding never culls a real hit.
    static bool SphereTouchesBox(const RayBatchCaster::Shape& shape, const glm::vec3& lo, const glm::vec3& hi){
        if (shape.radius >= 1.0e30f) { return true; }
        glm::vec3 d = shape.center - glm::clamp(shape.center, lo, hi);
        float reach = shape.radius * 1.001f + 1.0e-3f;
        return glm::dot(d, d) <= reach * reach;
    }

    void RayBatchCaster::Prepare(const std::vector<Shape>& shapes, const std::vector<Ray>& rays, const std::vector<Group>& groups_){
        if (!m_Batch) { m_Batch = std::make_unique<Batch>(); }
        Batch& b = *m_Batch;
        PackShapes(shapes, b.shapes);
        std::vector<Group> all;
        if (groups_.empty()) { all.push_back({ 0, (int)rays.size() }); }
        const std::vector<Group>& groups = groups_.empty() ? all : groups_;

        // Pack the rays and box each group.
        b.rays.assign(rays.size() * RAY_FLOATS, 0.0f);
        b.groupMin.assign(groups.size(), glm::vec3(3.0e38f));
        b.groupMax.assign(groups.size(), glm::vec3(-3.0e38f));
        glm::vec2 extent(0.0f);
        for (size_t g = 0; g < groups.size(); g++){
            for (int r = groups[g].firstRay; r < groups[g].firstRay + groups[g].rayCount; r++){
                const Ray& ray = rays[(size_t)r];
                b.groupMin[g] = glm::min(b.groupMin[g], glm::min(ray.origin, ray.origin + ray.delta));
                b.groupMax[g] = glm::max(b.groupMax[g], glm::max(ray.origin, ray.origin + ray.delta));
                float* dst = &b.rays[(size_t)r * RAY_FLOATS];
                dst[0] = ray.origin.x; dst[1] = ray.origin.y; dst[2] = ray.origin.z;
                dst[3] = ray.delta.x;  dst[4] = ray.delta.y;  dst[5] = ray.delta.z;
                int groupIndex = (int)g;
                std::memcpy(&dst[6], &ray.ignore, sizeof(int));
                std::memcpy(&dst[7], &groupIndex, sizeof(int));
            }
            if (groups[g].rayCount > 0){
                glm::vec3 size = b.groupMax[g] - b.groupMin[g];
                extent += glm::vec2(size.x, size.z);
            }
        }

        // Grid cells about the size of a group, over the cullable shapes' spheres.
        const float cell = std::max(1.0f, std::max(extent.x, extent.y) / (float)std::max<size_t>(1, groups.size()));
        glm::vec2 lo(3.0e38f), hi(-3.0e38f);
        for (const Shape& shape : shapes){
            if (shape.radius >= 1.0e30f) { continue; }
            lo = glm::min(lo, glm::vec2(shape.center.x, shape.center.z) - shape.radius);
            hi = glm::max(hi, glm::vec2(shape.center.x, shape.center.z) + shape.radius);
        }
        const int nx = lo.x <= hi.x ? std::min(1024, (int)((hi.x - lo.x) / cell) + 1) : 1;
        const int nz = lo.x <= hi.x ? std::min(1024, (int)((hi.y - lo.y) / cell) + 1) : 1;
        auto cellX = [&](float x){ return std::clamp((int)std::floor((x - lo.x) / cell), 0, nx - 1); };
        auto cellZ = [&](float z){ return std::clamp((int)std::floor((z - lo.y) / cell), 0, nz - 1); };
        // A shape over more cells than this is cheaper to check from every group.
        const int maxCells = 16;
        b.global.clear();
        b.cellStart.assign((size_t)nx * nz + 1, 0);
        for (int pass = 0; pass < 2; pass++){
            for (size_t i = 0; i < shapes.size(); i++){
                const Shape& shape = shapes[i];
                if (shape.radius >= 1.0e30f){ if (pass == 0) { b.global.push_back((int)i); } continue; }
                int x0 = cellX(shape.center.x - shape.radius), x1 = cellX(shape.center.x + shape.radius);
                int z0 = cellZ(shape.center.z - shape.radius), z1 = cellZ(shape.center.z + shape.radius);
                if ((x1 - x0 + 1) * (z1 - z0 + 1) > maxCells){ if (pass == 0) { b.global.push_back((int)i); } continue; }
                for (int z = z0; z <= z1; z++)
                    for (int x = x0; x <= x1; x++){
                        size_t c = (size_t)z * nx + x;
                        if (pass == 0) { b.cellStart[c + 1]++; }
                        else { b.cellShapes[(size_t)b.cellStart[c] + (size_t)b.stamp[c]++] = (int)i; }
                    }
            }
            if (pass == 0){
                for (size_t c = 0; c + 1 < b.cellStart.size(); c++) { b.cellStart[c + 1] += b.cellStart[c]; }
                b.cellShapes.resize((size_t)b.cellStart.back());
                b.stamp.assign((size_t)nx * nz, 0);   // fill cursor per cell
            }
        }

        // Candidates per group: grid cells under its box plus the global list, each
        // shape once, sphere-tested, in ascending order.
        b.stamp.assign(shapes.size(), 0);
        b.candidates.clear();
        b.ranges.clear();
        b.tests = 0;
        for (size_t g = 0; g < groups.size(); g++){
            const int first = (int)b.candidates.size();
            const uint32_t mark = (uint32_t)g + 1;
            auto consider = [&](int i){
                if (b.stamp[(size_t)i] == mark) { return; }
                b.stamp[(size_t)i] = mark;
                if (SphereTouchesBox(shapes[(size_t)i], b.groupMin[g], b.groupMax[g])) { b.candidates.push_back(i); }
            };
            if (groups[g].rayCount > 0){
                for (int i : b.global) { consider(i); }
                for (int z = cellZ(b.groupMin[g].z); z <= cellZ(b.groupMax[g].z); z++)
                    for (int x = cellX(b.groupMin[g].x); x <= cellX(b.groupMax[g].x); x++){
                        size_t c = (size_t)z * nx + x;
                        for (int k = b.cellStart[c]; k < b.cellStart[c + 1]; k++) { consider(b.cellShapes[(size_t)k]); }
                    }
                std::sort(b.candidates.begin() + first, b.candidates.end());
            }
            b.ranges.push_back(first);
            b.ranges.push_back((int)b.candidates.size() - first);
            b.tests += (size_t)groups[g].rayCount * (size_t)((int)b.candidates.size() - first);
        }
        m_LastTests = b.tests;
    }

    void RayBatchCaster::CastCPU(const std::vector<Shape>& shapes, const std::vector<Ray>& rays, const std::vector<Group>& groups,
                                 float maxT, std::vector<Hit>& out){
        Prepare(shapes, rays, groups);
        RunCPU(rays.size(), maxT, out);
    }

    void RayBatchCaster::RunCPU(size_t rayCount, float maxT, std::vector<Hit>& out){
        const Batch& b = *m_Batch;
        out.assign(rayCount, Hit{ maxT, -1 });
        for (size_t r = 0; r < rayCount; r++){
            const float* ray = &b.rays[r * RAY_FLOATS];
            int ignore, group;
            std::memcpy(&ignore, &ray[6], sizeof(int));
            std::memcpy(&group, &ray[7], sizeof(int));
            Hit& hit = out[r];
            const int first = b.ranges[(size_t)group * 2], count = b.ranges[(size_t)group * 2 + 1];
            for (int c = first; c < first + count; c++){
                const float* shape = &b.shapes[(size_t)b.candidates[(size_t)c] * SHAPE_FLOATS];
                int type, owner;
                std::memcpy(&type, &shape[15], sizeof(int));
                std::memcpy(&owner, &shape[16], sizeof(int));
                if (owner == ignore) { continue; }
                float t = phx_cast_shape(shape, type, &ray[0], &ray[3]);
                if (t < hit.t){
                    hit.t = t;
                    hit.shape = b.candidates[(size_t)c];
                }
            }
        }
    }

#ifdef PHOENIX_OPENCL
    // One work-item per ray, looping over its group's candidate shapes (built after
    // s_SharedSource).
    static const char* s_KernelMain = R"CLC(
__kernel void ray_batch(__global const float* shapes, __global const int* candidates,
                        __global const int* ranges, __global const float* rays, int rayCount,
                        float maxT, __global float* outT, __global int* outShape){
    int r = get_global_id(0);
    if (r >= rayCount) { return; }
    __global const float* ray = rays + r * 8;
    float origin[3] = { ray[0], ray[1], ray[2] };
    float delta[3] = { ray[3], ray[4], ray[5] };
    int ignore = as_int(ray[6]);
    int group = as_int(ray[7]);
    int first = ranges[group * 2], count = ranges[group * 2 + 1];

    float best = maxT;
    int bestShape = -1;
    float m[15];
    for (int c = first; c < first + count; c++){
        int s = candidates[c];
        __global const float* g = shapes + s * 18;
        if (as_int(g[16]) == ignore) { continue; }
        for (int i = 0; i < 15; i++) { m[i] = g[i]; }
        float t = phx_cast_shape(m, as_int(g[15]), origin, delta);
        if (t < best){ best = t; bestShape = s; }
    }
    outT[r] = best;
    outShape[r] = bestShape;
}
)CLC";

    struct RayBatchCaster::Device{
        cl_program program = nullptr;
        cl_kernel kernel = nullptr;
        cl_mem shapes = nullptr, candidates = nullptr, ranges = nullptr, rays = nullptr, outT = nullptr, outShape = nullptr;
        size_t shapeCapacity = 0, candidateCapacity = 0, rangeCapacity = 0, rayCapacity = 0, tCapacity = 0, shapeOutCapacity = 0;
        std::vector<float> tData;
        std::vector<int> shapeIndex;

        ~Device(){
            for (cl_mem m : { shapes, candidates, ranges, rays, outT, outShape }) { if (m) { clReleaseMemObject(m); } }
            if (kernel) { clReleaseKernel(kernel); }
            if (program) { clReleaseProgram(program); }
        }

        bool Init(std::string& status){
            program = Compute::BuildProgram({ s_SharedSource, s_KernelMain }, status);
            if (!program) { return false; }
            cl_int err;
            kernel = clCreateKernel(program, "ray_batch", &err);
            if (err != CL_SUCCESS) { status = "clCreateKernel failed (" + std::to_string(err) + ")"; return false; }
            status = Compute::Status();
            return true;
        }

        bool Cast(const Batch& b, size_t rayCount, float maxT, std::vector<Hit>& out){
            using Compute::Reserve;
            using Compute::Write;
            if (!Reserve(shapes, shapeCapacity, b.shapes.size(), sizeof(float), CL_MEM_READ_ONLY)
                || !Reserve(candidates, candidateCapacity, b.candidates.size(), sizeof(int), CL_MEM_READ_ONLY)
                || !Reserve(ranges, rangeCapacity, b.ranges.size(), sizeof(int), CL_MEM_READ_ONLY)
                || !Reserve(rays, rayCapacity, b.rays.size(), sizeof(float), CL_MEM_READ_ONLY)
                || !Reserve(outT, tCapacity, rayCount, sizeof(float), CL_MEM_WRITE_ONLY)
                || !Reserve(outShape, shapeOutCapacity, rayCount, sizeof(int), CL_MEM_WRITE_ONLY)) { return false; }

            // In-order queue: the non-blocking writes finish before the kernel runs.
            cl_command_queue queue = Compute::GetDevice()->queue;
            cl_int err = Write(shapes, b.shapes.data(), b.shapes.size() * sizeof(float));
            err |= Write(candidates, b.candidates.data(), b.candidates.size() * sizeof(int));
            err |= Write(ranges, b.ranges.data(), b.ranges.size() * sizeof(int));
            err |= Write(rays, b.rays.data(), b.rays.size() * sizeof(float));
            cl_int rc = (cl_int)rayCount;
            err |= clSetKernelArg(kernel, 0, sizeof(cl_mem), &shapes);
            err |= clSetKernelArg(kernel, 1, sizeof(cl_mem), &candidates);
            err |= clSetKernelArg(kernel, 2, sizeof(cl_mem), &ranges);
            err |= clSetKernelArg(kernel, 3, sizeof(cl_mem), &rays);
            err |= clSetKernelArg(kernel, 4, sizeof(cl_int), &rc);
            err |= clSetKernelArg(kernel, 5, sizeof(float), &maxT);
            err |= clSetKernelArg(kernel, 6, sizeof(cl_mem), &outT);
            err |= clSetKernelArg(kernel, 7, sizeof(cl_mem), &outShape);
            err |= Compute::Launch(kernel, rayCount);
            tData.resize(rayCount);
            shapeIndex.resize(rayCount);
            err |= clEnqueueReadBuffer(queue, outT, CL_FALSE, 0, rayCount * sizeof(float), tData.data(), 0, nullptr, nullptr);
            err |= clEnqueueReadBuffer(queue, outShape, CL_TRUE, 0, rayCount * sizeof(int), shapeIndex.data(), 0, nullptr, nullptr);
            if (err != CL_SUCCESS) { return false; }

            out.resize(rayCount);
            for (size_t r = 0; r < rayCount; r++) { out[r] = { tData[r], shapeIndex[r] }; }
            return true;
        }
    };
#else
    struct RayBatchCaster::Device{};
#endif

    RayBatchCaster::RayBatchCaster()
        : m_Status("OpenCL off") {}
    RayBatchCaster::~RayBatchCaster() = default;

    bool RayBatchCaster::OpenCLCompiled() { return Compute::Compiled(); }

    bool RayBatchCaster::UsingOpenCL() const { return m_Device != nullptr; }

    bool RayBatchCaster::UseOpenCL(bool enable){
        if (!enable) { m_Device.reset(); m_Status = "OpenCL off"; return false; }
        if (m_Device) { return true; }
#ifdef PHOENIX_OPENCL
        auto device = std::make_unique<Device>();
        if (!device->Init(m_Status)) { return false; }
        m_Device = std::move(device);
        return true;
#else
        m_Status = Compute::Status();
        return false;
#endif
    }

    void RayBatchCaster::Cast(const std::vector<Shape>& shapes, const std::vector<Ray>& rays, const std::vector<Group>& groups,
                              float maxT, std::vector<Hit>& out){
        m_LastOnDevice = false;
        Prepare(shapes, rays, groups);
#ifdef PHOENIX_OPENCL
        if (m_Device && !shapes.empty() && !rays.empty() && m_Batch->tests >= minDeviceWork){
            if (m_Device->Cast(*m_Batch, rays.size(), maxT, out)) { m_LastOnDevice = true; return; }
            // A failed launch (device lost, out of memory) drops back to the CPU for good.
            m_Device.reset();
            m_Status = "OpenCL launch failed; using the CPU";
        }
#endif
        RunCPU(rays.size(), maxT, out);
    }
}
