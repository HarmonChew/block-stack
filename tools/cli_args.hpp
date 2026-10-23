#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace blocks {

inline std::string value_after(int& index, int argc, char** argv, const std::string& option) {
    if (++index >= argc) throw std::invalid_argument("missing value for option " + option);
    return argv[index];
}

inline std::uint64_t numeric_arg(const std::string& option, const std::string& text,
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

} // namespace blocks
