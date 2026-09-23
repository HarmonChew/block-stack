#include "blocks/c_api.h"

#include "blocks/game.hpp"

#include <algorithm>
#include <cstring>
#include <exception>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>

struct BlocksGame {
    blocks::Game game;
    explicit BlocksGame(blocks::Config config) : game(config) {}
    explicit BlocksGame(const blocks::Game& original) : game(original) {}
};

namespace {
thread_local std::string last_error;

template <class Function>
int checked(Function&& function) noexcept {
    try {
        last_error.clear();
        function();
        return 0;
    } catch (const std::exception& error) {
        last_error = error.what();
    } catch (...) {
        last_error = "unknown C++ exception";
    }
    return -1;
}

template <class Result, class Function>
Result checked_result(Result failed, Function&& function) noexcept {
    try {
        last_error.clear();
        return function();
    } catch (const std::exception& error) {
        last_error = error.what();
    } catch (...) {
        last_error = "unknown C++ exception";
    }
    return failed;
}

blocks::Game& require_game(BlocksGame* game) {
    if (!game) throw std::invalid_argument("game handle is null");
    return game->game;
}

const blocks::Game& require_game(const BlocksGame* game) {
    if (!game) throw std::invalid_argument("game handle is null");
    return game->game;
}

void copy_state(const blocks::Game& game, BlocksState& out) {
    const auto& state = game.state();
    out = {};
    std::copy(state.board.begin(), state.board.end(), out.board);
    std::copy(state.hidden_rows.begin(), state.hidden_rows.end(), out.hidden_rows);
    out.current_piece = static_cast<uint8_t>(state.current);
    out.next_piece = static_cast<uint8_t>(state.next);
    out.previous_piece = static_cast<uint8_t>(state.previous_piece);
    out.phase = static_cast<uint8_t>(state.phase);
    out.x = state.x;
    out.y = state.y;
    out.orientation = state.orientation;
    out.frame = state.frame;
    out.score = state.score;
    out.lines = state.lines;
    out.level = state.level;
    out.gravity_counter = state.gravity_counter;
    out.first_delay_remaining = state.first_delay_remaining;
    out.das_counter = state.das_counter;
    out.das_direction = state.das_direction;
    out.previous_input = state.previous_input;
    out.soft_drop_counter = state.soft_drop_counter;
    out.soft_drop_cells = state.soft_drop_cells;
    out.entry_remaining = state.entry_remaining;
    out.clear_remaining = state.clear_remaining;
    out.clearing_rows = state.clearing_rows;
    out.rng_state = state.rng_state;
    out.piece_count = state.piece_count;
    std::copy(state.stats.piece_counts.begin(), state.stats.piece_counts.end(), out.piece_counts);
    std::copy(state.stats.clear_counts.begin(), state.stats.clear_counts.end(), out.clear_counts);
    out.pieces = state.stats.pieces;
    out.current_i_drought = state.stats.current_i_drought;
    out.max_i_drought = state.stats.max_i_drought;
    out.transition_score = state.stats.transition_score;
    out.level_19_score = state.stats.level_19_score;
    out.level_29_score = state.stats.level_29_score;
    out.input_presses = state.stats.input_presses;
    out.terminal = game.terminal();
    out.ruleset = static_cast<uint8_t>(game.config().ruleset);
    out.mode = static_cast<uint8_t>(game.config().mode);
    out.start_level = game.config().start_level;
    out.height = game.config().height;
    out.seed = game.config().seed;
}

void copy_events(const blocks::Events& events, BlocksEvents& out) {
    out = {};
    out.moved = events.moved;
    out.rotated = events.rotated;
    out.locked = events.locked;
    out.spawned = events.spawned;
    out.gravity_drop = events.gravity_drop;
    out.soft_drop = events.soft_drop;
    out.level_changed = events.level_changed;
    out.game_over = events.game_over;
    out.challenge_completed = events.challenge_completed;
    out.lines_cleared = events.lines_cleared;
    out.score_delta = events.score_delta;
    out.spawned_piece = static_cast<uint8_t>(events.spawned_piece);
}

size_t copy_string(const std::string& value, char* out, size_t capacity) {
    const auto required = value.size() + 1;
    if (out && capacity >= required) std::memcpy(out, value.c_str(), required);
    return required;
}
} // namespace

