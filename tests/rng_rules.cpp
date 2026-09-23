#include "blocks/game.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string row_string(
    const std::array<std::uint8_t, blocks::board_width * blocks::board_height>& board,
    int y) {
    std::string result;
    for (int x = 0; x < blocks::board_width; ++x) {
        result.push_back(static_cast<char>('0' + board[static_cast<std::size_t>(
            y * blocks::board_width + x)]));
    }
    return result;
}

} // namespace

void test_rng_rules() {
    using namespace blocks;

    // Values follow directly from the documented $8988 seed and LFSR taps.
    require(rng_advance(0x8988) == 0x44C4, "first LFSR step");
    require(rng_advance(0x44C4) == 0x2262, "second LFSR step");
    require(rng_advance(0) == 0, "zero is an exact, if degenerate, LFSR state");

    std::uint16_t seed = 0x8988;
    require(rng_piece(seed, 1, Piece::None) == Piece::Z, "initial piece selection");
    require(seed == 0x8988, "accepted first candidate must not advance RNG");
    seed = rng_advance(seed); // The game does this between current and preview.
    require(rng_piece(seed, 2, Piece::Z) == Piece::I, "initial preview selection");
    require(seed == 0x44C4, "accepted preview must not advance RNG");

    // A rejected duplicate triggers exactly one LFSR advance. The retry may
    // itself return the previous piece, unlike a history/bag randomizer.
    seed = 0x0200;
    require(rng_piece(seed, 0, Piece::Z) == Piece::Z, "retry may repeat");
    require(seed == 0x8100, "duplicate retry advances once");
    seed = 0x0700;
    require(rng_piece(seed, 0, Piece::None) == Piece::O, "dummy index rerolls");
    require(seed == 0x8380, "dummy retry advances once");

    constexpr std::array<int, 30> expected_gravity{
        48, 43, 38, 33, 28, 23, 18, 13, 8, 6,
         5,  5,  5,  4,  4,  4,  3,  3, 3, 2,
         2,  2,  2,  2,  2,  2,  2,  2, 2, 1,
    };
    for (int level = 0; level < 30; ++level) {
        require(gravity_period(level) == expected_gravity[static_cast<std::size_t>(level)],
                "gravity table entry");
    }
    require(gravity_period(255) == 1, "gravity remains one frame above level 29");

    constexpr std::array<int, 20> expected_first_transition{
         10,  20,  30,  40,  50,  60,  70,  80,  90, 100,
        100, 100, 100, 100, 100, 100, 110, 120, 130, 140,
    };
    for (int start = 0; start < 20; ++start) {
        require(first_transition_lines(start) ==
                    expected_first_transition[static_cast<std::size_t>(start)],
                "first-transition threshold");
    }

    const auto strict = ruleset(RulesetId::ClassicNtscStrict);
    const auto extended = ruleset(RulesetId::ClassicNtscExtended);
    require(strict.score_capped && strict.level_wrap, "strict ruleset flags");
    require(!extended.score_capped && !extended.level_wrap, "extended ruleset flags");
    require(strict.timing.gravity == expected_gravity, "ruleset gravity data");
    require(strict.timing.das_initial == 16 && strict.timing.das_repeat == 6,
            "NTSC DAS constants");
    require(strict.timing.first_piece_delay == 96, "first-piece wait constant");
    require(ruleset_from_name(ruleset_name(RulesetId::ClassicNtscStrict)) ==
                RulesetId::ClassicNtscStrict,
            "strict ruleset name roundtrip");
    require(ruleset_from_name(ruleset_name(RulesetId::ClassicNtscExtended)) ==
                RulesetId::ClassicNtscExtended,
            "extended ruleset name roundtrip");

    constexpr std::array<int, 6> expected_rows{0, 3, 5, 8, 10, 12};
    for (int height = 0; height <= 5; ++height) {
        std::array<std::uint8_t, board_width * board_height> board{};
        std::uint16_t board_seed = 0x8988;
        initialize_challenge_board(board, height, board_seed);
        require(board_seed == 0xB2B9, "B-Type RNG draw sequence");
        const int first_row = board_height - expected_rows[static_cast<std::size_t>(height)];
        for (int y = 0; y < first_row; ++y) {
            require(row_string(board, y) == "0000000000", "blank area above B-Type garbage");
        }
        for (int y = first_row; y < board_height; ++y) {
            bool has_hole = false;
            for (int x = 0; x < board_width; ++x) {
                has_hole |= board[static_cast<std::size_t>(y * board_width + x)] == 0;
            }
            require(has_hole, "each B-Type garbage row has a hole");
        }
        if (height > 0) {
            require(board[static_cast<std::size_t>(first_row * board_width)] == 0,
                    "inclusive B-Type blanking clears the next row's first cell");
        }
        if (height == 5) {
            require(row_string(board, 19) == "3200302003", "fixed-seed bottom row");
        }
    }
}
