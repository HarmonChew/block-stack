"""Frame-accurate Python access to the shared C++ falling-block simulation.

The package uses a small C ABI and ctypes; no game rules are implemented here.
Set ``BLOCKS_NATIVE_LIB`` to the compiled shared library path when it is not
installed beside this package or in a conventional CMake build directory.
"""

from __future__ import annotations

import ctypes as C
from dataclasses import dataclass
from enum import IntFlag
import os
from pathlib import Path
from typing import Any, Callable, Iterable, Sequence

try:
    import numpy as _np
except ImportError:  # The native API remains usable without NumPy.
    _np = None


_PIECES = ("I", "J", "L", "O", "S", "T", "Z", "None")
_PHASES = ("active", "line_clear", "entry_delay", "game_over", "challenge_complete")
_RULESETS = ("classic_ntsc_strict", "classic_ntsc_extended")
_MODES = ("endless", "challenge")
_WIDTH = 10
_HEIGHT = 20


class Input(IntFlag):
    NONE = 0
    LEFT = 1 << 0
    RIGHT = 1 << 1
    DOWN = 1 << 2
    CW = 1 << 3
    CCW = 1 << 4
    START = 1 << 5
    SELECT = 1 << 6


NONE = Input.NONE
LEFT = Input.LEFT
RIGHT = Input.RIGHT
DOWN = Input.DOWN
CW = Input.CW
CCW = Input.CCW


class _NativeState(C.Structure):
    _fields_ = [
        ("board", C.c_uint8 * 200),
        ("hidden_rows", C.c_uint8 * 20),
        ("current_piece", C.c_uint8),
        ("next_piece", C.c_uint8),
        ("previous_piece", C.c_uint8),
        ("phase", C.c_uint8),
        ("x", C.c_int32),
        ("y", C.c_int32),
        ("orientation", C.c_int32),
        ("frame", C.c_uint64),
        ("score", C.c_uint64),
        ("lines", C.c_int32),
        ("level", C.c_int32),
        ("gravity_counter", C.c_int32),
        ("first_delay_remaining", C.c_int32),
        ("das_counter", C.c_int32),
        ("das_direction", C.c_int32),
        ("previous_input", C.c_uint8),
        ("soft_drop_counter", C.c_int32),
        ("soft_drop_cells", C.c_int32),
        ("entry_remaining", C.c_int32),
        ("clear_remaining", C.c_int32),
        ("clearing_rows", C.c_uint32),
        ("rng_state", C.c_uint16),
        ("piece_count", C.c_uint32),
        ("piece_counts", C.c_uint32 * 7),
        ("clear_counts", C.c_uint32 * 4),
        ("pieces", C.c_uint32),
        ("current_i_drought", C.c_uint32),
        ("max_i_drought", C.c_uint32),
        ("transition_score", C.c_uint64),
        ("level_19_score", C.c_uint64),
        ("level_29_score", C.c_uint64),
        ("input_presses", C.c_uint32),
        ("terminal", C.c_uint8),
        ("ruleset", C.c_uint8),
        ("mode", C.c_uint8),
        ("start_level", C.c_int32),
        ("height", C.c_int32),
        ("seed", C.c_uint16),
    ]


class _NativeEvents(C.Structure):
    _fields_ = [
        ("moved", C.c_uint8),
        ("rotated", C.c_uint8),
        ("locked", C.c_uint8),
        ("spawned", C.c_uint8),
        ("gravity_drop", C.c_uint8),
        ("soft_drop", C.c_uint8),
        ("level_changed", C.c_uint8),
        ("game_over", C.c_uint8),
        ("challenge_completed", C.c_uint8),
        ("lines_cleared", C.c_int32),
        ("score_delta", C.c_uint64),
        ("spawned_piece", C.c_uint8),
    ]


_library: C.CDLL | None = None


def _library_candidates() -> Iterable[Path]:
    explicit = os.environ.get("BLOCKS_NATIVE_LIB")
    if explicit:
        yield Path(explicit).expanduser()
    package = Path(__file__).resolve().parent
    root = package.parents[1]
    for directory in (package, root / "build", root / "build-headless", root / "build" / "core", root / "build-headless" / "core"):
        for name in ("libblocks_native.so", "libblocks_c.so", "libblocks_native.dylib", "blocks_native.dll"):
            yield directory / name


