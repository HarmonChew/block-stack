# Live controller connection

`--controller-stdio` starts a new desktop game controlled through its standard
input and output. The desktop owns the game and frame clock. A controller
process receives the current state, chooses one gameplay mask, and returns it
before the next tick. No replay file is read.

Launch flags are `--rules`, `--mode`, `--level`, `--height`, `--seed`, `--frames`
(positive safety cap, default 60000), `--controller-label` (1–32 characters),
`--speed` (0.25, 0.5, 1, 2, 4, 8), and optionally `--paused`. The existing
screen capture options remain available. Configuration stays fixed across R
restarts. Live mode cannot be combined with `--replay`; Esc quits the session.

The process first writes `BLOCK_STACK_CONTROLLER 1` followed by a newline.
Subsequent lines have a message name, a space, and the lowercase hex encoding
of `Game::save_state()` (`BLKSST01`, documented in `replay-format.md`):

- `BEGIN <state>`: initial state of a fresh or restarted game; reset the agent.
- `STATE <state>`: observation before a tick. Reply with one decimal mask from
  0 through 31 and a newline, then flush the pipe. Exactly one tick consumes it.
- `END <state>`: game over, challenge completion, or frame cap. No reply.
- `ABORT <state>`: an incomplete game was restarted or the window closed. No reply.

Snapshots and requests are flushed immediately. The desktop waits for each
reply; the controller should return promptly and close its pipes if it fails.
EOF on input ends the application without advancing the requested frame.
Malformed or out-of-range masks fail before a tick. Standard error is reserved
for diagnostics. Quit and terminate the child when the controller itself exits.

Pause, single-step and speed controls are presentation controls. The agent is
asked for a mask only when a logical frame advances. Live keyboard/gamepad
gameplay inputs are ignored; F5 and automatic terminal recording still save
replays of inputs actually applied. `--controller-smoke` tests pause, a single
frame, restart, then an unpaced game through this same protocol and exits; it is
intended for automated integration checks.
