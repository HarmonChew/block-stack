#include "blocks/game.hpp"
#include "blocks/replay.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace blocks;

constexpr int logical_w = 960;
constexpr int logical_h = 720;
constexpr int board_x = 354;
constexpr int board_y = 107;
constexpr int cell_size = 25;

std::uint64_t unsigned_value(const std::string& value, std::uint64_t maximum) {
    std::uint64_t number = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || number > maximum)
        throw std::invalid_argument("invalid unsigned value: " + value);
    return number;
}

struct Color { Uint8 r, g, b, a = 255; };
constexpr Color background{10, 16, 27};
constexpr Color panel{21, 31, 47};
constexpr Color panel_edge{54, 72, 91};
constexpr Color white{239, 244, 247};
constexpr Color muted{143, 163, 177};
constexpr Color accent{103, 223, 204};
constexpr std::array<Color, 7> piece_colors{{
    {91, 220, 229}, {100, 155, 244}, {255, 177, 91}, {233, 218, 111},
    {113, 217, 142}, {190, 141, 236}, {238, 116, 145}
}};
constexpr std::array<Color, 7> high_contrast_colors{{
    {255, 255, 255}, {255, 210, 80}, {80, 210, 255}, {255, 100, 210},
    {135, 255, 95}, {255, 145, 75}, {170, 150, 255}
}};
constexpr std::array<const char*, 5> action_names{{"LEFT", "RIGHT", "DOWN", "ROTATE CW", "ROTATE CCW"}};
constexpr std::array<InputFrame, 5> action_bits{{Left, Right, Down, RotateCW, RotateCCW}};

struct Settings {
    std::array<SDL_Scancode, 5> keys{{SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
        SDL_SCANCODE_DOWN, SDL_SCANCODE_X, SDL_SCANCODE_Z}};
    std::array<SDL_GamepadButton, 5> pad{{SDL_GAMEPAD_BUTTON_DPAD_LEFT,
        SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_DPAD_DOWN,
        SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST}};
    float volume = 0.7f;
    bool fullscreen = false;
    bool vsync = true;
    bool integer_scale = false;
    bool show_next = true;
    bool high_contrast = false;
    int start_level = 0;
    int height = 0;
    int mode = 0;
    int ruleset = 1;
    int seed = 0x8988;
};

std::string config_dir() {
    char* path = SDL_GetPrefPath("project_name", "project_name");
    if (!path) return ".";
    std::string result(path);
    SDL_free(path);
    return result;
}
std::string settings_path() { return config_dir() + "settings.ini"; }
std::string last_replay_path() { return config_dir() + "last.rep"; }
Settings load_settings() {
    Settings settings;
    std::ifstream in(settings_path());
    std::string line;
    while (std::getline(in, line)) {
        const auto at = line.find('=');
        if (at == std::string::npos) continue;
        const auto key = line.substr(0, at);
        const auto value = line.substr(at + 1);
        try {
            if (key == "volume") settings.volume = std::clamp(std::stof(value), 0.0f, 1.0f);
            else if (key == "fullscreen") settings.fullscreen = std::stoi(value) != 0;
            else if (key == "vsync") settings.vsync = std::stoi(value) != 0;
            else if (key == "integer_scale") settings.integer_scale = std::stoi(value) != 0;
            else if (key == "show_next") settings.show_next = std::stoi(value) != 0;
            else if (key == "high_contrast") settings.high_contrast = std::stoi(value) != 0;
            else if (key == "start_level") settings.start_level = std::clamp(std::stoi(value), 0, 19);
            else if (key == "height") settings.height = std::clamp(std::stoi(value), 0, 5);
            else if (key == "mode") settings.mode = std::clamp(std::stoi(value), 0, 1);
            else if (key == "ruleset") settings.ruleset = std::clamp(std::stoi(value), 0, 1);
            else if (key == "seed") settings.seed = std::clamp(std::stoi(value), 0, 65535);
            else {
                for (int i = 0; i < 5; ++i) {
                    if (key == std::string("key_") + std::to_string(i)) {
                        const auto n = std::stoi(value);
                        if (n > 0 && n < SDL_SCANCODE_COUNT) settings.keys[i] = static_cast<SDL_Scancode>(n);
                    }
                    if (key == std::string("pad_") + std::to_string(i)) {
                        const auto n = std::stoi(value);
                        if (n >= 0 && n < SDL_GAMEPAD_BUTTON_COUNT) settings.pad[i] = static_cast<SDL_GamepadButton>(n);
                    }
                }
            }
        } catch (...) { /* Ignore a malformed setting; retain the safe default. */ }
    }
    return settings;
}
void save_settings(const Settings& s) {
    std::ofstream out(settings_path());
    if (!out) return;
    out << "volume=" << s.volume << "\nfullscreen=" << s.fullscreen
        << "\nvsync=" << s.vsync << "\nshow_next=" << s.show_next
        << "\ninteger_scale=" << s.integer_scale
        << "\nhigh_contrast=" << s.high_contrast << "\nstart_level=" << s.start_level
        << "\nheight=" << s.height << "\nmode=" << s.mode << "\nruleset=" << s.ruleset
        << "\nseed=" << s.seed << '\n';
    for (int i = 0; i < 5; ++i)
        out << "key_" << i << '=' << static_cast<int>(s.keys[i]) << "\npad_" << i
            << '=' << static_cast<int>(s.pad[i]) << '\n';
}

void draw_color(SDL_Renderer* r, Color c) { SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a); }
void fill(SDL_Renderer* r, float x, float y, float w, float h, Color c) {
    draw_color(r, c);
    const SDL_FRect rect{x, y, w, h};
    SDL_RenderFillRect(r, &rect);
}
void outline(SDL_Renderer* r, float x, float y, float w, float h, Color c) {
    draw_color(r, c);
    const SDL_FRect rect{x, y, w, h};
    SDL_RenderRect(r, &rect);
}
void text(SDL_Renderer* r, float x, float y, const std::string& value, Color c = white, int scale = 2) {
    draw_color(r, c);
    SDL_SetRenderScale(r, static_cast<float>(scale), static_cast<float>(scale));
    SDL_RenderDebugText(r, x / scale, y / scale, value.c_str());
    SDL_SetRenderScale(r, 1.0f, 1.0f);
}
std::string grouped(std::uint64_t value) {
    auto s = std::to_string(value);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(i, ",");
    return s;
}

