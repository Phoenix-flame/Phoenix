#include <Phoenix/AI/Bridge.h>
#include <Phoenix/AI/Environment.h>
#include <Phoenix/core/log.h>
#include <yaml-cpp/yaml.h>

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace Phoenix{

    static double NowSeconds(){
        using namespace std::chrono;
        return duration<double>(steady_clock::now().time_since_epoch()).count();
    }

    static void SetNonBlocking(int fd){
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    // ---- Minimal JSON writing (the protocol only needs numbers, bools and strings) ----

    static void AppendNumber(std::string& out, float v){
        if (!std::isfinite(v)) { v = 0.0f; }
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.6g", v);
        out += buf;
    }

    static void AppendString(std::string& out, const std::string& s){
        out += '"';
        for (char c : s){
            if (c == '"' || c == '\\') { out += '\\'; out += c; }
            else if ((unsigned char)c < 0x20) { out += ' '; }
            else { out += c; }
        }
        out += '"';
    }

    static void AppendArray(std::string& out, const std::vector<float>& values){
        out += '[';
        for (size_t i = 0; i < values.size(); i++){
            if (i) { out += ','; }
            AppendNumber(out, values[i]);
        }
        out += ']';
    }

    Bridge::~Bridge(){
        Stop();
    }

    bool Bridge::Start(int port){
        Stop();
        m_LastError.clear();
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0){ m_LastError = std::strerror(errno); return false; }
        int yes = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

        // Loopback only: the bridge runs scene scripts' worlds for a local trainer and
        // has no authentication, so it must not be reachable from the network.
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons((uint16_t)port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(fd, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(fd, 1) != 0){
            m_LastError = std::strerror(errno);
            close(fd);
            PHX_CORE_ERROR("Python bridge: cannot listen on port {0}: {1}", port, m_LastError);
            return false;
        }
        SetNonBlocking(fd);
        m_ListenSocket = fd;
        m_Port = port;
        m_RateStart = NowSeconds();
        PHX_CORE_INFO("Python bridge listening on 127.0.0.1:{0}", port);
        return true;
    }

    void Bridge::Stop(){
        DropClient();
        if (m_ListenSocket >= 0){
            close(m_ListenSocket);
            m_ListenSocket = -1;
        }
        m_WantsPlay = false;
    }

    void Bridge::DropClient(){
        if (m_ClientSocket >= 0){
            close(m_ClientSocket);
            m_ClientSocket = -1;
            PHX_CORE_INFO("Python bridge: client {0} disconnected", m_ClientAddress);
        }
        m_ClientAddress.clear();
        m_ReadBuffer.clear();
        m_Pending.clear();
        m_WantsPlay = false;
    }

    void Bridge::Accept(){
        if (m_ListenSocket < 0 || m_ClientSocket >= 0) { return; }
        sockaddr_in addr{};
        socklen_t len = sizeof(addr);
        int fd = accept(m_ListenSocket, (sockaddr*)&addr, &len);
        if (fd < 0) { return; }
        SetNonBlocking(fd);
        int yes = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)); // small lockstep messages
        m_ClientSocket = fd;
        char ip[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
        m_ClientAddress = std::string(ip) + ":" + std::to_string(ntohs(addr.sin_port));
        PHX_CORE_INFO("Python bridge: client {0} connected", m_ClientAddress);
    }

    bool Bridge::ReadLines(){
        if (m_ClientSocket < 0) { return false; }
        char buf[65536];
        for (;;){
            ssize_t n = recv(m_ClientSocket, buf, sizeof(buf), 0);
            if (n > 0){
                m_ReadBuffer.append(buf, (size_t)n);
                if (m_ReadBuffer.size() > (64u << 20)) { DropClient(); return false; } // runaway client
                continue;
            }
            if (n == 0) { DropClient(); return false; }
            if (errno == EAGAIN || errno == EWOULDBLOCK) { break; }
            DropClient();
            return false;
        }
        size_t pos;
        while ((pos = m_ReadBuffer.find('\n')) != std::string::npos){
            std::string line = m_ReadBuffer.substr(0, pos);
            m_ReadBuffer.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') { line.pop_back(); }
            if (!line.empty()) { m_Pending.push_back(line); }
        }
        return true;
    }

    void Bridge::Send(const std::string& line){
        if (m_ClientSocket < 0) { return; }
        std::string data = line + "\n";
        size_t sent = 0;
        const double deadline = NowSeconds() + 2.0;
        while (sent < data.size()){
            ssize_t n = send(m_ClientSocket, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
            if (n > 0) { sent += (size_t)n; continue; }
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && NowSeconds() < deadline) { continue; }
            DropClient();
            return;
        }
    }

    void Bridge::Process(Environment* environment, float budgetMs){
        if (m_ListenSocket < 0) { return; }
        Accept();
        const double start = NowSeconds();
        bool answered = false;
        while (m_ClientSocket >= 0){
            if (m_Pending.empty() && !ReadLines()) { break; }
            if (m_Pending.empty()){
                // Lockstep: the client sends its next request right after our reply, so
                // wait a moment for it instead of giving up until the next frame.
                double left = budgetMs - (NowSeconds() - start) * 1000.0;
                if (!answered || !environment || left <= 0.0) { break; }
                pollfd pfd{ m_ClientSocket, POLLIN, 0 };
                if (poll(&pfd, 1, (int)std::min(5.0, std::max(1.0, left))) <= 0) { break; }
                continue;
            }
            if (!environment){
                m_WantsPlay = true; // answer once the scene is playing
                break;
            }
            m_WantsPlay = false;
            std::string request = m_Pending.front();
            m_Pending.pop_front();
            Send(Handle(request, *environment));
            m_Requests++;
            answered = true;
            if ((NowSeconds() - start) * 1000.0 > budgetMs) { break; }
        }

        double now = NowSeconds();
        if (now - m_RateStart >= 0.5){
            m_Rate = (float)((double)(m_Requests - m_RateRequests) / (now - m_RateStart));
            m_RateRequests = m_Requests;
            m_RateStart = now;
        }
    }

    // Observations + per-agent outcome of the External agents.
    static std::string StateJson(Environment& env, bool includeRewards){
        std::vector<AgentState*> agents = env.ExternalAgents();
        std::string out = "{\"ok\":true,\"obs\":[";
        for (size_t i = 0; i < agents.size(); i++){
            if (i) { out += ','; }
            AppendArray(out, agents[i]->observation);
        }
        out += ']';
        if (includeRewards){
            out += ",\"rewards\":[";
            for (size_t i = 0; i < agents.size(); i++){ if (i) { out += ','; } AppendNumber(out, agents[i]->lastReward); }
            out += "],\"terminated\":[";
            for (size_t i = 0; i < agents.size(); i++){ if (i) { out += ','; } out += (agents[i]->done && !agents[i]->truncated) ? "true" : "false"; }
            out += "],\"truncated\":[";
            for (size_t i = 0; i < agents.size(); i++){ if (i) { out += ','; } out += (agents[i]->done && agents[i]->truncated) ? "true" : "false"; }
            out += ']';
        }
        out += ",\"episode\":" + std::to_string(env.Episode()) + "}";
        return out;
    }

    std::string Bridge::Handle(const std::string& request, Environment& env){
        YAML::Node msg;
        try { msg = YAML::Load(request); }
        catch (const std::exception& e){
            std::string out = "{\"ok\":false,\"error\":";
            AppendString(out, std::string("bad JSON: ") + e.what());
            return out + "}";
        }
        std::string cmd = msg["cmd"] ? msg["cmd"].as<std::string>("") : "";

        if (cmd == "hello" || cmd == "spec"){
            std::string out = "{\"ok\":true,\"version\":1,\"agents\":[";
            std::vector<AgentState*> agents = env.ExternalAgents();
            for (size_t i = 0; i < agents.size(); i++){
                if (i) { out += ','; }
                out += "{\"name\":";
                AppendString(out, agents[i]->name);
                out += ",\"obs_size\":" + std::to_string(agents[i]->observationLabels.size());
                out += ",\"action_type\":";
                out += agents[i]->discrete ? "\"discrete\"" : "\"continuous\"";
                out += ",\"action_size\":" + std::to_string(agents[i]->actionSize);
                out += ",\"obs_labels\":[";
                for (size_t k = 0; k < agents[i]->observationLabels.size(); k++){
                    if (k) { out += ','; }
                    AppendString(out, agents[i]->observationLabels[k]);
                }
                out += "]}";
            }
            out += "]}";
            if (agents.empty())
                return "{\"ok\":false,\"error\":\"no agents use the External policy (set Policy = External on an Agent)\"}";
            return out;
        }
        if (cmd == "reset"){
            if (msg["seed"] && !msg["seed"].IsNull()) { env.Seed(msg["seed"].as<unsigned>(0u)); }
            env.ExternalReset();
            return StateJson(env, false);
        }
        if (cmd == "step"){
            std::vector<std::vector<float>> actions;
            try{
                for (const auto& row : msg["actions"]){
                    std::vector<float> a;
                    if (row.IsSequence()) { for (const auto& v : row) { a.push_back(v.as<float>(0.0f)); } }
                    else                  { a.push_back(row.as<float>(0.0f)); }
                    actions.push_back(a);
                }
            }
            catch (const std::exception&){
                return "{\"ok\":false,\"error\":\"actions must be a list (one entry per agent)\"}";
            }
            env.ExternalStep(actions);
            return StateJson(env, true);
        }
        if (cmd == "close"){
            Send("{\"ok\":true}");
            DropClient();
            return "";
        }
        std::string out = "{\"ok\":false,\"error\":";
        AppendString(out, "unknown cmd '" + cmd + "'");
        return out + "}";
    }
}
