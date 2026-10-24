#pragma once
#include <Phoenix/core/base.h>
#include <Phoenix/Scene/Component.h>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <deque>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Phoenix{

    class Scene;

    // One finished environment episode (all agents done, or reset).
    struct EpisodeRecord{
        int episode;
        float meanReturn;  // average over the agents
        int length;        // decisions of the longest agent
        float simTime;     // simulated seconds since play started
    };

    // Episode returns/lengths and counters kept across Play sessions (owned by the editor).
    struct EpisodeLog{
        std::vector<EpisodeRecord> episodes;
        long long totalSteps = 0;     // decisions across all agents
        void Clear() { episodes.clear(); totalSteps = 0; }
    };

    // Live state of one agent (what the Agent Monitor shows).
    struct AgentState{
        entt::entity entity = entt::null;
        std::string name;
        AgentComponent::Policy policy = AgentComponent::Policy::Random;

        std::vector<float> observation;
        std::vector<std::string> observationLabels;
        std::vector<float> action;          // discrete: { index }; continuous: values in [-1, 1]
        int actionSize = 0;
        bool discrete = true;

        float lastReward = 0.0f;            // reward of the last completed decision
        float episodeReturn = 0.0f;
        int episodeStep = 0;                // decisions this episode
        int episodes = 0;
        float lastEpisodeReturn = 0.0f;
        bool done = false;
        bool truncated = false;             // ended by the step limit, not the task
        std::deque<float> returnTrace;      // cumulative return per decision (sparkline)

        // Ray sensor results for drawing: per ray, the hit fraction and detected tag index (-1 none).
        std::vector<float> rayFractions;
        std::vector<int> rayTags;

        // Internals.
        int stepsUntilDecision = 0;
        bool awaitingAction = false;        // External: observation ready, waiting for the client
        bool externalActionReady = false;
        float pendingReward = 0.0f;
        bool pendingEnd = false;
        bool pendingTruncate = false;
        float previousTargetDistance = -1.0f;
        std::unordered_set<uint32_t> zonesInside;
        std::vector<float> customObservations;
    };

    // Runs the scene as a reinforcement-learning environment while playing:
    //  - steps physics at a fixed timestep, time-scaled (or as fast as possible),
    //  - asks each agent's policy for an action every decisionPeriod steps and applies it,
    //  - accumulates rewards (step reward, approach shaping, reward zones, falls, scripts),
    //  - ends episodes and resets the scene to its edit-time layout plus randomization.
    // External agents run in lockstep with a client: the scene only advances on
    // ExternalStep(), so a Python trainer controls the pace.
    class Environment{
    public:
        Environment(Scene* scene, EpisodeLog* log);
        ~Environment();

        // Call right after Scene::OnRuntimeStart(): snapshots the layout, finds agents
        // and starts the first episode.
        void Start();

        // Advance by a frame of real time (scaled; capped by a per-frame time budget).
        // Does nothing while paused or while external agents drive the pace.
        void Update(float frameSeconds);
        // Advance exactly one decision of the slowest agent (works while paused).
        void StepDecision();
        // Reset all agents and the scene layout (starts a new episode).
        void ResetEpisode();

        // ---- Time control ----
        float timeScale = 1.0f;
        bool maxSpeed = false;      // run as many steps as fit in the frame budget
        bool paused = false;
        float frameBudgetMs = 14.0f;

        // ---- External (Python) lockstep ----
        bool HasExternalAgents() const;
        // True when external agents should be driven by a client instead of frame time.
        bool externalControl = false;
        // A bridge client is connected (it may use only the physics API, without agents).
        bool clientConnected = false;
        // The scene advances only on client requests (ExternalStep / Simulate).
        bool ClientDriven() const { return externalControl && (clientConnected || HasExternalAgents()); }
        // Advance `steps` physics steps (agents keep deciding with their own policies;
        // External agents hold still unless an action was sent). For physics-only clients.
        void Simulate(int steps);
        Scene* GetScene() { return m_Scene; }
        std::vector<AgentState*> ExternalAgents();
        void ExternalReset();
        // Re-seed episode randomization and random policies (Gymnasium reset(seed=...)).
        void Seed(unsigned seed) { m_Rng.seed(seed); }
        // actions[i] belongs to ExternalAgents()[i]; runs until every external agent
        // needs a new action again (or is done).
        void ExternalStep(const std::vector<std::vector<float>>& actions);

        // ---- Script hooks ----
        void AddReward(entt::entity agent, float reward);
        void EndEpisode(entt::entity agent);
        // Called from the script's CollectObservations().
        void AddObservation(entt::entity agent, float value);
        AgentState* FindAgent(entt::entity agent);

        // ---- Introspection for the editor ----
        std::vector<AgentState>& Agents() { return m_Agents; }
        int Episode() const { return m_Episode; }
        int EpisodeStep() const;
        long long PhysicsSteps() const { return m_PhysicsSteps; }
        float StepsPerSecond() const { return m_StepsPerSecond; }   // physics steps / real second
        float SimSpeed() const { return m_SimSpeed; }               // simulated / real seconds
        const EpisodeLog& Log() const { return *m_Log; }

        // Rays of a sensor in world space (also used by the viewport overlay in edit mode).
        static void SensorRays(const glm::vec3& position, float yaw, const RaySensorComponent& sensor,
                               std::vector<glm::vec3>& outDirections, glm::vec3& outOrigin);
        static glm::vec3 Forward(float yaw);
        // Names of the observation vector an agent will produce (its length is the
        // observation size); usable in edit mode for the inspector.
        static std::vector<std::string> DescribeObservations(Scene& scene, entt::entity agent);
        // Short names of an agent's actions ("Forward", "Turn", ...).
        static std::vector<std::string> DescribeActions(const AgentComponent& agent);

    private:
        void PhysicsStep(float dt);
        void PruneAgents();
        void BeginAgentEpisode(AgentState& agent);
        void Decide(AgentState& agent);
        void CollectObservation(AgentState& agent);
        void ChooseAction(AgentState& agent);
        void ApplyAction(AgentState& agent, float dt);
        void EvaluateZones(AgentState& agent, float dt);
        void FinishDecision(AgentState& agent);
        void FinishAgentEpisode(AgentState& agent);
        bool AllDone() const;
        void BuildObservationLabels(AgentState& agent);
        float TargetDistance(AgentState& agent);
        bool FindTarget(const AgentComponent& config, const glm::vec3& from, glm::vec3& outPosition);

        Scene* m_Scene;
        EpisodeLog* m_Log;
        std::vector<AgentState> m_Agents;
        std::unordered_map<uint32_t, TransformComponent> m_InitialTransforms;
        std::mt19937 m_Rng;

        int m_Episode = 0;
        long long m_PhysicsSteps = 0;
        float m_Accumulator = 0.0f;
        // Rolling speed measurement.
        double m_RateWindowStart = 0.0;
        long long m_RateSteps = 0;
        float m_RateSimTime = 0.0f;
        float m_StepsPerSecond = 0.0f;
        float m_SimSpeed = 0.0f;
    };
}
