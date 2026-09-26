# Controls

The desktop client maps keyboard and gamepad buttons to the same per-frame controller mask used by the headless engine. A held button stays set in each frame's input. Rotation reacts to a new press, so release and press again to rotate twice. There is no hard drop, hold piece, or modern lock reset.

## Desktop gameplay

| Action | Default keyboard | Default gamepad |
|---|---|---|
| Move left | Left arrow | D-pad left |
| Move right | Right arrow | D-pad right |
| Soft drop | Down arrow | D-pad down |
| Rotate clockwise | X | South face button |
| Rotate counterclockwise | Z | East face button |

The first connected SDL3 gamepad is used. Keyboard and gamepad states are combined, so either can set a gameplay bit. Both rotation keys can be held as raw input; the core gives a newly pressed clockwise rotation priority. Left+Right raw input gives Right movement priority. Holding Down prevents horizontal movement in the current core.

| Key | During play or replay |
|---|---|
| P | Pause or resume |
| . (period) | Advance one logical frame while paused |
| R | Restart from the selected configuration and seed |
| [ / ] | Decrease/increase playback speed through 0.25×, 0.5×, 1×, 2×, 4×, 8× |
| F1 | Toggle debug counters and state hash |
| Tab | Toggle the live or final statistics panel |
| F5 | Save the current live recording to the app preference directory as `last.rep` |
| F6 | Load and play `last.rep`, when present |
| N | Show or hide the next-piece preview |
| H | Toggle the high-contrast color palette |
| F11 | Toggle fullscreen |
| F3 / F4 | Toggle VSync / integer scaling |
| Esc | Return to the menu; save the current live recording |

The pause, speed, debug, preview visibility, fullscreen, and palette switches are desktop presentation controls. They do not add game mechanics to a frame input. F5 writes a replay after at least one live frame has been recorded; pressing it again overwrites `last.rep` with the recording through the newer frame. Completed or abandoned runs also receive a separate `run-*.rep` archive in the app preference directory. F5 has no save effect during replay playback. Replays can also be loaded on launch with `./build/project_name --replay path/to/game.rep`.

For a visual snapshot from the command line, run `./build/project_name --screenshot capture.bmp --smoke-frames 2` after building the SDL3 target. The screenshot is saved as BMP on the first rendered frame, or at a requested logical replay frame with `--screenshot-frame N`; `--smoke-frames` exits after the requested number of rendered frames. These options do not change the frame rules.

Use `./build/project_name --replay path/to/game.rep --paused` to inspect a replay
from its initial frame. Press `.` to advance one frame or P to play. This is
useful for short AI input experiments that would otherwise finish immediately.

When launched by a live AI controller, the desktop displays `LIVE ... AI`.
P pauses, period advances one frame, brackets change speed, and R starts the
same seed again. Gameplay buttons are supplied by the controller; Esc or the
gamepad Back button closes the live session. The board stays visible while
paused. A frame limit or game over stops the agent and leaves the result visible.

## Menu and settings

| Key | Menu action |
|---|---|
| Enter / Space | Start a game |
| Up / Down | Increase/decrease starting level (0–19) |
| Left / Right | Decrease/increase challenge height (0–5) |
| M | Switch Endless / 25-line Challenge |
| T | Switch Strict / Extended rules |
| S | Increment deterministic seed (wraps after 65535) |
| E | Type an exact decimal seed (Enter saves, Esc cancels) |
| F2 | Open controls and accessibility settings |
| F6 | Play the most recently saved replay |
| F7 | Browse saved `.rep` files |
| H / N | Toggle high contrast / next preview |
| F11 | Toggle fullscreen |
| F3 / F4 | Toggle VSync / integer scaling |
| Esc | Quit from the menu |

In Settings, Up/Down chooses one of five keyboard or five gamepad gameplay bindings. Enter starts rebinding; press a keyboard key or gamepad button to assign it, or Esc to cancel keyboard capture. Left/Right adjusts sound volume by 10%. Esc returns to the menu. Settings, including bindings, seed, volume, contrast, next-piece visibility, VSync, integer scaling, and fullscreen, are saved as `settings.ini` in the SDL app preference directory.

Gamepad South starts from the menu. The D-pad changes menu level and height; West changes mode and North changes rules. During play, Start pauses, North restarts, Back returns to the menu, and West toggles statistics. The gamepad and keyboard can be used together.

## Headless controller bytes

The C++ `InputFrame` and Python `Input` bitmasks are:

| Bit | Value | Action |
|---:|---:|---|
| 0 | 1 | Left |
| 1 | 2 | Right |
| 2 | 4 | Down |
| 3 | 8 | Clockwise rotation |
| 4 | 16 | Counterclockwise rotation |
| 5 | 32 | Start, currently accepted but not handled by core gameplay |
| 6 | 64 | Select, currently accepted but not handled by core gameplay |

`project_name_headless --input-file controls.bin` reads one byte per logical frame. Bits may be combined (for example 9 = Left + clockwise rotation); use 0 to release all buttons. The five Gymnasium gameplay actions exclude Start and Select. See [ai-api.md](ai-api.md) for the API and [compatibility.md](compatibility.md) for exact versus approximate NES input behavior.
