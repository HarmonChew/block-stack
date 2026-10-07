#include "blocks/game.hpp"

#include <array>
#include <stdexcept>
#include <string_view>

namespace blocks {
namespace {

// NTSC values in the game's gravity lookup table; the fallback at 29+ is 1.
constexpr std::array<int, 30> gravity_by_level{
    48, 43, 38, 33, 28, 23, 18, 13, 8, 6,
     5,  5,  5,  4,  4,  4,  3,  3, 3, 2,
     2,  2,  2,  2,  2,  2,  2,  2, 2, 1,
};

// The mixed binary/BCD comparison in the NES produces these thresholds.
constexpr std::array<int, 20> first_transition_by_start_level{
     10,  20,  30,  40,  50,  60,  70,  80,  90, 100,
    100, 100, 100, 100, 100, 100, 110, 120, 130, 140,
};

} // namespace

Ruleset ruleset(RulesetId id) {
    Ruleset result;
    result.id = id;
    result.timing.gravity = gravity_by_level;
    result.timing.das_initial = 16;
    result.timing.das_repeat = 6;
    result.timing.first_piece_delay = 96;
    switch (id) {
    case RulesetId::ClassicNtscStrict:
        result.score_capped = true;
        result.level_wrap = true;
        return result;
    case RulesetId::ClassicNtscExtended:
        result.score_capped = false;
        result.level_wrap = false;
        return result;
    }
    throw std::invalid_argument("unknown ruleset id");
}

RulesetId ruleset_from_name(const std::string& name) {
    const std::string_view value{name};
    if (value == "classic_ntsc_strict" || value == "classic-ntsc-strict" ||
        value == "strict") {
        return RulesetId::ClassicNtscStrict;
    }
    if (value == "classic_ntsc_extended" || value == "classic-ntsc-extended" ||
        value == "extended") {
        return RulesetId::ClassicNtscExtended;
    }
    throw std::invalid_argument("unknown ruleset: " + name + " (valid names: " +
                                ruleset_name(RulesetId::ClassicNtscStrict) + ", " +
                                ruleset_name(RulesetId::ClassicNtscExtended) + ")");
}

const char* ruleset_name(RulesetId id) {
    switch (id) {
    case RulesetId::ClassicNtscStrict: return "classic_ntsc_strict";
    case RulesetId::ClassicNtscExtended: return "classic_ntsc_extended";
    }
    throw std::invalid_argument("unknown ruleset id");
}

int gravity_period(int level) {
    if (level < 0) {
        throw std::invalid_argument("level must be nonnegative");
    }
    return gravity_by_level[static_cast<std::size_t>(level < 29 ? level : 29)];
}

int first_transition_lines(int start_level) {
    if (start_level < 0 || start_level >=
            static_cast<int>(first_transition_by_start_level.size())) {
        throw std::invalid_argument("start level must be in [0, 19]");
    }
    return first_transition_by_start_level[static_cast<std::size_t>(start_level)];
}

} // namespace blocks
