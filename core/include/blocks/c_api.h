#pragma once

// Stable, dependency-free C boundary for the Python ctypes package and other
// foreign-language callers. All functions catch C++ exceptions. A failure is
// reported as -1/null; blocks_last_error() explains the most recent failure
// on the calling thread. The caller owns every BlocksGame handle.

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && defined(BLOCKS_C_API_EXPORTS)
#define BLOCKS_API __declspec(dllexport)
#elif defined(_WIN32)
#define BLOCKS_API __declspec(dllimport)
#else
#define BLOCKS_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BlocksGame BlocksGame;

#define BLOCKS_ABI_VERSION 1u

typedef struct BlocksState {
    uint8_t board[200];
    uint8_t hidden_rows[20];
    uint8_t current_piece;
    uint8_t next_piece;
    uint8_t previous_piece;
    uint8_t phase;
    int32_t x;
    int32_t y;
    int32_t orientation;
    uint64_t frame;
    uint64_t score;
    int32_t lines;
    int32_t level;
    int32_t gravity_counter;
    int32_t first_delay_remaining;
    int32_t das_counter;
    int32_t das_direction;
    uint8_t previous_input;
    int32_t soft_drop_counter;
    int32_t soft_drop_cells;
    int32_t entry_remaining;
    int32_t clear_remaining;
    uint32_t clearing_rows;
    uint16_t rng_state;
    uint32_t piece_count;
    uint32_t piece_counts[7];
    uint32_t clear_counts[4];
    uint32_t pieces;
    uint32_t current_i_drought;
    uint32_t max_i_drought;
    uint64_t transition_score;
    uint64_t level_19_score;
    uint64_t level_29_score;
    uint32_t input_presses;
    uint8_t terminal;
    uint8_t ruleset;
    uint8_t mode;
    int32_t start_level;
    int32_t height;
    uint16_t seed;
} BlocksState;

typedef struct BlocksEvents {
    uint8_t moved;
    uint8_t rotated;
    uint8_t locked;
    uint8_t spawned;
    uint8_t gravity_drop;
    uint8_t soft_drop;
    uint8_t level_changed;
    uint8_t game_over;
    uint8_t challenge_completed;
    int32_t lines_cleared;
    uint64_t score_delta;
    uint8_t spawned_piece;
} BlocksEvents;

BLOCKS_API const char* blocks_last_error(void);
BLOCKS_API uint32_t blocks_abi_version(void);
BLOCKS_API BlocksGame* blocks_game_create(const char* ruleset, const char* mode,
                                          int32_t start_level, int32_t height,
                                          uint32_t seed);
BLOCKS_API BlocksGame* blocks_game_clone(const BlocksGame* game);
BLOCKS_API void blocks_game_destroy(BlocksGame* game);
BLOCKS_API int blocks_game_reset(BlocksGame* game, uint32_t seed);
BLOCKS_API int blocks_game_tick(BlocksGame* game, uint32_t input, BlocksEvents* out);
BLOCKS_API int blocks_game_tick_many(BlocksGame* const* games, const uint8_t* inputs,
                                     BlocksEvents* out, size_t count);
BLOCKS_API int blocks_game_get_state(const BlocksGame* game, BlocksState* out);
BLOCKS_API int blocks_game_get_states(BlocksGame* const* games, BlocksState* out,
                                      size_t count);
BLOCKS_API uint64_t blocks_game_state_hash(const BlocksGame* game);

// Returns the required buffer length. A null/short output buffer is left
// untouched. Call once with nullptr, then allocate and call again.
BLOCKS_API size_t blocks_game_save_state(const BlocksGame* game, uint8_t* out,
                                         size_t capacity);
BLOCKS_API int blocks_game_load_state(BlocksGame* game, const uint8_t* data,
                                      size_t length);
BLOCKS_API size_t blocks_game_debug_string(const BlocksGame* game, char* out,
                                           size_t capacity);

// Testing/research API; not used by normal gameplay.
BLOCKS_API int blocks_game_set_board(BlocksGame* game, const uint8_t* board,
                                     size_t length);
BLOCKS_API int blocks_game_set_piece(BlocksGame* game, const char* piece,
                                     int32_t x, int32_t y, int32_t orientation);

#ifdef __cplusplus
}
#endif
