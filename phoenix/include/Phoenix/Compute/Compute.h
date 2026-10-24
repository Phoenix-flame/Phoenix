#pragma once
#include <string>

namespace Phoenix::Compute{

    // The engine's shared OpenCL device (a GPU when one is found), used by the batched
    // ray sensors, the water ripple simulation and terrain generation. It is set up on
    // first use; each of those features keeps a CPU path with identical results, so
    // nothing depends on a device being present.

    // Built with -DPHOENIX_OPENCL=ON.
    bool Compiled();
    // A device is set up (tries once, on the first call).
    bool Available();
    // The device name, or why there is none (for the log and the editor).
    const std::string& Status();
}
