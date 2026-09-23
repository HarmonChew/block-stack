# Replay and state formats

There are two distinct binary artifacts: a **replay** stores starting configuration plus one controller mask for each logical frame; a **state snapshot** stores everything needed to resume from a particular frame. Both are produced by the shared C++ core.

## Replay v1 (`BLOKREP1`)

All multi-byte integers are little-endian. Offsets below are from the start of the file; the input and checkpoint sections have variable lengths.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 8 | ASCII magic `BLOKREP1` |
| 8 | 4 | Version, currently `1` (`uint32`) |
| 12 | 1 | Ruleset: `0` strict NTSC, `1` extended NTSC |
| 13 | 1 | Mode: `0` Endless, `1` Challenge |
| 14 | 1 | Starting level, 0–19 |
| 15 | 1 | Challenge height, 0–5 |
| 16 | 2 | Raw 16-bit LFSR seed |
| 18 | 4 | Hash checkpoint interval in frames (`uint32`; default 600, zero disables periodic checkpoints) |
| 22 | 8 | Number of recorded frames (`uint64`) |
| 30 | 8 | Number of checkpoints (`uint64`) |
| 38 | `frame_count` | One 7-bit controller mask per frame, stored as bytes |
| after inputs | `16 × checkpoint_count` | Each checkpoint: frame number (`uint64`) then state hash (`uint64`) |
| after checkpoints | 8 | Final state hash (`uint64`) |

The frame number in a checkpoint is one based: checkpoint 600 describes the state **after** the 600th input was applied. `Replay::append(input, game_after_tick)` stores an input, updates the final hash, and adds a checkpoint when `inputs.size()` is a multiple of `hash_interval`. `Replay::load()` validates magic, version, config ranges, size limits, checkpoint ordering, and trailing bytes. `verify_replay()` starts a new `Game` from the replay config, applies each input, checks periodic hashes, then checks the final hash. It reports the first differing checkpoint frame or the final frame; it does not embed or restore intermediate states.

A zero-frame CLI recording stores the initial game-state hash and verifies as a valid empty replay.

Example:

```sh
./build/project_name_headless --seed 42 --frames 10000 --replay-out run.rep --final-hash
./build/project_name_replay run.rep
./build/project_name --replay run.rep
```

The desktop client also records live frames and writes `last.rep` in its SDL preference directory when F5 is pressed, when the game ends, or when returning to the menu. Completed or abandoned runs additionally receive a uniquely named `run-*.rep` archive, browsable from the menu with F7. The command line `--replay-out` cannot be combined with `--state-in` because a v1 replay always starts from its configuration and seed.

Replay v1 is a research format, not a promise that a file will produce the same hashes under future incompatible rule changes. Archive the engine version with long-running experiments.

## Full state snapshots (`BLKSST01`)

`Game::save_state()` writes ASCII `BLKSST01`, a little-endian `uint64` schema version (`1`), then the configuration and complete simulation state in a fixed order. That state includes the visible and hidden boards, current/next piece and orientation, phase and counters, frame, score, lines, level, RNG state, input edge history, and statistics. `Game::load_state()` checks the signature, version, ranges, and exact byte length before replacing the game state. The snapshot is independent of process memory addresses and is suitable for deterministic branching or pausing an AI run.

`Game::state_hash()` is 64-bit FNV-1a over the exact bytes returned by `save_state()`. It therefore includes configuration as well as dynamic state. Do not treat it as a cryptographic integrity signature.

```sh
./build/project_name_headless --seed 42 --frames 1000 --state-out checkpoint.state
./build/project_name_headless --state-in checkpoint.state --frames 1000 --state-out resumed.state
```

The Python API exposes the same mechanism as `Game.save_state() -> bytes` and `Game.load_state(bytes) -> State`. Cloning a Python `Game` creates another native C++ instance with the same state. `BlocksState` and the Gym raw observation also expose hidden rows and first-piece delay; they are snapshots for inspection, while the binary state is the supported format for restoring a complete simulation.

## External frame traces

`project_name_verify trace.jsonl` compares an external JSON Lines trace against a fresh run. Each nonempty line must contain an integer `input` from 0 to 127. Optional expected fields are `frame`, `x`, `y`, `orientation`, `level`, `lines`, `score`, `das`, `gravity`, `piece`, and `board`. `board`, when present, is a 200-character row-major occupancy string using `.` or `0` for empty cells and any other character for occupied cells. The tool stops at the first mismatch and names the field and frame.

```json
{"input": 0, "frame": 1}
{"input": 8, "frame": 2}
```

Use the same ruleset, mode, level, height, and seed options with `project_name_verify` as the source trace. This trace is a comparison input, not a replay file and not a substitute for a full state snapshot.
