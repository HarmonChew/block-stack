"""Optional Gymnasium adapter for the authoritative frame-level C++ game.

The primary action space is five binary NES gameplay buttons in this order:
Left, Right, Down, clockwise rotation, counter-clockwise rotation. A discrete
bitmask action mode is also available. Reward policies belong to this adapter,
never to the simulation core.
"""

from __future__ import annotations

from dataclasses import asdict
from typing import Any, Callable

from . import Game, Input, State, Events

try:
    import gymnasium as gymnasium
    from gymnasium import spaces
    import numpy as np
except ImportError:
    gymnasium = None
    spaces = None
    np = None


def _requires_gymnasium() -> None:
    if gymnasium is None:
        raise ImportError("GymEnv requires gymnasium and numpy; install the optional Python AI dependencies")


_BaseEnv = gymnasium.Env if gymnasium is not None else object


class GymEnv(_BaseEnv):
    """Gymnasium environment with one controller-state action per logical frame."""

    metadata = {"render_modes": ["ansi"], "render_fps": 60}

    def __init__(self, *, ruleset: str = "classic_ntsc_extended", mode: str = "endless",
                 start_level: int = 0, height: int = 0, seed: int = 0x8988,
                 action_mode: str = "multibinary", observation_mode: str = "raw",
                 reward: str | Callable[[State, Events], float] = "score_delta",
                 max_frames: int | None = None, render_mode: str | None = None):
        _requires_gymnasium()
        if action_mode not in ("multibinary", "bitmask"):
            raise ValueError("action_mode must be 'multibinary' or 'bitmask'")
        if observation_mode not in ("raw", "neural"):
            raise ValueError("observation_mode must be 'raw' or 'neural'")
        if reward not in ("score_delta", "lines", "survival") and not callable(reward):
            raise ValueError("reward must be score_delta, lines, survival, or a callback")
        if max_frames is not None and max_frames <= 0:
            raise ValueError("max_frames must be positive")
        if render_mode not in (None, "ansi"):
            raise ValueError("only ANSI text rendering is supported by the headless adapter")

        self.game = Game(ruleset=ruleset, mode=mode, start_level=start_level, height=height, seed=seed)
        self.action_mode = action_mode
        self.observation_mode = observation_mode
        self.reward_policy = reward
        self.max_frames = max_frames
        self.render_mode = render_mode
        self.action_space = spaces.MultiBinary(5) if action_mode == "multibinary" else spaces.Discrete(32)
        self.observation_space = _raw_space() if observation_mode == "raw" else _neural_space()

    def reset(self, *, seed: int | None = None,
              options: dict[str, Any] | None = None) -> tuple[dict[str, Any], dict[str, Any]]:
        super().reset(seed=seed)
        state = self.game.reset(seed)
        return self._observe(state), {"state_hash": self.game.state_hash()}

    def step(self, action: Any) -> tuple[dict[str, Any], float, bool, bool, dict[str, Any]]:
        mask = self._decode_action(action)
        state, events = self.game.step(mask)
        reward = self._reward(state, events)
        terminated = state.terminal
        truncated = self.max_frames is not None and state.frame >= self.max_frames and not terminated
        info = {"events": asdict(events), "state_hash": self.game.state_hash(),
                "four_line_rate": state.stats.four_line_rate}
        return self._observe(state), reward, terminated, truncated, info

    def render(self) -> str:
        return self.game.debug_string()

    def close(self) -> None:
        self.game.close()

    def _decode_action(self, action: Any) -> int:
        if self.action_mode == "bitmask":
            mask = int(action)
            if not 0 <= mask < 32:
                raise ValueError("bitmask action must be 0..31")
            return mask
        if isinstance(action, (int, Input)):
            mask = int(action)
            if not 0 <= mask < 32:
                raise ValueError("gameplay input must use only the lower five bits")
            return mask
        values = np.asarray(action)
        if values.shape != (5,) or not np.all((values == 0) | (values == 1)):
            raise ValueError("multibinary action must contain five 0/1 button states")
        return sum(int(value) << index for index, value in enumerate(values))

    def _reward(self, state: State, events: Events) -> float:
        if callable(self.reward_policy):
            return float(self.reward_policy(state, events))
        if self.reward_policy == "score_delta":
            return float(events.score_delta)
        if self.reward_policy == "lines":
            return float(events.lines_cleared)
        return 1.0 if not state.terminal else 0.0

    def _observe(self, state: State) -> dict[str, Any]:
        return _raw_observation(state) if self.observation_mode == "raw" else _neural_observation(state)


def _scalar(value: int, dtype: Any) -> Any:
    return np.asarray(value, dtype=dtype)


