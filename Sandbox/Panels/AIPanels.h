#pragma once
#include <Phoenix/AI/Bridge.h>
#include <Phoenix/AI/Environment.h>
#include <Phoenix/Scene/Entity.h>
#include <Phoenix/Scene/Scene.h>
#include <Phoenix/imGui/imgui.h>
#include <functional>

namespace Phoenix{

    // What the AI panels operate on; filled in by the main layer every frame.
    struct AIPanelContext{
        Scene* scene = nullptr;
        Environment* environment = nullptr; // null when the scene isn't playing
        EpisodeLog* log = nullptr;
        Bridge* bridge = nullptr;
        Entity selected;

        // Run settings that outlive a Play session.
        float* timeScale = nullptr;
        bool* maxSpeed = nullptr;
        bool* paused = nullptr;
        bool* bridgeEnabled = nullptr;
        bool* renderAtMaxSpeed = nullptr;
        int* bridgePort = nullptr;

        std::function<void()> togglePlay;
        std::function<void(Entity)> select;
    };

    // The reinforcement-learning side of the editor:
    //  - Training: run/time controls, episode metrics and the Python bridge (the RL
    //    algorithm itself lives in the client).
    //  - Agent Monitor: one agent's live observations, actions, rewards and sensors.
    //  - Viewport overlay: ray sensors, reward zones, spawn ranges and agent headings.
    class AIPanels{
    public:
        bool showTraining = true;
        bool showAgentMonitor = true;
        bool showOverlays = true;

        void DrawTrainingPanel(AIPanelContext& ctx);
        void DrawAgentMonitor(AIPanelContext& ctx);
        void DrawViewportOverlay(AIPanelContext& ctx, const ImVec2& imageMin, const ImVec2& imageSize,
                                 const glm::mat4& viewProjection);

    private:
        void DrawRunControls(AIPanelContext& ctx);
        void DrawMetrics(AIPanelContext& ctx);
        void DrawBridge(AIPanelContext& ctx);
        void DrawData(AIPanelContext& ctx);

        char m_CsvPath[256] = "training_metrics.csv";
        int m_SmoothWindow = 20;
        int m_MonitorAgent = 0;
        std::string m_Notice;  // last save/load/export result
        float m_NoticeTime = 0.0f;
    };
}
