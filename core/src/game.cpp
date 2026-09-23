#include "blocks/game.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace blocks {
namespace {

constexpr InputFrame horizontal_buttons = Left | Right;
constexpr InputFrame direction_buttons = Left | Right | Down;
constexpr InputFrame valid_buttons = Left | Right | Down | RotateCW | RotateCCW | Start | Select;
constexpr std::array<std::uint64_t, 5> clear_points{0, 40, 100, 300, 1200};

int entry_delay(int origin_y) {
    // The timing bands follow the origin coordinate, not the piece's lowest
    // mino. There are two bottom rows in the 10-frame band and four rows per
    // 2-frame band above it (capped at 18).
    return 10 + 2 * std::clamp((21 - origin_y) / 4, 0, 4);
}

std::uint8_t piece_cell(Piece piece) {
    return static_cast<std::uint8_t>(piece) + 1;
}

bool valid_piece(Piece piece) {
    return static_cast<unsigned>(piece) <= static_cast<unsigned>(Piece::None);
}

bool valid_config(const Config& config) {
    return (config.ruleset == RulesetId::ClassicNtscStrict ||
            config.ruleset == RulesetId::ClassicNtscExtended) &&
           (config.mode == Mode::Endless || config.mode == Mode::Challenge) &&
           config.start_level >= 0 && config.start_level <= 19 &&
           config.height >= 0 && config.height <= 5;
}

struct Writer {
    StateBlob bytes;
    void u8(std::uint8_t value) { bytes.push_back(value); }
    void u64(std::uint64_t value) {
        for (int i = 0; i < 8; ++i) {
            u8(static_cast<std::uint8_t>(value));
            value >>= 8;
        }
    }
    void integer(int value) { u64(static_cast<std::uint64_t>(static_cast<std::int64_t>(value))); }
    template <std::size_t N>
    void array(const std::array<std::uint8_t, N>& values) {
        bytes.insert(bytes.end(), values.begin(), values.end());
    }
};

struct Reader {
    std::span<const std::uint8_t> bytes;
    std::size_t cursor = 0;
    std::uint8_t u8() {
        if (cursor >= bytes.size()) throw std::invalid_argument("truncated game state");
        return bytes[cursor++];
    }
    std::uint64_t u64() {
        std::uint64_t value = 0;
        for (int i = 0; i < 8; ++i) value |= static_cast<std::uint64_t>(u8()) << (8 * i);
        return value;
    }
    std::uint64_t bounded(std::uint64_t maximum) {
        const auto value = u64();
        if (value > maximum) throw std::invalid_argument("game state value out of range");
        return value;
    }
    int integer() {
        const auto bits = u64();
        if (bits <= static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
            return static_cast<int>(bits);
        }
        const auto magnitude = ~bits + 1u;
        const auto limit = static_cast<std::uint64_t>(
            -static_cast<std::int64_t>(std::numeric_limits<int>::min()));
        if (magnitude == 0 || magnitude > limit) {
            throw std::invalid_argument("game state integer out of range");
        }
        return static_cast<int>(-static_cast<std::int64_t>(magnitude));
    }
    template <std::size_t N>
    void array(std::array<std::uint8_t, N>& values) {
        for (auto& value : values) value = u8();
    }
};

void validate_state(const State& state) {
    for (const auto cell : state.board) {
        if (cell > 7) throw std::invalid_argument("invalid board cell");
    }
    for (const auto cell : state.hidden_rows) {
        if (cell > 7) throw std::invalid_argument("invalid hidden board cell");
    }
    if (!valid_piece(state.current) || !valid_piece(state.next) ||
        !valid_piece(state.previous_piece)) {
        throw std::invalid_argument("invalid state piece");
    }
    if (static_cast<unsigned>(state.phase) > static_cast<unsigned>(Phase::ChallengeComplete)) {
        throw std::invalid_argument("invalid game phase");
    }
    if (state.phase == Phase::Active && state.current == Piece::None) {
        throw std::invalid_argument("active state has no piece");
    }
    if (state.current != Piece::None &&
        (state.orientation < 0 || state.orientation >= orientation_count(state.current))) {
        throw std::invalid_argument("invalid state orientation");
    }
    if (state.x < -4 || state.x > 13 || state.y < -4 || state.y > 21 ||
        state.level < 0 || state.lines < 0 || state.gravity_counter < 0 ||
        state.das_counter < 0 || state.soft_drop_counter < 0 ||
        state.soft_drop_cells < 0 || state.first_delay_remaining < 0 ||
        state.first_delay_remaining > 96 || state.entry_remaining < 0 ||
        state.clear_remaining < 0 || state.das_direction < -1 ||
        state.das_direction > 1 || (state.previous_input & ~valid_buttons) != 0 ||
        (state.clearing_rows & ~((1u << board_height) - 1u)) != 0) {
        throw std::invalid_argument("invalid game state values");
    }
}

} // namespace

