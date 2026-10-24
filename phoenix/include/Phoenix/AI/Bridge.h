#pragma once
#include <deque>
#include <string>

namespace YAML { class Node; }

namespace Phoenix{

    class Environment;

    // Local TCP server that exposes the playing scene to an external trainer (Python,
    // see python/phoenix_env.py) using newline-delimited JSON:
    //   {"cmd":"hello"}                      -> spaces of the External agents
    //   {"cmd":"reset","seed":N}             -> {"obs": [[...], ...]} (seed optional)
    //   {"cmd":"step","actions":[[...],...]} -> obs, rewards, terminated, truncated
    //   {"cmd":"close"}                      -> disconnects
    // plus the physics API (bodies, forces, ray casts, contacts, gravity, stepping),
    // see HandlePhysics in BridgePhysics.cpp and python/phoenix_env.py.
    // External agents run in lockstep: the scene advances only on "step". One client
    // at a time; everything is non-blocking and serviced from the editor's frame loop.
    class Bridge{
    public:
        Bridge() = default;
        ~Bridge();

        bool Start(int port);
        void Stop();
        // Close the current client's connection (it sees the socket close).
        void Disconnect() { DropClient(); }
        bool IsListening() const { return m_ListenSocket >= 0; }
        bool HasClient() const { return m_ClientSocket >= 0; }
        int Port() const { return m_Port; }
        const std::string& ClientAddress() const { return m_ClientAddress; }
        const std::string& LastError() const { return m_LastError; }

        // Accept/read and answer requests for up to budgetMs. `environment` is null when
        // the scene isn't playing: requests then wait in the queue and WantsPlay() turns
        // true so the editor can start the simulation for the client.
        void Process(Environment* environment, float budgetMs);
        bool WantsPlay() const { return m_WantsPlay; }

        long long RequestCount() const { return m_Requests; }
        float RequestsPerSecond() const { return m_Rate; }

    private:
        void Accept();
        bool ReadLines();
        void Send(const std::string& line);
        void DropClient();
        std::string Handle(const std::string& request, Environment& env);
        // Physics commands; returns false for commands it doesn't know.
        bool HandlePhysics(const std::string& cmd, const YAML::Node& msg, Environment& env, std::string& outReply);

        int m_ListenSocket = -1;
        int m_ClientSocket = -1;
        int m_Port = 0;
        std::string m_ClientAddress;
        std::string m_LastError;
        std::string m_ReadBuffer;
        std::deque<std::string> m_Pending;
        bool m_WantsPlay = false;
        long long m_Requests = 0;
        long long m_RateRequests = 0;
        double m_RateStart = 0.0;
        float m_Rate = 0.0f;
    };
}
