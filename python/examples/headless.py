"""Run a deterministic headless game with one controller input per frame."""

from project_name import DOWN, Game, LEFT, NONE, RIGHT


def main() -> None:
    with Game(start_level=18, seed=42) as game:
        game.reset()
        for frame in range(10_000):
            if game.terminal:
                break
            action = DOWN | (LEFT if frame % 24 < 8 else RIGHT if frame % 24 < 16 else NONE)
            game.step_frame(action)
        state = game.state
        print(f"frames={state.frame} score={state.score} lines={state.lines} hash={game.state_hash():016x}")
        print(game.debug_string())


if __name__ == "__main__":
    main()