Game::Game(Config config) : config_(config), rules_(ruleset(config.ruleset)) {
    if (!valid_config(config)) throw std::invalid_argument("invalid game configuration");
    reset(config.seed);
}

void Game::reset(std::uint16_t seed) {
    config_.seed = seed;
    state_ = {};
    state_.level = config_.start_level;
    state_.rng_state = seed;
    state_.phase = Phase::Active;
    state_.x = 5;
    state_.y = 0;
    state_.first_delay_remaining = rules_.timing.first_piece_delay;

    // The preview is selected before the B-Type garbage board is generated.
    ++state_.piece_count;
    state_.current = rng_piece(state_.rng_state, state_.piece_count, Piece::None);
    state_.previous_piece = state_.current;
    state_.rng_state = rng_advance(state_.rng_state);
    ++state_.piece_count;
    state_.next = rng_piece(state_.rng_state, state_.piece_count, state_.previous_piece);
    state_.previous_piece = state_.next;
    // The original initialization crosses one vertical blank before the
    // initial playfield is prepared, in both game types.
    state_.rng_state = rng_advance(state_.rng_state);
    if (config_.mode == Mode::Challenge) {
        initialize_challenge_board(state_.board, config_.height, state_.rng_state);
    }

    state_.stats.pieces = 1;
    ++state_.stats.piece_counts[static_cast<int>(state_.current)];
    if (state_.current == Piece::I) {
        state_.stats.current_i_drought = 0;
    } else {
        state_.stats.current_i_drought = 1;
        state_.stats.max_i_drought = 1;
    }
}

bool Game::terminal() const {
    return state_.phase == Phase::GameOver || state_.phase == Phase::ChallengeComplete;
}

bool Game::fits(Piece piece, int orientation, int x, int y) const {
    if (piece == Piece::None) return false;
    for (const auto& cell : cells(piece, orientation)) {
        const int col = x + cell.x;
        const int row = y + cell.y;
        if (col < 0 || col >= board_width || row < -2 || row >= board_height) return false;
        if (row < 0) {
            if (state_.hidden_rows[(row + 2) * board_width + col] != 0) return false;
        } else if (state_.board[row * board_width + col] != 0) {
            return false;
        }
    }
    return true;
}

bool Game::move(int dx, int dy) {
    const int x = state_.x + dx;
    const int y = state_.y + dy;
    if (!fits(state_.current, state_.orientation, x, y)) return false;
    state_.x = x;
    state_.y = y;
    return true;
}

void Game::spawn(Events& events) {
    state_.current = state_.next;
    state_.x = 5;
    state_.y = 0;
    state_.orientation = 0;
    state_.gravity_counter = 0;
    state_.soft_drop_counter = 0;
    state_.soft_drop_cells = 0;
    state_.entry_remaining = 0;
    state_.clear_remaining = 0;
    state_.clearing_rows = 0;
    state_.first_delay_remaining = 0;
    state_.phase = Phase::Active;

    // No collision test here: an overlapped entry can still be rescued by an
    // input before a downward collision reaches the lock commit path.
    ++state_.piece_count;
    state_.next = rng_piece(state_.rng_state, state_.piece_count, state_.current);
    state_.previous_piece = state_.next;
    ++state_.stats.pieces;
    ++state_.stats.piece_counts[static_cast<int>(state_.current)];
    if (state_.current == Piece::I) {
        state_.stats.current_i_drought = 0;
    } else {
        ++state_.stats.current_i_drought;
        state_.stats.max_i_drought =
            std::max(state_.stats.max_i_drought, state_.stats.current_i_drought);
    }
    events.spawned = true;
    events.spawned_piece = state_.current;
}

