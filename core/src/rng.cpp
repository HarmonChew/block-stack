#include "blocks/game.hpp"

#include <array>
#include <stdexcept>

namespace blocks {
namespace {

// The NES indexes pieces in statistics order, which differs from our Piece enum.
constexpr std::array<Piece, 7> selection_order{
    Piece::T, Piece::J, Piece::Z, Piece::O, Piece::S, Piece::L, Piece::I,
};

constexpr int spawn_orientation_id(Piece piece) {
    switch (piece) {
    case Piece::T: return 2;
    case Piece::J: return 7;
    case Piece::Z: return 8;
    case Piece::O: return 10;
    case Piece::S: return 11;
    case Piece::L: return 14;
    case Piece::I: return 18;
    case Piece::None: return 0; // Initial value before the first selection.
    }
    throw std::invalid_argument("invalid previous piece");
}

} // namespace

std::uint16_t rng_advance(std::uint16_t state) {
    const unsigned feedback = ((state >> 1u) ^ (state >> 9u)) & 1u;
    return static_cast<std::uint16_t>((state >> 1u) | (feedback << 15u));
}

Piece rng_piece(std::uint16_t& state, std::uint32_t piece_count, Piece previous) {
    // piece_count includes the piece being selected. The original 8-bit counter
    // increments immediately before the first candidate is calculated.
    const int previous_id = spawn_orientation_id(previous);
    int index = static_cast<int>(((state >> 8u) + piece_count) & 7u);
    if (index < 7 && selection_order[static_cast<std::size_t>(index)] != previous) {
        return selection_order[static_cast<std::size_t>(index)];
    }

    // There is exactly one retry. Its result is accepted even if it repeats.
    state = rng_advance(state);
    index = (static_cast<int>((state >> 8u) & 7u) + previous_id) % 7;
    return selection_order[static_cast<std::size_t>(index)];
}

void initialize_challenge_board(
    std::array<std::uint8_t, board_width * board_height>& board,
    int height,
    std::uint16_t& rng_state) {
    if (height < 0 || height > 5) {
        throw std::invalid_argument("challenge height must be in [0, 5]");
    }

    board.fill(0);
    // The original board uses three visual tile variants for occupied cells.
    // Here 1, 2, and 3 are original palette classes, not tetromino ownership.
    constexpr std::array<std::uint8_t, 8> garbage_cell{0, 1, 0, 2, 3, 3, 0, 0};
    for (int y = 8; y < board_height; ++y) {
        for (int x = board_width - 1; x >= 0; --x) {
            rng_state = rng_advance(rng_state);
            const auto index = static_cast<std::size_t>((rng_state >> 8u) & 7u);
            board[static_cast<std::size_t>(y * board_width + x)] = garbage_cell[index];
        }

        // A separate draw guarantees at least one empty cell in each row.
        int hole;
        do {
            rng_state = rng_advance(rng_state);
            hole = static_cast<int>((rng_state >> 8u) & 15u);
        } while (hole >= board_width);
        board[static_cast<std::size_t>(y * board_width + hole)] = 0;

        // The cartridge waits for vertical blanking after each generated row;
        // its NMI advances the LFSR once more before the next row.
        rng_state = rng_advance(rng_state);
    }

    // All twelve rows are generated even at height zero. The original
    // inclusive blanking loop also clears column zero in the next row.
    constexpr std::array<int, 6> blank_through{200, 170, 150, 120, 100, 80};
    const int last_visible = (blank_through[static_cast<std::size_t>(height)] < 200)
        ? blank_through[static_cast<std::size_t>(height)] : 199;
    for (int i = 0; i <= last_visible; ++i) {
        board[static_cast<std::size_t>(i)] = 0;
    }
}

} // namespace blocks