class Sound {
public:
    explicit Sound(float volume) : volume_(volume) {
        SDL_AudioSpec spec{};
        spec.format = SDL_AUDIO_F32;
        spec.channels = 1;
        spec.freq = 48000;
        stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (stream_) SDL_ResumeAudioStreamDevice(stream_);
    }
    ~Sound() { if (stream_) SDL_DestroyAudioStream(stream_); }
    void volume(float value) { volume_ = value; }
    void tone(float frequency, float seconds, float brightness = 0.0f) {
        if (!stream_ || volume_ <= 0.0f) return;
        const int count = static_cast<int>(seconds * 48000);
        std::vector<float> samples(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            const float t = static_cast<float>(i) / 48000.0f;
            const float envelope = std::pow(1.0f - static_cast<float>(i) / count, 2.0f);
            samples[i] = volume_ * 0.12f * envelope *
                (std::sin(6.2831853f * frequency * t) +
                 brightness * std::sin(6.2831853f * frequency * 2.01f * t));
        }
        SDL_PutAudioStreamData(stream_, samples.data(), static_cast<int>(samples.size() * sizeof(float)));
    }
private:
    SDL_AudioStream* stream_ = nullptr;
    float volume_ = 0.7f;
};

enum class Screen { Menu, Playing, Settings, ReplayBrowser };

class App {
public:
    App() : settings_(load_settings()), game_(make_config()), sound_(settings_.volume) {
        window_ = SDL_CreateWindow("[PROJECT_NAME]", logical_w, logical_h,
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (!window_) throw std::runtime_error(SDL_GetError());
        renderer_ = SDL_CreateRenderer(window_, nullptr);
        if (!renderer_) throw std::runtime_error(SDL_GetError());
        SDL_SetRenderLogicalPresentation(renderer_, logical_w, logical_h,
            settings_.integer_scale ? SDL_LOGICAL_PRESENTATION_INTEGER_SCALE :
                                      SDL_LOGICAL_PRESENTATION_LETTERBOX);
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderVSync(renderer_, settings_.vsync ? 1 : 0);
        SDL_SetWindowFullscreen(window_, settings_.fullscreen);
        open_gamepad();
    }
    ~App() {
        save_settings(settings_);
        if (pad_) SDL_CloseGamepad(pad_);
        if (renderer_) SDL_DestroyRenderer(renderer_);
        if (window_) SDL_DestroyWindow(window_);
    }
    void load_replay(const std::string& path, bool paused = false) {
        if (controller_mode_) return;
        if (screen_ == Screen::Playing && !replay_mode_) save_replay(true);
        replay_playback_ = Replay::load(path);
        game_ = Game(replay_playback_.config);
        replay_mode_ = true;
        replay_pos_ = 0;
        visual_board_valid_ = false;
        stats_overlay_ = false;
        paused_ = paused;
        screen_ = Screen::Playing;
    }
    void start_controller(Config config, std::uint64_t frame_limit, const std::string& label, bool paused) {
        controller_mode_ = true;
        controller_config_ = config;
        controller_frame_limit_ = frame_limit;
        controller_label_ = label;
        std::cout << "BLOCK_STACK_CONTROLLER 1\n" << std::flush;
        start();
        paused_ = paused;
    }
    void set_speed(const std::string& value) {
        const std::array<std::string, 6> names{{"0.25", "0.5", "1", "2", "4", "8"}};
        const auto found = std::find(names.begin(), names.end(), value);
        if (found == names.end()) throw std::invalid_argument("speed must be 0.25, 0.5, 1, 2, 4 or 8");
        speed_index_ = static_cast<int>(found - names.begin());
    }
    void controller_smoke() {
        if (!controller_mode_) throw std::invalid_argument("--controller-smoke requires --controller-stdio");
        const auto key = [this](SDL_Scancode code) {
            SDL_Event event{};
            event.type = SDL_EVENT_KEY_DOWN;
            event.key.scancode = code;
            handle(event);
        };
        paused_ = false;
        key(SDL_SCANCODE_P);
        if (!paused_ || game_.state().frame != 0) throw std::runtime_error("controller pause failed");
        key(SDL_SCANCODE_PERIOD);
        if (game_.state().frame != 1 || !paused_) throw std::runtime_error("controller frame step failed");
        key(SDL_SCANCODE_R);
        if (game_.state().frame != 0 || paused_) throw std::runtime_error("controller restart failed");
        while (running_ && !controller_finished_) step();
        render();
    }
    void set_screenshot(std::string path, std::uint64_t at_frame = 0) {
        screenshot_path_ = std::move(path);
        screenshot_frame_ = at_frame;
    }
    void keyboard_smoke() {
        Config config;
        config.start_level = 0;
        config.seed = 42;
        game_ = Game(config);
        screen_ = Screen::Playing;
        paused_ = false;
        recording_ = Replay{};
        recording_.config = config;
        const int old_x = game_.state().x;
        SDL_Event down{};
        down.type = SDL_EVENT_KEY_DOWN;
        down.key.scancode = settings_.keys[0];
        down.key.down = true;
        handle(down);
        step();
        if (game_.state().x != old_x - 1)
            throw std::runtime_error("keyboard left input did not reach the simulation");
        SDL_Event up{};
        up.type = SDL_EVENT_KEY_UP;
        up.key.scancode = settings_.keys[0];
        up.key.down = false;
        handle(up);
    }
    int run(std::uint64_t smoke_frames = 0) {
        const double ns_per_frame = 1'000'000'000.0 / ntsc_frames_per_second;
        auto last = SDL_GetTicksNS();
        double accumulator = 0.0;
        std::uint64_t rendered = 0;
        while (running_) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) handle(event);
            const auto now = SDL_GetTicksNS();
            const auto elapsed = now - last;
            last = now;
            if (screen_ == Screen::Playing && !paused_) accumulator += elapsed * speeds_[speed_index_];
            else accumulator = 0.0;
            while (running_ && accumulator >= ns_per_frame && screen_ == Screen::Playing && !paused_) {
                step();
                accumulator -= ns_per_frame;
            }
            render();
            ++rendered;
            if (smoke_frames && rendered >= smoke_frames) break;
            if (!settings_.vsync) SDL_Delay(1);
        }
        if (controller_mode_ && !controller_finished_) {
            controller_snapshot("ABORT");
            save_replay(true);
        }
        return 0;
    }