void Game::clear_rows() {
    std::array<std::uint8_t, board_width * board_height> compact{};
    int destination = board_height - 1;
    for (int source = board_height - 1; source >= 0; --source) {
        if ((state_.clearing_rows & (1u << source)) != 0) continue;
        std::copy_n(state_.board.begin() + source * board_width, board_width,
                    compact.begin() + destination * board_width);
        --destination;
    }
    state_.board = compact;
}

void Game::update_level(int cleared, Events& events) {
    if (cleared == 0 || config_.mode == Mode::Challenge) return;
    const int threshold = first_transition_lines(config_.start_level);
    if (state_.lines < threshold) return;
    const int target = config_.start_level + 1 + (state_.lines - threshold) / 10;
    if (target <= state_.level) return;
    state_.level = rules_.level_wrap ? (target & 0xff) : target;
    events.level_changed = true;
}

void Game::lock(Events& events) {
    events.locked = true;
    // NES checks validity on the lock path, rather than during entry. This
    // includes collisions with the two hidden rows.
    if (!fits(state_.current, state_.orientation, state_.x, state_.y)) {
        state_.phase = Phase::GameOver;
        events.game_over = true;
        return;
    }
    const int lock_y = state_.y;
    for (const auto& cell : cells(state_.current, state_.orientation)) {
        const int col = state_.x + cell.x;
        const int row = state_.y + cell.y;
        if (row < 0) {
            state_.hidden_rows[(row + 2) * board_width + col] = piece_cell(state_.current);
        } else {
            state_.board[row * board_width + col] = piece_cell(state_.current);
        }
    }

    state_.clearing_rows = 0;
    for (int row = 0; row < board_height; ++row) {
        if (std::all_of(state_.board.begin() + row * board_width,
                        state_.board.begin() + (row + 1) * board_width,
                        [](std::uint8_t cell) { return cell != 0; })) {
            state_.clearing_rows |= 1u << row;
        }
    }
    const int cleared = std::popcount(state_.clearing_rows);
    if (cleared > 0) clear_rows();
    events.lines_cleared = cleared;
    state_.lines += cleared;
    if (cleared > 0) ++state_.stats.clear_counts[cleared - 1];

    // The original counts qualifying soft-drop attempts, including the final
    // failed downward attempt, then subtracts one when awarding the bonus.
    // A gravity lock can therefore award one less than the visible dropped
    // distance when no final Down attempt occurred.
    const std::uint64_t soft_bonus = state_.soft_drop_cells >= 2
        ? static_cast<std::uint64_t>(state_.soft_drop_cells - 1) : 0;
    state_.soft_drop_cells = 0;
    state_.soft_drop_counter = 0;
    const std::uint64_t old_score = state_.score;
    state_.score += soft_bonus;
    const int old_level = state_.level;
    update_level(cleared, events);
    if (cleared > 0) {
        state_.score += clear_points[cleared] * static_cast<std::uint64_t>(state_.level + 1);
    }
    if (rules_.score_capped) {
        // The numeric API uses the six displayed digits for this preset.
        // The original BCD display can produce nondecimal glyphs beyond this
        // point; that visual overflow is outside this gameplay state.
        state_.score = std::min<std::uint64_t>(state_.score, 999999);
    }
    if (events.level_changed) {
        if (old_level == config_.start_level && state_.stats.transition_score == 0) {
            state_.stats.transition_score = state_.score;
        }
        if (old_level < 19 && state_.level >= 19) state_.stats.level_19_score = state_.score;
        if (old_level < 29 && state_.level >= 29) state_.stats.level_29_score = state_.score;
    }
    events.score_delta = state_.score >= old_score ? state_.score - old_score : 0;

    state_.entry_remaining = entry_delay(lock_y);
    if (cleared > 0) {
        // The cartridge spends most of ARE in its lock and row-scan states,
        // inserts the animation, then finishes with four post-clear states.
        // Keep those four frames for after the animation.
        state_.entry_remaining -= 4;
        state_.clear_remaining = 0;
        state_.phase = Phase::EntryDelay;
    } else {
        state_.phase = Phase::EntryDelay;
    }
}

