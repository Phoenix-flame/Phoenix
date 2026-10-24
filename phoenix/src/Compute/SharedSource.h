#pragma once
// Code written once for the CPU and for OpenCL kernels:
//
//     PHX_SHARED_SOURCE(s_MySource,
//         static float phx_something(float x){ ... }
//     )
//
// compiles the functions as C++ and also stores them, stringified, in s_MySource for
// the kernel build (after Compute's prelude). They must stick to what C++ and OpenCL C
// share: float/int/phx_uint scalars, pointers to private memory, phx_sqrt/phx_abs/
// phx_floor and FLT_MAX; no preprocessor lines or string literals, and no names that
// are OpenCL C keywords or types (half, image2d_t, ...). Files using it are
// built with -ffp-contract=off (see phoenix/CMakeLists.txt) and kernels with
// FP_CONTRACT OFF, so both sides round the same way.
#include <cfloat>
#include <cmath>
#include <cstdint>

#define PHX_SHARED_SOURCE(name, ...) __VA_ARGS__ [[maybe_unused]] static const char* name = #__VA_ARGS__;

namespace Phoenix{
    typedef uint32_t phx_uint;
    [[maybe_unused]] static inline float phx_sqrt(float x) { return std::sqrt(x); }
    [[maybe_unused]] static inline float phx_abs(float x) { return std::fabs(x); }
    [[maybe_unused]] static inline float phx_floor(float x) { return std::floor(x); }
}
