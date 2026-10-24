#include <Phoenix/renderer/WaterRipples.h>
#include <Phoenix/Compute/Compute.h>
#include "../Compute/ComputeCL.h"
#include "../Compute/SharedSource.h"
#define GL_GLEXT_PROTOTYPES
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace Phoenix{

    // Fixed substep of the wave equation. Its constants live in phx_ripple_next:
    // C2 = 0.32 is (wave speed * dt / dx)^2 in grid units and must stay <= 0.5 for the
    // 2D explicit scheme to be stable; DAMPING = 0.987 is per substep.
    static constexpr float SUBSTEP = 1.0f / 60.0f;

    // Shared with the kernel (see SharedSource.h).
    PHX_SHARED_SOURCE(s_RippleSource,
    static float phx_ripple_next(float c, float p, float l, float r, float u, float d){
        float lap = l + r + u + d - 4.0f * c;
        return 0.987f * (2.0f * c - p + 0.32f * lap);
    }

    static float phx_ripple_bump(float x, float y, float cx, float cy, float r){
        float dx = (x - cx) / r, dy = (y - cy) / r;
        float d2 = dx * dx + dy * dy;
        if (d2 > 1.0f) { return -1.0f; }
        return (1.0f - d2) * (1.0f - d2);
    }
    )

    RippleGrid::RippleGrid(int resolution){
        m_Res = std::max(16, resolution);
        m_Prev.assign((size_t)m_Res * m_Res, 0.0f);
        m_Cur.assign((size_t)m_Res * m_Res, 0.0f);
        m_Next.assign((size_t)m_Res * m_Res, 0.0f);
    }

    void RippleGrid::AddImpulse(float u, float v, float radius, float strength){
        float cx = std::clamp(u, 0.0f, 1.0f) * (m_Res - 1);
        float cy = std::clamp(v, 0.0f, 1.0f) * (m_Res - 1);
        int r = std::max(1, (int)std::ceil(radius * (m_Res - 1)));

        int x0 = std::max(0, (int)cx - r), x1 = std::min(m_Res - 1, (int)cx + r);
        int y0 = std::max(0, (int)cy - r), y1 = std::min(m_Res - 1, (int)cy + r);
        for (int y = y0; y <= y1; y++){
            for (int x = x0; x <= x1; x++){
                // Smooth bump: (1 - d^2)^2 falls to zero at the rim (negative = outside).
                float w = phx_ripple_bump((float)x, (float)y, cx, cy, (float)r);
                if (w < 0.0f) { continue; }
                m_Cur[(size_t)y * m_Res + x] -= strength * w;
            }
        }
        // The host copy is current between steps; the device needs these rows again.
        if (m_Device){
            m_DirtyRowMin = m_DirtyRowMax < m_DirtyRowMin ? y0 : std::min(m_DirtyRowMin, y0);
            m_DirtyRowMax = std::max(m_DirtyRowMax, y1);
        }
    }

    void RippleGrid::StepCPU(){
        const int N = m_Res;
        for (int y = 0; y < N; y++){
            int yu = std::max(0, y - 1), yd = std::min(N - 1, y + 1);
            for (int x = 0; x < N; x++){
                int xl = std::max(0, x - 1), xr = std::min(N - 1, x + 1);
                size_t i = (size_t)y * N + x;
                m_Next[i] = phx_ripple_next(m_Cur[i], m_Prev[i], m_Cur[(size_t)y * N + xl], m_Cur[(size_t)y * N + xr],
                                            m_Cur[(size_t)yu * N + x], m_Cur[(size_t)yd * N + x]);
            }
        }
        m_Prev.swap(m_Cur);
        m_Cur.swap(m_Next);
    }

    float RippleGrid::SampleHeight(float u, float v) const{
        float x = std::clamp(u, 0.0f, 1.0f) * (m_Res - 1);
        float y = std::clamp(v, 0.0f, 1.0f) * (m_Res - 1);
        int x0 = (int)x, y0 = (int)y;
        int x1 = std::min(x0 + 1, m_Res - 1), y1 = std::min(y0 + 1, m_Res - 1);
        float fx = x - x0, fy = y - y0;
        float h00 = m_Cur[(size_t)y0 * m_Res + x0], h10 = m_Cur[(size_t)y0 * m_Res + x1];
        float h01 = m_Cur[(size_t)y1 * m_Res + x0], h11 = m_Cur[(size_t)y1 * m_Res + x1];
        return (h00 * (1 - fx) + h10 * fx) * (1 - fy) + (h01 * (1 - fx) + h11 * fx) * fy;
    }

#ifdef PHOENIX_OPENCL
    static const char* s_RippleKernel = R"CLC(
__kernel void ripple_step(__global const float* prev, __global const float* cur, __global float* next, int n){
    int i = get_global_id(0);
    if (i >= n * n) { return; }
    int y = i / n, x = i - y * n;
    int yu = y > 0 ? y - 1 : 0, yd = y < n - 1 ? y + 1 : n - 1;
    int xl = x > 0 ? x - 1 : 0, xr = x < n - 1 ? x + 1 : n - 1;
    next[i] = phx_ripple_next(cur[i], prev[i], cur[y * n + xl], cur[y * n + xr], cur[yu * n + x], cur[yd * n + x]);
}
)CLC";

    // One program for every grid, built on first use (nullptr if that failed).
    static cl_program RippleProgram(){
        static cl_program program = []{
            std::string error;
            return Compute::BuildProgram({ s_RippleSource, s_RippleKernel }, error);
        }();
        return program;
    }

    // Three device buffers rotated like m_Prev/m_Cur/m_Next; the host keeps an exact
    // copy of `cur` after every Step.
    struct RippleGrid::Device{
        cl_kernel kernel = nullptr;
        cl_mem buffers[3] = {};
        int prev = 0, cur = 1, next = 2;

        ~Device(){
            for (cl_mem b : buffers) { if (b) { clReleaseMemObject(b); } }
            if (kernel) { clReleaseKernel(kernel); }
        }
    };

    bool RippleGrid::UseOpenCL(bool enable){
        if (enable == (m_Device != nullptr)) { return enable; }
        const size_t bytes = m_Cur.size() * sizeof(float);
        if (!enable){
            // Bring the full state back (prev lives only on the device).
            cl_command_queue queue = Compute::GetDevice()->queue;
            clEnqueueReadBuffer(queue, m_Device->buffers[m_Device->prev], CL_TRUE, 0, bytes, m_Prev.data(), 0, nullptr, nullptr);
            m_Device.reset();
            return false;
        }
        cl_program program = RippleProgram();
        if (!program) { return false; }
        auto device = std::make_unique<Device>();
        cl_int err;
        device->kernel = clCreateKernel(program, "ripple_step", &err);
        if (err != CL_SUCCESS) { return false; }
        for (cl_mem& b : device->buffers){
            b = clCreateBuffer(Compute::GetDevice()->context, CL_MEM_READ_WRITE, bytes, nullptr, &err);
            if (err != CL_SUCCESS) { return false; }
        }
        cl_command_queue queue = Compute::GetDevice()->queue;
        err = clEnqueueWriteBuffer(queue, device->buffers[device->prev], CL_TRUE, 0, bytes, m_Prev.data(), 0, nullptr, nullptr);
        err |= clEnqueueWriteBuffer(queue, device->buffers[device->cur], CL_TRUE, 0, bytes, m_Cur.data(), 0, nullptr, nullptr);
        if (err != CL_SUCCESS) { return false; }
        m_Device = std::move(device);
        m_DirtyRowMax = -1;
        return true;
    }

    void RippleGrid::Step(int substeps){
        if (substeps <= 0) { return; }
        if (!m_Device){
            for (int i = 0; i < substeps; i++) { StepCPU(); }
            return;
        }
        Device& d = *m_Device;
        cl_command_queue queue = Compute::GetDevice()->queue;
        cl_int err = CL_SUCCESS;
        if (m_DirtyRowMax >= m_DirtyRowMin){
            // Splashes since the last step: send just those rows.
            const size_t offset = (size_t)m_DirtyRowMin * m_Res, count = (size_t)(m_DirtyRowMax - m_DirtyRowMin + 1) * m_Res;
            err |= clEnqueueWriteBuffer(queue, d.buffers[d.cur], CL_FALSE, offset * sizeof(float), count * sizeof(float),
                                        m_Cur.data() + offset, 0, nullptr, nullptr);
            m_DirtyRowMax = -1;
        }
        const cl_int n = m_Res;
        for (int i = 0; i < substeps; i++){
            err |= clSetKernelArg(d.kernel, 0, sizeof(cl_mem), &d.buffers[d.prev]);
            err |= clSetKernelArg(d.kernel, 1, sizeof(cl_mem), &d.buffers[d.cur]);
            err |= clSetKernelArg(d.kernel, 2, sizeof(cl_mem), &d.buffers[d.next]);
            err |= clSetKernelArg(d.kernel, 3, sizeof(cl_int), &n);
            err |= Compute::Launch(d.kernel, (size_t)n * n);
            // prev <- cur, cur <- next, next <- prev (as StepCPU's swaps).
            int oldPrev = d.prev;
            d.prev = d.cur;
            d.cur = d.next;
            d.next = oldPrev;
        }
        err |= clEnqueueReadBuffer(queue, d.buffers[d.cur], CL_TRUE, 0, m_Cur.size() * sizeof(float), m_Cur.data(), 0, nullptr, nullptr);
        if (err != CL_SUCCESS){
            // Device trouble: carry on with the CPU from the host copy (prev is lost, so
            // the waves restart from rest at the current heights).
            m_Device.reset();
            m_Prev = m_Cur;
        }
    }