void Game::process_horizontal(InputFrame input, Events& events) {
    if ((input & Down) != 0) return;
    if ((input & horizontal_buttons) == 0) return;
    const InputFrame newly_pressed = input & ~state_.previous_input;
    bool attempt = false;
    if ((newly_pressed & horizontal_buttons) != 0) {
        state_.das_counter = 0;
        attempt = true;
    } else if (++state_.das_counter >= rules_.timing.das_initial) {
        state_.das_counter = rules_.timing.das_initial - rules_.timing.das_repeat;
        attempt = true;
    }
    if (!attempt) return;
    // Right wins for a raw impossible Left+Right combination, matching the
    // priority of the original button tests.
    const int direction = (input & Right) != 0 ? 1 : -1;
    state_.das_direction = direction;
    if (move(direction, 0)) {
        events.moved = true;
    } else {
        // A failed shift immediately charges DAS and repeated wall failures
        // leave it fully charged. Releasing direction does not clear it.
        state_.das_counter = rules_.timing.das_initial;
    }
}

void Game::process_rotation(InputFrame input, Events& events) {
    const InputFrame newly_pressed = input & ~state_.previous_input;
    int delta = 0;
    if ((newly_pressed & RotateCW) != 0) delta = 1;
    else if ((newly_pressed & RotateCCW) != 0) delta = -1;
    if (delta == 0) return;
    const int count = orientation_count(state_.current);
    const int candidate = (state_.orientation + delta + count) % count;
    if (!fits(state_.current, candidate, state_.x, state_.y)) return;
    state_.orientation = candidate;
    events.rotated = true;
}

void Game::process_drop(InputFrame input, Events& events) {
    ++state_.gravity_counter;
    const InputFrame newly_pressed = input & ~state_.previous_input;
    if (state_.first_delay_remaining > 0) {
        if ((newly_pressed & Down) != 0) {
            state_.first_delay_remaining = 0;
        } else {
            --state_.first_delay_remaining;
            return;
        }
    }

    bool soft_attempt = false;
    if (state_.soft_drop_counter == 0) {
        if ((input & horizontal_buttons) == 0 &&
            (newly_pressed & direction_buttons) == Down) {
            state_.soft_drop_counter = 1;
        }
    } else if ((input & direction_buttons) != Down) {
        state_.soft_drop_counter = 0;
        state_.soft_drop_cells = 0;
    } else {
        ++state_.soft_drop_counter;
        if (state_.soft_drop_counter >= 3) {
            state_.soft_drop_counter = 1;
            ++state_.soft_drop_cells; // qualifying attempt, even if blocked
            soft_attempt = true;
        }
    }

    const bool gravity_attempt = state_.gravity_counter >= gravity_period(state_.level);
    if (!soft_attempt && !gravity_attempt) return;
    // A coincident soft drop and gravity tick produce one downward attempt.
    state_.gravity_counter = 0;
    if (move(0, 1)) {
        events.soft_drop = soft_attempt;
        events.gravity_drop = !soft_attempt && gravity_attempt;
    } else {
        lock(events);
    }
}

Events Game::tick(InputFrame input) {
    if ((input & ~valid_buttons) != 0) throw std::invalid_argument("invalid input bits");
    Events events;
    ++state_.frame;
    state_.rng_state = rng_advance(state_.rng_state);
    const InputFrame newly_pressed = input & ~state_.previous_input;
    state_.stats.input_presses += static_cast<std::uint32_t>(
        std::popcount(static_cast<unsigned>(newly_pressed)));

    switch (state_.phase) {
    case Phase::Active:
        process_horizontal(input, events);
        process_rotation(input, events);
        process_drop(input, events);
        break;
    case Phase::LineClear:
        if (--state_.clear_remaining <= 0) {
            state_.clear_remaining = 0;
            state_.clearing_rows = 0;
            state_.entry_remaining = 4;
            state_.phase = Phase::EntryDelay;
        }
        break;
    case Phase::EntryDelay:
        if (--state_.entry_remaining <= 0) {
            if (state_.clearing_rows != 0) {
                // Five clearing passes advance only on global frame % 4 == 0.
                // The first pass is 1..4 frames after entry; four more passes
                // follow at four-frame intervals.
                state_.clear_remaining = 16 + (4 - static_cast<int>(state_.frame & 3u));
                state_.phase = Phase::LineClear;
            } else if (config_.mode == Mode::Challenge && state_.lines >= 25) {
                const std::uint64_t old_score = state_.score;
                // The B-Type completion bonus uses the menu's 0..9 level
                // selection even when the A-button starts play at 10..19.
                state_.score += 1000ull * static_cast<std::uint64_t>(
                    config_.start_level % 10 + config_.height);
                if (rules_.score_capped) {
                    state_.score = std::min<std::uint64_t>(state_.score, 999999);
                }
                events.score_delta = state_.score >= old_score ? state_.score - old_score : 0;
                state_.phase = Phase::ChallengeComplete;
                events.challenge_completed = true;
            } else {
                spawn(events);
            }
        }
        break;
    case Phase::GameOver:
    case Phase::ChallengeComplete:
        break;
    }
    state_.previous_input = input;
    return events;
}

