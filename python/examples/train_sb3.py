"""Train a Phoenix agent with Stable-Baselines3 PPO, then watch the result.

    pip install gymnasium stable-baselines3
    python python/examples/train_sb3.py --steps 200000

In the editor: set one agent's Policy to Python, enable Training > Python Bridge,
and choose Max speed (the client drives the pace; rendering keeps up as it can).
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from phoenix_env import PhoenixEnv  # noqa: E402

try:
    from stable_baselines3 import PPO
except ImportError:
    sys.exit("This example needs stable-baselines3:  pip install gymnasium stable-baselines3")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=5555)
    parser.add_argument("--steps", type=int, default=200_000)
    parser.add_argument("--model", default="phoenix_ppo")
    parser.add_argument("--watch", type=int, default=5, help="episodes to run with the trained policy")
    args = parser.parse_args()

    env = PhoenixEnv(port=args.port)
    if os.path.exists(args.model + ".zip"):
        model = PPO.load(args.model, env=env)
        print(f"continuing from {args.model}.zip")
    else:
        model = PPO("MlpPolicy", env, n_steps=1024, batch_size=256, gamma=0.99, verbose=1)
    model.learn(total_timesteps=args.steps)
    model.save(args.model)
    print(f"saved {args.model}.zip")

    for episode in range(args.watch):
        obs, _ = env.reset()
        total, done = 0.0, False
        while not done:
            action, _ = model.predict(obs, deterministic=True)
            obs, reward, terminated, truncated, _ = env.step(action)
            total += reward
            done = terminated or truncated
        print(f"evaluation episode {episode + 1}: return {total:+.3f}")
    env.close()


if __name__ == "__main__":
    main()
