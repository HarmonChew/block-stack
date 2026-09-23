#include "blocks/game.hpp"

#include <stdexcept>

namespace blocks {
namespace {

// Explicit NES orientation geometry, expressed around each piece's game origin.
// Orientation zero is the entry orientation; successive entries follow the A
// button's clockwise cycle. These are geometry, not extracted art or ROM data.
constexpr std::array<std::array<Orientation, 4>, 7> shapes{{
    // I: horizontal, vertical. The two-state rotation is intentionally not a
    // generic ninety-degree transform around a modern SRS center.
    {{{{{-2, 0}, {-1, 0}, {0, 0}, {1, 0}}},
      {{{0, -2}, {0, -1}, {0, 0}, {0, 1}}}}},
    // J: down, left, up, right.
    {{{{{-1, 0}, {0, 0}, {1, 0}, {1, 1}}},
      {{{0, -1}, {0, 0}, {-1, 1}, {0, 1}}},
      {{{-1, -1}, {-1, 0}, {0, 0}, {1, 0}}},
      {{{0, -1}, {1, -1}, {0, 0}, {0, 1}}}}},
    // L: down, left, up, right.
    {{{{{-1, 0}, {0, 0}, {1, 0}, {-1, 1}}},
      {{{-1, -1}, {0, -1}, {0, 0}, {0, 1}}},
      {{{1, -1}, {-1, 0}, {0, 0}, {1, 0}}},
      {{{0, -1}, {0, 0}, {0, 1}, {1, 1}}}}},
    // O has one fixed orientation.
    {{{{{-1, 0}, {0, 0}, {-1, 1}, {0, 1}}}}},
    // S: horizontal, vertical.
    {{{{{0, 0}, {1, 0}, {-1, 1}, {0, 1}}},
      {{{0, -1}, {0, 0}, {1, 0}, {1, 1}}}}},
    // T: down, left, up, right.
    {{{{{-1, 0}, {0, 0}, {1, 0}, {0, 1}}},
      {{{0, -1}, {-1, 0}, {0, 0}, {0, 1}}},
      {{{-1, 0}, {0, 0}, {1, 0}, {0, -1}}},
      {{{0, -1}, {0, 0}, {1, 0}, {0, 1}}}}},
    // Z: horizontal, vertical.
    {{{{{-1, 0}, {0, 0}, {0, 1}, {1, 1}}},
      {{{1, -1}, {0, 0}, {1, 0}, {0, 1}}}}}
}};

constexpr std::array<int, 7> orientation_counts{2, 4, 4, 1, 2, 4, 2};
constexpr std::array<const char*, 7> names{"I", "J", "L", "O", "S", "T", "Z"};

int index(Piece piece) {
    const auto value = static_cast<int>(piece);
    if (value < 0 || value >= 7) {
        throw std::invalid_argument("invalid piece");
    }
    return value;
}

} // namespace

const Orientation& cells(Piece piece, int orientation) {
    const int i = index(piece);
    if (orientation < 0 || orientation >= orientation_counts[i]) {
        throw std::invalid_argument("invalid piece orientation");
    }
    return shapes[i][orientation];
}

int orientation_count(Piece piece) { return orientation_counts[index(piece)]; }

const char* piece_name(Piece piece) {
    return piece == Piece::None ? "None" : names[index(piece)];
}

Piece piece_from_name(const std::string& name) {
    for (int i = 0; i < 7; ++i) {
        if (name == names[i]) return static_cast<Piece>(i);
    }
    if (name == "None") return Piece::None;
    throw std::invalid_argument("unknown piece name: " + name);
}

} // namespace blocks
