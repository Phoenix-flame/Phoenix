"""Gymnasium environments backed by a Phoenix editor scene.

In the editor: give one or more agents the *Python* policy (Agent > Policy), then
tick *Training > Python Bridge > Listen for a Python client*. The scene advances
only when this client steps it, so any RL library can train against it:

    from phoenix_env import PhoenixEnv
    env = PhoenixEnv(port=5555)
    obs, info = env.reset()
    obs, reward, terminated, truncated, info = env.step(env.action_space.sample())

`PhoenixEnv` controls one agent (the others with the Python policy receive a
no-op action); `PhoenixMultiAgentEnv` steps all of them together. Every env has a
`physics` attribute (`PhoenixPhysics`) to read and drive rigid bodies directly, and
`PhoenixSim` gives that physics API alone, for scenes without Python agents:

    from phoenix_env import PhoenixSim
    sim = PhoenixSim(port=5555)
    sim.reset()
    ball = sim.physics.body("Ball")                 # by tag or id
    sim.physics.apply_impulse("Ball", [0, 5, 0])
    sim.physics.simulate(60)                        # one simulated second at 60 Hz
    hit = sim.physics.raycast([0, 5, 0], [0, -1, 0])

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


def _vec3(v):
    return [float(x) for x in np.asarray(v, dtype=np.float64).reshape(3)]


class PhoenixPhysics:
    """Rigid-body access to the playing scene (shares the env's connection).

    Entities are addressed by id (see `bodies()`) or tag (first match). Vectors are
    [x, y, z]; rotations are XYZ Euler angles in radians. Bodies are returned as
    dicts with id, tag, type, position, rotation, scale, velocity, angular_velocity,
    mass, friction and restitution. Forces and torques act during the next physics
    step; impulses change velocity immediately.
    """

    def __init__(self, client):
        self._client = client

    def _call(self, cmd, **args):
        args = {k: v for k, v in args.items() if v is not None}
        return self._client.request({"cmd": cmd, **args})

    def info(self):
        """Gravity, fixed timestep, simulated time and the number of bodies."""
        return self._call("physics_info")

    def set_gravity(self, gravity):
        self._call("set_gravity", gravity=_vec3(gravity))

    def bodies(self, tag=None, all=False):
        """Every rigid body (or every entity with all=True), optionally filtered by tag."""
        return self._call("bodies", tag=tag, all=bool(all))["bodies"]

    def body(self, entity):
        return self._call("body", entity=entity)["body"]

    def set_body(self, entity, position=None, rotation=None, velocity=None, angular_velocity=None):
        """Teleport and/or set velocities; returns the updated body."""
        return self._call("set_body", entity=entity,
                          position=None if position is None else _vec3(position),
                          rotation=None if rotation is None else _vec3(rotation),
                          velocity=None if velocity is None else _vec3(velocity),
                          angular_velocity=None if angular_velocity is None else _vec3(angular_velocity))["body"]

    def apply_force(self, entity, force, point=None):
        """Newtons, for the next step; at a world point it also adds torque."""
        self._call("apply_force", entity=entity, force=_vec3(force), point=None if point is None else _vec3(point))

    def apply_impulse(self, entity, impulse, point=None):
        self._call("apply_impulse", entity=entity, impulse=_vec3(impulse), point=None if point is None else _vec3(point))

    def apply_torque(self, entity, torque):
        self._call("apply_torque", entity=entity, torque=_vec3(torque))

    def apply_angular_impulse(self, entity, impulse):
        self._call("apply_angular_impulse", entity=entity, impulse=_vec3(impulse))

    def set_material(self, entity, friction=None, restitution=None):
        self._call("set_material", entity=entity, friction=friction, restitution=restitution)

    def raycast(self, origin, direction, max_distance=100.0, ignore=None):
        """First hit as a dict (entity, tag, point, normal, distance), or None."""
        reply = self._call("raycast", origin=_vec3(origin), direction=_vec3(direction),
                           max_distance=float(max_distance), ignore=ignore)
        return reply if reply.get("hit") else None

    def contacts(self):
        """Contacts that began (entered=True) or ended since the last call."""
        return self._call("contacts")["contacts"]

    def simulate(self, steps=1):
        """Advance `steps` fixed physics steps; returns the simulated time."""
        return self._call("simulate", steps=int(steps))["time"]


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
        self.physics = PhoenixPhysics(self._client)
        self.agents = self._client.request({"cmd": "hello"})["agents"]
        if not self.agents:
            raise PhoenixError("no agent uses the Python policy (use PhoenixSim for physics-only scenes)")
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
        self.physics = PhoenixPhysics(self._client)
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


class PhoenixSim:
    """Physics-only connection: no agents needed. The scene advances only through
    `physics.simulate(steps)` (and `reset()` restores the edit-time layout plus
    randomization), so a script fully controls the simulation."""

    def __init__(self, host="127.0.0.1", port=5555, timeout=60.0):
        self._client = PhoenixClient(host, port, timeout)
        self.physics = PhoenixPhysics(self._client)
        self.agents = self._client.request({"cmd": "hello"})["agents"]

    def reset(self, seed=None):
        message = {"cmd": "reset"}
        if seed is not None:
            message["seed"] = int(seed)
        self._client.request(message)

    def close(self):
        self._client.close()
