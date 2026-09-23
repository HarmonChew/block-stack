# AI and simulation API

The simulation is frame based. An action is the complete controller state for **one** logical frame; consecutive frames with the same bit set mean the button remains held. No interface asks the engine to move a piece directly to a target column.

## Input bits

| Bit | Decimal | C++ | Python | Action |
|---:|---:|---|---|---|
| 0 | 1 | `blocks::Left` | `Input.LEFT` / `LEFT` | Hold left |
| 1 | 2 | `blocks::Right` | `Input.RIGHT` / `RIGHT` | Hold right |
| 2 | 4 | `blocks::Down` | `Input.DOWN` / `DOWN` | Soft drop |
| 3 | 8 | `blocks::RotateCW` | `Input.CW` / `CW` | Rotate clockwise on press edge |
| 4 | 16 | `blocks::RotateCCW` | `Input.CCW` / `CCW` | Rotate counterclockwise on press edge |
| 5 | 32 | `blocks::Start` | `Input.START` | Reserved controller bit; the current core does not pause on it |
| 6 | 64 | `blocks::Select` | `Input.SELECT` | Reserved controller bit; the current core does not toggle preview on it |

The current core accepts masks 0–127 and rejects other bits. The Gymnasium adapter exposes only the five gameplay bits (0–4); it has no pause/select actions. A `0` action releases all buttons.

## C++

`#include <blocks/game.hpp>` and link `blocks_core`:

```cpp
blocks::Config config;
config.ruleset = blocks::RulesetId::ClassicNtscExtended;
config.mode = blocks::Mode::Endless;
config.start_level = 18;
config.seed = 42;

blocks::Game game(config);
for (int frame = 0; frame < 1000 && !game.terminal(); ++frame) {
    const blocks::InputFrame input = frame % 12 < 4 ? blocks::Left | blocks::Down : 0;
    const blocks::Events events = game.tick(input);
    if (events.lines_cleared) { /* evaluate the new state */ }
}
const auto& state = game.state();
const auto hash = game.state_hash();
```

`Game::state()` exposes the full `State`, including locked board cells, hidden rows, current and next piece, phase, frame, score, lines, level, DAS and gravity counters, RNG state, and statistics. `Game::occupancy()` returns only locked visible cells as 0/1. `Game::save_state()` and `load_state()` serialize and restore the complete deterministic state. `set_board`, `set_piece`, and `set_state` are research/testing hooks; regular play should advance with `tick()`.

`Events` describes the latest frame: movement, rotation, lock, spawn, automatic or soft downward motion, line count, score delta, level transition, top-out, and challenge completion. The active piece is not included in `State::board` until it locks. Coordinates use top-left origin and a 10×20 visible field; `State::hidden_rows` covers y = −2 and −1.

## Python

Build the `blocks_native` target and point `BLOCKS_NATIVE_LIB` at its shared library when needed. Set `PYTHONPATH=python` to import from this checkout. The package uses `ctypes` over the C ABI and works without NumPy; if NumPy is available, `State.board` is a read-only `(20, 10)` `uint8` array. Otherwise it is a tuple of rows.

```python
from project_name import Game, LEFT, DOWN, VectorEnv

with Game(ruleset="classic_ntsc_extended", start_level=18, seed=42) as game:
    state, events = game.step(LEFT | DOWN)
    for _ in range(100):
        events = game.step_frame(0)
    checkpoint = game.save_state()
    copy = game.clone()
    assert copy.state_hash() == game.state_hash()
    copy.close()

with VectorEnv(32, seed=42, auto_reset=True) as envs:
    states, events = envs.step([0] * 32)
```

`Game.step_frame(mask)` returns only events; `Game.step(mask)` returns `(State, Events)`. `Game.reset(seed=None)` returns a new `State`; an explicit seed replaces the seed used by later no-argument resets. `Game.clone()`, `save_state()`/`load_state()`, `state_hash()`, `set_board()`, `set_piece()`, and `debug_string()` support search and testing. `VectorEnv` steps independent C++ games in one C call. Environment `i` starts from `(base_seed + i) & 0xffff`; automatic resets advance that seed by the environment count.

The C ABI is declared in [`core/include/blocks/c_api.h`](../core/include/blocks/c_api.h). Handles are caller owned. Functions return `0` on success or `-1`/null on failure; `blocks_last_error()` holds the calling thread's latest error text. For `blocks_game_save_state` and `blocks_game_debug_string`, call first with a null output pointer to obtain the required buffer length. `blocks_game_tick_many` processes games in array order; callers should validate inputs before the call if they need all-or-nothing batch behavior.

## Gymnasium adapter

Install `gymnasium` and `numpy` for `project_name.gym`. The adapter supplies its own reward, action, observation, and truncation policies; gameplay remains in the C++ core.

```python
from project_name.gym import make

env = make(start_level=18, seed=42, action_mode="bitmask",
           observation_mode="raw", reward="score_delta", max_frames=10000)
try:
    observation, info = env.reset(seed=42)
    observation, reward, terminated, truncated, info = env.step(0)
finally:
    env.close()
```

`action_mode="multibinary"` (default) uses `MultiBinary(5)` in left/right/down/CW/CCW order. `action_mode="bitmask"` uses `Discrete(32)`. `observation_mode="raw"` provides the visible and hidden boards, current/next/previous piece IDs, phase, position, orientation, frame, score, lines, timing and first-piece counters, RNG state, piece count, terminal flag, configuration, and statistics. `observation_mode="neural"` provides board channels `(20,10,8)`, one-hot pieces/orientation, and 12 scaled timing values. Reward can be `"score_delta"` (default), `"lines"`, `"survival"`, or a callback `(state, events) -> float`. The optional `max_frames` sets Gymnasium `truncated`; game over or challenge completion sets `terminated`. `info` includes events, state hash, and four-line rate.

For benchmark and research runs, use `project_name_benchmark` or the Python `VectorEnv`. Timing and performance figures should always name the ruleset, seed policy, input policy, number of environments, and hardware.
