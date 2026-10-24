#include <Phoenix/Compute/Compute.h>
#include "ComputeCL.h"
#include <algorithm>
#include <vector>

namespace Phoenix::Compute{

    static std::string s_Status = "not set up";

#ifdef PHOENIX_OPENCL
    static const char* s_Prelude = R"CLC(
#pragma OPENCL FP_CONTRACT OFF
typedef uint phx_uint;
#define phx_sqrt(x) sqrt(x)
#define phx_abs(x) fabs(x)
#define phx_floor(x) floor(x)
)CLC";

    static Device s_Device;
    static std::string s_BuildOptions;

    // Prefers a GPU, then any other device (a CPU runtime such as PoCL still works).
    // The device lives until the process exits; it is never released (drivers differ
    // on whether releasing during static destruction is safe).
    static bool Init(){
        cl_uint platformCount = 0;
        if (clGetPlatformIDs(0, nullptr, &platformCount) != CL_SUCCESS || platformCount == 0){
            s_Status = "no OpenCL platform found";
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
        if (!device) { s_Status = "no OpenCL device found"; return false; }

        cl_int err;
        cl_context context = clCreateContext(nullptr, 1, &device, nullptr, nullptr, &err);
        if (err != CL_SUCCESS) { s_Status = "clCreateContext failed (" + std::to_string(err) + ")"; return false; }
        cl_command_queue queue = clCreateCommandQueue(context, device, 0, &err);
        if (err != CL_SUCCESS){
            clReleaseContext(context);
            s_Status = "clCreateCommandQueue failed (" + std::to_string(err) + ")";
            return false;
        }
        s_Device = { device, context, queue };

        // Correctly rounded division and square root keep kernels identical to the CPU.
        cl_device_fp_config fp = 0;
        clGetDeviceInfo(device, CL_DEVICE_SINGLE_FP_CONFIG, sizeof(fp), &fp, nullptr);
        if (fp & CL_FP_CORRECTLY_ROUNDED_DIVIDE_SQRT) { s_BuildOptions = "-cl-fp32-correctly-rounded-divide-sqrt"; }

        char name[256] = {};
        clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(name) - 1, name, nullptr);
        s_Status = name;
        return true;
    }

    const Device* GetDevice(){
        static const bool ready = Init();
        return ready ? &s_Device : nullptr;
    }

    cl_program BuildProgram(std::initializer_list<const char*> sources, std::string& error){
        const Device* d = GetDevice();
        if (!d) { error = s_Status; return nullptr; }
        std::vector<const char*> all = { s_Prelude };
        all.insert(all.end(), sources.begin(), sources.end());
        cl_int err;
        cl_program program = clCreateProgramWithSource(d->context, (cl_uint)all.size(), all.data(), nullptr, &err);
        if (err != CL_SUCCESS) { error = "clCreateProgramWithSource failed (" + std::to_string(err) + ")"; return nullptr; }
        if (clBuildProgram(program, 1, &d->device, s_BuildOptions.c_str(), nullptr, nullptr) != CL_SUCCESS){
            size_t length = 0;
            clGetProgramBuildInfo(program, d->device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &length);
            std::string log(length, '\0');
            clGetProgramBuildInfo(program, d->device, CL_PROGRAM_BUILD_LOG, length, log.data(), nullptr);
            error = "kernel build failed: " + log;
            clReleaseProgram(program);
            return nullptr;
        }
        return program;
    }

    bool Reserve(cl_mem& buffer, size_t& capacity, size_t count, size_t elementSize, cl_mem_flags flags){
        if (buffer && capacity >= count) { return true; }
        size_t size = std::max<size_t>(count, std::max<size_t>(64, capacity * 2));
        if (buffer) { clReleaseMemObject(buffer); }
        cl_int err;
        buffer = clCreateBuffer(GetDevice()->context, flags, size * elementSize, nullptr, &err);
        capacity = buffer ? size : 0;
        return err == CL_SUCCESS;
    }

    cl_int Write(cl_mem buffer, const void* data, size_t bytes){
        return bytes ? clEnqueueWriteBuffer(GetDevice()->queue, buffer, CL_FALSE, 0, bytes, data, 0, nullptr, nullptr) : CL_SUCCESS;
    }

    cl_int Launch(cl_kernel kernel, size_t count){
        const size_t local = 64, global = (count + local - 1) / local * local;
        return clEnqueueNDRangeKernel(GetDevice()->queue, kernel, 1, nullptr, &global, &local, 0, nullptr, nullptr);
    }
#endif

    bool Compiled(){
#ifdef PHOENIX_OPENCL
        return true;
#else
        return false;
#endif
    }

    bool Available(){
#ifdef PHOENIX_OPENCL
        return GetDevice() != nullptr;
#else
        s_Status = "engine built without OpenCL (configure with -DPHOENIX_OPENCL=ON)";
        return false;
#endif
    }

    const std::string& Status(){
        Available();
        return s_Status;
    }
}
