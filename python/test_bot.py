# pip install gymnasium  (python/ in the repository)
from phoenix_env import PhoenixEnv
env = PhoenixEnv(port=5555)
obs, info = env.reset()
for _ in range(1000):
    obs, reward, terminated, truncated, info = env.step(env.action_space.sample())
    if terminated or truncated:
        obs, info = env.reset()

