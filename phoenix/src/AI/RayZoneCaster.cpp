#include <Phoenix/AI/RayZoneCaster.h>
#include <algorithm>
#include <cmath>
#include <cstring>

#ifdef PHOENIX_OPENCL
#define CL_TARGET_OPENCL_VERSION 120
#ifdef __APPLE__
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>
#endif
#endif

namespace Phoenix{

    // Zones go to the device as 16 floats: rows 0-2 of the inverse transform (enough
    // for the xyz of a transformed point/vector), then the half extents and a pad.
    static constexpr int ZONE_FLOATS = 16;
    // Rays: origin xyz, delta xyz, ignoreZone (int bits), pad.
    static constexpr int RAY_FLOATS = 8;

    // Segment origin + t * delta (t in [0, 1]) against a zone's box, in its local space.
    // Mirrored by the OpenCL kernel below, operation for operation.
    static bool SegmentHitsBox(const glm::vec3& o, const glm::vec3& d, const glm::vec3& halfExtents, float& outT){
        float tMin = 0.0f, tMax = 1.0f;
        for (int i = 0; i < 3; i++){
            if (std::abs(d[i]) < 1e-8f){
                if (o[i] < -halfExtents[i] || o[i] > halfExtents[i]) { return false; }
                continue;
            }
            float t1 = (-halfExtents[i] - o[i]) / d[i];
            float t2 = ( halfExtents[i] - o[i]) / d[i];
            if (t1 > t2) { std::swap(t1, t2); }
            tMin = std::max(tMin, t1);
            tMax = std::min(tMax, t2);
            if (tMin > tMax) { return false; }
        }
        outT = tMin;
        return true;
    }

