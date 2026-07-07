#include <Phoenix/renderer/WaterRipples.h>
#define GL_GLEXT_PROTOTYPES
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>

namespace Phoenix{

    // Wave-equation constants. C2 is (wave speed * dt / dx)^2 in grid units and must
    // stay <= 0.5 for the 2D explicit scheme to be stable. DAMPING is per-substep.
    static constexpr float SUBSTEP = 1.0f / 60.0f;
    static constexpr float C2 = 0.32f;
    static constexpr float DAMPING = 0.987f;

    WaterRipples::WaterRipples(int resolution){
        m_Res = std::max(16, resolution);
        m_Prev.assign((size_t)m_Res * m_Res, 0.0f);
        m_Cur.assign((size_t)m_Res * m_Res, 0.0f);
        m_Next.assign((size_t)m_Res * m_Res, 0.0f);

        glGenTextures(1, &m_Texture);
        glBindTexture(GL_TEXTURE_2D, m_Texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, m_Res, m_Res, 0, GL_RED, GL_FLOAT, m_Cur.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    WaterRipples::~WaterRipples(){
        if (m_Texture) { glDeleteTextures(1, &m_Texture); } // count, then pointer
    }

    void WaterRipples::AddImpulse(float u, float v, float radius, float strength){
        float cx = std::clamp(u, 0.0f, 1.0f) * (m_Res - 1);
        float cy = std::clamp(v, 0.0f, 1.0f) * (m_Res - 1);
        int r = std::max(1, (int)std::ceil(radius * (m_Res - 1)));

        int x0 = std::max(0, (int)cx - r), x1 = std::min(m_Res - 1, (int)cx + r);
        int y0 = std::max(0, (int)cy - r), y1 = std::min(m_Res - 1, (int)cy + r);
        for (int y = y0; y <= y1; y++){
            for (int x = x0; x <= x1; x++){
                float dx = (x - cx) / r, dy = (y - cy) / r;
                float d2 = dx * dx + dy * dy;
                if (d2 > 1.0f) { continue; }
                // Smooth bump: (1 - d^2)^2 falls to zero at the rim.
                float w = (1.0f - d2) * (1.0f - d2);
                m_Cur[(size_t)y * m_Res + x] -= strength * w;
            }
        }
    }

    void WaterRipples::StepOnce(){
        const int N = m_Res;
        for (int y = 0; y < N; y++){
            int yu = std::max(0, y - 1), yd = std::min(N - 1, y + 1);
            for (int x = 0; x < N; x++){
                int xl = std::max(0, x - 1), xr = std::min(N - 1, x + 1);
                size_t i = (size_t)y * N + x;
                float lap = m_Cur[(size_t)y * N + xl] + m_Cur[(size_t)y * N + xr]
                          + m_Cur[(size_t)yu * N + x] + m_Cur[(size_t)yd * N + x]
                          - 4.0f * m_Cur[i];
                m_Next[i] = DAMPING * (2.0f * m_Cur[i] - m_Prev[i] + C2 * lap);
            }
        }
        m_Prev.swap(m_Cur);
        m_Cur.swap(m_Next);
    }

    void WaterRipples::Step(float dt){
        // Fixed substeps keep the scheme stable regardless of frame rate; cap the
        // accumulator so a long hitch doesn't spiral.
        m_Accumulator = std::min(m_Accumulator + dt, 0.25f);
        while (m_Accumulator >= SUBSTEP){
            StepOnce();
            m_Accumulator -= SUBSTEP;
        }
    }

    void WaterRipples::Upload(){
        glBindTexture(GL_TEXTURE_2D, m_Texture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_Res, m_Res, GL_RED, GL_FLOAT, m_Cur.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    float WaterRipples::SampleHeight(float u, float v) const{
        float x = std::clamp(u, 0.0f, 1.0f) * (m_Res - 1);
        float y = std::clamp(v, 0.0f, 1.0f) * (m_Res - 1);
        int x0 = (int)x, y0 = (int)y;
        int x1 = std::min(x0 + 1, m_Res - 1), y1 = std::min(y0 + 1, m_Res - 1);
        float fx = x - x0, fy = y - y0;
        float h00 = m_Cur[(size_t)y0 * m_Res + x0], h10 = m_Cur[(size_t)y0 * m_Res + x1];
        float h01 = m_Cur[(size_t)y1 * m_Res + x0], h11 = m_Cur[(size_t)y1 * m_Res + x1];
        return (h00 * (1 - fx) + h10 * fx) * (1 - fy) + (h01 * (1 - fx) + h11 * fx) * fy;
    }
}
