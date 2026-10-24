#include <Phoenix/renderer/WaterRipples.h>
#define GL_GLEXT_PROTOTYPES
#include <GLFW/glfw3.h>

#include <algorithm>

namespace Phoenix{

    // Fixed substep of the ripple wave equation (RippleGrid's default constants:
    // c2 = 0.32, damping 0.987 per substep).
    static constexpr float SUBSTEP = 1.0f / 60.0f;

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