    void RayZoneCaster::CastCPU(const std::vector<Zone>& zones, const std::vector<Ray>& rays, std::vector<Hit>& out){
        out.assign(rays.size(), Hit{});
        for (size_t r = 0; r < rays.size(); r++){
            const Ray& ray = rays[r];
            Hit& hit = out[r];
            for (size_t z = 0; z < zones.size(); z++){
                if ((int)z == ray.ignoreZone) { continue; }
                const glm::mat4& inv = zones[z].inverse;
                glm::vec3 o = glm::vec3(inv * glm::vec4(ray.origin, 1.0f));
                glm::vec3 d = glm::vec3(inv * glm::vec4(ray.delta, 0.0f));
                float t;
                if (SegmentHitsBox(o, d, zones[z].halfExtents, t) && t < hit.t){
                    hit.t = t;
                    hit.zone = (int)z;
                }
            }
        }
    }

#ifdef PHOENIX_OPENCL
    // One work-item per ray, looping over all zones. Contraction into FMA is off and
    // the sums are grouped like glm's mat4 * vec4, so results match the CPU path
    // except where the device's division is not correctly rounded.
    static const char* s_KernelSource = R"CLC(
#pragma OPENCL FP_CONTRACT OFF

static float3 transform(__global const float* m, float3 v, float w){
    float3 r;
    r.x = (m[0] * v.x + m[1] * v.y) + (m[2]  * v.z + m[3]  * w);
    r.y = (m[4] * v.x + m[5] * v.y) + (m[6]  * v.z + m[7]  * w);
    r.z = (m[8] * v.x + m[9] * v.y) + (m[10] * v.z + m[11] * w);
    return r;
}

__kernel void ray_zones(__global const float* zones, int zoneCount,
                        __global const float* rays, int rayCount,
                        __global float* outT, __global int* outZone){
    int r = get_global_id(0);
    if (r >= rayCount) { return; }
    __global const float* ray = rays + r * 8;
    float3 origin = (float3)(ray[0], ray[1], ray[2]);
    float3 delta  = (float3)(ray[3], ray[4], ray[5]);
    int ignore = as_int(ray[6]);

    float best = 1.0f;
    int bestZone = -1;
    for (int z = 0; z < zoneCount; z++){
        if (z == ignore) { continue; }
        __global const float* m = zones + z * 16;
        float o[3], d[3], h[3] = { m[12], m[13], m[14] };
        float3 lo = transform(m, origin, 1.0f), ld = transform(m, delta, 0.0f);
        o[0] = lo.x; o[1] = lo.y; o[2] = lo.z;
        d[0] = ld.x; d[1] = ld.y; d[2] = ld.z;

        float tMin = 0.0f, tMax = 1.0f;
        bool hit = true;
        for (int i = 0; i < 3; i++){
            if (fabs(d[i]) < 1e-8f){
                if (o[i] < -h[i] || o[i] > h[i]) { hit = false; break; }
                continue;
            }
            float t1 = (-h[i] - o[i]) / d[i];
            float t2 = ( h[i] - o[i]) / d[i];
            if (t1 > t2) { float s = t1; t1 = t2; t2 = s; }
            tMin = (tMin < t1) ? t1 : tMin;   // std::max
            tMax = (t2 < tMax) ? t2 : tMax;   // std::min
            if (tMin > tMax) { hit = false; break; }
        }
        if (hit && tMin < best){ best = tMin; bestZone = z; }
    }
    outT[r] = best;
    outZone[r] = bestZone;
}
)CLC";

    struct RayZoneCaster::Device{
        cl_context context = nullptr;
        cl_command_queue queue = nullptr;
        cl_program program = nullptr;
        cl_kernel kernel = nullptr;
        cl_mem zones = nullptr, rays = nullptr, outT = nullptr, outZone = nullptr;
        size_t zoneCapacity = 0, rayCapacity = 0, tCapacity = 0, zoneOutCapacity = 0;
        std::vector<float> zoneData, rayData, tData;
        std::vector<int> zoneIndex;

        ~Device(){
            for (cl_mem m : { zones, rays, outT, outZone }) { if (m) { clReleaseMemObject(m); } }
            if (kernel) { clReleaseKernel(kernel); }
            if (program) { clReleaseProgram(program); }
            if (queue) { clReleaseCommandQueue(queue); }
            if (context) { clReleaseContext(context); }
        }

        // Grows a buffer to hold at least `count` elements (doubling, so it settles quickly).
        bool Reserve(cl_mem& buffer, size_t& capacity, size_t count, size_t elementSize, cl_mem_flags flags){
            if (buffer && capacity >= count) { return true; }
            size_t size = std::max<size_t>(count, std::max<size_t>(64, capacity * 2));
            if (buffer) { clReleaseMemObject(buffer); }
            cl_int err;
            buffer = clCreateBuffer(context, flags, size * elementSize, nullptr, &err);
            capacity = buffer ? size : 0;
            return err == CL_SUCCESS;
        }

        // Prefers a GPU, then any other device (a CPU runtime such as PoCL still works).
        bool Init(std::string& status){
            cl_uint platformCount = 0;
            if (clGetPlatformIDs(0, nullptr, &platformCount) != CL_SUCCESS || platformCount == 0){
                status = "no OpenCL platform found";
                return false;
            }
            std::vector<cl_platform_id> platforms(platformCount);
            clGetPlatformIDs(platformCount, platforms.data(), nullptr);
            cl_device_id device = nullptr;
            for (cl_device_type type : { (cl_device_type)CL_DEVICE_TYPE_GPU, (cl_device_type)CL_DEVICE_TYPE_ALL }){
                for (cl_platform_id p : platforms)
                    if (clGetDeviceIDs(p, type, 1, &device, nullptr) == CL_SUCCESS) { break; }
                if (device) { break; }
            }
            if (!device) { status = "no OpenCL device found"; return false; }

            char name[256] = {};
            clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(name) - 1, name, nullptr);
            cl_int err;
            context = clCreateContext(nullptr, 1, &device, nullptr, nullptr, &err);
            if (err != CL_SUCCESS) { status = "clCreateContext failed (" + std::to_string(err) + ")"; return false; }
            queue = clCreateCommandQueue(context, device, 0, &err);
            if (err != CL_SUCCESS) { status = "clCreateCommandQueue failed (" + std::to_string(err) + ")"; return false; }
            program = clCreateProgramWithSource(context, 1, &s_KernelSource, nullptr, &err);
            if (err != CL_SUCCESS) { status = "clCreateProgramWithSource failed (" + std::to_string(err) + ")"; return false; }

            // Correctly rounded division keeps hits identical to the CPU, where supported.
            cl_device_fp_config fp = 0;
            clGetDeviceInfo(device, CL_DEVICE_SINGLE_FP_CONFIG, sizeof(fp), &fp, nullptr);
            const char* options = (fp & CL_FP_CORRECTLY_ROUNDED_DIVIDE_SQRT) ? "-cl-fp32-correctly-rounded-divide-sqrt" : "";
            if (clBuildProgram(program, 1, &device, options, nullptr, nullptr) != CL_SUCCESS){
                size_t length = 0;
                clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &length);
                std::string log(length, '\0');
                clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, length, log.data(), nullptr);
                status = "kernel build failed: " + log;
                return false;
            }
            kernel = clCreateKernel(program, "ray_zones", &err);
            if (err != CL_SUCCESS) { status = "clCreateKernel failed (" + std::to_string(err) + ")"; return false; }
            status = name;
            return true;
        }

        bool Cast(const std::vector<Zone>& zones_, const std::vector<Ray>& rays_, std::vector<Hit>& out){
            const size_t zoneCount = zones_.size(), rayCount = rays_.size();
            zoneData.resize(zoneCount * ZONE_FLOATS);
            for (size_t z = 0; z < zoneCount; z++){
                float* dst = &zoneData[z * ZONE_FLOATS];
                const glm::mat4& m = zones_[z].inverse;
                for (int row = 0; row < 3; row++)
                    for (int col = 0; col < 4; col++) { dst[row * 4 + col] = m[col][row]; }
                dst[12] = zones_[z].halfExtents.x;
                dst[13] = zones_[z].halfExtents.y;
                dst[14] = zones_[z].halfExtents.z;
                dst[15] = 0.0f;
            }
            rayData.resize(rayCount * RAY_FLOATS);
            for (size_t r = 0; r < rayCount; r++){
                float* dst = &rayData[r * RAY_FLOATS];
                const Ray& ray = rays_[r];
                dst[0] = ray.origin.x; dst[1] = ray.origin.y; dst[2] = ray.origin.z;
                dst[3] = ray.delta.x;  dst[4] = ray.delta.y;  dst[5] = ray.delta.z;
                std::memcpy(&dst[6], &ray.ignoreZone, sizeof(int));
                dst[7] = 0.0f;
            }

            if (!Reserve(this->zones, zoneCapacity, zoneCount * ZONE_FLOATS, sizeof(float), CL_MEM_READ_ONLY)
                || !Reserve(this->rays, rayCapacity, rayCount * RAY_FLOATS, sizeof(float), CL_MEM_READ_ONLY)
                || !Reserve(outT, tCapacity, rayCount, sizeof(float), CL_MEM_WRITE_ONLY)
                || !Reserve(outZone, zoneOutCapacity, rayCount, sizeof(int), CL_MEM_WRITE_ONLY)) { return false; }

            // In-order queue: the non-blocking writes finish before the kernel runs.
            cl_int err = clEnqueueWriteBuffer(queue, this->zones, CL_FALSE, 0, zoneData.size() * sizeof(float), zoneData.data(), 0, nullptr, nullptr);
            err |= clEnqueueWriteBuffer(queue, this->rays, CL_FALSE, 0, rayData.size() * sizeof(float), rayData.data(), 0, nullptr, nullptr);
            cl_int zc = (cl_int)zoneCount, rc = (cl_int)rayCount;
            err |= clSetKernelArg(kernel, 0, sizeof(cl_mem), &this->zones);
            err |= clSetKernelArg(kernel, 1, sizeof(cl_int), &zc);
            err |= clSetKernelArg(kernel, 2, sizeof(cl_mem), &this->rays);
            err |= clSetKernelArg(kernel, 3, sizeof(cl_int), &rc);
            err |= clSetKernelArg(kernel, 4, sizeof(cl_mem), &outT);
            err |= clSetKernelArg(kernel, 5, sizeof(cl_mem), &outZone);
            const size_t local = 64, global = (rayCount + local - 1) / local * local;
            err |= clEnqueueNDRangeKernel(queue, kernel, 1, nullptr, &global, &local, 0, nullptr, nullptr);
            tData.resize(rayCount);
            zoneIndex.resize(rayCount);
            err |= clEnqueueReadBuffer(queue, outT, CL_FALSE, 0, rayCount * sizeof(float), tData.data(), 0, nullptr, nullptr);
            err |= clEnqueueReadBuffer(queue, outZone, CL_TRUE, 0, rayCount * sizeof(int), zoneIndex.data(), 0, nullptr, nullptr);
            if (err != CL_SUCCESS) { return false; }

            out.resize(rayCount);
            for (size_t r = 0; r < rayCount; r++) { out[r] = { tData[r], zoneIndex[r] }; }
            return true;
        }
    };
