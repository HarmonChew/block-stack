#include "blocks/game.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

void test_rng_rules();
void test_replay();

namespace {

using namespace blocks;

void check(bool condition, const std::string& description) {
    if (!condition) throw std::runtime_error(description);
}

template <class F>
void expect_invalid(F&& operation, const std::string& description) {
    try {
        operation();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error(description);
}

State active_state(Game& game, Piece piece = Piece::T, int x = 5, int y = 0) {
    State state = game.state();
    state.current = piece;
    state.next = Piece::I;
    state.x = x;
    state.y = y;
    state.orientation = 0;
    state.phase = Phase::Active;
    state.first_delay_remaining = 0;
    state.gravity_counter = 0;
    state.das_counter = 0;
    state.previous_input = 0;
    state.soft_drop_counter = 0;
    state.soft_drop_cells = 0;
    state.board.fill(0);
    state.hidden_rows.fill(0);
    return state;
}

void test_pieces() {
    check(orientation_count(Piece::I) == 2, "I must have two orientations");
    check(orientation_count(Piece::O) == 1, "O must have one orientation");
    check(orientation_count(Piece::S) == 2, "S must have two orientations");
    check(orientation_count(Piece::T) == 4, "T must have four orientations");
    for (int piece = 0; piece < 7; ++piece) {
        const Piece p = static_cast<Piece>(piece);
        check(piece_from_name(piece_name(p)) == p, "piece name roundtrip");
        for (int orientation = 0; orientation < orientation_count(p); ++orientation) {
            const auto& squares = cells(p, orientation);
            for (int a = 0; a < 4; ++a) {
                for (int b = a + 1; b < 4; ++b) {
                    check(squares[a].x != squares[b].x || squares[a].y != squares[b].y,
                          "orientation contains duplicate cells");
                }
            }
        }
    }
    // The I has the classic asymmetry: horizontal reaches two columns left;
    // vertical reaches two rows above its center. These distinguish it from SRS.
    check(cells(Piece::I, 0)[0].x == -2, "I horizontal pivot");
    check(cells(Piece::I, 1)[0].y == -2, "I vertical pivot");
    expect_invalid([] { cells(Piece::T, 4); }, "invalid orientation accepted");

    // Independent 4x4 occupancy fixtures centered on the NES piece origin.
    // Bit (y+2)*4+(x+2) corresponds to one occupied square. Unused states
    // are zero; these fixtures cover all 19 meaningful orientations.
    constexpr std::array<std::array<std::uint16_t, 4>, 7> masks{{
        {{0x0f00, 0x4444, 0, 0}},
        {{0x8e00, 0x6440, 0x0e20, 0x44c0}},
        {{0x2e00, 0x4460, 0x0e80, 0xc440}},
        {{0x6600, 0, 0, 0}},
        {{0x6c00, 0x8c40, 0, 0}},
        {{0x4e00, 0x4640, 0x0e40, 0x4c40}},
        {{0xc600, 0x4c80, 0, 0}},
    }};
    Game game;
    for (int type = 0; type < 7; ++type) {
        const auto piece = static_cast<Piece>(type);
        for (int orientation = 0; orientation < orientation_count(piece); ++orientation) {
            std::uint16_t actual = 0;
            for (const auto cell : cells(piece, orientation)) {
                check(cell.x >= -2 && cell.x <= 1 && cell.y >= -2 && cell.y <= 1,
                      "orientation outside NES fixture grid");
                actual |= static_cast<std::uint16_t>(1u << ((cell.y + 2) * 4 + cell.x + 2));
            }
            check(actual == masks[type][orientation], "orientation fixture mismatch");

            auto state = active_state(game, piece, 5, 6);
            state.orientation = orientation;
            game.set_state(state);
            game.tick(RotateCW);
            check(game.state().orientation == (orientation + 1) % orientation_count(piece),
                  "clockwise state cycle mismatch");
            game.set_state(state);
            game.tick(RotateCCW);
            check(game.state().orientation ==
                  (orientation + orientation_count(piece) - 1) % orientation_count(piece),
                  "counterclockwise state cycle mismatch");
        }
    }
}

void test_all_clear_scores() {
    constexpr std::array<std::uint64_t, 5> base{0, 40, 100, 300, 1200};
    for (int level : {5, 19}) {
        for (int count = 1; count <= 4; ++count) {
            Game game({.start_level = level});
            auto state = active_state(game, Piece::I, 4, 18);
            state.orientation = 1;
            state.level = level;
            state.gravity_counter = gravity_period(level) - 1;
            for (int row = board_height - count; row < board_height; ++row) {
                for (int col = 0; col < board_width; ++col)
                    if (col != 4) state.board[row * board_width + col] = 1;
            }
            game.set_state(state);
            const auto events = game.tick();
            check(events.locked && events.lines_cleared == count,
                  "clear count mismatch");
            check(events.score_delta == base[count] * static_cast<std::uint64_t>(level + 1),
                  "clear score mismatch");
            check(std::count_if(game.state().board.begin(), game.state().board.end(),
                                [](auto cell) { return cell != 0; }) == 4 - count,
                  "board collapse mismatch");
        }
    }
}

void test_gravity_and_opening() {
    Game game({.start_level = 19});
    auto opening_state = game.state();
    opening_state.level = 29;
    game.set_state(opening_state);
    check(game.state().x == 5 && game.state().y == 0, "spawn origin");
    for (int i = 0; i < 96; ++i) game.tick();
    check(game.state().y == 0 && game.state().first_delay_remaining == 0,
          "96 first-piece frames must suspend gravity");
    game.tick();
    check(game.state().y == 1, "first automatic gravity after opening delay");

    Game low({.start_level = 18});
    auto state = active_state(low);
    state.level = 18;
    low.set_state(state);
    low.tick();
    low.tick();
    check(low.state().y == 0, "level 18 gravity too early");
    low.tick();
    check(low.state().y == 1, "level 18 gravity period");

    state = active_state(low);
    state.first_delay_remaining = 96;
    low.set_state(state);
    low.tick(Down);
    check(low.state().first_delay_remaining == 0, "fresh Down must cancel opening delay");
}

void test_das_and_rotation() {
    Game game({.start_level = 0});
    game.set_state(active_state(game));
    check(game.tick(Left).moved && game.state().x == 4, "tap must shift immediately");
    for (int i = 0; i < 15; ++i) game.tick(Left);
    check(game.state().x == 4, "DAS early repeat");
    game.tick(Left);
    check(game.state().x == 3, "DAS 16-frame repeat");
    for (int i = 0; i < 5; ++i) game.tick(Left);
    check(game.state().x == 3, "DAS repeated too soon");
    game.tick(Left);
    check(game.state().x == 2, "DAS 6-frame repeat");

    game.set_state(active_state(game));
    game.tick(Left);
    game.tick();
    check(game.tick(Right).moved && game.state().x == 5 &&
          game.state().das_counter == 0,
          "direction reversal must make a fresh tap and reset DAS");

    game.set_state(active_state(game, Piece::I, 2));
    check(!game.tick(Left).moved && game.state().das_counter == 16,
          "blocked shift must charge DAS");
    game.tick();
    check(game.state().das_counter == 16, "neutral must retain DAS charge");

    auto state = active_state(game);
    state.phase = Phase::EntryDelay;
    state.entry_remaining = 1;
    state.das_counter = 16;
    state.previous_input = Left;
    game.set_state(state);
    check(game.tick(Right).spawned, "entry delay must end in spawn");
    check(game.state().das_counter == 16, "ARE must preserve DAS counter");
    check(game.tick(Right).moved && game.state().x == 6,
          "retained DAS must redirect to held direction after spawn");

    game.set_state(active_state(game));
    check(!game.tick(Left | Down).moved && game.state().x == 5,
          "Down blocks horizontal movement");
    game.tick(Left | Down);
    game.tick(Left | Down);
    check(game.state().y == 0, "horizontal held must inhibit soft drop");

    game.set_state(active_state(game));
    check(game.tick(RotateCW).rotated && game.state().orientation == 1,
          "A rotation edge");
    check(!game.tick(RotateCW).rotated && game.state().orientation == 1,
          "holding A must not auto-rotate");
    game.tick();
    check(game.tick(RotateCW).rotated && game.state().orientation == 2,
          "second A edge must rotate");
    game.tick();
    check(game.tick(RotateCCW).rotated && game.state().orientation == 1,
          "clockwise followed by counterclockwise must reverse one state");
    game.set_state(active_state(game));
    check(game.tick(RotateCW | RotateCCW).rotated && game.state().orientation == 1,
          "A must win simultaneous A+B");

    game.set_state(active_state(game, Piece::I, 2, -1));
    check(!game.tick(RotateCW).rotated && game.state().orientation == 0,
          "rotation crossing hidden ceiling must fail without kick");

    state = active_state(game, Piece::I, 9, 5);
    state.orientation = 1;
    game.set_state(state);
    check(!game.tick(RotateCW).rotated && game.state().orientation == 1,
          "rotation at right wall must fail without kick");
    state = active_state(game, Piece::T, 5, 5);
    state.board[4 * 10 + 5] = 1;
    game.set_state(state);
    check(!game.tick(RotateCW).rotated && game.state().orientation == 0,
          "rotation into stack must fail");

    game.set_state(active_state(game));
    check(game.tick(Left | Right).moved && game.state().x == 6,
          "raw opposing horizontal inputs must prefer right");
}

void test_soft_drop_and_lock() {
    Game game({.start_level = 0});
    game.set_state(active_state(game));
    game.tick(Down);
    game.tick(Down);
    check(game.state().y == 0, "soft drop before third held frame");
    check(game.tick(Down).soft_drop && game.state().y == 1,
          "soft drop third held frame");
    game.tick(Down);
    check(game.state().y == 1, "soft drop repeat too early");
    check(game.tick(Down).soft_drop && game.state().y == 2,
          "soft drop every other frame");

    auto state = active_state(game, Piece::O, 5, 18);
    state.soft_drop_counter = 2;
    state.soft_drop_cells = 2;
    state.previous_input = Down;
    game.set_state(state);
    check(game.tick(Down).locked && game.state().score == 2,
          "failed soft-drop attempt counts before one-point subtraction");

    state = active_state(game, Piece::O, 5, 18);
    state.soft_drop_counter = 1;
    state.soft_drop_cells = 2;
    state.previous_input = Down;
    state.gravity_counter = 47;
    game.set_state(state);
    const auto before_gravity_lock_score = game.state().score;
    check(game.tick(Down).locked && game.state().score - before_gravity_lock_score == 1,
          "gravity-caused lock undercredits the last soft-drop cell");

    state = active_state(game, Piece::O, 5, 18);
    state.soft_drop_counter = 1;
    state.soft_drop_cells = 2;
    state.previous_input = Down;
    state.gravity_counter = 47;
    game.set_state(state);
    const auto before_release_score = game.state().score;
    check(game.tick().locked && game.state().score == before_release_score,
          "releasing Down must discard soft-drop bonus");

    state = active_state(game, Piece::O, 5, 18);
    state.gravity_counter = 47;
    game.set_state(state);
    const auto lock_event = game.tick();
    check(lock_event.locked && game.state().phase == Phase::EntryDelay,
          "failed gravity descent must lock");
    check(game.state().entry_remaining == 10, "bottom-row entry delay");
    check(game.state().board[18 * 10 + 4] != 0, "piece not committed on lock");
    for (int i = 0; i < 9; ++i) game.tick();
    check(game.state().phase == Phase::EntryDelay, "entry delay ended early");
    check(game.tick().spawned && game.state().phase == Phase::Active,
          "entry delay failed to spawn");

    for (int y : {14, 10, 6, 2}) {
        state = active_state(game, Piece::O, 5, y);
        state.gravity_counter = 47;
        state.board[(y + 2) * 10 + 4] = 1;
        game.set_state(state);
        game.tick();
        check(game.state().entry_remaining == 10 + 2 * ((18 - y) / 4),
              "entry delay height band");
    }
}

void test_lines_score_and_challenge() {
    Game game({.start_level = 5});
    auto state = active_state(game, Piece::I, 5, 19);
    state.gravity_counter = 22;
    state.lines = 59;
    state.level = 5;
    for (int x = 0; x < 10; ++x) {
        if (x < 3 || x > 6) state.board[19 * 10 + x] = 1;
    }
    game.set_state(state);
    const auto event = game.tick();
    check(event.locked && event.lines_cleared == 1, "single line clear");
    check(game.state().level == 6 && game.state().score == 280,
          "transition clear must use updated level for score");
    check(game.state().stats.transition_score == 280,
          "transition score must include the transition clear");
    check(game.state().phase == Phase::EntryDelay, "pre-clear entry phase");
    const int pre_delay = game.state().entry_remaining;
    check(pre_delay == 6, "bottom lock pre-clear entry delay");
    for (int i = 0; i < pre_delay - 1; ++i) game.tick();
    check(game.state().phase == Phase::EntryDelay, "clear animation started early");
    game.tick();
    check(game.state().phase == Phase::LineClear, "pre-clear entry failed to start animation");
    check(game.state().clear_remaining >= 17 && game.state().clear_remaining <= 20,
          "line clear phase duration");
    const int delay = game.state().clear_remaining;
    for (int i = 0; i < delay - 1; ++i) game.tick();
    check(game.state().phase == Phase::LineClear, "clear finished early");
    game.tick();
    check(game.state().phase == Phase::EntryDelay && game.state().entry_remaining == 4,
          "clear did not enter four-frame post-clear delay");

    Game challenge({.mode = Mode::Challenge, .start_level = 5, .height = 3});
    state = active_state(challenge, Piece::I, 5, 19);
    state.lines = 24;
    state.level = 5;
    state.gravity_counter = 47;
    for (int x = 0; x < 10; ++x) {
        if (x < 3 || x > 6) state.board[19 * 10 + x] = 1;
    }
    challenge.set_state(state);
    challenge.tick();
    const int challenge_pre = challenge.state().entry_remaining;
    for (int i = 0; i < challenge_pre; ++i) challenge.tick();
    check(challenge.state().phase == Phase::LineClear,
          "challenge pre-clear delay did not end");
    const int challenge_delay = challenge.state().clear_remaining;
    Events completion;
    for (int i = 0; i < challenge_delay; ++i) completion = challenge.tick();
    check(challenge.state().phase == Phase::EntryDelay,
          "challenge clear did not enter post-clear delay");
    for (int i = 0; i < 4; ++i) completion = challenge.tick();
    check(challenge.state().phase == Phase::ChallengeComplete && challenge.terminal(),
          "25-line challenge completion after clear phase");
    check(completion.score_delta == 8000 && challenge.state().score == 8240,
          "B-Type completion bonus must use selected level and height");

    Game high_challenge({.mode = Mode::Challenge, .start_level = 15, .height = 3});
    state = active_state(high_challenge, Piece::I, 5, 19);
    state.lines = 24;
    state.level = 15;
    state.gravity_counter = 3;
    for (int x = 0; x < 10; ++x) {
        if (x < 3 || x > 6) state.board[19 * 10 + x] = 1;
    }
    high_challenge.set_state(state);
    high_challenge.tick();
    for (int i = 0; i < 60 && !high_challenge.terminal(); ++i) high_challenge.tick();
    check(high_challenge.terminal(), "high-level challenge did not complete");
    check(high_challenge.state().score == 8640,
          "level 15 challenge bonus must use menu level five");
}

void test_top_out_and_serialization() {
    Game game({.start_level = 0});
    auto state = active_state(game, Piece::O, 5, 0);
    state.board[4] = 1; // overlap with O at (4,0)
    game.set_state(state);
    check(game.tick(Right).moved && !game.terminal(),
          "overlapping spawn must be rescuable before downward lock");

    state = active_state(game, Piece::O, 5, 0);
    state.board[4] = 1;
    state.board[2 * 10 + 4] = 1; // blocks a downward attempt
    state.gravity_counter = 47;
    game.set_state(state);
    check(game.tick().game_over && game.terminal(),
          "invalid locked position must top out");

    Game source({.start_level = 18, .seed = 0x1234});
    for (int i = 0; i < 220; ++i) {
        const InputFrame input = (i % 19 < 8 ? Left : 0) |
            (i % 23 == 3 ? RotateCW : 0) | (i % 31 < 4 ? Down : 0);
        source.tick(input);
    }
    const auto blob = source.save_state();
    Game copy;
    copy.load_state(blob);
    check(source.state_hash() == copy.state_hash(), "save/load hash equality");
    for (int i = 0; i < 100; ++i) {
        const InputFrame input = (i % 11 < 5 ? Right : 0) |
            (i % 13 == 4 ? RotateCCW : 0);
        source.tick(input);
        copy.tick(input);
        check(source.state_hash() == copy.state_hash(), "save/load deterministic continuation");
    }
    auto bad_blob = blob;
    bad_blob.pop_back();
    expect_invalid([&] { copy.load_state(bad_blob); }, "truncated state accepted");
    check(copy.state_hash() == source.state_hash(), "failed load mutated game");
    check(source.debug_string().find("frame=") != std::string::npos,
          "debug dump missing frame");
}

void test_phase_snapshots_and_limits() {
    Game game({.start_level = 0});
    auto state = active_state(game, Piece::I, 5, 19);
    state.gravity_counter = 47;
    for (int x = 0; x < 10; ++x) {
        if (x < 3 || x > 6) state.board[19 * 10 + x] = 1;
    }
    state.hidden_rows[0] = 3;
    game.set_state(state);
    game.tick();
    auto roundtrip = [](const Game& original, const std::string& label) {
        Game restored;
        restored.load_state(original.save_state());
        check(original.state_hash() == restored.state_hash(), label + " save/load hash");
        check(original.state().hidden_rows == restored.state().hidden_rows,
              label + " hidden rows");
        restored.tick();
        Game advanced = original;
        advanced.tick();
        check(advanced.state_hash() == restored.state_hash(),
              label + " deterministic continuation");
    };
    check(game.state().phase == Phase::EntryDelay && game.state().clearing_rows != 0,
          "expected pre-clear entry phase");
    roundtrip(game, "pre-clear ARE");
    for (int i = 0; i < 6; ++i) game.tick();
    check(game.state().phase == Phase::LineClear, "expected line-clear phase");
    roundtrip(game, "line clear");
    const int clear_frames = game.state().clear_remaining;
    for (int i = 0; i < clear_frames; ++i) game.tick();
    check(game.state().phase == Phase::EntryDelay && game.state().entry_remaining == 4,
          "expected post-clear ARE");
    roundtrip(game, "post-clear ARE");

    auto make_four_line_state = [](Game& target) {
        auto position = active_state(target, Piece::I, 5, 18);
        position.orientation = 1;
        position.level = 19;
        position.score = 999000;
        position.gravity_counter = 1;
        for (int y = 16; y < 20; ++y) {
            for (int x = 0; x < 10; ++x) {
                if (x != 5) position.board[y * 10 + x] = 1;
            }
        }
        target.set_state(position);
    };
    Game extended({.ruleset = RulesetId::ClassicNtscExtended, .start_level = 19});
    make_four_line_state(extended);
    check(extended.tick().lines_cleared == 4 && extended.state().score == 1023000,
          "extended score must cross one million without cap");
    Game strict({.ruleset = RulesetId::ClassicNtscStrict, .start_level = 19});
    make_four_line_state(strict);
    check(strict.tick().lines_cleared == 4 && strict.state().score == 999999,
          "strict preset must keep six-digit numeric score");

    Game fast({.start_level = 19});
    for (int level : {29, 300}) {
        state = active_state(fast, Piece::T, 5, 0);
        state.level = level;
        fast.set_state(state);
        check(fast.tick().gravity_drop && fast.state().y == 1,
              "level 29 and safe high levels must use 1G");
    }
}

void test_randomized_branch() {
    Game source({.start_level = 0, .seed = 0x2b76});
    std::uint32_t generator = 0x6d2b79f5u;
    auto next_input = [&]() -> InputFrame {
        generator ^= generator << 13;
        generator ^= generator >> 17;
        generator ^= generator << 5;
        InputFrame input = 0;
        if ((generator & 15u) < 4u) input |= Left;
        if (((generator >> 4u) & 15u) < 3u) input |= Right;
        if (((generator >> 8u) & 15u) < 2u) input |= Down;
        if (((generator >> 12u) & 15u) == 0u) input |= RotateCW;
        if (((generator >> 16u) & 15u) == 0u) input |= RotateCCW;
        return input;
    };
    for (int i = 0; i < 150; ++i) source.tick(next_input());
    Game branch;
    branch.load_state(source.save_state());
    check(source.state_hash() == branch.state_hash(), "randomized branch initial equality");
    for (int i = 0; i < 500; ++i) {
        const auto input = next_input();
        source.tick(input);
        branch.tick(input);
        check(source.state_hash() == branch.state_hash(),
              "randomized branch diverged at frame " + std::to_string(i));
    }
}

} // namespace

int main() {
    try {
        test_pieces();
        test_all_clear_scores();
        test_gravity_and_opening();
        test_das_and_rotation();
        test_soft_drop_and_lock();
        test_lines_score_and_challenge();
        test_top_out_and_serialization();
        test_phase_snapshots_and_limits();
        test_randomized_branch();
        test_rng_rules();
        test_replay();
        std::cout << "core tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "core test failed: " << error.what() << '\n';
        return 1;
    }
}
