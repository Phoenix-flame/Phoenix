"""Gymnasium environments backed by a Phoenix editor scene.

In the editor: give one or more agents the *Python* policy (Agent > Policy), then
tick *Training > Python Bridge > Listen for a Python client*. The scene advances
only when this client steps it, so any RL library can train against it:

    from phoenix_env import PhoenixEnv
    env = PhoenixEnv(port=5555)
    obs, info = env.reset()
    obs, reward, terminated, truncated, info = env.step(env.action_space.sample())

`PhoenixEnv` controls one agent (the others with the Python policy receive a
no-op action); `PhoenixMultiAgentEnv` steps all of them together.

The wire protocol is newline-delimited JSON over a local TCP socket, so this
module only needs numpy; gymnasium is used when installed (for compatibility
with Stable-Baselines3 and friends) and replaced by small stand-ins otherwise.
"""

import json
import socket

import numpy as np

try:
    import gymnasium as gym
    from gymnasium import spaces

    _EnvBase = gym.Env
except ImportError:  # minimal stand-ins so the client also works without gymnasium
    gym = None

    class _Discrete:
        def __init__(self, n):
            self.n = int(n)
            self.shape = ()
            self._rng = np.random.default_rng()

        def sample(self):
            return int(self._rng.integers(self.n))

        def __repr__(self):
            return f"Discrete({self.n})"

    class _Box:
        def __init__(self, low, high, shape, dtype=np.float32):
            self.low, self.high, self.shape, self.dtype = low, high, tuple(shape), dtype
            self._rng = np.random.default_rng()

        def sample(self):
            low = -1.0 if not np.isfinite(self.low) else self.low
            high = 1.0 if not np.isfinite(self.high) else self.high
            return self._rng.uniform(low, high, self.shape).astype(self.dtype)

        def __repr__(self):
            return f"Box({self.low}, {self.high}, {self.shape})"

    class spaces:  # noqa: N801 - mirrors gymnasium.spaces
        Discrete = _Discrete
        Box = _Box

    _EnvBase = object


class PhoenixError(RuntimeError):
    """The editor rejected a request (no Python agents, bad action shape, ...)."""


class PhoenixClient:
    """Line-oriented JSON request/response connection to the editor's bridge."""

    def __init__(self, host="127.0.0.1", port=5555, timeout=60.0):
        self._sock = socket.create_connection((host, port), timeout=timeout)
        self._sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self._file = self._sock.makefile("rb")

    def request(self, message):
        self._sock.sendall((json.dumps(message) + "\n").encode())
        line = self._file.readline()
        if not line:
            raise ConnectionError("the Phoenix editor closed the connection")
        reply = json.loads(line)
        if not reply.get("ok", False):
            raise PhoenixError(reply.get("error", "request failed"))
        return reply

    def close(self):
        try:
            self.request({"cmd": "close"})
        except (OSError, ConnectionError, PhoenixError, ValueError):
            pass
        self._file.close()
        self._sock.close()


def _make_spaces(agent):
    observation = spaces.Box(-np.inf, np.inf, (agent["obs_size"],), np.float32)
    if agent["action_type"] == "discrete":
        action = spaces.Discrete(agent["action_size"])
    else:
        action = spaces.Box(-1.0, 1.0, (agent["action_size"],), np.float32)
    return observation, action


def _encode_action(agent, action):
    if agent["action_type"] == "discrete":
        return [int(np.asarray(action).reshape(-1)[0])]
    return np.asarray(action, dtype=np.float32).reshape(-1).tolist()


def _noop(agent):
    return [0] if agent["action_type"] == "discrete" else [0.0] * agent["action_size"]


class PhoenixEnv(_EnvBase):
    """Single-agent Gymnasium environment for one Python-policy agent of the scene."""

    metadata = {"render_modes": []}

    def __init__(self, host="127.0.0.1", port=5555, agent=0, timeout=60.0):
        self._client = PhoenixClient(host, port, timeout)
        self.agents = self._client.request({"cmd": "hello"})["agents"]
        if not 0 <= agent < len(self.agents):
            raise PhoenixError(f"agent index {agent} out of range ({len(self.agents)} Python agents)")
        self.agent_index = agent
        self.spec_info = self.agents[agent]
        self.observation_space, self.action_space = _make_spaces(self.spec_info)
        self.observation_labels = self.spec_info.get("obs_labels", [])
        self.episode = 0  # the editor's episode counter, updated on reset()

    def reset(self, *, seed=None, options=None):
        if gym is not None:
            super().reset(seed=seed)
        message = {"cmd": "reset"}
        if seed is not None:
            message["seed"] = int(seed)
        reply = self._client.request(message)
        self.episode = reply["episode"]
        obs = np.asarray(reply["obs"][self.agent_index], dtype=np.float32)
        return obs, {}

    def step(self, action):
        actions = [_noop(a) for a in self.agents]
        actions[self.agent_index] = _encode_action(self.spec_info, action)
        reply = self._client.request({"cmd": "step", "actions": actions})
        i = self.agent_index
        obs = np.asarray(reply["obs"][i], dtype=np.float32)
        return obs, float(reply["rewards"][i]), bool(reply["terminated"][i]), bool(reply["truncated"][i]), {}

    def close(self):
        self._client.close()


class PhoenixMultiAgentEnv:
    """All Python-policy agents stepped together (lists in, lists out).

    An agent whose episode ended stays frozen until `reset()`, which restarts the
    whole scene; check `all(terminated[i] or truncated[i])` to know when.
    """

    def __init__(self, host="127.0.0.1", port=5555, timeout=60.0):
        self._client = PhoenixClient(host, port, timeout)
        self.agents = self._client.request({"cmd": "hello"})["agents"]
        self.num_agents = len(self.agents)
        built = [_make_spaces(a) for a in self.agents]
        self.observation_spaces = [b[0] for b in built]
        self.action_spaces = [b[1] for b in built]

    def reset(self, seed=None):
        message = {"cmd": "reset"}
        if seed is not None:
            message["seed"] = int(seed)
        reply = self._client.request(message)
        return [np.asarray(o, dtype=np.float32) for o in reply["obs"]], {"episode": reply["episode"]}

    def step(self, actions):
        encoded = [_encode_action(a, act) for a, act in zip(self.agents, actions)]
        reply = self._client.request({"cmd": "step", "actions": encoded})
        obs = [np.asarray(o, dtype=np.float32) for o in reply["obs"]]
        return obs, reply["rewards"], reply["terminated"], reply["truncated"], {"episode": reply["episode"]}

    def close(self):
        self._client.close()