#else
    struct RayZoneCaster::Device{};
#endif

    RayZoneCaster::RayZoneCaster()
        : m_Status("OpenCL off") {}
    RayZoneCaster::~RayZoneCaster() = default;

    bool RayZoneCaster::OpenCLCompiled(){
#ifdef PHOENIX_OPENCL
        return true;
#else
        return false;
#endif
    }

    bool RayZoneCaster::UsingOpenCL() const { return m_Device != nullptr; }

    bool RayZoneCaster::UseOpenCL(bool enable){
        if (!enable) { m_Device.reset(); m_Status = "OpenCL off"; return false; }
        if (m_Device) { return true; }
#ifdef PHOENIX_OPENCL
        auto device = std::make_unique<Device>();
        if (!device->Init(m_Status)) { return false; }
        m_Device = std::move(device);
        return true;
#else
        m_Status = "engine built without OpenCL (configure with -DPHOENIX_OPENCL=ON)";
        return false;
#endif
    }

    void RayZoneCaster::Cast(const std::vector<Zone>& zones, const std::vector<Ray>& rays, std::vector<Hit>& out){
        m_LastOnDevice = false;
#ifdef PHOENIX_OPENCL
        if (m_Device && !zones.empty() && !rays.empty() && zones.size() * rays.size() >= minDeviceWork){
            if (m_Device->Cast(zones, rays, out)) { m_LastOnDevice = true; return; }
            // A failed launch (device lost, out of memory) drops back to the CPU for good.
            m_Device.reset();
            m_Status = "OpenCL launch failed; using the CPU";
        }
#endif
        CastCPU(zones, rays, out);
    }
}