#else
    struct RippleGrid::Device{};

    bool RippleGrid::UseOpenCL(bool) { return false; }

    void RippleGrid::Step(int substeps){
        for (int i = 0; i < substeps; i++) { StepCPU(); }
    }
#endif

    RippleGrid::~RippleGrid() = default;

    bool WaterRipples::s_UseOpenCL = false;
    int WaterRipples::minDeviceResolution = 512;

    WaterRipples::WaterRipples(int resolution)
        : m_Grid(resolution){
        const int res = m_Grid.GetResolution();
        glGenTextures(1, &m_Texture);
        glBindTexture(GL_TEXTURE_2D, m_Texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, res, res, 0, GL_RED, GL_FLOAT, m_Grid.Heights().data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    WaterRipples::~WaterRipples(){
        if (m_Texture) { glDeleteTextures(1, &m_Texture); } // count, then pointer
    }

    void WaterRipples::Step(float dt){
        m_Grid.UseOpenCL(s_UseOpenCL && m_Grid.GetResolution() >= minDeviceResolution);
        // Fixed substeps keep the scheme stable regardless of frame rate; cap the
        // accumulator so a long hitch doesn't spiral.
        m_Accumulator = std::min(m_Accumulator + dt, 0.25f);
        int substeps = 0;
        while (m_Accumulator >= SUBSTEP){
            substeps++;
            m_Accumulator -= SUBSTEP;
        }
        m_Grid.Step(substeps);
    }

    void WaterRipples::Upload(){
        const int res = m_Grid.GetResolution();
        glBindTexture(GL_TEXTURE_2D, m_Texture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, res, res, GL_RED, GL_FLOAT, m_Grid.Heights().data());
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}
