"""Behavioral checks for the C++/ctypes bridge and optional Gym adapter."""

from __future__ import annotations

import random
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "python"))

from project_name import CCW, CW, DOWN, LEFT, NONE, RIGHT, Game, VectorEnv


class GameBindingsTest(unittest.TestCase):
    def test_reset_and_frame_input(self) -> None:
        with Game(seed=1234, start_level=18) as game:
            initial_hash = game.state_hash()
            initial = game.state
            self.assertEqual(initial.level, 18)
            self.assertEqual(len(initial.board), 20)
            self.assertEqual(len(initial.board[0]), 10)
            self.assertEqual(len(initial.occupancy), 20)
            events = game.step_frame(LEFT | CW)
            self.assertIsInstance(events.moved, bool)
            self.assertEqual(game.state.frame, initial.frame + 1)
            game.reset(1234)
            self.assertEqual(game.state_hash(), initial_hash)
            with self.assertRaises(ValueError):
                game.step_frame(128)

    def test_save_load_clone_differential(self) -> None:
        rng = random.Random(111)
        actions = [NONE, LEFT, RIGHT, DOWN, LEFT | DOWN, RIGHT | CW, CCW]
        with Game(seed=4321) as original:
            for _ in range(150):
                original.step_frame(rng.choice(actions))
            blob = original.save_state()
            clone = original.clone()
            restored = Game(seed=9)
            try:
                restored.load_state(blob)
                self.assertEqual(original.state_hash(), clone.state_hash())
                self.assertEqual(original.state_hash(), restored.state_hash())
                for _ in range(250):
                    action = rng.choice(actions)
                    first = original.step_frame(action)
                    self.assertEqual(first, clone.step_frame(action))
                    self.assertEqual(first, restored.step_frame(action))
                    self.assertEqual(original.state_hash(), clone.state_hash())
                    self.assertEqual(original.state_hash(), restored.state_hash())
            finally:
                clone.close()
                restored.close()

    def test_research_board_and_piece(self) -> None:
        with Game(seed=5) as game:
            board = [[0] * 10 for _ in range(20)]
            board[19][0] = 1
            state = game.set_board(board)
            self.assertEqual(int(state.board[19][0]), 1)
            self.assertEqual(int(state.occupancy[19][0]), 1)
            state = game.set_piece("T", x=4, y=5, rotation=1)
            self.assertEqual((state.current_piece, state.x, state.y, state.orientation),
                             ("T", 4, 5, 1))
            with self.assertRaises(ValueError):
                game.set_board([[0] * 9 for _ in range(20)])
            self.assertIn("frame=", game.debug_string())

    def test_vector_matches_scalar(self) -> None:
        rng = random.Random(2026)
        count = 5
        choices = [NONE, LEFT, RIGHT, DOWN, CW, CCW, LEFT | DOWN, RIGHT | CW]
        with VectorEnv(count, seed=1400) as vector:
            scalars = [Game(seed=1400 + i) for i in range(count)]
            try:
                for _ in range(300):
                    actions = [rng.choice(choices) for _ in range(count)]
                    vector_states, vector_events = vector.step(actions)
                    for i, scalar in enumerate(scalars):
                        self.assertEqual(vector_events[i], scalar.step_frame(actions[i]))
                        self.assertEqual(vector_states[i].frame, scalar.state.frame)
                        self.assertEqual(vector.games[i].state_hash(), scalar.state_hash())
                self.assertEqual(vector.reset_one(2, 77).frame, scalars[2].reset(77).frame)
                self.assertEqual(vector.games[2].state_hash(), scalars[2].state_hash())
            finally:
                for scalar in scalars:
                    scalar.close()


class GymTest(unittest.TestCase):
    def test_gymnasium_contract(self) -> None:
        try:
            import gymnasium  # noqa: F401
        except ImportError:
            self.skipTest("gymnasium optional dependency not installed")
        from project_name.gym import make

        env = make(seed=21, action_mode="bitmask", observation_mode="raw", max_frames=3)
        try:
            obs, info = env.reset(seed=21)
            self.assertTrue(env.observation_space.contains(obs))
            self.assertIn("state_hash", info)
            for frame in range(1, 4):
                obs, reward, terminated, truncated, info = env.step(0)
                self.assertTrue(env.observation_space.contains(obs))
                self.assertIsInstance(reward, float)
                self.assertEqual(truncated, frame == 3 and not terminated)
                self.assertIn("events", info)
        finally:
            env.close()

        neural = make(seed=21, observation_mode="neural")
        try:
            obs, _ = neural.reset(seed=21)
            self.assertTrue(neural.observation_space.contains(obs))
            obs, *_ = neural.step(neural.action_space.sample())
            self.assertTrue(neural.observation_space.contains(obs))
        finally:
            neural.close()


if __name__ == "__main__":
    unittest.main()
