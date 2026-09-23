#pragma once
#include "blocks/game.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace blocks {

struct Replay {
    Config config;
    std::vector<InputFrame> inputs;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> checkpoints;
    std::uint64_t final_hash = 0;
    std::uint32_t hash_interval = 600;

    void append(InputFrame input, const Game& after_tick);
    void save(const std::string& path) const;
    static Replay load(const std::string& path);
};

struct ReplayResult {
    bool valid = false;
    std::uint64_t checked_frames = 0;
    std::uint64_t divergence_frame = 0;
    std::uint64_t expected_hash = 0;
    std::uint64_t actual_hash = 0;
    std::string error;
};

ReplayResult verify_replay(const Replay& replay);

} // namespace blocks