def _get_library() -> C.CDLL:
    global _library
    if _library is not None:
        return _library
    paths = list(_library_candidates())
    for path in paths:
        if path.is_file():
            library = C.CDLL(str(path))
            break
    else:
        raise RuntimeError(
            "C++ simulation library not found. Build the blocks_native shared "
            "target and set BLOCKS_NATIVE_LIB to its absolute path. Searched: "
            + ", ".join(str(path) for path in paths)
        )

    try:
        library.blocks_abi_version.restype = C.c_uint32
        version = library.blocks_abi_version()
    except AttributeError as error:
        raise RuntimeError("native simulation library has no ABI version; rebuild it") from error
    if version != 1:
        raise RuntimeError(f"unsupported native simulation ABI version {version}; expected 1")
    library.blocks_last_error.restype = C.c_char_p
    library.blocks_game_create.argtypes = [C.c_char_p, C.c_char_p, C.c_int32, C.c_int32, C.c_uint32]
    library.blocks_game_create.restype = C.c_void_p
    library.blocks_game_clone.argtypes = [C.c_void_p]
    library.blocks_game_clone.restype = C.c_void_p
    library.blocks_game_destroy.argtypes = [C.c_void_p]
    library.blocks_game_reset.argtypes = [C.c_void_p, C.c_uint32]
    library.blocks_game_reset.restype = C.c_int
    library.blocks_game_tick.argtypes = [C.c_void_p, C.c_uint32, C.POINTER(_NativeEvents)]
    library.blocks_game_tick.restype = C.c_int
    library.blocks_game_tick_many.argtypes = [C.POINTER(C.c_void_p), C.POINTER(C.c_uint8), C.POINTER(_NativeEvents), C.c_size_t]
    library.blocks_game_tick_many.restype = C.c_int
    library.blocks_game_get_state.argtypes = [C.c_void_p, C.POINTER(_NativeState)]
    library.blocks_game_get_state.restype = C.c_int
    library.blocks_game_get_states.argtypes = [C.POINTER(C.c_void_p), C.POINTER(_NativeState), C.c_size_t]
    library.blocks_game_get_states.restype = C.c_int
    library.blocks_game_state_hash.argtypes = [C.c_void_p]
    library.blocks_game_state_hash.restype = C.c_uint64
    library.blocks_game_save_state.argtypes = [C.c_void_p, C.POINTER(C.c_uint8), C.c_size_t]
    library.blocks_game_save_state.restype = C.c_size_t
    library.blocks_game_load_state.argtypes = [C.c_void_p, C.POINTER(C.c_uint8), C.c_size_t]
    library.blocks_game_load_state.restype = C.c_int
    library.blocks_game_debug_string.argtypes = [C.c_void_p, C.c_void_p, C.c_size_t]
    library.blocks_game_debug_string.restype = C.c_size_t
    library.blocks_game_set_board.argtypes = [C.c_void_p, C.POINTER(C.c_uint8), C.c_size_t]
    library.blocks_game_set_board.restype = C.c_int
    library.blocks_game_set_piece.argtypes = [C.c_void_p, C.c_char_p, C.c_int32, C.c_int32, C.c_int32]
    library.blocks_game_set_piece.restype = C.c_int
    _library = library
    return library


def _error(library: C.CDLL) -> RuntimeError:
    message = library.blocks_last_error()
    return RuntimeError(message.decode("utf-8", "replace") if message else "native simulation call failed")


def _check(library: C.CDLL, status: int) -> None:
    if status != 0:
        raise _error(library)


def _seed(value: int) -> int:
    if not isinstance(value, int) or value < 0:
        raise ValueError("seed must be a non-negative integer")
    return value & 0xFFFF  # A seed is the exact 16-bit classic PRNG state.


def _action(value: int | Input) -> int:
    result = int(value)
    if result < 0 or result > 0x7F:
        raise ValueError("input must be a 7-bit controller bitmask")
    return result


@dataclass(frozen=True, slots=True)
class Stats:
    piece_counts: dict[str, int]
    clear_counts: dict[str, int]
    pieces: int
    current_i_drought: int
    max_i_drought: int
    transition_score: int
    level_19_score: int
    level_29_score: int
    input_presses: int

    @property
    def four_line_rate(self) -> float:
        lines = sum((index + 1) * count for index, count in enumerate(self.clear_counts.values()))
        return 4.0 * self.clear_counts["four_line"] / lines if lines else 0.0