private:
    Settings settings_;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Gamepad* pad_ = nullptr;
    Game game_;
    Sound sound_;
    Replay recording_;
    Replay replay_playback_;
    Screen screen_ = Screen::Menu;
    bool running_ = true;
    bool paused_ = false;
    bool debug_ = false;
    bool stats_overlay_ = false;
    bool editing_seed_ = false;
    std::string seed_buffer_;
    std::array<bool, SDL_SCANCODE_COUNT> key_held_{};
    bool replay_mode_ = false;
    bool controller_mode_ = false;
    bool controller_finished_ = false;
    Config controller_config_;
    std::uint64_t controller_frame_limit_ = 60000;
    std::string controller_label_ = "LIVE AI";
    bool recording_archived_ = false;
    std::size_t replay_pos_ = 0;
    std::vector<std::filesystem::path> replay_files_;
    std::size_t replay_selected_ = 0;
    std::string replay_browser_error_;
    std::array<std::uint8_t, board_width * board_height> visual_board_{};
    bool visual_board_valid_ = false;
    std::string screenshot_path_;
    std::uint64_t screenshot_frame_ = 0;
    int settings_index_ = 0;
    bool capturing_ = false;
    int speed_index_ = 2;
    constexpr static std::array<double, 6> speeds_{{0.25, 0.5, 1.0, 2.0, 4.0, 8.0}};

    Config make_config() const {
        Config config;
        config.ruleset = settings_.ruleset ? RulesetId::ClassicNtscExtended : RulesetId::ClassicNtscStrict;
        config.mode = settings_.mode ? Mode::Challenge : Mode::Endless;
        config.start_level = settings_.start_level;
        config.height = settings_.height;
        config.seed = static_cast<std::uint16_t>(settings_.seed);
        return config;
    }
    void open_gamepad() {
        if (pad_) return;
        int count = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&count);
        if (ids && count > 0) pad_ = SDL_OpenGamepad(ids[0]);
        SDL_free(ids);
    }
    void start() {
        if (controller_mode_ && screen_ == Screen::Playing && !controller_finished_)
            controller_snapshot("ABORT");
        if (screen_ == Screen::Playing && !replay_mode_) save_replay(true);
        game_ = Game(controller_mode_ ? controller_config_ : make_config());
        recording_ = Replay{};
        recording_.config = game_.config();
        replay_mode_ = false;
        recording_archived_ = false;
        visual_board_valid_ = false;
        paused_ = false;
        stats_overlay_ = false;
        screen_ = Screen::Playing;
        controller_finished_ = false;
        if (controller_mode_) controller_snapshot("BEGIN");
        sound_.tone(440, 0.07f, 0.3f);
        save_settings(settings_);
    }
    void save_replay(bool archive = false) {
        if (!replay_mode_ && !recording_.inputs.empty()) {
            recording_.save(last_replay_path());
            if (archive && !recording_archived_) {
                const std::string stem = "run-" + std::to_string(game_.config().seed) + "-" +
                    std::to_string(game_.state().frame) + "-" + std::to_string(SDL_GetTicks());
                auto path = std::filesystem::path(config_dir()) / (stem + ".rep");
                int suffix = 1;
                while (std::filesystem::exists(path))
                    path = std::filesystem::path(config_dir()) / (stem + "-" + std::to_string(suffix++) + ".rep");
                recording_.save(path.string());
                recording_archived_ = true;
            }
        }
    }
    void refresh_replays() {
        replay_files_.clear();
        replay_browser_error_.clear();
        std::error_code error;
        for (std::filesystem::directory_iterator it(config_dir(), error), end;
             !error && it != end; it.increment(error)) {
            if (it->is_regular_file(error) && it->path().extension() == ".rep")
                replay_files_.push_back(it->path());
        }
        std::sort(replay_files_.begin(), replay_files_.end(), [](const auto& a, const auto& b) {
            std::error_code a_error, b_error;
            const auto a_time = std::filesystem::last_write_time(a, a_error);
            const auto b_time = std::filesystem::last_write_time(b, b_error);
            if (!a_error && !b_error && a_time != b_time) return a_time > b_time;
            return a.string() > b.string();
        });
        replay_selected_ = 0;
        screen_ = Screen::ReplayBrowser;
    }
    void open_selected_replay() {
        if (replay_files_.empty()) return;
        try { load_replay(replay_files_[replay_selected_].string()); }
        catch (const std::exception& e) { replay_browser_error_ = e.what(); }
    }
    void toggle_fullscreen() {
        settings_.fullscreen = !settings_.fullscreen;
        SDL_SetWindowFullscreen(window_, settings_.fullscreen);
        save_settings(settings_);
    }
    void toggle_scale() {
        settings_.integer_scale = !settings_.integer_scale;
        SDL_SetRenderLogicalPresentation(renderer_, logical_w, logical_h,
            settings_.integer_scale ? SDL_LOGICAL_PRESENTATION_INTEGER_SCALE :
                                      SDL_LOGICAL_PRESENTATION_LETTERBOX);
        save_settings(settings_);
    }
    void handle(const SDL_Event& event) {
        if (event.type == SDL_EVENT_QUIT) { running_ = false; return; }
        if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) key_held_.fill(false);
        if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
            if (event.key.scancode >= 0 && event.key.scancode < SDL_SCANCODE_COUNT)
                key_held_[event.key.scancode] = event.type == SDL_EVENT_KEY_DOWN;
        }
        if (event.type == SDL_EVENT_GAMEPAD_ADDED) open_gamepad();
        if (event.type == SDL_EVENT_GAMEPAD_REMOVED && pad_ && !SDL_GamepadConnected(pad_)) {
            SDL_CloseGamepad(pad_); pad_ = nullptr; open_gamepad();
        }
        if (screen_ == Screen::Settings && capturing_) {
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                if (event.key.scancode != SDL_SCANCODE_ESCAPE && settings_index_ >= 5) return;
                if (event.key.scancode != SDL_SCANCODE_ESCAPE)
                    settings_.keys[settings_index_] = event.key.scancode;
                capturing_ = false; sound_.tone(690, 0.04f); save_settings(settings_);
                return;
            }
            if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                if (settings_index_ < 5) return;
                settings_.pad[settings_index_ - 5] = static_cast<SDL_GamepadButton>(event.gbutton.button);
                capturing_ = false; sound_.tone(690, 0.04f); save_settings(settings_);
                return;
            }
        }
        if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
            const auto button = static_cast<SDL_GamepadButton>(event.gbutton.button);
            if (screen_ == Screen::Playing) {
                if (std::find(settings_.pad.begin(), settings_.pad.end(), button) != settings_.pad.end())
                    return;
                if (button == SDL_GAMEPAD_BUTTON_START) paused_ = !paused_;
                else if (button == SDL_GAMEPAD_BUTTON_NORTH) start();
                else if (button == SDL_GAMEPAD_BUTTON_BACK) {
                    save_replay(true);
                    if (controller_mode_) running_ = false;
                    else { screen_ = Screen::Menu; paused_ = false; }
                } else if (button == SDL_GAMEPAD_BUTTON_WEST) stats_overlay_ = !stats_overlay_;
            } else if (screen_ == Screen::Menu) {
                if (button == SDL_GAMEPAD_BUTTON_SOUTH) start();
                else if (button == SDL_GAMEPAD_BUTTON_DPAD_UP) settings_.start_level = std::min(19, settings_.start_level + 1);
                else if (button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) settings_.start_level = std::max(0, settings_.start_level - 1);
                else if (button == SDL_GAMEPAD_BUTTON_DPAD_LEFT) settings_.height = std::max(0, settings_.height - 1);
                else if (button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) settings_.height = std::min(5, settings_.height + 1);
                else if (button == SDL_GAMEPAD_BUTTON_WEST) settings_.mode = 1 - settings_.mode;
                else if (button == SDL_GAMEPAD_BUTTON_NORTH) settings_.ruleset = 1 - settings_.ruleset;
                save_settings(settings_);
            } else if (screen_ == Screen::ReplayBrowser) {
                if (button == SDL_GAMEPAD_BUTTON_BACK) screen_ = Screen::Menu;
                else if (button == SDL_GAMEPAD_BUTTON_SOUTH) open_selected_replay();
                else if (button == SDL_GAMEPAD_BUTTON_DPAD_UP && replay_selected_ > 0) --replay_selected_;
                else if (button == SDL_GAMEPAD_BUTTON_DPAD_DOWN && replay_selected_ + 1 < replay_files_.size()) ++replay_selected_;
            }
            return;
        }
        if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat) return;
        const auto key = event.key.scancode;
        if (screen_ == Screen::Menu && editing_seed_) {
            if (key == SDL_SCANCODE_ESCAPE) editing_seed_ = false;
            else if (key == SDL_SCANCODE_RETURN) {
                if (!seed_buffer_.empty()) settings_.seed = std::stoi(seed_buffer_);
                editing_seed_ = false;
                save_settings(settings_);
            } else if (key == SDL_SCANCODE_BACKSPACE && !seed_buffer_.empty()) {
                seed_buffer_.pop_back();
            } else if (event.key.key >= '0' && event.key.key <= '9' && seed_buffer_.size() < 5) {
                const auto candidate = seed_buffer_ + static_cast<char>(event.key.key);
                if (std::stoul(candidate) <= 65535) seed_buffer_ = candidate;
            }
            return;
        }
        // In play, a remapped gameplay key takes precedence over shortcuts.
        if (screen_ == Screen::Playing &&
            std::find(settings_.keys.begin(), settings_.keys.end(), key) != settings_.keys.end())
            return;
        if (key == SDL_SCANCODE_F11) { toggle_fullscreen(); return; }
        if (key == SDL_SCANCODE_F3) {
            settings_.vsync = !settings_.vsync;
            SDL_SetRenderVSync(renderer_, settings_.vsync ? 1 : 0);
            save_settings(settings_); return;
        }
        if (key == SDL_SCANCODE_F4) { toggle_scale(); return; }
        if (key == SDL_SCANCODE_ESCAPE) {
            if (screen_ == Screen::Settings) screen_ = Screen::Menu;
            else if (screen_ == Screen::ReplayBrowser) screen_ = Screen::Menu;
            else if (screen_ == Screen::Playing) {
                save_replay(true);
                if (controller_mode_) running_ = false;
                else { screen_ = Screen::Menu; paused_ = false; }
            }
            else running_ = false;
            return;
        }
        if (screen_ == Screen::Menu) {
            if (key == SDL_SCANCODE_RETURN || key == SDL_SCANCODE_SPACE) start();
            else if (key == SDL_SCANCODE_UP) settings_.start_level = std::min(19, settings_.start_level + 1);
            else if (key == SDL_SCANCODE_DOWN) settings_.start_level = std::max(0, settings_.start_level - 1);
            else if (key == SDL_SCANCODE_LEFT) settings_.height = std::max(0, settings_.height - 1);
            else if (key == SDL_SCANCODE_RIGHT) settings_.height = std::min(5, settings_.height + 1);
            else if (key == SDL_SCANCODE_M) settings_.mode = 1 - settings_.mode;
            else if (key == SDL_SCANCODE_T) settings_.ruleset = 1 - settings_.ruleset;
            else if (key == SDL_SCANCODE_S) settings_.seed = (settings_.seed + 1) & 65535;
            else if (key == SDL_SCANCODE_E) { editing_seed_ = true; seed_buffer_.clear(); }
            else if (key == SDL_SCANCODE_F2) screen_ = Screen::Settings;
            else if (key == SDL_SCANCODE_F6 && std::filesystem::exists(last_replay_path())) load_replay(last_replay_path());
            else if (key == SDL_SCANCODE_F7) refresh_replays();
            else if (key == SDL_SCANCODE_H) settings_.high_contrast = !settings_.high_contrast;
            else if (key == SDL_SCANCODE_N) settings_.show_next = !settings_.show_next;
            sound_.tone(500, 0.025f);
            save_settings(settings_);
            return;
        }
        if (screen_ == Screen::Settings) {
            if (key == SDL_SCANCODE_UP) settings_index_ = (settings_index_ + 9) % 10;
            else if (key == SDL_SCANCODE_DOWN) settings_index_ = (settings_index_ + 1) % 10;
            else if (key == SDL_SCANCODE_RETURN) capturing_ = true;
            else if (key == SDL_SCANCODE_LEFT) settings_.volume = std::max(0.0f, settings_.volume - 0.1f);
            else if (key == SDL_SCANCODE_RIGHT) settings_.volume = std::min(1.0f, settings_.volume + 0.1f);
            sound_.volume(settings_.volume);
            save_settings(settings_);
            return;
        }
        if (screen_ == Screen::ReplayBrowser) {
            if (key == SDL_SCANCODE_UP && replay_selected_ > 0) --replay_selected_;
            else if (key == SDL_SCANCODE_DOWN && replay_selected_ + 1 < replay_files_.size()) ++replay_selected_;
            else if (key == SDL_SCANCODE_RETURN) open_selected_replay();
            return;
        }
        if (key == SDL_SCANCODE_P) paused_ = !paused_;
        else if (key == SDL_SCANCODE_PERIOD && paused_) step();
        else if (key == SDL_SCANCODE_R) start();
        else if (key == SDL_SCANCODE_F1) debug_ = !debug_;
        else if (key == SDL_SCANCODE_TAB) stats_overlay_ = !stats_overlay_;
        else if (key == SDL_SCANCODE_N) settings_.show_next = !settings_.show_next;
        else if (key == SDL_SCANCODE_H) settings_.high_contrast = !settings_.high_contrast;
        else if (key == SDL_SCANCODE_LEFTBRACKET) speed_index_ = std::max(0, speed_index_ - 1);
        else if (key == SDL_SCANCODE_RIGHTBRACKET) speed_index_ = std::min(5, speed_index_ + 1);
        else if (key == SDL_SCANCODE_F5) save_replay();
        else if (key == SDL_SCANCODE_F6 && std::filesystem::exists(last_replay_path())) load_replay(last_replay_path());
        save_settings(settings_);
    }
    InputFrame current_input() const {
        InputFrame result = 0;
        for (int i = 0; i < 5; ++i) {
            if (key_held_[settings_.keys[i]] || (pad_ && SDL_GetGamepadButton(pad_, settings_.pad[i])))
                result |= action_bits[i];
        }
        return result;
    }
    void controller_snapshot(const char* kind) const {
        constexpr char digits[] = "0123456789abcdef";
        const auto state = game_.save_state();
        std::string encoded;
        encoded.reserve(state.size() * 2);
        for (const auto byte : state) {
            encoded.push_back(digits[byte >> 4]);
            encoded.push_back(digits[byte & 15]);
        }
        std::cout << kind << ' ' << encoded << '\n' << std::flush;
        if (!std::cout) throw std::runtime_error("live controller output closed");
    }
    void step() {
        if (controller_mode_ && controller_finished_) { paused_ = true; return; }
        if (game_.terminal() && !replay_mode_) return;
        const auto before = game_.state();
        InputFrame input = 0;
        if (replay_mode_) {
            if (replay_pos_ >= replay_playback_.inputs.size()) { paused_ = true; return; }
            input = replay_playback_.inputs[replay_pos_++];
        } else if (controller_mode_) {
            controller_snapshot("STATE");
            std::string response;
            if (!std::getline(std::cin, response)) { running_ = false; return; }
            if (!response.empty() && response.back() == '\r') response.pop_back();
            input = static_cast<InputFrame>(unsigned_value(response, 31));
        } else input = current_input();
        const auto events = game_.tick(input);
        if (events.lines_cleared > 0) {
            // The cartridge's PPU keeps the filled board visible through the
            // clear animation although playfield RAM has already collapsed.
            visual_board_ = before.board;
            for (const auto cell : cells(before.current, before.orientation)) {
                const int x = before.x + cell.x;
                const int y = before.y + cell.y;
                if (x >= 0 && x < board_width && y >= 0 && y < board_height)
                    visual_board_[y * board_width + x] = static_cast<std::uint8_t>(before.current) + 1;
            }
            visual_board_valid_ = true;
        }
        if (game_.state().clearing_rows == 0) visual_board_valid_ = false;
        if (!replay_mode_) recording_.append(input, game_);
        if (events.moved) sound_.tone(245, 0.015f);
        if (events.rotated) sound_.tone(560, 0.025f, 0.15f);
        if (events.locked) sound_.tone(110, 0.055f, 0.45f);
        if (events.lines_cleared) sound_.tone(410 + events.lines_cleared * 130.0f, 0.14f, 0.4f);
        if (events.level_changed) sound_.tone(990, 0.19f, 0.3f);
        if (events.game_over) { sound_.tone(100, 0.36f, 0.2f); save_replay(true); stats_overlay_ = true; }
        if (events.challenge_completed) { sound_.tone(840, 0.36f, 0.6f); save_replay(true); stats_overlay_ = true; }
        if (controller_mode_ && (game_.terminal() || game_.state().frame >= controller_frame_limit_)) {
            controller_finished_ = true;
            paused_ = true;
            save_replay(true);
            controller_snapshot("END");
        }
    }
    Color piece_color(int piece) const {
        if (piece < 1 || piece > 7) return muted;
        return settings_.high_contrast ? high_contrast_colors[piece - 1] : piece_colors[piece - 1];
    }
    void block(float x, float y, int piece, float size = cell_size) const {
        const auto color = piece_color(piece);
        fill(renderer_, x + 1, y + 1, size - 2, size - 2, color);
        fill(renderer_, x + 4, y + 4, size - 8, 3, {255, 255, 255, 75});
        fill(renderer_, x + size - 5, y + 4, 2, size - 8, {0, 0, 0, 45});
    }
    void background_art() const {
        draw_color(renderer_, background);
        SDL_RenderClear(renderer_);
        for (int y = 0; y < logical_h; y += 12)
            fill(renderer_, 0, static_cast<float>(y), logical_w, 1, {62, 94, 116, 18});
        fill(renderer_, 0, 0, logical_w, 7, accent);
        fill(renderer_, 0, logical_h - 7, logical_w, 7, {55, 91, 109});
        fill(renderer_, 322, 60, 1, 620, {70, 98, 116, 60});
        fill(renderer_, 635, 60, 1, 620, {70, 98, 116, 60});
    }
    void board() const {
        fill(renderer_, board_x - 10, board_y - 10, 270, 520, {47, 74, 91, 120});
        fill(renderer_, board_x - 5, board_y - 5, 260, 510, {6, 13, 22});
        for (int y = 0; y < board_height; ++y) {
            for (int x = 0; x < board_width; ++x) {
                const float px = board_x + x * cell_size;
                const float py = board_y + y * cell_size;
                outline(renderer_, px, py, cell_size, cell_size, {48, 69, 79, 95});
                const auto& visible = visual_board_valid_ ? visual_board_ : game_.state().board;
                const int cell = visible[y * board_width + x];
                if (cell) block(px, py, cell);
                if (game_.state().phase == Phase::LineClear && (game_.state().clearing_rows & (1u << y)))
                    fill(renderer_, px + 1, py + 1, cell_size - 2, cell_size - 2, {235, 245, 251, 65});
            }
        }
        if (game_.state().phase == Phase::Active && game_.state().current != Piece::None) {
            const auto& state = game_.state();
            for (const auto cell : cells(state.current, state.orientation)) {
                const int x = state.x + cell.x;
                const int y = state.y + cell.y;
                if (x >= 0 && x < board_width && y >= 0 && y < board_height)
                    block(board_x + x * cell_size, board_y + y * cell_size,
                          static_cast<int>(state.current) + 1);
            }
        }
        outline(renderer_, board_x - 5, board_y - 5, 260, 510, accent);
    }
    void next_piece() const {
        fill(renderer_, 664, 107, 260, 126, panel);
        outline(renderer_, 664, 107, 260, 126, panel_edge);
        text(renderer_, 682, 124, "NEXT", accent);
        if (!settings_.show_next) { text(renderer_, 682, 171, "HIDDEN", muted); return; }
        if (game_.state().next == Piece::None) return;
        for (const auto cell : cells(game_.state().next, 0))
            block(746 + cell.x * 23, 157 + cell.y * 23,
                  static_cast<int>(game_.state().next) + 1, 22);
    }
    void label_value(int y, const std::string& label, const std::string& value, bool highlight = false) const {
        text(renderer_, 45, static_cast<float>(y), label, muted, 2);
        text(renderer_, 45, static_cast<float>(y + 22), value, highlight ? accent : white, 2);
    }
    void hud() const {
        text(renderer_, 42, 35, "[PROJECT_NAME]", white, 3);
        text(renderer_, 665, 43, "FRAME LAB  /  NTSC", accent, 2);
        label_value(112, "SCORE", grouped(game_.state().score), true);
        label_value(181, game_.config().mode == Mode::Challenge ? "LEVEL / TO GO" : "LEVEL / LINES",
            std::to_string(game_.state().level) + " / " +
            std::to_string(game_.config().mode == Mode::Challenge ?
                std::max(0, 25 - game_.state().lines) : game_.state().lines));
        label_value(250, "PIECES / FRAMES", std::to_string(game_.state().stats.pieces) + " / " + grouped(game_.state().frame));
        std::ostringstream rate;
        rate << std::fixed << std::setprecision(1) << game_.four_line_rate() * 100.0 << "%";
        label_value(319, "FOUR-LINE RATE", rate.str());
        label_value(388, "I DROUGHT", std::to_string(game_.state().stats.current_i_drought)
            + " / " + std::to_string(game_.state().stats.max_i_drought));
        const auto& clears = game_.state().stats.clear_counts;
        label_value(457, "CLEARS  1  2  3  4", std::to_string(clears[0]) + " / " +
            std::to_string(clears[1]) + " / " + std::to_string(clears[2]) + " / " + std::to_string(clears[3]));
        label_value(526, "SCORE / LINE", game_.state().lines ?
            grouped(game_.state().score / game_.state().lines) : "0");
        next_piece();
        fill(renderer_, 664, 250, 260, 352, panel);
        outline(renderer_, 664, 250, 260, 352, panel_edge);
        text(renderer_, 682, 268, "SESSION", accent);
        text(renderer_, 682, 303, game_.config().mode == Mode::Challenge ? "25-LINE CHALLENGE" : "ENDLESS", white);
        text(renderer_, 682, 329, ruleset_name(game_.config().ruleset), muted, 1);
        text(renderer_, 682, 363, "SEED  " + std::to_string(game_.config().seed), white);
        std::ostringstream speed;
        speed << "SPEED " << std::fixed << std::setprecision(2) << speeds_[speed_index_] << "X";
        text(renderer_, 682, 391, speed.str(), white);
        text(renderer_, 682, 437, "P  PAUSE   R  RESTART", muted, 1);
        text(renderer_, 682, 456, "F1 DEBUG  TAB STATS", muted, 1);
        text(renderer_, 682, 475, "[ ] SPEED   . STEP", muted, 1);
        text(renderer_, 682, 494, "F5 SAVE REPLAY", muted, 1);
        text(renderer_, 682, 513, controller_mode_ ? "ESC QUIT" : "ESC MENU", muted, 1);
        text(renderer_, 682, 551, controller_mode_ ? controller_label_ : replay_mode_ ?
            (paused_ ? "REPLAY PAUSED - P / ." : "REPLAY PLAYBACK") : "LIVE CONTROLLER", accent, 1);
        if (controller_mode_ && paused_)
            text(renderer_, 682, 573, controller_finished_ ? "FINISHED - R RESTART" : "PAUSED - P / .", accent, 1);
        text(renderer_, 45, 647, "ONE FRAME = ONE TICK   /   RENDER SPEED DOES NOT CHANGE PHYSICS", muted, 1);
    }
    void overlay(const std::string& title, const std::string& subtitle) const {
        fill(renderer_, 340, 251, 278, 174, {7, 12, 20, 230});
        outline(renderer_, 340, 251, 278, 174, accent);
        text(renderer_, 366, 281, title, white, 2);
        text(renderer_, 366, 334, subtitle, muted, 1);
        text(renderer_, 366, 366, "R RESTART  ESC MENU", accent, 1);
    }
    void render_menu() const {
        text(renderer_, 82, 101, "[PROJECT_NAME]", white, 4);
        text(renderer_, 85, 158, "A DETERMINISTIC FALLING-BLOCK LAB", accent, 2);
        fill(renderer_, 81, 212, 800, 340, panel);
        outline(renderer_, 81, 212, 800, 340, panel_edge);
        text(renderer_, 113, 247, "START LEVEL", muted);
        text(renderer_, 392, 247, std::to_string(settings_.start_level), white, 3);
        text(renderer_, 113, 307, "MODE", muted);
        text(renderer_, 392, 307, settings_.mode ? "25-LINE CHALLENGE" : "ENDLESS", white);
        text(renderer_, 113, 357, "HEIGHT", muted);
        text(renderer_, 392, 357, std::to_string(settings_.height), white);
        text(renderer_, 113, 407, "RULES", muted);
        text(renderer_, 392, 407, settings_.ruleset ? "EXTENDED" : "STRICT", white);
        text(renderer_, 113, 457, "SEED (E EDIT)", muted);
        text(renderer_, 392, 457, editing_seed_ ? seed_buffer_ + "_" : std::to_string(settings_.seed), white);
        text(renderer_, 113, 508, "ENTER START", accent);
        text(renderer_, 84, 580, "UP/DOWN LEVEL   LEFT/RIGHT HEIGHT   M MODE   T RULES   S SEED", muted, 1);
        text(renderer_, 84, 601, "F2 CONTROLS  F6 LAST  F7 REPLAYS  H CONTRAST  N NEXT", muted, 1);
        text(renderer_, 84, 631, "ARROWS MOVE   X/Z ROTATE   P PAUSE   F11 FULLSCREEN", white, 1);
    }
    void render_settings() const {
        text(renderer_, 80, 70, "CONTROLS & ACCESSIBILITY", white, 3);
        text(renderer_, 80, 111, "UP/DOWN SELECT   ENTER REBIND   ESC BACK", muted, 1);
        fill(renderer_, 80, 151, 800, 470, panel);
        outline(renderer_, 80, 151, 800, 470, panel_edge);
        for (int i = 0; i < 10; ++i) {
            const int y = 177 + i * 38;
            if (i == settings_index_) fill(renderer_, 93, y - 5, 775, 31, {42, 71, 82});
            const auto label = std::string(i < 5 ? "KEY " : "PAD ") + action_names[i % 5];
            std::string binding;
            if (i < 5) binding = SDL_GetScancodeName(settings_.keys[i]);
            else binding = SDL_GetGamepadStringForButton(settings_.pad[i % 5]);
            text(renderer_, 110, y, label, i == settings_index_ ? accent : muted, 2);
            text(renderer_, 490, y, binding, white, 2);
        }
        text(renderer_, 82, 649, capturing_ ? "PRESS A KEY OR GAMEPAD BUTTON" :
            "LEFT/RIGHT VOLUME: " + std::to_string(static_cast<int>(settings_.volume * 100)) +
            "%   F3 VSYNC   F4 INTEGER SCALE   H CONTRAST", accent, 1);
    }
    void render_replay_browser() const {
        text(renderer_, 80, 70, "REPLAY LIBRARY", white, 3);
        text(renderer_, 80, 110, "UP/DOWN SELECT   ENTER PLAY   ESC BACK", muted, 1);
        fill(renderer_, 80, 150, 800, 470, panel);
        outline(renderer_, 80, 150, 800, 470, panel_edge);
        if (replay_files_.empty()) text(renderer_, 105, 190, "NO REPLAYS SAVED YET", muted, 2);
        const std::size_t page_start = (replay_selected_ / 11) * 11;
        for (std::size_t i = page_start; i < replay_files_.size() && i < page_start + 11; ++i) {
            const int y = 178 + static_cast<int>(i - page_start) * 37;
            if (i == replay_selected_) fill(renderer_, 94, y - 6, 772, 32, {42, 71, 82});
            auto name = replay_files_[i].filename().string();
            if (name.size() > 42) name.resize(42);
            text(renderer_, 109, y, name, i == replay_selected_ ? accent : white, 2);
        }
        if (!replay_browser_error_.empty()) text(renderer_, 95, 646, replay_browser_error_, {238, 116, 145}, 1);
        else text(renderer_, 95, 646, std::to_string(replay_files_.size()) + " REPLAY FILES", muted, 1);
    }
    void render_debug() const {
        fill(renderer_, 655, 611, 280, 90, {3, 7, 10, 220});
        const auto& s = game_.state();
        text(renderer_, 663, 617, "F " + std::to_string(s.frame) + " PH " + std::to_string(static_cast<int>(s.phase)), white, 1);
        text(renderer_, 663, 634, "G " + std::to_string(s.gravity_counter) + "/" + std::to_string(gravity_period(s.level))
            + " DAS " + std::to_string(s.das_direction) + "/" + std::to_string(s.das_counter), white, 1);
        text(renderer_, 663, 651, "ARE " + std::to_string(s.entry_remaining) + " CLEAR " + std::to_string(s.clear_remaining)
            + " RNG " + std::to_string(s.rng_state), white, 1);
        std::ostringstream hash; hash << std::hex << game_.state_hash();
        text(renderer_, 663, 668, "HASH " + hash.str(), accent, 1);
    }
    void render_stats() const {
        fill(renderer_, 335, 85, 290, 530, {6, 13, 21, 242});
        outline(renderer_, 335, 85, 290, 530, accent);
        const auto& s = game_.state();
        text(renderer_, 355, 106, game_.terminal() ? "FINAL STATS" : "LIVE STATS", white, 2);
        text(renderer_, 355, 146, "SCORE " + grouped(s.score), accent, 2);
        text(renderer_, 355, 178, "LINES " + std::to_string(s.lines) +
            "  PIECES " + std::to_string(s.stats.pieces), white, 1);
        text(renderer_, 355, 200, "FRAMES " + grouped(s.frame), muted, 1);
        text(renderer_, 355, 220, "SECONDS " + std::to_string(static_cast<int>(s.frame / ntsc_frames_per_second)), muted, 1);
        text(renderer_, 355, 251, "CLEAR COUNTS", accent, 1);
        text(renderer_, 355, 273, "SINGLE " + std::to_string(s.stats.clear_counts[0]) +
            "  DOUBLE " + std::to_string(s.stats.clear_counts[1]), white, 1);
        text(renderer_, 355, 292, "TRIPLE " + std::to_string(s.stats.clear_counts[2]) +
            "  FOUR " + std::to_string(s.stats.clear_counts[3]), white, 1);
        std::ostringstream rate;
        rate << std::fixed << std::setprecision(1) << game_.four_line_rate() * 100.0;
        text(renderer_, 355, 316, "FOUR-LINE RATE " + rate.str() + "%", white, 1);
        text(renderer_, 355, 345, "PIECE DISTRIBUTION", accent, 1);
        for (int i = 0; i < 7; ++i) {
            const int x = i < 4 ? 355 : 492;
            const int y = 370 + (i < 4 ? i : i - 4) * 26;
            text(renderer_, static_cast<float>(x), static_cast<float>(y),
                std::string(piece_name(static_cast<Piece>(i))) + " " +
                std::to_string(s.stats.piece_counts[i]), piece_color(i + 1), 2);
        }
        text(renderer_, 355, 487, "I DROUGHT " + std::to_string(s.stats.current_i_drought) +
            " / " + std::to_string(s.stats.max_i_drought), white, 1);
        text(renderer_, 355, 509, "TRANSITION " + grouped(s.stats.transition_score), white, 1);
        text(renderer_, 355, 534, game_.terminal() ? "R RESTART  ESC MENU" : "TAB CLOSE  P PAUSE", accent, 1);
    }
    void render() {
        background_art();
        if (screen_ == Screen::Menu) render_menu();
        else if (screen_ == Screen::Settings) render_settings();
        else if (screen_ == Screen::ReplayBrowser) render_replay_browser();
        else {
            board(); hud();
            if (stats_overlay_) render_stats();
            else if (paused_ && !replay_mode_ && !controller_mode_) overlay("PAUSED", "FRAME STEP READY");
            else if (game_.state().phase == Phase::GameOver) overlay("GAME OVER", "REPLAY SAVED TO CONFIG");
            else if (game_.state().phase == Phase::ChallengeComplete) overlay("CHALLENGE COMPLETE", "25 LINES CLEARED");
            if (debug_) render_debug();
        }
        if (!screenshot_path_.empty() &&
            (screen_ != Screen::Playing || game_.state().frame >= screenshot_frame_)) {
            SDL_Surface* capture = SDL_RenderReadPixels(renderer_, nullptr);
            if (!capture || !SDL_SaveBMP(capture, screenshot_path_.c_str())) {
                std::cerr << "screenshot failed: " << SDL_GetError() << '\n';
            }
            if (capture) SDL_DestroySurface(capture);
            screenshot_path_.clear();
        }
        SDL_RenderPresent(renderer_);
    }
};
}

