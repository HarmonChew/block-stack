# Architecture

The C++20 library `blocks_core` owns every gameplay rule. The desktop client, command line tools, and Python boundary call that library. They do not implement separate physics, rotation, scoring, or randomizers.

```text
Controller mask per logical frame
            |
            v
   blocks::Game::tick()        <- Config: ruleset, mode, level, height, seed
            |
            +--> State + Events --> SDL3 desktop presentation
            +--> State + Events --> command line tools
            +--> C ABI --> ctypes Python --> optional Gymnasium adapter
            +--> Replay / save state / state hash
```

## Source layout

| Component | Files | Role |
|---|---|---|
| Simulation contract | `core/include/blocks/game.hpp` | Configuration, controller bits, public state/events, ruleset, and `Game` API. |
| Mechanics | `core/src/game.cpp`, `core/src/pieces.cpp`, `core/src/rules.cpp`, `core/src/rng.cpp` | Frame stepping, explicit rotations, level/timing constants, LFSR and B-Type board generation. |
| Replay and snapshots | `core/include/blocks/replay.hpp`, `core/src/replay.cpp`, `core/src/game.cpp` | Input recordings, periodic hashes, full state snapshots, and deterministic verification. |
| Language boundary | `core/include/blocks/c_api.h`, `core/src/c_api.cpp` | A dependency-free C ABI with opaque game handles and caller-owned output buffers. |
| Python | `python/project_name/__init__.py`, `python/project_name/gym.py` | `ctypes` game wrapper, vector stepping, and optional Gymnasium spaces/rewards. |
| Tools | `tools/cli.cpp` | Headless runs, replay verification, benchmarks, RNG sampling, gravity info, and trace comparison. |
| Desktop | `app/src/main.cpp` | Optional SDL3 client, linked to the same `blocks_core` library. |

## Frame and state model

One `Game::tick(InputFrame)` advances one **integer logical frame**. The NTSC reference clock is `60.0988138974405` frames per second; headless calls can run as fast as the CPU allows. The renderer schedules ticks against a real clock but does not pass elapsed time into the physics. A caller must supply the complete button mask for each frame, including held buttons.

`State` stores a 10×20 row-major visible board with a top-left origin (x 0–9, y 0–19), plus two hidden rows above it. Zero is empty. Occupied visible cells use values 1–7. The active piece is stored separately as its type, orientation, and origin; the board does not include it until lock. `Game::occupancy()` returns a binary version of locked visible cells. `Events` reports movements, rotations, downward motion, lock, spawn, lines, score delta, level change, top-out, and challenge completion for the just completed frame.

The phase state machine goes from `Active` to `EntryDelay` after a lock. A line clear then enters `LineClear` and returns to `EntryDelay` for four final frames before spawn or challenge completion. A lock without a clear proceeds from `EntryDelay` directly to spawn. `GameOver` and `ChallengeComplete` are terminal phases. On an active frame, the implementation processes horizontal movement, rotation, then downward movement. The original game has no modern hard drop, hold, wall kick, floor kick, or lock-reset timer. A downward attempt that cannot move the piece causes lock; validity is checked then, so a spawned piece can briefly overlap the stack and potentially escape. Line clearing and entry use frame counters, not wall-clock timers.

`Config` selects `endless` or the fixed-level 25-line `challenge`, start level 0–19, height 0–5, an exact 16-bit RNG state, and strict or extended NTSC rules. `reset(seed)` discards the prior game state and starts deterministically from that raw LFSR state. The cartridge's number of menu frames and session-wide piece count are intentionally not inferred from a seed. The strict preset caps score at 999,999 and wraps level at 256; the default extended preset does neither. Neither preset intentionally executes legacy CPU crashes. See [compatibility.md](compatibility.md) for verified behavior and deviations.

## RNG and reproducibility

The RNG is the cartridge's 16-bit LFSR. Piece selection uses the high RNG byte, an incrementing piece count, and the previous piece's NES spawn orientation ID. The LFSR advances on logical frames and on specific selection/board-generation steps. The two initial pieces are selected before the challenge board is populated. Board generation consumes RNG even when height 0 leaves the visible board empty.

For exact reproducibility, preserve the ruleset, mode, start level, height, seed, **and every controller mask for every frame**. A `Game` can be cloned, saved as a full binary state, or replayed from config plus an input stream. `state_hash()` is FNV-1a over the versioned save-state bytes. Replay checkpoints locate divergence; they do not correct it. See [replay-format.md](replay-format.md).

## API boundaries

The public C++ API is the reference. The C ABI catches C++ exceptions and reports failures as `-1` or null, with a thread-local message from `blocks_last_error()`. Python owns its C handles and converts C state into immutable snapshots. `VectorEnv.step()` calls the C batch function to step independent games; its rules remain in `blocks_core`. Gymnasium action decoding, observation shaping, reward choice, and truncation are adapter policies outside the simulation.

Desktop drawing, text, sound, and input mapping belong outside `blocks_core`. This keeps the library usable without SDL3 and allows tests and AI jobs to run without a window or real-time sleep.