@dataclass(frozen=True, slots=True)
class State:
    board: Any
    hidden_rows: Any
    current_piece: str
    next_piece: str
    previous_piece: str
    piece_id: int
    next_piece_id: int
    phase: str
    phase_id: int
    x: int
    y: int
    orientation: int
    frame: int
    score: int
    lines: int
    level: int
    gravity_counter: int
    first_delay_remaining: int
    das_counter: int
    das_direction: int
    previous_input: int
    soft_drop_counter: int
    soft_drop_cells: int
    entry_remaining: int
    clear_remaining: int
    clearing_rows: int
    rng_state: int
    piece_count: int
    stats: Stats
    terminal: bool
    ruleset: str
    mode: str
    start_level: int
    height: int
    seed: int

    @property
    def occupancy(self) -> Any:
        if _np is not None:
            return (self.board != 0).astype(_np.uint8)
        return tuple(tuple(int(cell != 0) for cell in row) for row in self.board)


@dataclass(frozen=True, slots=True)
class Events:
    moved: bool
    rotated: bool
    locked: bool
    spawned: bool
    gravity_drop: bool
    soft_drop: bool
    level_changed: bool
    game_over: bool
    challenge_completed: bool
    lines_cleared: int
    score_delta: int
    spawned_piece: str


def _state_from_native(raw: _NativeState) -> State:
    cells = bytes(raw.board)
    hidden_cells = bytes(raw.hidden_rows)
    if _np is not None:
        board = _np.frombuffer(cells, dtype=_np.uint8).reshape(_HEIGHT, _WIDTH).copy()
        board.setflags(write=False)
        hidden_rows = _np.frombuffer(hidden_cells, dtype=_np.uint8).reshape(2, _WIDTH).copy()
        hidden_rows.setflags(write=False)
    else:
        board = tuple(tuple(cells[row * _WIDTH:(row + 1) * _WIDTH]) for row in range(_HEIGHT))
        hidden_rows = tuple(tuple(hidden_cells[row * _WIDTH:(row + 1) * _WIDTH]) for row in range(2))
    stats = Stats(
        piece_counts=dict(zip(_PIECES[:7], map(int, raw.piece_counts))),
        clear_counts=dict(zip(("single", "double", "triple", "four_line"), map(int, raw.clear_counts))),
        pieces=raw.pieces,
        current_i_drought=raw.current_i_drought,
        max_i_drought=raw.max_i_drought,
        transition_score=raw.transition_score,
        level_19_score=raw.level_19_score,
        level_29_score=raw.level_29_score,
        input_presses=raw.input_presses,
    )
    return State(
        board=board,
        hidden_rows=hidden_rows,
        current_piece=_PIECES[raw.current_piece],
        next_piece=_PIECES[raw.next_piece],
        previous_piece=_PIECES[raw.previous_piece],
        piece_id=raw.current_piece,
        next_piece_id=raw.next_piece,
        phase=_PHASES[raw.phase],
        phase_id=raw.phase,
        x=raw.x,
        y=raw.y,
        orientation=raw.orientation,
        frame=raw.frame,
        score=raw.score,
        lines=raw.lines,
        level=raw.level,
        gravity_counter=raw.gravity_counter,
        first_delay_remaining=raw.first_delay_remaining,
        das_counter=raw.das_counter,
        das_direction=raw.das_direction,
        previous_input=raw.previous_input,
        soft_drop_counter=raw.soft_drop_counter,
        soft_drop_cells=raw.soft_drop_cells,
        entry_remaining=raw.entry_remaining,
        clear_remaining=raw.clear_remaining,
        clearing_rows=raw.clearing_rows,
        rng_state=raw.rng_state,
        piece_count=raw.piece_count,
        stats=stats,
        terminal=bool(raw.terminal),
        ruleset=_RULESETS[raw.ruleset],
        mode=_MODES[raw.mode],
        start_level=raw.start_level,
        height=raw.height,
        seed=raw.seed,
    )


