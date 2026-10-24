#include "AIPanels.h"
#include "EditorUI.h"
#include <Phoenix/Scene/Component.h>
#include <Phoenix/imGui/ImGuiLayer.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>

namespace Phoenix{

    // ---- Small formatting helpers ----

    static std::string Compact(double v){
        char buf[32];
        double a = std::abs(v);
        if (a >= 1e9)      { std::snprintf(buf, sizeof(buf), "%.2fB", v / 1e9); }
        else if (a >= 1e6) { std::snprintf(buf, sizeof(buf), "%.2fM", v / 1e6); }
        else if (a >= 1e4) { std::snprintf(buf, sizeof(buf), "%.1fk", v / 1e3); }
        else               { std::snprintf(buf, sizeof(buf), "%.0f", v); }
        return buf;
    }

    static const char* PolicyName(AgentComponent::Policy p){
        switch (p){
            case AgentComponent::Policy::Heuristic: return "Keyboard";
            case AgentComponent::Policy::Random:    return "Random";
            case AgentComponent::Policy::External:  return "Python";
        }
        return "?";
    }

    // Run state for pills and the status line.
    struct RunStatus{ const char* text; ImU32 color; };
    static RunStatus GetRunStatus(AIPanelContext& ctx){
        Environment* env = ctx.environment;
        if (!env) { return { "Stopped", EditorUI::MutedColor() }; }
        if (env->paused) { return { "Paused", IM_COL32(240, 200, 70, 255) }; }
        if (env->ClientDriven()){
            if (ctx.bridge && ctx.bridge->HasClient()) { return { "Python connected", IM_COL32(90, 170, 255, 255) }; }
            return { "Waiting for Python", IM_COL32(90, 170, 255, 255) };
        }
        return { "Running", EditorUI::Success() };
    }