std::array<std::uint8_t, board_width * board_height> Game::occupancy() const {
    auto result = state_.board;
    for (auto& value : result) value = value == 0 ? 0 : 1;
    return result;
}

double Game::four_line_rate() const {
    return state_.lines == 0 ? 0.0 :
        4.0 * state_.stats.clear_counts[3] / static_cast<double>(state_.lines);
}

void Game::set_board(std::span<const std::uint8_t> board) {
    if (board.size() != state_.board.size()) throw std::invalid_argument("board must be 200 cells");
    if (std::any_of(board.begin(), board.end(), [](auto value) { return value > 7; })) {
        throw std::invalid_argument("board cell must be 0..7");
    }
    std::copy(board.begin(), board.end(), state_.board.begin());
}

void Game::set_piece(Piece piece, int x, int y, int orientation) {
    if (!valid_piece(piece) || piece == Piece::None ||
        orientation < 0 || orientation >= orientation_count(piece) ||
        !fits(piece, orientation, x, y)) {
        throw std::invalid_argument("piece does not fit at requested position");
    }
    state_.current = piece;
    state_.x = x;
    state_.y = y;
    state_.orientation = orientation;
    state_.phase = Phase::Active;
}

void Game::set_state(const State& state) {
    validate_state(state);
    state_ = state;
}

StateBlob Game::save_state() const {
    Writer w;
    for (char ch : std::string("BLKSST01")) w.u8(static_cast<std::uint8_t>(ch));
    w.u64(1); // portable little-endian schema version
    w.u64(static_cast<unsigned>(config_.ruleset));
    w.u64(static_cast<unsigned>(config_.mode));
    w.integer(config_.start_level);
    w.integer(config_.height);
    w.u64(config_.seed);
    w.array(state_.board);
    w.array(state_.hidden_rows);
    w.u64(static_cast<unsigned>(state_.current));
    w.u64(static_cast<unsigned>(state_.next));
    w.integer(state_.x);
    w.integer(state_.y);
    w.integer(state_.orientation);
    w.u64(static_cast<unsigned>(state_.phase));
    w.u64(state_.frame);
    w.u64(state_.score);
    w.integer(state_.lines);
    w.integer(state_.level);
    w.integer(state_.gravity_counter);
    w.integer(state_.das_counter);
    w.integer(state_.das_direction);
    w.u64(state_.previous_input);
    w.integer(state_.soft_drop_counter);
    w.integer(state_.soft_drop_cells);
    w.integer(state_.first_delay_remaining);
    w.integer(state_.entry_remaining);
    w.integer(state_.clear_remaining);
    w.u64(state_.clearing_rows);
    w.u64(state_.rng_state);
    w.u64(state_.piece_count);
    w.u64(static_cast<unsigned>(state_.previous_piece));
    for (const auto value : state_.stats.piece_counts) w.u64(value);
    for (const auto value : state_.stats.clear_counts) w.u64(value);
    w.u64(state_.stats.pieces);
    w.u64(state_.stats.current_i_drought);
    w.u64(state_.stats.max_i_drought);
    w.u64(state_.stats.transition_score);
    w.u64(state_.stats.level_19_score);
    w.u64(state_.stats.level_29_score);
    w.u64(state_.stats.input_presses);
    return std::move(w.bytes);
}