def _events_from_native(raw: _NativeEvents) -> Events:
    return Events(
        moved=bool(raw.moved), rotated=bool(raw.rotated), locked=bool(raw.locked),
        spawned=bool(raw.spawned), gravity_drop=bool(raw.gravity_drop),
        soft_drop=bool(raw.soft_drop), level_changed=bool(raw.level_changed),
        game_over=bool(raw.game_over), challenge_completed=bool(raw.challenge_completed),
        lines_cleared=raw.lines_cleared, score_delta=raw.score_delta,
        spawned_piece=_PIECES[raw.spawned_piece],
    )


class Game:
    """One independent C++ simulation; one :meth:`step_frame` is one NES frame."""

    def __init__(self, *, ruleset: str = "classic_ntsc_extended", mode: str = "endless",
                 start_level: int = 0, height: int = 0, seed: int = 0x8988):
        self._lib = _get_library()
        self._initial_seed = _seed(seed)
        self.ruleset = ruleset
        self.mode = mode
        self.start_level = start_level
        self.height = height
        handle = self._lib.blocks_game_create(ruleset.encode(), mode.encode(), start_level, height, self._initial_seed)
        if not handle:
            raise _error(self._lib)
        self._handle = handle

    def close(self) -> None:
        handle = getattr(self, "_handle", None)
        if handle:
            self._lib.blocks_game_destroy(handle)
            self._handle = None

    def __del__(self) -> None:
        self.close()

    def __enter__(self) -> Game:
        return self

    def __exit__(self, *_: object) -> None:
        self.close()

    def _require_open(self) -> int:
        if self._handle is None:
            raise RuntimeError("game is closed")
        return self._handle

    def reset(self, seed: int | None = None) -> State:
        if seed is None:
            seed = self._initial_seed
        else:
            seed = _seed(seed)
            self._initial_seed = seed
        _check(self._lib, self._lib.blocks_game_reset(self._require_open(), seed))
        return self.state

    def step_frame(self, action: int | Input = NONE) -> Events:
        raw = _NativeEvents()
        _check(self._lib, self._lib.blocks_game_tick(self._require_open(), _action(action), C.byref(raw)))
        return _events_from_native(raw)

    def step(self, action: int | Input = NONE) -> tuple[State, Events]:
        events = self.step_frame(action)
        return self.state, events

    @property
    def state(self) -> State:
        raw = _NativeState()
        _check(self._lib, self._lib.blocks_game_get_state(self._require_open(), C.byref(raw)))
        return _state_from_native(raw)

    @property
    def terminal(self) -> bool:
        return self.state.terminal

    @property
    def score(self) -> int:
        return self.state.score

    @property
    def stats(self) -> Stats:
        return self.state.stats

    def state_hash(self) -> int:
        result = self._lib.blocks_game_state_hash(self._require_open())
        if self._lib.blocks_last_error():
            raise _error(self._lib)
        return result

    def save_state(self) -> bytes:
        handle = self._require_open()
        size = self._lib.blocks_game_save_state(handle, None, 0)
        if not size:
            raise _error(self._lib)
        buffer = (C.c_uint8 * size)()
        copied = self._lib.blocks_game_save_state(handle, buffer, size)
        if copied != size:
            raise _error(self._lib)
        return bytes(buffer)

    def load_state(self, state: bytes | bytearray | memoryview) -> State:
        data = bytes(state)
        buffer = (C.c_uint8 * len(data)).from_buffer_copy(data)
        _check(self._lib, self._lib.blocks_game_load_state(self._require_open(), buffer, len(data)))
        loaded = self.state
        self.ruleset = loaded.ruleset
        self.mode = loaded.mode
        self.start_level = loaded.start_level
        self.height = loaded.height
        self._initial_seed = loaded.seed
        return loaded

    def clone(self) -> Game:
        handle = self._lib.blocks_game_clone(self._require_open())
        if not handle:
            raise _error(self._lib)
        child = object.__new__(Game)
        child._lib = self._lib
        child._handle = handle
        child._initial_seed = self._initial_seed
        child.ruleset = self.ruleset
        child.mode = self.mode
        child.start_level = self.start_level
        child.height = self.height
        return child

    def set_board(self, board: Sequence[Sequence[int]] | Sequence[int]) -> State:
        """Replace the board for testing/search; accepted values are 0..7."""
        if len(board) == _HEIGHT and all(hasattr(row, "__len__") for row in board):
            if any(len(row) != _WIDTH for row in board):
                raise ValueError("board must have shape (20, 10)")
            flat = [int(cell) for row in board for cell in row]  # type: ignore[union-attr]
        else:
            flat = [int(cell) for cell in board]  # type: ignore[arg-type]
        if len(flat) != 200 or any(cell < 0 or cell > 7 for cell in flat):
            raise ValueError("board must contain 200 cells with values 0..7")
        buffer = (C.c_uint8 * 200)(*flat)
        _check(self._lib, self._lib.blocks_game_set_board(self._require_open(), buffer, 200))
        return self.state

    def set_piece(self, piece: str, *, x: int, y: int, rotation: int = 0) -> State:
        """Replace the active piece for testing/search."""
        _check(self._lib, self._lib.blocks_game_set_piece(self._require_open(), piece.encode(), x, y, rotation))
        return self.state

    def debug_string(self) -> str:
        handle = self._require_open()
        size = self._lib.blocks_game_debug_string(handle, None, 0)
        if not size:
            raise _error(self._lib)
        buffer = C.create_string_buffer(size)
        self._lib.blocks_game_debug_string(handle, buffer, size)
        return buffer.value.decode("utf-8", "replace")

    def __str__(self) -> str:
        return self.debug_string()