int main(int argc, char** argv) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << '\n';
        return 1;
    }
    try {
        std::uint64_t smoke_frames = 0;
        std::string replay_path;
        std::string screenshot_path;
        std::uint64_t screenshot_frame = 0;
        bool keyboard_smoke = false;
        bool browser_smoke = false;
        bool replay_paused = false;
        bool controller = false;
        bool controller_smoke = false;
        bool controller_options = false;
        Config controller_config;
        std::uint64_t frame_limit = 60000;
        std::string controller_label = "LIVE AI";
        std::string speed = "1";
        const auto value_after = [&](int& index) -> std::string {
            if (++index >= argc) throw std::invalid_argument("missing option value");
            return argv[index];
        };
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--controller-smoke" || arg == "--controller-label" || arg == "--rules" ||
                arg == "--mode" || arg == "--level" || arg == "--height" || arg == "--seed" || arg == "--frames")
                controller_options = true;
            if (arg == "--replay" && i + 1 < argc) replay_path = argv[++i];
            else if (arg == "--paused") replay_paused = true;
            else if (arg == "--controller-stdio") controller = true;
            else if (arg == "--controller-smoke") controller_smoke = true;
            else if (arg == "--controller-label") controller_label = value_after(i);
            else if (arg == "--rules") controller_config.ruleset = ruleset_from_name(value_after(i));
            else if (arg == "--mode") {
                const auto mode = value_after(i);
                if (mode != "endless" && mode != "challenge") throw std::invalid_argument("invalid mode");
                controller_config.mode = mode == "endless" ? Mode::Endless : Mode::Challenge;
            }
            else if (arg == "--level") controller_config.start_level = static_cast<int>(unsigned_value(value_after(i), 19));
            else if (arg == "--height") controller_config.height = static_cast<int>(unsigned_value(value_after(i), 5));
            else if (arg == "--seed") controller_config.seed = static_cast<std::uint16_t>(unsigned_value(value_after(i), 65535));
            else if (arg == "--frames") frame_limit = unsigned_value(value_after(i), 1'000'000'000);
            else if (arg == "--speed") speed = value_after(i);
            else if (arg == "--smoke-frames" && i + 1 < argc) smoke_frames = std::stoull(argv[++i]);
            else if (arg == "--screenshot" && i + 1 < argc) screenshot_path = argv[++i];
            else if (arg == "--screenshot-frame" && i + 1 < argc) screenshot_frame = std::stoull(argv[++i]);
            else if (arg == "--keyboard-smoke") keyboard_smoke = true;
            else if (arg == "--browser-smoke") browser_smoke = true;
            else throw std::invalid_argument("unknown option: " + arg);
        }
        if (replay_paused && replay_path.empty() && !controller)
            throw std::invalid_argument("--paused requires --replay FILE or --controller-stdio");
        if (controller && (!replay_path.empty() || keyboard_smoke || browser_smoke))
            throw std::invalid_argument("live controller cannot be combined with replay or other smoke modes");
        if (controller_options && !controller)
            throw std::invalid_argument("controller configuration requires --controller-stdio");
        if (frame_limit == 0 || controller_label.empty() || controller_label.size() > 32)
            throw std::invalid_argument("positive frame limit and controller label of 1..32 characters required");
        int result = 0;
        {
            App app;
            app.set_speed(speed);
            if (controller) app.start_controller(controller_config, frame_limit, controller_label, replay_paused);
            if (!replay_path.empty()) app.load_replay(replay_path, replay_paused);
            if (!screenshot_path.empty()) app.set_screenshot(screenshot_path, screenshot_frame);
            if (keyboard_smoke) app.keyboard_smoke();
            if (browser_smoke) {
                SDL_Event event{};
                event.type = SDL_EVENT_KEY_DOWN;
                event.key.scancode = SDL_SCANCODE_F7;
                event.key.key = SDLK_F7;
                SDL_PushEvent(&event);
            }
            if (controller_smoke) app.controller_smoke();
            result = keyboard_smoke || controller_smoke ? 0 : app.run(smoke_frames);
        }
        SDL_Quit();
        return result;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        SDL_Quit();
        return 1;
    }
}
