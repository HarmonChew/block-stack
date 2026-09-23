#include "blocks/replay.hpp"

#include <array>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace blocks {
namespace {
constexpr std::array<char, 8> magic{'B','L','O','K','R','E','P','1'};

template <class T> void write_le(std::ostream& out, T value) {
    for (std::size_t i = 0; i < sizeof(T); ++i) out.put(static_cast<char>((value >> (i * 8)) & 0xff));
    if (!out) throw std::runtime_error("failed writing replay");
}
template <class T> T read_le(std::istream& in) {
    T value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        const int c = in.get();
        if (c == EOF) throw std::runtime_error("truncated replay");
        value |= static_cast<T>(static_cast<unsigned char>(c)) << (i * 8);
    }
    return value;
}
}

void Replay::append(InputFrame input, const Game& after_tick) {
    inputs.push_back(input);
    final_hash = after_tick.state_hash();
    if (hash_interval != 0 && inputs.size() % hash_interval == 0)
        checkpoints.emplace_back(inputs.size(), final_hash);
}

void Replay::save(const std::string& path) const {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot open replay for writing: " + path);
    out.write(magic.data(), magic.size());
    write_le<std::uint32_t>(out, 1);
    write_le<std::uint8_t>(out, static_cast<std::uint8_t>(config.ruleset));
    write_le<std::uint8_t>(out, static_cast<std::uint8_t>(config.mode));
    write_le<std::uint8_t>(out, static_cast<std::uint8_t>(config.start_level));
    write_le<std::uint8_t>(out, static_cast<std::uint8_t>(config.height));
    write_le<std::uint16_t>(out, config.seed);
    write_le<std::uint32_t>(out, hash_interval);
    write_le<std::uint64_t>(out, static_cast<std::uint64_t>(inputs.size()));
    write_le<std::uint64_t>(out, static_cast<std::uint64_t>(checkpoints.size()));
    for (auto input : inputs) write_le<std::uint8_t>(out, input);
    for (const auto& [frame, hash] : checkpoints) {
        write_le<std::uint64_t>(out, frame);
        write_le<std::uint64_t>(out, hash);
    }
    write_le<std::uint64_t>(out, final_hash);
}

Replay Replay::load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open replay: " + path);
    std::array<char, 8> header{};
    in.read(header.data(), header.size());
    if (!in || header != magic) throw std::runtime_error("invalid replay magic");
    if (read_le<std::uint32_t>(in) != 1) throw std::runtime_error("unsupported replay version");
    Replay replay;
    const auto rules = read_le<std::uint8_t>(in);
    const auto mode = read_le<std::uint8_t>(in);
    replay.config.start_level = read_le<std::uint8_t>(in);
    replay.config.height = read_le<std::uint8_t>(in);
    replay.config.seed = read_le<std::uint16_t>(in);
    replay.hash_interval = read_le<std::uint32_t>(in);
    const auto frame_count = read_le<std::uint64_t>(in);
    const auto checkpoint_count = read_le<std::uint64_t>(in);
    if (rules > 1 || mode > 1 || replay.config.start_level > 19 || replay.config.height > 5)
        throw std::runtime_error("invalid replay configuration");
    if (frame_count > 100'000'000 || checkpoint_count > frame_count)
        throw std::runtime_error("replay size limit exceeded");
    replay.config.ruleset = static_cast<RulesetId>(rules);
    replay.config.mode = static_cast<Mode>(mode);
    replay.inputs.resize(static_cast<std::size_t>(frame_count));
    for (std::size_t i = 0; i < replay.inputs.size(); ++i) {
        replay.inputs[i] = read_le<std::uint8_t>(in);
        if (replay.inputs[i] > 0x7f)
            throw std::runtime_error("invalid replay input at frame " + std::to_string(i + 1));
    }
    replay.checkpoints.reserve(static_cast<std::size_t>(checkpoint_count));
    std::uint64_t previous = 0;
    for (std::uint64_t i = 0; i < checkpoint_count; ++i) {
        const auto frame = read_le<std::uint64_t>(in);
        const auto hash = read_le<std::uint64_t>(in);
        if (frame <= previous || frame > frame_count)
            throw std::runtime_error("invalid replay checkpoint order");
        replay.checkpoints.emplace_back(frame, hash);
        previous = frame;
    }
    replay.final_hash = read_le<std::uint64_t>(in);
    if (in.get() != EOF) throw std::runtime_error("trailing data in replay");
    return replay;
}

ReplayResult verify_replay(const Replay& replay) {
    ReplayResult result;
    try {
        Game game(replay.config);
        std::size_t next_checkpoint = 0;
        for (std::size_t i = 0; i < replay.inputs.size(); ++i) {
            result.divergence_frame = i + 1;
            game.tick(replay.inputs[i]);
            result.checked_frames = i + 1;
            if (next_checkpoint < replay.checkpoints.size() &&
                replay.checkpoints[next_checkpoint].first == i + 1) {
                const auto actual = game.state_hash();
                const auto expected = replay.checkpoints[next_checkpoint].second;
                if (actual != expected) {
                    result.expected_hash = expected;
                    result.actual_hash = actual;
                    result.error = "checkpoint hash mismatch";
                    return result;
                }
                ++next_checkpoint;
            }
        }
        if (next_checkpoint != replay.checkpoints.size()) {
            result.error = "replay checkpoint frame is not present in input stream";
            return result;
        }
        result.expected_hash = replay.final_hash;
        result.actual_hash = game.state_hash();
        result.valid = result.actual_hash == result.expected_hash;
        if (!result.valid) {
            result.divergence_frame = replay.inputs.size();
            result.error = "final hash mismatch";
        } else {
            result.divergence_frame = 0;
        }
    } catch (const std::exception& e) {
        result.error = e.what();
    }
    return result;
}

} // namespace blocks
