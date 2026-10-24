"""Drive a Phoenix agent with random actions and print episode returns.

A smoke test for a new environment: needs only numpy. In the editor set an
agent's Policy to Python and enable Training > Python Bridge first.

    python python/examples/random_agent.py --episodes 5
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from phoenix_env import PhoenixEnv  # noqa: E402


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=5555)
    parser.add_argument("--episodes", type=int, default=5)
    args = parser.parse_args()

    env = PhoenixEnv(port=args.port)
    print(f"agent '{env.spec_info['name']}': obs {env.observation_space.shape}, actions {env.action_space}")
    for episode in range(args.episodes):
        obs, info = env.reset()
        total, steps, done = 0.0, 0, False
        while not done:
            obs, reward, terminated, truncated, info = env.step(env.action_space.sample())
            total += reward
            steps += 1
            done = terminated or truncated
        print(f"episode {episode + 1}: return {total:+.3f} in {steps} steps ({'goal/fail' if terminated else 'time limit'})")
    env.close()


if __name__ == "__main__":
    main()
