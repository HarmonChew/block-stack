#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace blocks {

constexpr int board_width = 10;
constexpr int board_height = 20;
constexpr double ntsc_frames_per_second = 60.0988138974405;

enum class Piece : std::uint8_t { I, J, L, O, S, T, Z, None = 7 };
enum class Phase : std::uint8_t { Active, LineClear, EntryDelay, GameOver, ChallengeComplete };
enum class Mode : std::uint8_t { Endless, Challenge };
enum class RulesetId : std::uint8_t { ClassicNtscStrict, ClassicNtscExtended };

enum Button : std::uint8_t {
    Left = 1u << 0,
    Right = 1u << 1,
    Down = 1u << 2,
    RotateCW = 1u << 3,
    RotateCCW = 1u << 4,
    Start = 1u << 5,
    Select = 1u << 6,
};
using InputFrame = std::uint8_t;

struct Cell { int x; int y; };
using Orientation = std::array<Cell, 4>;
const Orientation& cells(Piece piece, int orientation);
int orientation_count(Piece piece);
const char* piece_name(Piece piece);
Piece piece_from_name(const std::string& name);

struct TimingRules {
    std::array<int, 30> gravity{};
    int das_initial = 16;
    int das_repeat = 6;
    int first_piece_delay = 96;
};
struct Ruleset {
    RulesetId id = RulesetId::ClassicNtscExtended;
    TimingRules timing;
    bool score_capped = false;
    bool level_wrap = false;
};
Ruleset ruleset(RulesetId id);
RulesetId ruleset_from_name(const std::string& name);
const char* ruleset_name(RulesetId id);
int gravity_period(int level);
int first_transition_lines(int start_level);

struct Config {
    RulesetId ruleset = RulesetId::ClassicNtscExtended;
    Mode mode = Mode::Endless;
    int start_level = 0;
    int height = 0;
    std::uint16_t seed = 0x8988;
};

struct Stats {
    std::array<std::uint32_t, 7> piece_counts{};
    std::array<std::uint32_t, 4> clear_counts{};
    std::uint32_t pieces = 0;
    std::uint32_t current_i_drought = 0;
    std::uint32_t max_i_drought = 0;
    std::uint64_t transition_score = 0;
    std::uint64_t level_19_score = 0;
    std::uint64_t level_29_score = 0;
    std::uint32_t input_presses = 0;
};

struct State {
    // Row-major, top-left origin. Zero is empty; Piece enum value + 1 owns a cell.
    std::array<std::uint8_t, board_width * board_height> board{};
    // Two logical rows above the visible board, y=-2 and y=-1.
    std::array<std::uint8_t, board_width * 2> hidden_rows{};
    Piece current = Piece::None;
    Piece next = Piece::None;
    int x = 0;
    int y = 0;
    int orientation = 0;
    Phase phase = Phase::Active;
    std::uint64_t frame = 0;
    std::uint64_t score = 0;
    int lines = 0;
    int level = 0;
    int gravity_counter = 0;
    int first_delay_remaining = 0;
    int das_counter = 0;
    int das_direction = 0;
    InputFrame previous_input = 0;
    int soft_drop_counter = 0;
    int soft_drop_cells = 0;
    int entry_remaining = 0;
    int clear_remaining = 0;
    std::uint32_t clearing_rows = 0;
    std::uint16_t rng_state = 0;
    std::uint32_t piece_count = 0;
    Piece previous_piece = Piece::None;
    Stats stats;
};

struct Events {
    bool moved = false;
    bool rotated = false;
    bool locked = false;
    bool spawned = false;
    bool gravity_drop = false;
    bool soft_drop = false;
    bool level_changed = false;
    bool game_over = false;
    bool challenge_completed = false;
    int lines_cleared = 0;
    std::uint64_t score_delta = 0;
    Piece spawned_piece = Piece::None;
};

using StateBlob = std::vector<std::uint8_t>;

class Game {
public:
    explicit Game(Config config = {});
    void reset(std::uint16_t seed);
    Events tick(InputFrame input = 0);
    const Config& config() const { return config_; }
    const State& state() const { return state_; }
    bool terminal() const;
    std::uint64_t state_hash() const;
    StateBlob save_state() const;
    void load_state(std::span<const std::uint8_t> blob);
    std::string debug_string() const;
    std::array<std::uint8_t, board_width * board_height> occupancy() const;
    double four_line_rate() const;

    // Research API. Calls validate shape and piece identity, then replace state.
    void set_board(std::span<const std::uint8_t> board);
    void set_piece(Piece piece, int x, int y, int orientation);
    void set_state(const State& state);

private:
    Config config_;
    Ruleset rules_;
    State state_;
    bool fits(Piece piece, int orientation, int x, int y) const;
    bool move(int dx, int dy);
    void spawn(Events& events);
    void lock(Events& events);
    void clear_rows();
    void update_level(int cleared, Events& events);
    void process_horizontal(InputFrame input, Events& events);
    void process_rotation(InputFrame input, Events& events);
    void process_drop(InputFrame input, Events& events);
};

// Stateless NES-style 16-bit PRNG and piece selection. The game owns the state.
std::uint16_t rng_advance(std::uint16_t state);
// piece_count includes this selection (the NES increments its counter first).
Piece rng_piece(std::uint16_t& state, std::uint32_t piece_count, Piece previous);
// Reproduces B-Type starting occupancy and consumes the same RNG draws.
void initialize_challenge_board(
    std::array<std::uint8_t, board_width * board_height>& board,
    int height,
    std::uint16_t& rng_state);

} // namespace blocks
