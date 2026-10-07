#include "blocks/game.hpp"
#include "blocks/replay.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef BLOCKS_TOOL
#define BLOCKS_TOOL "headless"
#endif

namespace {
using namespace blocks;

struct Options {
    Config config;
    std::uint64_t frames = 100000;
    int envs = 1;
    int pieces = 100;
    std::string input_file;
    std::string replay_file;
    std::string replay_out;
    std::string state_in;
    std::string state_out;
    std::string dump_state;
    std::string trace_file;
    bool final_hash = false;
    bool verify = false;
    bool gravity_table = false;
};

std::string value_after(int& i, int argc, char** argv, const std::string& option) {
    if (++i >= argc) throw std::invalid_argument("missing value for option " + option);
    return argv[i];
}
std::uint64_t numeric_arg(const std::string& option, const std::string& text,
                          std::uint64_t minimum, std::uint64_t maximum) {
    std::size_t used = 0;
    std::uint64_t value = 0;
    bool parsed = false;
    try {
        value = std::stoull(text, &used, 0);
        parsed = used == text.size();
    } catch (const std::exception&) {
        parsed = false;
    }
    if (!parsed || value < minimum || value > maximum) {
        throw std::invalid_argument("invalid value for " + option + ": '" + text +
                                    "' (expected integer in " + std::to_string(minimum) + ".." +
                                    std::to_string(maximum) + ")");
    }
    return value;
}
void help(const std::string& tool) {
    std::cout << "project_name_" << tool << " — deterministic falling-block research tools\n"
              << "Options: --rules classic_ntsc_strict|classic_ntsc_extended\n"
              << "         --mode endless|challenge --level 0..19 --height 0..5\n"
              << "         --seed 0..65535 --frames N --input-file INPUTS.bin\n"
              << "         --state-in FILE --state-out FILE --dump-state FILE\n"
              << "         --replay-out FILE --final-hash --verify\n"
              << "Benchmark: --envs N; RNG: --pieces N; info: --gravity-table\n";
}
Options parse(int argc, char** argv, const std::string& tool) {
    Options options;
    bool positional_taken = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") { help(tool); std::exit(0); }
        else if (arg == "--rules") options.config.ruleset = ruleset_from_name(value_after(i, argc, argv, arg));
        else if (arg == "--mode") {
            const auto value = value_after(i, argc, argv, arg);
            if (value == "endless") options.config.mode = Mode::Endless;
            else if (value == "challenge") options.config.mode = Mode::Challenge;
            else throw std::invalid_argument("invalid mode: " + value);
        } else if (arg == "--level") options.config.start_level = static_cast<int>(numeric_arg(arg, value_after(i, argc, argv, arg), 0, 19));
        else if (arg == "--height") options.config.height = static_cast<int>(numeric_arg(arg, value_after(i, argc, argv, arg), 0, 5));
        else if (arg == "--seed") options.config.seed = static_cast<std::uint16_t>(numeric_arg(arg, value_after(i, argc, argv, arg), 0, 65535));
        else if (arg == "--frames") options.frames = numeric_arg(arg, value_after(i, argc, argv, arg), 0, 1'000'000'000);
        else if (arg == "--envs") options.envs = static_cast<int>(numeric_arg(arg, value_after(i, argc, argv, arg), 1, 1'000'000));
        else if (arg == "--pieces") options.pieces = static_cast<int>(numeric_arg(arg, value_after(i, argc, argv, arg), 0, 1'000'000));
        else if (arg == "--input-file") options.input_file = value_after(i, argc, argv, arg);
        else if (arg == "--replay-out") options.replay_out = value_after(i, argc, argv, arg);
        else if (arg == "--state-in") options.state_in = value_after(i, argc, argv, arg);
        else if (arg == "--state-out") options.state_out = value_after(i, argc, argv, arg);
        else if (arg == "--dump-state") options.dump_state = value_after(i, argc, argv, arg);
        else if (arg == "--final-hash") options.final_hash = true;
        else if (arg == "--verify") options.verify = true;
        else if (arg == "--gravity-table") options.gravity_table = true;
        else if (!arg.empty() && arg[0] != '-' && !positional_taken) {
            positional_taken = true;
            if (tool == "verify") options.trace_file = arg;
            else options.replay_file = arg;
        } else throw std::invalid_argument("unknown option: " + arg);
    }
    return options;
}

std::vector<std::uint8_t> read_binary(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open input: " + path);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
void write_binary(const std::string& path, std::span<const std::uint8_t> data) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot open output: " + path);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!out) throw std::runtime_error("failed writing: " + path);
}
std::string state_json(const Game& game) {
    const auto& s = game.state();
    std::ostringstream out;
    out << "{\n  \"frame\": " << s.frame << ",\n  \"phase\": " << static_cast<int>(s.phase)
        << ",\n  \"piece\": \"" << piece_name(s.current) << "\",\n  \"next\": \""
        << piece_name(s.next) << "\",\n  \"x\": " << s.x << ",\n  \"y\": " << s.y
        << ",\n  \"orientation\": " << s.orientation << ",\n  \"level\": " << s.level
        << ",\n  \"lines\": " << s.lines << ",\n  \"score\": " << s.score
        << ",\n  \"gravity_counter\": " << s.gravity_counter
        << ",\n  \"das_counter\": " << s.das_counter
        << ",\n  \"rng_state\": " << s.rng_state
        << ",\n  \"hash\": " << game.state_hash() << ",\n  \"board\": [\n";
    for (int y = 0; y < board_height; ++y) {
        out << "    \"";
        for (int x = 0; x < board_width; ++x)
            out << (s.board[y * board_width + x] ? '#' : '.');
        out << "\"" << (y == board_height - 1 ? "\n" : ",\n");
    }
    out << "  ]\n}\n";
    return out.str();
}

int run_headless(const Options& options) {
    Game game(options.config);
    if (!options.state_in.empty()) game.load_state(read_binary(options.state_in));
    const auto inputs = options.input_file.empty() ? std::vector<std::uint8_t>{} : read_binary(options.input_file);
    Replay replay;
    replay.config = options.config;
    if (!options.replay_out.empty() && !options.state_in.empty())
        throw std::invalid_argument("recording from a loaded state is not a version-1 replay");
    // A zero-frame recording is still a valid replay of the initial state.
    if (!options.replay_out.empty()) replay.final_hash = game.state_hash();
    for (std::uint64_t frame = 0; frame < options.frames && !game.terminal(); ++frame) {
        const auto input = frame < inputs.size() ? inputs[frame] : InputFrame{0};
        game.tick(input);
        if (!options.replay_out.empty()) replay.append(input, game);
    }
    if (!options.state_out.empty()) write_binary(options.state_out, game.save_state());
    if (!options.dump_state.empty()) {
        std::ofstream out(options.dump_state);
        if (!out) throw std::runtime_error("cannot open state dump: " + options.dump_state);
        out << state_json(game);
    }
    if (!options.replay_out.empty()) replay.save(options.replay_out);
    std::cout << "frames=" << game.state().frame << " score=" << game.state().score
              << " lines=" << game.state().lines << " level=" << game.state().level
              << " phase=" << static_cast<int>(game.state().phase) << '\n';
    if (options.final_hash) std::cout << "hash=" << std::hex << std::setw(16) << std::setfill('0')
                                      << game.state_hash() << std::dec << '\n';
    return 0;
}

int run_replay(const Options& options) {
    if (options.replay_file.empty()) throw std::invalid_argument("replay file required");
    const auto replay = Replay::load(options.replay_file);
    const auto result = verify_replay(replay);
    if (!result.valid) {
        std::cerr << "Mismatch at frame " << result.divergence_frame << ": " << result.error
                  << "\nExpected hash: " << std::hex << result.expected_hash
                  << "\nActual hash:   " << result.actual_hash << std::dec << '\n';
        return 2;
    }
    std::cout << "Replay valid: " << result.checked_frames << " frames, hash "
              << std::hex << result.actual_hash << std::dec << '\n';
    return 0;
}

int run_benchmark(const Options& options) {
    if (options.envs < 1) throw std::invalid_argument("--envs must be positive");
    std::vector<Game> games;
    games.reserve(options.envs);
    for (int i = 0; i < options.envs; ++i) {
        auto config = options.config;
        config.seed = static_cast<std::uint16_t>(config.seed + i);
        games.emplace_back(config);
    }
    std::uint64_t steps = 0;
    std::uint64_t completed_games = 0;
    const auto start = std::chrono::steady_clock::now();
    for (std::uint64_t frame = 0; frame < options.frames; ++frame) {
        for (auto& game : games) {
            if (game.terminal()) game.reset(static_cast<std::uint16_t>(game.state().rng_state + 1));
            game.tick(0);
            ++steps;
            if (game.terminal()) ++completed_games;
        }
    }
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const auto frames_per_second = seconds > 0 ? static_cast<std::uint64_t>(steps / seconds) : 0;
    const auto games_per_second = seconds > 0 ? static_cast<double>(completed_games) / seconds : 0.0;
    std::cout << "envs=" << options.envs << " steps=" << steps
              << " completed_games=" << completed_games
              << " seconds=" << std::fixed << std::setprecision(3) << seconds
              << " frames_per_second=" << frames_per_second
              << " games_per_second=" << std::setprecision(2) << games_per_second << '\n';
    return 0;
}

int run_rng(const Options& options) {
    auto state = options.config.seed;
    Piece previous = Piece::None;
    for (int i = 1; i <= options.pieces; ++i) {
        const auto piece = rng_piece(state, static_cast<std::uint32_t>(i), previous);
        previous = piece;
        std::cout << piece_name(piece) << (i == options.pieces ? '\n' : ' ');
        state = rng_advance(state);
    }
    std::cout << "final_rng_state=" << state << '\n';
    return 0;
}

std::optional<std::int64_t> json_integer(const std::string& line, const char* name) {
    const std::regex pattern(std::string("\\\"") + name + "\\\"\\s*:\\s*(-?[0-9]+)");
    std::smatch match;
    if (!std::regex_search(line, match, pattern)) return std::nullopt;
    return std::stoll(match[1].str());
}
std::optional<std::string> json_string(const std::string& line, const char* name) {
    const std::regex pattern(std::string("\\\"") + name + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::smatch match;
    if (!std::regex_search(line, match, pattern)) return std::nullopt;
    return match[1].str();
}
int run_verify(const Options& options) {
    if (options.trace_file.empty()) throw std::invalid_argument("JSONL trace file required");
    std::ifstream in(options.trace_file);
    if (!in) throw std::runtime_error("cannot open trace: " + options.trace_file);
    Game game(options.config);
    std::string line;
    std::uint64_t records = 0;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        const auto input = json_integer(line, "input");
        if (!input || *input < 0 || *input > 127)
            throw std::runtime_error("trace record needs 0..127 input");
        game.tick(static_cast<InputFrame>(*input));
        ++records;
        const auto& s = game.state();
        const auto check = [&](const char* field, std::int64_t actual) {
            if (auto expected = json_integer(line, field); expected && *expected != actual) {
                std::ostringstream out;
                out << "Mismatch at frame " << s.frame << ": " << field
                    << " expected " << *expected << ", actual " << actual;
                throw std::runtime_error(out.str());
            }
        };
        check("frame", static_cast<std::int64_t>(s.frame));
        check("x", s.x); check("y", s.y); check("orientation", s.orientation);
        check("level", s.level); check("lines", s.lines);
        check("score", static_cast<std::int64_t>(s.score));
        check("das", s.das_counter); check("gravity", s.gravity_counter);
        if (const auto piece = json_string(line, "piece"); piece && *piece != piece_name(s.current))
            throw std::runtime_error("Mismatch at frame " + std::to_string(s.frame) + ": piece expected " + *piece + ", actual " + piece_name(s.current));
        if (const auto board = json_string(line, "board"); board) {
            if (board->size() != s.board.size()) throw std::runtime_error("trace board must contain 200 occupancy characters");
            for (std::size_t i = 0; i < board->size(); ++i)
                if (((*board)[i] != '.' && (*board)[i] != '0') != (s.board[i] != 0))
                    throw std::runtime_error("Mismatch at frame " + std::to_string(s.frame) + ": board cell " + std::to_string(i));
        }
    }
    std::cout << "Trace matched: " << records << " frames\n";
    return 0;
}
}

int main(int argc, char** argv) {
    try {
        const std::string tool = BLOCKS_TOOL;
        const auto options = parse(argc, argv, tool);
        if (tool == "headless") return run_headless(options);
        if (tool == "replay") return run_replay(options);
        if (tool == "benchmark") return run_benchmark(options);
        if (tool == "rng") return run_rng(options);
        if (tool == "verify") return run_verify(options);
        if (tool == "info") {
            std::cout << "NTSC logical FPS: " << std::setprecision(15) << ntsc_frames_per_second << '\n';
            for (int level = 0; level <= 40; ++level)
                std::cout << "level " << level << ": " << gravity_period(level) << " frames/cell\n";
            return 0;
        }
        throw std::runtime_error("unknown tool executable");
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
