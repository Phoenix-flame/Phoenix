#include <Phoenix/renderer/RippleGrid.h>
#include <Phoenix/Compute/Compute.h>
#include "../Compute/ComputeCL.h"
#include "../Compute/SharedSource.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace Phoenix{

    // Shared with the kernel (see SharedSource.h).
    PHX_SHARED_SOURCE(s_RippleSource,
    static float phx_ripple_next(float c, float p, float l, float r, float u, float d, float c2, float damping){
        float lap = l + r + u + d - 4.0f * c;
        return damping * (2.0f * c - p + c2 * lap);
    }

    static float phx_ripple_bump(float x, float y, float cx, float cy, float r){
        float dx = (x - cx) / r, dy = (y - cy) / r;
        float d2 = dx * dx + dy * dy;
        if (d2 > 1.0f) { return -1.0f; }
        return (1.0f - d2) * (1.0f - d2);
    }
    )

    RippleGrid::RippleGrid(int width, int height){
        m_W = std::max(16, width);
        m_H = std::max(16, height);
        m_Prev.assign((size_t)m_W * m_H, 0.0f);
        m_Cur.assign((size_t)m_W * m_H, 0.0f);
        m_Next.assign((size_t)m_W * m_H, 0.0f);
    }

    void RippleGrid::SetWaveParameters(float c2, float damping){
        m_C2 = std::clamp(c2, 0.0f, 0.5f);
        m_Damping = std::clamp(damping, 0.0f, 1.0f);
    }

    void RippleGrid::MarkDirty(int rowMin, int rowMax){
        // The host copy is current between steps; the device needs these rows again.
        if (!m_Device) { return; }
        m_DirtyRowMin = m_DirtyRowMax < m_DirtyRowMin ? rowMin : std::min(m_DirtyRowMin, rowMin);
        m_DirtyRowMax = std::max(m_DirtyRowMax, rowMax);
    }

    void RippleGrid::AddImpulse(float u, float v, float radius, float strength){
        float cx = std::clamp(u, 0.0f, 1.0f) * (m_W - 1);
        float cy = std::clamp(v, 0.0f, 1.0f) * (m_H - 1);
        int r = std::max(1, (int)std::ceil(radius * (m_W - 1)));

        int x0 = std::max(0, (int)cx - r), x1 = std::min(m_W - 1, (int)cx + r);
        int y0 = std::max(0, (int)cy - r), y1 = std::min(m_H - 1, (int)cy + r);
        for (int y = y0; y <= y1; y++){
            for (int x = x0; x <= x1; x++){
                // Smooth bump: (1 - d^2)^2 falls to zero at the rim (negative = outside).
                float w = phx_ripple_bump((float)x, (float)y, cx, cy, (float)r);
                if (w < 0.0f) { continue; }
                m_Cur[(size_t)y * m_W + x] -= strength * w;
            }
        }
        MarkDirty(y0, y1);
    }

    void RippleGrid::AddHeight(int x, int y, float delta){
        if (x < 0 || y < 0 || x >= m_W || y >= m_H) { return; }
        m_Cur[(size_t)y * m_W + x] += delta;
        MarkDirty(y, y);
    }

    void RippleGrid::StepCPU(){
        const int W = m_W, H = m_H;
        for (int y = 0; y < H; y++){
            int yu = std::max(0, y - 1), yd = std::min(H - 1, y + 1);
            for (int x = 0; x < W; x++){
                int xl = std::max(0, x - 1), xr = std::min(W - 1, x + 1);
                size_t i = (size_t)y * W + x;
                m_Next[i] = phx_ripple_next(m_Cur[i], m_Prev[i], m_Cur[(size_t)y * W + xl], m_Cur[(size_t)y * W + xr],
                                            m_Cur[(size_t)yu * W + x], m_Cur[(size_t)yd * W + x], m_C2, m_Damping);
            }
        }
        m_Prev.swap(m_Cur);
        m_Cur.swap(m_Next);
    }

    float RippleGrid::SampleHeight(float u, float v) const{
        float x = std::clamp(u, 0.0f, 1.0f) * (m_W - 1);
        float y = std::clamp(v, 0.0f, 1.0f) * (m_H - 1);
        int x0 = (int)x, y0 = (int)y;
        int x1 = std::min(x0 + 1, m_W - 1), y1 = std::min(y0 + 1, m_H - 1);
        float fx = x - x0, fy = y - y0;
        float h00 = m_Cur[(size_t)y0 * m_W + x0], h10 = m_Cur[(size_t)y0 * m_W + x1];
        float h01 = m_Cur[(size_t)y1 * m_W + x0], h11 = m_Cur[(size_t)y1 * m_W + x1];
        return (h00 * (1 - fx) + h10 * fx) * (1 - fy) + (h01 * (1 - fx) + h11 * fx) * fy;
    }

#ifdef PHOENIX_OPENCL
    static const char* s_RippleKernel = R"CLC(
__kernel void ripple_step(__global const float* prev, __global const float* cur, __global float* next,
                          int w, int h, float c2, float damping){
    int i = get_global_id(0);
    if (i >= w * h) { return; }
    int y = i / w, x = i - y * w;
    int yu = y > 0 ? y - 1 : 0, yd = y < h - 1 ? y + 1 : h - 1;
    int xl = x > 0 ? x - 1 : 0, xr = x < w - 1 ? x + 1 : w - 1;
    next[i] = phx_ripple_next(cur[i], prev[i], cur[y * w + xl], cur[y * w + xr], cur[yu * w + x], cur[yd * w + x], c2, damping);
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

    void RippleGrid::Reset(){
        std::fill(m_Prev.begin(), m_Prev.end(), 0.0f);
        std::fill(m_Cur.begin(), m_Cur.end(), 0.0f);
        if (m_Device){
            m_Device.reset();
            UseOpenCL(true);
        }
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
            const size_t offset = (size_t)m_DirtyRowMin * m_W, count = (size_t)(m_DirtyRowMax - m_DirtyRowMin + 1) * m_W;
            err |= clEnqueueWriteBuffer(queue, d.buffers[d.cur], CL_FALSE, offset * sizeof(float), count * sizeof(float),
                                        m_Cur.data() + offset, 0, nullptr, nullptr);
            m_DirtyRowMax = -1;
        }
        const cl_int w = m_W, h = m_H;
        for (int i = 0; i < substeps; i++){
            err |= clSetKernelArg(d.kernel, 0, sizeof(cl_mem), &d.buffers[d.prev]);
            err |= clSetKernelArg(d.kernel, 1, sizeof(cl_mem), &d.buffers[d.cur]);
            err |= clSetKernelArg(d.kernel, 2, sizeof(cl_mem), &d.buffers[d.next]);
            err |= clSetKernelArg(d.kernel, 3, sizeof(cl_int), &w);
            err |= clSetKernelArg(d.kernel, 4, sizeof(cl_int), &h);
            err |= clSetKernelArg(d.kernel, 5, sizeof(float), &m_C2);
            err |= clSetKernelArg(d.kernel, 6, sizeof(float), &m_Damping);
            err |= Compute::Launch(d.kernel, (size_t)w * h);
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

    void RippleGrid::Reset(){
        std::fill(m_Prev.begin(), m_Prev.end(), 0.0f);
        std::fill(m_Cur.begin(), m_Cur.end(), 0.0f);
    }

    void RippleGrid::Step(int substeps){
        for (int i = 0; i < substeps; i++) { StepCPU(); }
    }
#endif

    RippleGrid::~RippleGrid() = default;
}