void Game::load_state(std::span<const std::uint8_t> blob) {
    Reader r{blob};
    for (char ch : std::string("BLKSST01")) {
        if (r.u8() != static_cast<std::uint8_t>(ch)) throw std::invalid_argument("bad game state magic");
    }
    if (r.u64() != 1) throw std::invalid_argument("unsupported game state version");
    Config config;
    config.ruleset = static_cast<RulesetId>(r.bounded(1));
    config.mode = static_cast<Mode>(r.bounded(1));
    config.start_level = r.integer();
    config.height = r.integer();
    config.seed = static_cast<std::uint16_t>(r.bounded(UINT16_MAX));
    if (!valid_config(config)) throw std::invalid_argument("invalid saved configuration");
    State state;
    r.array(state.board);
    r.array(state.hidden_rows);
    state.current = static_cast<Piece>(r.bounded(7));
    state.next = static_cast<Piece>(r.bounded(7));
    state.x = r.integer();
    state.y = r.integer();
    state.orientation = r.integer();
    state.phase = static_cast<Phase>(r.bounded(4));
    state.frame = r.u64();
    state.score = r.u64();
    state.lines = r.integer();
    state.level = r.integer();
    state.gravity_counter = r.integer();
    state.das_counter = r.integer();
    state.das_direction = r.integer();
    state.previous_input = static_cast<InputFrame>(r.bounded(UINT8_MAX));
    state.soft_drop_counter = r.integer();
    state.soft_drop_cells = r.integer();
    state.first_delay_remaining = r.integer();
    state.entry_remaining = r.integer();
    state.clear_remaining = r.integer();
    state.clearing_rows = static_cast<std::uint32_t>(r.bounded(UINT32_MAX));
    state.rng_state = static_cast<std::uint16_t>(r.bounded(UINT16_MAX));
    state.piece_count = static_cast<std::uint32_t>(r.bounded(UINT32_MAX));
    state.previous_piece = static_cast<Piece>(r.bounded(7));
    for (auto& value : state.stats.piece_counts) {
        value = static_cast<std::uint32_t>(r.bounded(UINT32_MAX));
    }
    for (auto& value : state.stats.clear_counts) {
        value = static_cast<std::uint32_t>(r.bounded(UINT32_MAX));
    }
    state.stats.pieces = static_cast<std::uint32_t>(r.bounded(UINT32_MAX));
    state.stats.current_i_drought = static_cast<std::uint32_t>(r.bounded(UINT32_MAX));
    state.stats.max_i_drought = static_cast<std::uint32_t>(r.bounded(UINT32_MAX));
    state.stats.transition_score = r.u64();
    state.stats.level_19_score = r.u64();
    state.stats.level_29_score = r.u64();
    state.stats.input_presses = static_cast<std::uint32_t>(r.bounded(UINT32_MAX));
    if (r.cursor != blob.size()) throw std::invalid_argument("trailing game state data");
    validate_state(state);
    config_ = config;
    rules_ = ruleset(config.ruleset);
    state_ = state;
}

std::uint64_t Game::state_hash() const {
    // FNV-1a over the canonical versioned save-state bytes.
    std::uint64_t hash = 14695981039346656037ull;
    for (const auto byte : save_state()) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string Game::debug_string() const {
    std::ostringstream out;
    out << "frame=" << state_.frame << " phase=" << static_cast<int>(state_.phase)
        << " level=" << state_.level << " lines=" << state_.lines
        << " score=" << state_.score << " current=" << piece_name(state_.current)
        << " next=" << piece_name(state_.next) << " x=" << state_.x
        << " y=" << state_.y << " orientation=" << state_.orientation
        << " rng=" << state_.rng_state << '\n';
    out << "gravity=" << state_.gravity_counter << '/' << gravity_period(state_.level)
        << " first_delay=" << state_.first_delay_remaining
        << " das=" << state_.das_direction << '/' << state_.das_counter
        << " soft=" << state_.soft_drop_counter << '/' << state_.soft_drop_cells
        << " entry=" << state_.entry_remaining
        << " clear=" << state_.clear_remaining
        << " clearing_rows=" << state_.clearing_rows
        << " input=" << static_cast<int>(state_.previous_input) << '\n';
    auto display = state_.board;
    if (state_.phase == Phase::Active && state_.current != Piece::None) {
        for (const auto& cell : cells(state_.current, state_.orientation)) {
            const int x = state_.x + cell.x;
            const int y = state_.y + cell.y;
            if (x >= 0 && x < board_width && y >= 0 && y < board_height) {
                display[y * board_width + x] = piece_cell(state_.current);
            }
        }
    }
    for (int y = 0; y < board_height; ++y) {
        out << '|';
        for (int x = 0; x < board_width; ++x) {
            const auto cell = display[y * board_width + x];
            out << (cell == 0 ? '.' : piece_name(static_cast<Piece>(cell - 1))[0]);
        }
        out << "|\n";
    }
    return out.str();
}

} // namespace blocks