    static void Pill(const char* text, ImU32 dot){
        ImVec2 size = EditorUI::PillSize(text, 4.0f);
        EditorUI::DrawPill(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), text, dot,
            ImGui::GetColorU32(ImGuiCol_FrameBg), 4.0f);
        ImGui::Dummy(size);
    }

    // ===================================================================== Training

    void AIPanels::DrawTrainingPanel(AIPanelContext& ctx){
        if (!showTraining) { return; }
        if (!ImGui::Begin("Training")) { ImGui::End(); return; }

        DrawRunControls(ctx);
        ImGui::Spacing();
        DrawMetrics(ctx);
        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Python Bridge", ImGuiTreeNodeFlags_DefaultOpen))    { DrawBridge(ctx); }
        if (ImGui::CollapsingHeader("Data & Export"))                                    { DrawData(ctx); }

        ImGui::End();
    }

    void AIPanels::DrawRunControls(AIPanelContext& ctx){
        Environment* env = ctx.environment;
        const bool running = env != nullptr;

        RunStatus status = GetRunStatus(ctx);
        Pill(status.text, status.color);
        ImGui::SameLine();
        if (running){
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("Episode %d  |  step %d  |  %.0f sim s", env->Episode(), env->EpisodeStep(),
                ctx.scene->GetSimulationTime());
        }
        else{
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("Press Play to run episodes");
        }

        // Transport.
        if (EditorUI::IconTextButton("##play", running ? EditorUI::Icon::Stop : EditorUI::Icon::Play,
                running ? "Stop" : "Play", !running, running ? EditorUI::Danger() : 0, 84.0f))
            ctx.togglePlay();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(running ? "Stop and restore the edit-time scene (F5)" : "Start the simulation (F5)");
        ImGui::SameLine();
        ImGui::BeginDisabled(!running);
        if (EditorUI::IconTextButton("##pause", *ctx.paused ? EditorUI::Icon::Play : EditorUI::Icon::Pause,
                *ctx.paused ? "Resume" : "Pause", false, 0, 92.0f))
            *ctx.paused = !*ctx.paused;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Freeze the simulation (F6)");
        ImGui::SameLine();
        if (EditorUI::IconTextButton("##step", EditorUI::Icon::StepForward, "Step", false, 0, 72.0f) && env){
            *ctx.paused = true;
            env->paused = true;
            env->StepDecision();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Advance one decision while paused (F7)");
        ImGui::SameLine();
        if (EditorUI::IconTextButton("##reset", EditorUI::Icon::Reset, "Reset Episode", false, 0, 0.0f) && env)
            env->ResetEpisode();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Restart the episode with fresh randomization");
        ImGui::EndDisabled();

        // Speed.
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Speed");
        ImGui::SameLine(70.0f);
        static const float scales[] = { 0.25f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f };
        const char* labels[] = { "0.25x", "1x", "2x", "4x", "8x", "16x", "Max" };
        const char* tips[] = { "Slow motion", "Real time", nullptr, nullptr, nullptr, nullptr,
                               "As many steps as fit in each frame (fastest training)" };
        int current = 1;
        if (*ctx.maxSpeed) { current = 6; }
        else{
            for (int i = 0; i < 6; i++)
                if (std::abs(*ctx.timeScale - scales[i]) < 1e-3f) { current = i; }
        }
        if (EditorUI::Segmented("##speed", &current, labels, 7, 0.0f, tips)){
            *ctx.maxSpeed = current == 6;
            if (current < 6) { *ctx.timeScale = scales[current]; }
        }
        if (*ctx.maxSpeed){
            ImGui::Checkbox("Render viewport at Max speed", ctx.renderAtMaxSpeed);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Off: skip drawing the 3D view while fast-forwarding and give that time to the simulation.");
        }
        if (running){
            ImGui::TextDisabled("%s physics steps/s  |  %.1fx real time", Compact(env->StepsPerSecond()).c_str(), env->SimSpeed());
            if (env->ClientDriven())
                ImGui::TextDisabled("The Python client sets the pace: the speed control doesn't apply.");
        }
    }

    void AIPanels::DrawMetrics(AIPanelContext& ctx){
        const EpisodeLog& log = *ctx.log;
        const auto& episodes = log.episodes;

        std::vector<float> returns, lengths;
        returns.reserve(episodes.size());
        lengths.reserve(episodes.size());
        for (const auto& e : episodes){ returns.push_back(e.meanReturn); lengths.push_back((float)e.length); }

        int window = std::max(1, m_SmoothWindow);
        float recent = 0.0f, best = 0.0f;
        if (!returns.empty()){
            size_t from = returns.size() > (size_t)window ? returns.size() - window : 0;
            for (size_t i = from; i < returns.size(); i++) { recent += returns[i]; }
            recent /= (float)(returns.size() - from);
            best = *std::max_element(returns.begin(), returns.end());
        }

        // KPI tiles.
        float avail = ImGui::GetContentRegionAvail().x;
        int columns = avail > 560.0f ? 4 : 2;
        float spacing = ImGui::GetStyle().ItemSpacing.x;
        float tileW = (avail - spacing * (columns - 1)) / (float)columns;
        char value[64], detail[64];

        std::snprintf(value, sizeof(value), "%d", (int)episodes.size());
        std::snprintf(detail, sizeof(detail), "%s decisions", Compact((double)log.totalSteps).c_str());
        EditorUI::StatTile("Episodes", value, detail, tileW, EditorUI::Accent());
        ImGui::SameLine();
        if (returns.empty()) { std::snprintf(value, sizeof(value), "-"); }
        else                 { std::snprintf(value, sizeof(value), "%+.3f", recent); }
        std::snprintf(detail, sizeof(detail), "last %d", window);
        EditorUI::StatTile("Mean return", value, detail, tileW, EditorUI::Success());
        if (columns == 4) { ImGui::SameLine(); }
        if (returns.empty()) { std::snprintf(value, sizeof(value), "-"); }
        else                 { std::snprintf(value, sizeof(value), "%+.3f", best); }
        EditorUI::StatTile("Best episode", value, nullptr, tileW, IM_COL32(90, 170, 255, 255));
        ImGui::SameLine();
        if (lengths.empty()) { std::snprintf(value, sizeof(value), "-"); }
        else{
            float sum = 0.0f;
            size_t from = lengths.size() > (size_t)window ? lengths.size() - window : 0;
            for (size_t i = from; i < lengths.size(); i++) { sum += lengths[i]; }
            std::snprintf(value, sizeof(value), "%.0f", sum / (float)(lengths.size() - from));
        }
        EditorUI::StatTile("Episode length", value, "decisions", tileW, IM_COL32(200, 120, 255, 255));

        ImGui::Spacing();
        ImGui::TextUnformatted("Episode return");
        ImGui::SameLine();
        ImGui::TextDisabled("(average of the agents; line = moving average)");
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - 150.0f));
        ImGui::SetNextItemWidth(150.0f);
        ImGui::SliderInt("##smooth", &m_SmoothWindow, 1, 200, "smooth %d");
        std::vector<float> average = EditorUI::MovingAverage(returns, window);
        EditorUI::LineChart("##returns", returns, &average, ImVec2(0.0f, 130.0f),
            EditorUI::Accent(0.35f), EditorUI::Accent(), "%.2f", "episodes");

        ImGui::TextUnformatted("Episode length");
        std::vector<float> lengthAverage = EditorUI::MovingAverage(lengths, window);
        EditorUI::LineChart("##lengths", lengths, &lengthAverage, ImVec2(0.0f, 90.0f),
            IM_COL32(200, 120, 255, 90), IM_COL32(200, 120, 255, 255), "%.0f", "episodes");
    }

    void AIPanels::DrawBridge(AIPanelContext& ctx){
        Bridge& bridge = *ctx.bridge;
        ImGui::TextWrapped("Train with any Python RL library (Stable-Baselines3, CleanRL, RLlib...): agents set to "
                           "the Python policy are exposed as a Gymnasium environment over a local socket.");
        ImGui::Spacing();

        bool enabled = *ctx.bridgeEnabled;
        if (ImGui::Checkbox("Listen for a Python client", &enabled)) { *ctx.bridgeEnabled = enabled; }
        ImGui::SameLine();
        ImGui::BeginDisabled(enabled);
        ImGui::SetNextItemWidth(110.0f);
        ImGui::InputInt("Port", ctx.bridgePort, 0, 0);
        *ctx.bridgePort = std::clamp(*ctx.bridgePort, 1024, 65535);
        ImGui::EndDisabled();

        char text[128];
        if (!enabled)                 { Pill("Off", EditorUI::MutedColor()); }
        else if (!bridge.IsListening()){ Pill("Error", EditorUI::Danger()); ImGui::SameLine(); ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1), "%s", bridge.LastError().c_str()); }
        else if (bridge.HasClient()){
            std::snprintf(text, sizeof(text), "Connected  %s  -  %.0f requests/s", bridge.ClientAddress().c_str(), bridge.RequestsPerSecond());
            Pill(text, EditorUI::Success());
        }
        else{
            std::snprintf(text, sizeof(text), "Listening on 127.0.0.1:%d", bridge.Port());
            Pill(text, IM_COL32(90, 170, 255, 255));
        }

        // Which agents the client will drive.
        int external = 0;
        auto agents = ctx.scene->m_Registry.view<AgentComponent>();
        for (auto e : agents)
            if (agents.get<AgentComponent>(e).policy == AgentComponent::Policy::External) { external++; }
        if (external == 0)
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), "No agent uses the Python policy: clients can still use the physics API (PhoenixSim).");
        else
            ImGui::TextDisabled("%d agent%s will be driven by the client. A client's reset starts Play automatically.",
                external, external == 1 ? "" : "s");

        ImGui::Spacing();
        static const char* snippet =
            "# pip install gymnasium  (python/ in the repository)\n"
            "from phoenix_env import PhoenixEnv\n"
            "env = PhoenixEnv(port=5555)\n"
            "obs, info = env.reset()\n"
            "for _ in range(1000):\n"
            "    obs, reward, terminated, truncated, info = env.step(env.action_space.sample())\n"
            "    if terminated or truncated:\n"
            "        obs, info = env.reset()\n";
        std::string code = snippet;
        if (*ctx.bridgePort != 5555){
            size_t at = code.find("5555");
            if (at != std::string::npos) { code.replace(at, 4, std::to_string(*ctx.bridgePort)); }
        }
        ImGui::PushFont(ImGuiLayer::GetFont(ImGuiLayer::FontType::Small));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.07f, 0.075f, 0.085f, 1.0f));
        ImGui::InputTextMultiline("##snippet", (char*)code.c_str(), code.size() + 1,
            ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 8.6f), ImGuiInputTextFlags_ReadOnly);
        ImGui::PopStyleColor();
        ImGui::PopFont();
        if (ImGui::SmallButton("Copy snippet")) { ImGui::SetClipboardText(code.c_str()); }
    }

    void AIPanels::DrawData(AIPanelContext& ctx){
        ImGui::SetNextItemWidth(-200.0f);
        ImGui::InputText("##csv", m_CsvPath, sizeof(m_CsvPath));
        ImGui::SameLine();
        if (ImGui::Button("Export CSV")){
            std::ofstream out(m_CsvPath);
            if (out){
                out << "episode,mean_return,length,sim_time\n";
                for (const auto& e : ctx.log->episodes)
                    out << e.episode << "," << e.meanReturn << "," << e.length << "," << e.simTime << "\n";
                m_Notice = std::string("Exported ") + std::to_string(ctx.log->episodes.size()) + " episodes to " + m_CsvPath;
            }
            else { m_Notice = std::string("Could not write ") + m_CsvPath; }
            m_NoticeTime = (float)ImGui::GetTime();
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear History")) { ctx.log->Clear(); }
        if (!m_Notice.empty() && ImGui::GetTime() - m_NoticeTime < 4.0)
            ImGui::TextDisabled("%s", m_Notice.c_str());
        ImGui::TextDisabled("Files are written next to the executable.");
    }

    // ================================================================ Agent Monitor

    // Top-down fan of the agent's ray sensor: length = free distance, colour = detected tag.
    static void DrawRayRadar(const AgentState& agent, const RaySensorComponent& sensor, float size){
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + size), ImGui::GetColorU32(ImGuiCol_FrameBg), 6.0f);
        // Everything stays inside the box; the circles' lower halves are clipped.
        dl->PushClipRect(p, ImVec2(p.x + w, p.y + size), true);
        ImVec2 c(p.x + w * 0.5f, p.y + size - 14.0f);
        float radius = std::max(1.0f, std::min(size - 26.0f, w * 0.5f - 10.0f));
        dl->AddCircle(c, radius, IM_COL32(255, 255, 255, 20), 48);
        dl->AddCircle(c, radius * 0.5f, IM_COL32(255, 255, 255, 14), 48);

        int n = std::max(1, sensor.rays);
        float fov = glm::radians(std::clamp(sensor.fov, 0.0f, 360.0f));
        for (int i = 0; i < n; i++){
            float offset = (n == 1) ? 0.0f : fov * 0.5f - fov * (float)i / (float)(n - 1);
            // Screen up = agent forward; positive offsets turn left.
            ImVec2 dir(-std::sin(offset), -std::cos(offset));
            float frac = i < (int)agent.rayFractions.size() ? agent.rayFractions[i] : 1.0f;
            int tag = i < (int)agent.rayTags.size() ? agent.rayTags[i] : -1;
            ImVec2 end(c.x + dir.x * radius * frac, c.y + dir.y * radius * frac);
            ImU32 col = frac >= 0.999f ? IM_COL32(255, 255, 255, 50)
                      : tag >= 0 ? EditorUI::CategoryColor(tag) : IM_COL32(210, 210, 210, 200);
            dl->AddLine(c, end, col, 2.0f);
            if (frac < 0.999f) { dl->AddCircleFilled(end, 3.5f, col); }
        }
        dl->AddTriangleFilled(ImVec2(c.x, c.y - 7.0f), ImVec2(c.x - 5.0f, c.y + 4.0f), ImVec2(c.x + 5.0f, c.y + 4.0f), EditorUI::Accent());

        // Legend.
        float x = p.x + 8.0f;
        for (size_t k = 0; k < sensor.detectableTags.size(); k++){
            dl->AddCircleFilled(ImVec2(x + 4.0f, p.y + 12.0f), 4.0f, EditorUI::CategoryColor((int)k));
            dl->AddText(ImVec2(x + 12.0f, p.y + 5.0f), EditorUI::MutedColor(), sensor.detectableTags[k].c_str());
            x += 22.0f + ImGui::CalcTextSize(sensor.detectableTags[k].c_str()).x;
        }
        dl->PopClipRect();
        ImGui::Dummy(ImVec2(w, size));
    }

    void AIPanels::DrawAgentMonitor(AIPanelContext& ctx){
        if (!showAgentMonitor) { return; }
        if (!ImGui::Begin("Agent Monitor")) { ImGui::End(); return; }

        Scene& scene = *ctx.scene;
        Environment* env = ctx.environment;

        if (!env){
            // Edit mode: a summary of every agent's spaces.
            auto view = scene.m_Registry.view<AgentComponent>();
            if (view.size() == 0){
                EditorUI::EmptyState("No agents in the scene", "Create > AI > Agent, or File > Showcases > AI.");
                ImGui::End();
                return;
            }
            ImGui::TextDisabled("Press Play to watch live observations, actions and rewards.");
            if (ImGui::BeginTable("##agents", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)){
                ImGui::TableSetupColumn("Agent");
                ImGui::TableSetupColumn("Policy");
                ImGui::TableSetupColumn("Observations");
                ImGui::TableSetupColumn("Actions");
                ImGui::TableHeadersRow();
                for (auto e : view){
                    Entity entity{ e, &scene };
                    const auto& a = view.get<AgentComponent>(e);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    bool selected = ctx.selected == entity;
                    if (ImGui::Selectable(entity.GetComponent<TagComponent>().Tag.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
                        ctx.select(entity);
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(PolicyName(a.policy));
                    ImGui::TableNextColumn(); ImGui::Text("%d", (int)Environment::DescribeObservations(scene, e).size());
                    ImGui::TableNextColumn();
                    ImGui::Text("%d %s", a.ActionSize(), a.actionType == AgentComponent::ActionType::Discrete ? "discrete" : "continuous");
                }
                ImGui::EndTable();
            }
            ImGui::End();
            return;
        }

        auto& agents = env->Agents();
        if (agents.empty()){
            EditorUI::EmptyState("No agents are running", "Add an Agent component and press Play again.");
            ImGui::End();
            return;
        }

        // Follow the selection when it is an agent.
        for (size_t i = 0; i < agents.size(); i++)
            if (ctx.selected && agents[i].entity == (entt::entity)ctx.selected) { m_MonitorAgent = (int)i; }
        m_MonitorAgent = std::clamp(m_MonitorAgent, 0, (int)agents.size() - 1);

        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##agent", agents[m_MonitorAgent].name.c_str())){
            for (size_t i = 0; i < agents.size(); i++){
                char label[128];
                std::snprintf(label, sizeof(label), "%s##%d", agents[i].name.c_str(), (int)i);
                if (ImGui::Selectable(label, (int)i == m_MonitorAgent)){
                    m_MonitorAgent = (int)i;
                    ctx.select(Entity{ agents[i].entity, &scene });
                }
            }
            ImGui::EndCombo();
        }

        AgentState& agent = agents[m_MonitorAgent];
        if (!scene.m_Registry.valid(agent.entity) || !scene.m_Registry.all_of<AgentComponent>(agent.entity)){
            EditorUI::EmptyState("This agent no longer exists");
            ImGui::End();
            return;
        }
        const auto& config = scene.m_Registry.get<AgentComponent>(agent.entity);

        // Status line.
        Pill(PolicyName(agent.policy), agent.policy == AgentComponent::Policy::External ? IM_COL32(90, 170, 255, 255) : EditorUI::Accent());
        ImGui::SameLine();
        if (agent.done)
            Pill(agent.truncated ? "Time limit" : "Episode done", EditorUI::MutedColor());
        else if (agent.awaitingAction)
            Pill("Waiting for action", IM_COL32(90, 170, 255, 255));

        // Episode progress.
        char overlay[64];
        if (config.maxSteps > 0){
            std::snprintf(overlay, sizeof(overlay), "step %d / %d", agent.episodeStep, config.maxSteps);
            ImGui::ProgressBar((float)agent.episodeStep / (float)config.maxSteps, ImVec2(-FLT_MIN, 0.0f), overlay);
        }
        else { ImGui::Text("Step %d", agent.episodeStep); }

        float avail = ImGui::GetContentRegionAvail().x;
        float tileW = (avail - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        char value[32], detail[32];
        std::snprintf(value, sizeof(value), "%+.3f", agent.episodeReturn);
        std::snprintf(detail, sizeof(detail), "last %+.3f", agent.lastReward);
        EditorUI::StatTile("Return", value, detail, tileW, agent.episodeReturn >= 0.0f ? EditorUI::Success() : EditorUI::Danger());
        ImGui::SameLine();
        std::snprintf(value, sizeof(value), "%d", agent.episodes);
        std::snprintf(detail, sizeof(detail), "prev %+.2f", agent.lastEpisodeReturn);
        EditorUI::StatTile("Episodes", value, agent.episodes ? detail : nullptr, tileW, EditorUI::Accent());

        std::vector<float> trace(agent.returnTrace.begin(), agent.returnTrace.end());
        EditorUI::LineChart("##trace", trace, nullptr, ImVec2(0.0f, 70.0f), EditorUI::Success(), 0, "%.2f", "this episode");

        // Actions.
        EditorUI::SectionLabel("Action");
        std::vector<std::string> names = Environment::DescribeActions(config);
        if (agent.discrete){
            int chosen = agent.action.empty() ? -1 : (int)agent.action[0];
            float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * (names.size() - 1)) / (float)std::max<size_t>(1, names.size());
            for (size_t i = 0; i < names.size(); i++){
                if (i) { ImGui::SameLine(); }
                ImVec2 p = ImGui::GetCursorScreenPos();
                ImVec2 size(w, ImGui::GetFrameHeight());
                ImDrawList* dl = ImGui::GetWindowDrawList();
                bool on = (int)i == chosen;
                dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), on ? EditorUI::Accent(0.85f) : ImGui::GetColorU32(ImGuiCol_FrameBg), 4.0f);
                ImVec2 ts = ImGui::CalcTextSize(names[i].c_str());
                dl->PushClipRect(p, ImVec2(p.x + size.x, p.y + size.y), true);
                dl->AddText(ImVec2(p.x + (size.x - ts.x) * 0.5f, p.y + (size.y - ts.y) * 0.5f),
                    on ? IM_COL32(20, 20, 22, 255) : EditorUI::MutedColor(), names[i].c_str());
                dl->PopClipRect();
                ImGui::Dummy(size);
            }
        }
        else{
            for (size_t i = 0; i < names.size(); i++){
                float v = i < agent.action.size() ? agent.action[i] : 0.0f;
                ImGui::TextUnformatted(names[i].c_str());
                ImGui::SameLine(110.0f);
                EditorUI::ValueBar(v, -1.0f, 1.0f, ImVec2(ImGui::GetContentRegionAvail().x - 60.0f, ImGui::GetFrameHeight() - 6.0f), EditorUI::Accent());
                ImGui::SameLine();
                ImGui::Text("%+.2f", v);
            }
        }

        if (auto* sensor = scene.m_Registry.try_get<RaySensorComponent>(agent.entity)){
            EditorUI::SectionLabel("Ray sensor");
            DrawRayRadar(agent, *sensor, 150.0f);
        }

        EditorUI::SectionLabel("Observations");
        ImGui::TextDisabled("%d values", (int)agent.observation.size());
        if (ImGui::BeginTable("##obs", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
                              ImVec2(0.0f, std::max(120.0f, ImGui::GetContentRegionAvail().y)))){
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Bar", ImGuiTableColumnFlags_WidthStretch, 1.2f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 56.0f);
            ImGui::TableSetupScrollFreeze(0, 0);
            ImGuiListClipper clipper;
            clipper.Begin((int)agent.observation.size());
            while (clipper.Step()){
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++){
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    const char* label = i < (int)agent.observationLabels.size() ? agent.observationLabels[i].c_str() : "?";
                    ImGui::TextDisabled("%s", label);
                    ImGui::TableNextColumn();
                    float v = agent.observation[i];
                    EditorUI::ValueBar(v, -1.0f, 1.0f, ImVec2(-1.0f, ImGui::GetTextLineHeight()),
                        std::abs(v) > 1.0f ? IM_COL32(240, 200, 70, 255) : IM_COL32(90, 170, 255, 255));
                    ImGui::TableNextColumn();
                    ImGui::Text("%+.2f", v);
                }
            }
            ImGui::EndTable();
        }
        ImGui::End();
    }

    // ============================================================ Viewport overlay

    namespace {
        // World -> viewport-image projection with near-plane clipping for segments.
        struct Projector{
            glm::mat4 vp;
            ImVec2 min, size;
            static constexpr float kNear = 0.05f;

            ImVec2 ToScreen(const glm::vec4& c) const{
                glm::vec3 ndc = glm::vec3(c) / c.w;
                return ImVec2(min.x + (ndc.x * 0.5f + 0.5f) * size.x, min.y + (0.5f - ndc.y * 0.5f) * size.y);
            }
            bool Point(const glm::vec3& p, ImVec2& out) const{
                glm::vec4 c = vp * glm::vec4(p, 1.0f);
                if (c.w < kNear) { return false; }
                out = ToScreen(c);
                return true;
            }
            bool Segment(const glm::vec3& a, const glm::vec3& b, ImVec2& sa, ImVec2& sb) const{
                glm::vec4 ca = vp * glm::vec4(a, 1.0f), cb = vp * glm::vec4(b, 1.0f);
                if (ca.w < kNear && cb.w < kNear) { return false; }
                if (ca.w < kNear) { ca += (cb - ca) * ((kNear - ca.w) / (cb.w - ca.w)); }
                if (cb.w < kNear) { cb += (ca - cb) * ((kNear - cb.w) / (ca.w - cb.w)); }
                sa = ToScreen(ca);
                sb = ToScreen(cb);
                return true;
            }
            void Line(ImDrawList* dl, const glm::vec3& a, const glm::vec3& b, ImU32 col, float thickness) const{
                ImVec2 sa, sb;
                if (Segment(a, b, sa, sb)) { dl->AddLine(sa, sb, col, thickness); }
            }
            // Oriented box: model * [-he, he]^3.
            void Box(ImDrawList* dl, const glm::mat4& model, const glm::vec3& he, ImU32 edge, ImU32 fill, float thickness) const{
                glm::vec3 c[8];
                for (int i = 0; i < 8; i++)
                    c[i] = glm::vec3(model * glm::vec4((i & 1) ? he.x : -he.x, (i & 2) ? he.y : -he.y, (i & 4) ? he.z : -he.z, 1.0f));
                if (fill){
                    static const int faces[6][4] = { {0,1,3,2}, {4,5,7,6}, {0,1,5,4}, {2,3,7,6}, {0,2,6,4}, {1,3,7,5} };
                    for (const auto& f : faces){
                        ImVec2 s[4];
                        bool ok = true;
                        for (int k = 0; k < 4 && ok; k++) { ok = Point(c[f[k]], s[k]); }
                        if (ok) { dl->AddQuadFilled(s[0], s[1], s[2], s[3], fill); }
                    }
                }
                static const int edges[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
                for (const auto& e : edges) { Line(dl, c[e[0]], c[e[1]], edge, thickness); }
            }
        };
    }

    void AIPanels::DrawViewportOverlay(AIPanelContext& ctx, const ImVec2& imageMin, const ImVec2& imageSize,
                                       const glm::mat4& viewProjection){
        if (!showOverlays) { return; }
        Scene& scene = *ctx.scene;
        auto& registry = scene.m_Registry;
        Environment* env = ctx.environment;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(imageMin, ImVec2(imageMin.x + imageSize.x, imageMin.y + imageSize.y), true);
        Projector proj{ viewProjection, imageMin, imageSize };

        // Zones currently occupied by an agent glow brighter.
        std::unordered_set<uint32_t> occupied;
        if (env)
            for (const auto& agent : env->Agents()) { occupied.insert(agent.zonesInside.begin(), agent.zonesInside.end()); }

        // Reward zones.
        auto zones = registry.view<RewardZoneComponent, TransformComponent>();
        for (auto e : zones){
            const auto& z = zones.get<RewardZoneComponent>(e);
            const auto& t = zones.get<TransformComponent>(e);
            bool positive = z.reward >= 0.0f;
            bool hot = occupied.count((uint32_t)e) > 0;
            bool selected = ctx.selected && (entt::entity)ctx.selected == e;
            ImU32 edge = positive ? EditorUI::Success(hot || selected ? 1.0f : 0.7f) : EditorUI::Danger(hot || selected ? 1.0f : 0.7f);
            ImU32 fill = positive ? EditorUI::Success(hot ? 0.22f : 0.07f) : EditorUI::Danger(hot ? 0.22f : 0.07f);
            proj.Box(dl, t.GetTransform(), z.halfExtents, edge, fill, selected ? 2.0f : 1.2f);

            // Label above the zone.
            glm::vec3 top = glm::vec3(t.GetTransform() * glm::vec4(0.0f, z.halfExtents.y, 0.0f, 1.0f));
            ImVec2 s;
            if (proj.Point(top, s) && registry.all_of<TagComponent>(e)){
                char label[96];
                std::snprintf(label, sizeof(label), "%s  %+.2f%s", registry.get<TagComponent>(e).Tag.c_str(), z.reward,
                    z.trigger == RewardZoneComponent::Trigger::WhileInside ? "/s" : "");
                ImVec2 ts = ImGui::CalcTextSize(label);
                ImVec2 p(s.x - ts.x * 0.5f, s.y - ts.y - 6.0f);
                dl->AddRectFilled(ImVec2(p.x - 5, p.y - 2), ImVec2(p.x + ts.x + 5, p.y + ts.y + 2), IM_COL32(18, 19, 22, 190), 4.0f);
                dl->AddText(p, positive ? EditorUI::Success() : EditorUI::Danger(), label);
            }
        }

        // Spawn ranges (edit mode: where randomization can put things).
        if (!env){
            auto randomizers = registry.view<RandomizerComponent, TransformComponent>();
            for (auto e : randomizers){
                const auto& r = randomizers.get<RandomizerComponent>(e);
                if (glm::length(r.positionRange) < 1e-4f) { continue; }
                const auto& t = randomizers.get<TransformComponent>(e);
                bool selected = ctx.selected && (entt::entity)ctx.selected == e;
                glm::mat4 model = glm::translate(glm::mat4(1.0f), t.Translation);
                glm::vec3 he = glm::max(glm::abs(r.positionRange), glm::vec3(0.02f));
                proj.Box(dl, model, he, IM_COL32(240, 200, 70, selected ? 230 : 110), selected ? IM_COL32(240, 200, 70, 22) : 0, selected ? 1.6f : 1.0f);
            }
        }

        // Agents: heading arrow, ray sensors, live return.
        auto agents = registry.view<AgentComponent, TransformComponent>();
        for (auto e : agents){
            const auto& t = agents.get<TransformComponent>(e);
            const AgentState* state = env ? env->FindAgent(e) : nullptr;
            bool selected = ctx.selected && (entt::entity)ctx.selected == e;

            glm::vec3 base = t.Translation;
            glm::vec3 fwd = Environment::Forward(t.Rotation.y);
            glm::vec3 tip = base + fwd * 1.3f;
            ImU32 arrow = EditorUI::Accent(selected ? 1.0f : 0.75f);
            proj.Line(dl, base, tip, arrow, 2.5f);
            glm::vec3 side = glm::cross(fwd, glm::vec3(0, 1, 0)) * 0.25f;
            proj.Line(dl, tip, tip - fwd * 0.35f + side, arrow, 2.5f);
            proj.Line(dl, tip, tip - fwd * 0.35f - side, arrow, 2.5f);

            if (auto* sensor = registry.try_get<RaySensorComponent>(e)){
                std::vector<glm::vec3> dirs;
                glm::vec3 origin;
                Environment::SensorRays(t.Translation, t.Rotation.y, *sensor, dirs, origin);
                for (size_t i = 0; i < dirs.size(); i++){
                    float frac = 1.0f;
                    int tag = -1;
                    if (state && i < state->rayFractions.size()) { frac = state->rayFractions[i]; tag = state->rayTags[i]; }
                    glm::vec3 end = origin + dirs[i] * sensor->length * frac;
                    ImU32 col;
                    if (!state)            { col = IM_COL32(90, 200, 255, selected ? 140 : 60); }
                    else if (frac >= 0.999f){ col = IM_COL32(255, 255, 255, 45); }
                    else if (tag >= 0)     { col = (EditorUI::CategoryColor(tag) & 0x00FFFFFF) | (230u << 24); }
                    else                   { col = IM_COL32(210, 210, 210, 170); }
                    proj.Line(dl, origin, end, col, state && frac < 0.999f ? 1.6f : 1.0f);
                    ImVec2 s;
                    if (state && frac < 0.999f && proj.Point(end, s)) { dl->AddCircleFilled(s, 3.0f, col); }
                }
            }

            if (state){
                ImVec2 s;
                if (proj.Point(base + glm::vec3(0.0f, 1.3f, 0.0f), s)){
                    char label[48];
                    std::snprintf(label, sizeof(label), "%+.2f", state->episodeReturn);
                    ImVec2 ts = ImGui::CalcTextSize(label);
                    ImVec2 p(s.x - ts.x * 0.5f, s.y - ts.y);
                    ImU32 col = state->done ? EditorUI::MutedColor() : state->episodeReturn >= 0.0f ? EditorUI::Success() : EditorUI::Danger();
                    dl->AddRectFilled(ImVec2(p.x - 5, p.y - 2), ImVec2(p.x + ts.x + 5, p.y + ts.y + 2), IM_COL32(18, 19, 22, 190), 4.0f);
                    dl->AddText(p, col, label);
                }
            }
        }

        dl->PopClipRect();
    }
}