class VectorEnv:
    """Batch C++ frame stepping for independent deterministic games.

    Seed ``i`` is ``(base_seed + i) & 0xffff``. Each later automatic reset
    advances that environment's seed by ``count``. Explicit ``reset_one`` can
    choose another seed. States are snapshots; the C++ games remain authoritative.
    """

    def __init__(self, count: int, *, ruleset: str = "classic_ntsc_extended",
                 mode: str = "endless", start_level: int = 0, height: int = 0,
                 seed: int = 0x8988, auto_reset: bool = False):
        if not isinstance(count, int) or count <= 0:
            raise ValueError("count must be a positive integer")
        self.count = count
        self.auto_reset = auto_reset
        self._base_seed = _seed(seed)
        self._resets = [0] * count
        self.games = [Game(ruleset=ruleset, mode=mode, start_level=start_level,
                           height=height, seed=(self._base_seed + i) & 0xFFFF)
                      for i in range(count)]
        self._lib = self.games[0]._lib

    def close(self) -> None:
        for game in self.games:
            game.close()

    def __enter__(self) -> VectorEnv:
        return self

    def __exit__(self, *_: object) -> None:
        self.close()

    def reset(self, seed: int | None = None) -> list[State]:
        if seed is not None:
            self._base_seed = _seed(seed)
        self._resets = [0] * self.count
        return [game.reset((self._base_seed + i) & 0xFFFF) for i, game in enumerate(self.games)]

    def reset_one(self, index: int, seed: int | None = None) -> State:
        if not 0 <= index < self.count:
            raise IndexError(index)
        if seed is None:
            self._resets[index] += 1
            seed = (self._base_seed + index + self._resets[index] * self.count) & 0xFFFF
        return self.games[index].reset(seed)

    @property
    def states(self) -> list[State]:
        handles = (C.c_void_p * self.count)(*(game._require_open() for game in self.games))
        raw_states = (_NativeState * self.count)()
        _check(self._lib, self._lib.blocks_game_get_states(handles, raw_states, self.count))
        return [_state_from_native(raw) for raw in raw_states]

    def step(self, actions: Sequence[int | Input]) -> tuple[list[State], list[Events]]:
        if len(actions) != self.count:
            raise ValueError(f"expected {self.count} frame inputs")
        if self.auto_reset:
            for i, game in enumerate(self.games):
                if game.terminal:
                    self.reset_one(i)
        inputs = (C.c_uint8 * self.count)(*(_action(action) for action in actions))
        handles = (C.c_void_p * self.count)(*(game._require_open() for game in self.games))
        raw_events = (_NativeEvents * self.count)()
        _check(self._lib, self._lib.blocks_game_tick_many(handles, inputs, raw_events, self.count))
        return self.states, [_events_from_native(event) for event in raw_events]


def make_env(**kwargs: Any) -> Any:
    """Create the optional Gymnasium wrapper (requires gymnasium + NumPy)."""
    from .gym import make
    return make(**kwargs)


__all__ = ["Input", "NONE", "LEFT", "RIGHT", "DOWN", "CW", "CCW",
           "Game", "VectorEnv", "State", "Stats", "Events", "make_env"]