def _raw_space() -> Any:
    u32 = np.iinfo(np.uint32).max
    u64 = np.iinfo(np.uint64).max
    i32 = np.iinfo(np.int32)
    scalar = lambda low, high, dtype: spaces.Box(low=low, high=high, shape=(), dtype=dtype)
    return spaces.Dict({
        "board": spaces.Box(0, 7, shape=(20, 10), dtype=np.uint8),
        "hidden_rows": spaces.Box(0, 7, shape=(2, 10), dtype=np.uint8),
        "current_piece": spaces.Discrete(8),
        "next_piece": spaces.Discrete(8),
        "previous_piece": spaces.Discrete(8),
        "phase": spaces.Discrete(5),
        "position": spaces.Box(i32.min, i32.max, shape=(2,), dtype=np.int32),
        "orientation": spaces.Discrete(4),
        "frame": scalar(0, u64, np.uint64),
        "score": scalar(0, u64, np.uint64),
        "lines": scalar(0, i32.max, np.int32),
        "level": scalar(0, i32.max, np.int32),
        "gravity_counter": scalar(i32.min, i32.max, np.int32),
        "first_delay_remaining": scalar(i32.min, i32.max, np.int32),
        "das_counter": scalar(i32.min, i32.max, np.int32),
        "das_direction": scalar(-1, 1, np.int32),
        "previous_input": spaces.Discrete(128),
        "soft_drop_counter": scalar(i32.min, i32.max, np.int32),
        "soft_drop_cells": scalar(i32.min, i32.max, np.int32),
        "entry_remaining": scalar(i32.min, i32.max, np.int32),
        "clear_remaining": scalar(i32.min, i32.max, np.int32),
        "clearing_rows": scalar(0, u32, np.uint32),
        "rng_state": spaces.Discrete(65536),
        "piece_count": scalar(0, u32, np.uint32),
        "stats": spaces.Box(0, u64, shape=(18,), dtype=np.uint64),
        "terminal": spaces.Discrete(2),
        "ruleset": spaces.Discrete(2),
        "mode": spaces.Discrete(2),
        "start_level": scalar(0, i32.max, np.int32),
        "height": scalar(0, 5, np.int32),
        "seed": spaces.Discrete(65536),
    })


def _raw_observation(state: State) -> dict[str, Any]:
    stats = state.stats
    stats_array = [*(stats.piece_counts[piece] for piece in ("I", "J", "L", "O", "S", "T", "Z")),
                   *(stats.clear_counts[name] for name in ("single", "double", "triple", "four_line")),
                   stats.pieces, stats.current_i_drought, stats.max_i_drought,
                   stats.transition_score, stats.level_19_score, stats.level_29_score,
                   stats.input_presses]
    return {
        "board": np.asarray(state.board, dtype=np.uint8).copy(),
        "hidden_rows": np.asarray(state.hidden_rows, dtype=np.uint8).copy(),
        "current_piece": state.piece_id,
        "next_piece": state.next_piece_id,
        "previous_piece": ("I", "J", "L", "O", "S", "T", "Z", "None").index(state.previous_piece),
        "phase": state.phase_id,
        "position": np.asarray((state.x, state.y), dtype=np.int32),
        "orientation": state.orientation,
        "frame": _scalar(state.frame, np.uint64),
        "score": _scalar(state.score, np.uint64),
        "lines": _scalar(state.lines, np.int32),
        "level": _scalar(state.level, np.int32),
        "gravity_counter": _scalar(state.gravity_counter, np.int32),
        "first_delay_remaining": _scalar(state.first_delay_remaining, np.int32),
        "das_counter": _scalar(state.das_counter, np.int32),
        "das_direction": _scalar(state.das_direction, np.int32),
        "previous_input": state.previous_input,
        "soft_drop_counter": _scalar(state.soft_drop_counter, np.int32),
        "soft_drop_cells": _scalar(state.soft_drop_cells, np.int32),
        "entry_remaining": _scalar(state.entry_remaining, np.int32),
        "clear_remaining": _scalar(state.clear_remaining, np.int32),
        "clearing_rows": _scalar(state.clearing_rows, np.uint32),
        "rng_state": state.rng_state,
        "piece_count": _scalar(state.piece_count, np.uint32),
        "stats": np.asarray(stats_array, dtype=np.uint64),
        "terminal": int(state.terminal),
        "ruleset": ("classic_ntsc_strict", "classic_ntsc_extended").index(state.ruleset),
        "mode": ("endless", "challenge").index(state.mode),
        "start_level": _scalar(state.start_level, np.int32),
        "height": _scalar(state.height, np.int32),
        "seed": state.seed,
    }


def _neural_space() -> Any:
    return spaces.Dict({
        "board_channels": spaces.Box(0, 1, shape=(20, 10, 8), dtype=np.uint8),
        "current_piece": spaces.Box(0, 1, shape=(8,), dtype=np.uint8),
        "next_piece": spaces.Box(0, 1, shape=(8,), dtype=np.uint8),
        "orientation": spaces.Box(0, 1, shape=(4,), dtype=np.uint8),
        "timing": spaces.Box(-1, 1, shape=(12,), dtype=np.float32),
    })


def _neural_observation(state: State) -> dict[str, Any]:
    board = np.asarray(state.board, dtype=np.uint8)
    channels = np.empty((20, 10, 8), dtype=np.uint8)
    channels[:, :, 0] = board != 0
    for piece_id in range(1, 8):
        channels[:, :, piece_id] = board == piece_id
    current = np.zeros(8, dtype=np.uint8)
    current[state.piece_id] = 1
    next_piece = np.zeros(8, dtype=np.uint8)
    next_piece[state.next_piece_id] = 1
    orientation = np.zeros(4, dtype=np.uint8)
    orientation[state.orientation % 4] = 1
    timing = np.asarray((
        state.x / 10, state.y / 20, min(state.level / 100, 1),
        min(state.lines / 1000, 1), min(state.gravity_counter / 48, 1),
        min(state.das_counter / 16, 1), state.das_direction,
        min(state.soft_drop_counter / 48, 1), min(state.entry_remaining / 20, 1),
        min(state.clear_remaining / 25, 1), state.rng_state / 65535,
        state.phase_id / 4,
    ), dtype=np.float32)
    return {"board_channels": channels, "current_piece": current,
            "next_piece": next_piece, "orientation": orientation, "timing": timing}


def make(**kwargs: Any) -> GymEnv:
    """Construct a Gymnasium-compatible frame-level environment."""
    return GymEnv(**kwargs)


__all__ = ["GymEnv", "make"]
