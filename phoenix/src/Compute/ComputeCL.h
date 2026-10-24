#pragma once
// OpenCL side of Phoenix::Compute, for the engine's own .cpp files (only built with
// PHOENIX_OPENCL). Everything shares one context and one in-order queue.
#ifdef PHOENIX_OPENCL
#define CL_TARGET_OPENCL_VERSION 120
#ifdef __APPLE__
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>
#endif
#include <initializer_list>
#include <string>

namespace Phoenix::Compute{

    struct Device{
        cl_device_id device = nullptr;
        cl_context context = nullptr;
        cl_command_queue queue = nullptr;
    };

    // The shared device, or nullptr when there is none.
    const Device* GetDevice();

    // Builds a program from the prelude (FP_CONTRACT OFF and the phx_* helpers of
    // SharedSource.h) followed by `sources`. Returns nullptr and fills `error` on failure.
    cl_program BuildProgram(std::initializer_list<const char*> sources, std::string& error);

    // Grows a buffer to hold at least `count` elements (doubling, so it settles quickly).
    bool Reserve(cl_mem& buffer, size_t& capacity, size_t count, size_t elementSize, cl_mem_flags flags);
    // Non-blocking upload on the shared queue; empty uploads are skipped (a zero-size
    // write is an error).
    cl_int Write(cl_mem buffer, const void* data, size_t bytes);
    // 1D launch of `count` work-items, padded to whole work-groups of 64.
    cl_int Launch(cl_kernel kernel, size_t count);
}
#endif