extern "C" {

const char* blocks_last_error(void) { return last_error.c_str(); }
uint32_t blocks_abi_version(void) { return BLOCKS_ABI_VERSION; }

BlocksGame* blocks_game_create(const char* ruleset, const char* mode,
                               int32_t start_level, int32_t height,
                               uint32_t seed) {
    return checked_result<BlocksGame*>(nullptr, [&] {
        if (!ruleset || !mode) throw std::invalid_argument("ruleset and mode are required");
        blocks::Config config;
        config.ruleset = blocks::ruleset_from_name(ruleset);
        const std::string mode_name(mode);
        if (mode_name == "endless") config.mode = blocks::Mode::Endless;
        else if (mode_name == "challenge") config.mode = blocks::Mode::Challenge;
        else throw std::invalid_argument("unknown mode: " + mode_name);
        config.start_level = start_level;
        config.height = height;
        config.seed = static_cast<uint16_t>(seed);
        return new BlocksGame(config);
    });
}

BlocksGame* blocks_game_clone(const BlocksGame* game) {
    return checked_result<BlocksGame*>(nullptr, [&] {
        return new BlocksGame(require_game(game));
    });
}

void blocks_game_destroy(BlocksGame* game) { delete game; }

int blocks_game_reset(BlocksGame* game, uint32_t seed) {
    return checked([&] { require_game(game).reset(static_cast<uint16_t>(seed)); });
}

int blocks_game_tick(BlocksGame* game, uint32_t input, BlocksEvents* out) {
    return checked([&] {
        if (!out) throw std::invalid_argument("event output is null");
        if (input > 0x7f) throw std::invalid_argument("input must use only NES controller bits 0..6");
        copy_events(require_game(game).tick(static_cast<blocks::InputFrame>(input)), *out);
    });
}

int blocks_game_tick_many(BlocksGame* const* games, const uint8_t* inputs,
                          BlocksEvents* out, size_t count) {
    return checked([&] {
        if (count && (!games || !inputs || !out))
            throw std::invalid_argument("batch inputs, games, and events are required");
        for (size_t index = 0; index < count; ++index) {
            if (inputs[index] > 0x7f)
                throw std::invalid_argument("batch input must use only NES controller bits 0..6");
            require_game(games[index]);
        }
        for (size_t index = 0; index < count; ++index) {
            copy_events(require_game(games[index]).tick(inputs[index]), out[index]);
        }
    });
}

int blocks_game_get_state(const BlocksGame* game, BlocksState* out) {
    return checked([&] {
        if (!out) throw std::invalid_argument("state output is null");
        copy_state(require_game(game), *out);
    });
}

int blocks_game_get_states(BlocksGame* const* games, BlocksState* out, size_t count) {
    return checked([&] {
        if (count && (!games || !out))
            throw std::invalid_argument("batch games and states are required");
        for (size_t index = 0; index < count; ++index) require_game(games[index]);
        for (size_t index = 0; index < count; ++index) copy_state(games[index]->game, out[index]);
    });
}

uint64_t blocks_game_state_hash(const BlocksGame* game) {
    return checked_result<uint64_t>(0, [&] { return require_game(game).state_hash(); });
}

size_t blocks_game_save_state(const BlocksGame* game, uint8_t* out, size_t capacity) {
    return checked_result<size_t>(0, [&] {
        const auto blob = require_game(game).save_state();
        if (out && capacity >= blob.size()) std::copy(blob.begin(), blob.end(), out);
        return blob.size();
    });
}

int blocks_game_load_state(BlocksGame* game, const uint8_t* data, size_t length) {
    return checked([&] {
        if (length && !data) throw std::invalid_argument("state data is null");
        require_game(game).load_state(std::span<const uint8_t>(data, length));
    });
}

size_t blocks_game_debug_string(const BlocksGame* game, char* out, size_t capacity) {
    return checked_result<size_t>(0, [&] {
        return copy_string(require_game(game).debug_string(), out, capacity);
    });
}

int blocks_game_set_board(BlocksGame* game, const uint8_t* board, size_t length) {
    return checked([&] {
        if (!board && length) throw std::invalid_argument("board data is null");
        require_game(game).set_board(std::span<const uint8_t>(board, length));
    });
}

int blocks_game_set_piece(BlocksGame* game, const char* piece,
                          int32_t x, int32_t y, int32_t orientation) {
    return checked([&] {
        if (!piece) throw std::invalid_argument("piece name is null");
        require_game(game).set_piece(blocks::piece_from_name(piece), x, y, orientation);
    });
}

} // extern "C"
