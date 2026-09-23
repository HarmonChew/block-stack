"""Minimal Gymnasium agent with frame-level controller actions."""

from project_name.gym import make


def main() -> None:
    env = make(start_level=18, action_mode="bitmask", max_frames=10_000)
    try:
        observation, info = env.reset(seed=42)
        while True:
            action = env.action_space.sample()
            observation, reward, terminated, truncated, info = env.step(action)
            if terminated or truncated:
                break
        print("score", int(observation["score"]), "hash", f"{info['state_hash']:016x}")
    finally:
        env.close()


if __name__ == "__main__":
    main()
