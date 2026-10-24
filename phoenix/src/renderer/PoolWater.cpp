#include <Phoenix/renderer/PoolWater.h>
#define GL_GLEXT_PROTOTYPES
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>

namespace Phoenix{

    // Courant number per substep: 0.6 keeps c2 = 0.36 under the scheme's 0.5 limit.
    static constexpr float MAX_COURANT = 0.6f;
    static constexpr int MAX_SUBSTEPS = 16;

    static int VertexCount(float extent, float cellSize){
        return std::max(16, (int)std::lround(extent / std::max(cellSize, 0.005f)) + 1);
    }

    PoolWater::PoolWater(const glm::vec2& size, float cellSize)
        : m_Size(glm::max(size, glm::vec2(0.1f))), m_CellSize(cellSize),
          m_Grid(VertexCount(m_Size.x, cellSize), VertexCount(m_Size.y, cellSize)){
        m_Spacing = std::min(m_Size.x / (m_Grid.GetWidth() - 1), m_Size.y / (m_Grid.GetHeight() - 1));
        m_Columns.assign(m_Grid.Heights().size(), 0.0f);

        glGenTextures(1, &m_HeightTexture);
        glBindTexture(GL_TEXTURE_2D, m_HeightTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, m_Grid.GetWidth(), m_Grid.GetHeight(), 0, GL_RED, GL_FLOAT, m_Grid.Heights().data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        // Caustics: light reaching the pool floor, over the same rectangle (512 texels
        // along the longer side).
        const float longer = std::max(m_Size.x, m_Size.y);
        const int cw = std::max(64, (int)std::lround(512.0f * m_Size.x / longer));
        const int ch = std::max(64, (int)std::lround(512.0f * m_Size.y / longer));
        m_CausticsSize = { cw, ch };
        glGenTextures(1, &m_CausticsTexture);
        glBindTexture(GL_TEXTURE_2D, m_CausticsTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, cw, ch, 0, GL_RED, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
        glGenFramebuffers(1, &m_CausticsFBO);
        GLint previous = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
        glBindFramebuffer(GL_FRAMEBUFFER, m_CausticsFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_CausticsTexture, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previous);
    }

    PoolWater::~PoolWater(){
        if (m_CausticsFBO) { glDeleteFramebuffers(1, &m_CausticsFBO); }
        if (m_CausticsTexture) { glDeleteTextures(1, &m_CausticsTexture); }
        if (m_HeightTexture) { glDeleteTextures(1, &m_HeightTexture); }
    }

    void PoolWater::Step(float dt, float waveSpeed, float damping){
        if (dt <= 0.0f) { return; }
        float speed = std::max(waveSpeed, 0.0f);
        int substeps = std::max(1, (int)std::ceil(speed * dt / m_Spacing / MAX_COURANT));
        if (substeps > MAX_SUBSTEPS){
            substeps = MAX_SUBSTEPS;
            speed = MAX_COURANT * m_Spacing * MAX_SUBSTEPS / dt;
        }
        const float h = dt / substeps;
        const float courant = speed * h / m_Spacing;
        m_Grid.SetWaveParameters(courant * courant, std::exp(-std::max(damping, 0.0f) * h));
        m_Grid.Step(substeps);
    }

    void PoolWater::SetDisplacement(const std::vector<float>& columns){
        if (m_Settle){
            if (columns.empty()) { std::fill(m_Columns.begin(), m_Columns.end(), 0.0f); }
            else { m_Columns = columns; }
            m_Settle = false;
            return;
        }
        const int w = m_Grid.GetWidth(), hgt = m_Grid.GetHeight();
        for (int j = 0; j < hgt; j++){
            for (int i = 0; i < w; i++){
                size_t k = (size_t)j * w + i;
                float now = columns.empty() ? 0.0f : columns[k];
                if (now != m_Columns[k]){
                    m_Grid.AddHeight(i, j, now - m_Columns[k]);
                    m_Columns[k] = now;
                }
            }
        }
    }

    void PoolWater::Reset(){
        m_Grid.Reset();
        std::fill(m_Columns.begin(), m_Columns.end(), 0.0f);
        m_Settle = true;
    }

    float PoolWater::SurfaceHeight(float x, float z) const{
        return m_Grid.SampleHeight(x / m_Size.x + 0.5f, z / m_Size.y + 0.5f);
    }

    glm::ivec2 PoolWater::CellAt(float x, float z) const{
        const int w = m_Grid.GetWidth(), h = m_Grid.GetHeight();
        int i = (int)std::lround((x / m_Size.x + 0.5f) * (w - 1));
        int j = (int)std::lround((z / m_Size.y + 0.5f) * (h - 1));
        return { std::clamp(i, 0, w - 1), std::clamp(j, 0, h - 1) };
    }

    glm::vec2 PoolWater::CellPosition(int i, int j) const{
        return { -0.5f * m_Size.x + m_Size.x * (float)i / (float)(m_Grid.GetWidth() - 1),
                 -0.5f * m_Size.y + m_Size.y * (float)j / (float)(m_Grid.GetHeight() - 1) };
    }

    void PoolWater::Upload(){
        glBindTexture(GL_TEXTURE_2D, m_HeightTexture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_Grid.GetWidth(), m_Grid.GetHeight(), GL_RED, GL_FLOAT, m_Grid.Heights().data());
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}
