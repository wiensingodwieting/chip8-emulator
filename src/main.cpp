#include "Chip8.hpp"

#include <SDL.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
const std::unordered_map<SDL_Keycode, std::uint8_t> KeyMap = {
    {SDLK_1,1},{SDLK_2,2},{SDLK_3,3},{SDLK_4,0xC}, {SDLK_q,4},{SDLK_w,5},{SDLK_e,6},{SDLK_r,0xD},
    {SDLK_a,7},{SDLK_s,8},{SDLK_d,9},{SDLK_f,0xE}, {SDLK_z,0xA},{SDLK_x,0},{SDLK_c,0xB},{SDLK_v,0xF},
};

struct Palette {
    const char* name;
    std::uint32_t background;
    std::uint32_t foreground;
};

constexpr std::array<Palette, 4> Palettes = {{
    {"Classic",   0x000000FFu, 0xFFFFFFFFu},
    {"Matrix",    0x001A08FFu, 0x00FF41FFu},
    {"Game Boy",  0x0F380FFFu, 0x9BBC0FFFu},
    {"Cyberpunk", 0x10002BFFu, 0xFF2BD6FFu},
}};

std::array<std::int16_t, 735> make_tone(double& phase) {
    std::array<std::int16_t, 735> samples{};
    constexpr double PhaseStep = 440.0 / 44100.0;
    for (auto& sample : samples) {
        sample = phase < 0.5 ? 7000 : -7000;
        phase += PhaseStep;
        if (phase >= 1.0) phase -= 1.0;
    }
    return samples;
}

std::array<std::uint8_t, 5> glyph_rows(char ch) {
    const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    switch (c) {
    case 'A': return {0b010, 0b101, 0b111, 0b101, 0b101};
    case 'B': return {0b110, 0b101, 0b110, 0b101, 0b110};
    case 'C': return {0b011, 0b100, 0b100, 0b100, 0b011};
    case 'D': return {0b110, 0b101, 0b101, 0b101, 0b110};
    case 'E': return {0b111, 0b100, 0b110, 0b100, 0b111};
    case 'F': return {0b111, 0b100, 0b110, 0b100, 0b100};
    case 'G': return {0b011, 0b100, 0b101, 0b101, 0b011};
    case 'H': return {0b101, 0b101, 0b111, 0b101, 0b101};
    case 'I': return {0b111, 0b010, 0b010, 0b010, 0b111};
    case 'J': return {0b001, 0b001, 0b001, 0b101, 0b010};
    case 'K': return {0b101, 0b101, 0b110, 0b101, 0b101};
    case 'L': return {0b100, 0b100, 0b100, 0b100, 0b111};
    case 'M': return {0b101, 0b111, 0b111, 0b101, 0b101};
    case 'N': return {0b101, 0b111, 0b111, 0b101, 0b101};
    case 'O': return {0b010, 0b101, 0b101, 0b101, 0b010};
    case 'P': return {0b110, 0b101, 0b110, 0b100, 0b100};
    case 'Q': return {0b010, 0b101, 0b101, 0b010, 0b001};
    case 'R': return {0b110, 0b101, 0b110, 0b101, 0b101};
    case 'S': return {0b011, 0b100, 0b010, 0b001, 0b110};
    case 'T': return {0b111, 0b010, 0b010, 0b010, 0b010};
    case 'U': return {0b101, 0b101, 0b101, 0b101, 0b111};
    case 'V': return {0b101, 0b101, 0b101, 0b101, 0b010};
    case 'W': return {0b101, 0b101, 0b111, 0b111, 0b101};
    case 'X': return {0b101, 0b101, 0b010, 0b101, 0b101};
    case 'Y': return {0b101, 0b101, 0b010, 0b010, 0b010};
    case 'Z': return {0b111, 0b001, 0b010, 0b100, 0b111};
    case '0': return {0b111, 0b101, 0b101, 0b101, 0b111};
    case '1': return {0b010, 0b110, 0b010, 0b010, 0b111};
    case '2': return {0b110, 0b001, 0b010, 0b100, 0b111};
    case '3': return {0b110, 0b001, 0b010, 0b001, 0b110};
    case '4': return {0b101, 0b101, 0b111, 0b001, 0b001};
    case '5': return {0b111, 0b100, 0b110, 0b001, 0b110};
    case '6': return {0b011, 0b100, 0b110, 0b101, 0b010};
    case '7': return {0b111, 0b001, 0b001, 0b010, 0b010};
    case '8': return {0b010, 0b101, 0b010, 0b101, 0b010};
    case '9': return {0b010, 0b101, 0b011, 0b001, 0b110};
    case ':': return {0b000, 0b010, 0b000, 0b010, 0b000};
    case '.': return {0b000, 0b000, 0b000, 0b000, 0b010};
    case '/': return {0b001, 0b001, 0b010, 0b100, 0b100};
    case '-': return {0b000, 0b000, 0b111, 0b000, 0b000};
    case '!': return {0b010, 0b010, 0b010, 0b000, 0b010};
    case '?': return {0b110, 0b001, 0b010, 0b000, 0b010};
    case '%': return {0b101, 0b001, 0b010, 0b100, 0b101};
    case '+': return {0b000, 0b010, 0b111, 0b010, 0b000};
    case '=': return {0b000, 0b111, 0b000, 0b111, 0b000};
    case '(': return {0b001, 0b010, 0b010, 0b010, 0b001};
    case ')': return {0b100, 0b010, 0b010, 0b010, 0b100};
    case '_': return {0b000, 0b000, 0b000, 0b000, 0b111};
    default: return {0b000, 0b000, 0b000, 0b000, 0b000};
    }
}

void draw_osd_text(SDL_Renderer* renderer, const std::string& text) {
    constexpr int scale = 3;
    constexpr int x = 8;
    constexpr int y = 8;
    const int text_width = static_cast<int>(text.size()) * 4 * scale - scale;
    const int text_height = 5 * scale;
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    const SDL_Rect background{x - 4, y - 4, text_width + 8, text_height + 8};
    SDL_RenderFillRect(renderer, &background);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto rows = glyph_rows(text[i]);
        const int glyph_x = x + static_cast<int>(i) * 4 * scale;
        for (int row = 0; row < 5; ++row) {
            for (int col = 0; col < 3; ++col) {
                if ((rows[static_cast<std::size_t>(row)] >> (2 - col)) & 1) {
                    const SDL_Rect pixel{glyph_x + col * scale, y + row * scale, scale, scale};
                    SDL_RenderFillRect(renderer, &pixel);
                }
            }
        }
    }
}

std::string lower_copy(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool quirks_for_profile(const std::string& raw_name, Quirks& quirks, std::string* profile_name = nullptr) {
    const std::string name = lower_copy(raw_name);
    if (name == "modern") {
        quirks = Quirks{};
        if (profile_name) *profile_name = "modern";
        return true;
    }
    if (name == "original") {
        quirks = Quirks{};
        quirks.shift_uses_vy = false;
        quirks.memory_increments_i = true;
        quirks.logic_resets_vf = true;
        quirks.draw_wraps = false;
        quirks.add_i_sets_vf = false;
        if (profile_name) *profile_name = "original";
        return true;
    }
    if (name == "superchip" || name == "super-chip" || name == "schip") {
        quirks = Quirks{};
        quirks.shift_uses_vy = true;
        quirks.memory_increments_i = false;
        quirks.logic_resets_vf = false;
        quirks.draw_wraps = true;
        quirks.add_i_sets_vf = true;
        if (profile_name) *profile_name = "superchip";
        return true;
    }
    return false;
}

std::filesystem::path state_path_for(const std::string& rom_path) {
    return std::filesystem::path{rom_path}.concat(".state");
}

void print_usage(const char* program) {
    std::cout << "Usage: " << program << " [options] <path-to-rom>\n"
              << "Options:\n"
              << "  --profile modern|original|superchip  CHIP-8 quirks profile (default: modern)\n"
              << "  --scale N                            Integer window scale (default: 10)\n"
              << "  --fullscreen                         Start in fullscreen desktop mode\n"
              << "  --help                               Show this help\n";
}

struct Options {
    std::string rom;
    std::string profile = "modern";
    int scale = 10;
    bool fullscreen = false;
};

bool parse_options(int argc, char* argv[], Options& options, std::string& error) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--profile") {
            if (i + 1 >= argc) { error = "--profile requires a value"; return false; }
            options.profile = argv[++i];
        } else if (arg.rfind("--profile=", 0) == 0) {
            options.profile = arg.substr(10);
        } else if (arg == "--scale") {
            if (i + 1 >= argc) { error = "--scale requires a value"; return false; }
            try {
                std::size_t used = 0;
                options.scale = std::stoi(argv[++i], &used);
                if (used == 0) throw std::invalid_argument("empty");
            } catch (...) {
                error = "--scale must be an integer from 1 to 32";
                return false;
            }
            if (options.scale < 1 || options.scale > 32) { error = "--scale must be an integer from 1 to 32"; return false; }
        } else if (arg.rfind("--scale=", 0) == 0) {
            try {
                std::size_t used = 0;
                options.scale = std::stoi(arg.substr(8), &used);
                if (used == 0) throw std::invalid_argument("empty");
            } catch (...) {
                error = "--scale must be an integer from 1 to 32";
                return false;
            }
            if (options.scale < 1 || options.scale > 32) { error = "--scale must be an integer from 1 to 32"; return false; }
        } else if (arg == "--fullscreen") {
            options.fullscreen = true;
        } else if (arg == "--") {
            if (i + 1 >= argc) { error = "missing ROM path"; return false; }
            if (!options.rom.empty()) { error = "multiple ROM paths given"; return false; }
            options.rom = argv[++i];
        } else if (arg.rfind("--", 0) == 0) {
            error = "unknown option: " + arg;
            return false;
        } else {
            if (!options.rom.empty()) { error = "multiple ROM paths given"; return false; }
            options.rom = arg;
        }
    }
    if (options.rom.empty()) { error = "missing ROM path"; return false; }
    return true;
}
}

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (std::string{argv[i]} == "--help" || std::string{argv[i]} == "-h") {
            print_usage(argv[0]);
            return 0;
        }
    }

    Options options;
    std::string option_error;
    if (!parse_options(argc, argv, options, option_error)) {
        std::cerr << option_error << '\n';
        print_usage(argv[0]);
        return 1;
    }

    std::string profile_name;
    Quirks quirks;
    if (!quirks_for_profile(options.profile, quirks, &profile_name)) {
        std::cerr << "Unknown profile: " << options.profile << " (use modern, original, or superchip)\n";
        return 1;
    }

    Chip8 chip8;
    chip8.set_quirks(quirks);
    std::string rom_path = options.rom;
    std::filesystem::path state_path = state_path_for(rom_path);
    if (!chip8.load_rom(rom_path)) { std::cerr << "Could not load ROM: " << rom_path << '\n'; return 1; }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) { std::cerr << "SDL: " << SDL_GetError() << '\n'; return 1; }
    const bool audio_initialized = SDL_InitSubSystem(SDL_INIT_AUDIO) == 0;
    if (!audio_initialized) std::cerr << "Audio disabled: " << SDL_GetError() << '\n';

    const int window_width = Chip8::VIDEO_WIDTH * options.scale;
    const int window_height = Chip8::VIDEO_HEIGHT * options.scale;
    const Uint32 window_flags = options.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_WINDOW_SHOWN;
    SDL_Window* window = SDL_CreateWindow("CHIP-8 Emulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, window_width, window_height, window_flags);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED) : nullptr;
    SDL_Texture* texture = renderer ? SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, Chip8::VIDEO_WIDTH, Chip8::VIDEO_HEIGHT) : nullptr;
    if (!texture) { std::cerr << "SDL: " << SDL_GetError() << '\n'; if (renderer) SDL_DestroyRenderer(renderer); if (window) SDL_DestroyWindow(window); SDL_Quit(); return 1; }

    SDL_AudioDeviceID audio_device = 0;
    if (audio_initialized) {
        SDL_AudioSpec desired{};
        desired.freq = 44100;
        desired.format = AUDIO_S16SYS;
        desired.channels = 1;
        desired.samples = 735;
        audio_device = SDL_OpenAudioDevice(nullptr, 0, &desired, nullptr, 0);
        if (audio_device == 0) std::cerr << "Audio disabled: " << SDL_GetError() << '\n';
        else SDL_PauseAudioDevice(audio_device, 0);
    }
    double tone_phase = 0.0;

    using clock = std::chrono::steady_clock;
    constexpr auto TimerInterval = std::chrono::microseconds(16666);
    int cpu_hz = 700;
    auto cpu_interval = std::chrono::microseconds(1000000 / cpu_hz);
    auto last_cpu = clock::now(), last_timer = last_cpu;
    std::size_t palette_index = 0;
    bool running = true;
    bool paused = false;
    bool muted = false;
    bool redraw = true;
    SDL_Event event{};
    std::array<std::uint32_t, Chip8::VIDEO_WIDTH * Chip8::VIDEO_HEIGHT> display_pixels{};
    std::string osd_text;
    auto osd_until = clock::now();
    auto show_osd = [&](std::string text, std::chrono::milliseconds ttl = std::chrono::milliseconds(1800)) {
        osd_text = std::move(text);
        osd_until = clock::now() + ttl;
    };
    auto reset_current_rom = [&]() {
        chip8.reset();
        if (chip8.load_rom(rom_path)) {
            redraw = true;
            show_osd("RESET");
            return true;
        }
        std::cerr << "Could not reload ROM: " << rom_path << '\n';
        show_osd("RESET ERROR", std::chrono::milliseconds(2500));
        return false;
    };
    const std::vector<std::string> profiles = {"modern", "original", "superchip"};
    auto profile_index = static_cast<std::size_t>(std::find(profiles.begin(), profiles.end(), profile_name) - profiles.begin());

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_DROPFILE && event.drop.file) {
                const std::string dropped = event.drop.file;
                SDL_free(event.drop.file);
                chip8.reset();
                if (chip8.load_rom(dropped)) {
                    rom_path = dropped;
                    state_path = state_path_for(rom_path);
                    redraw = true;
                    show_osd("ROM LOADED");
                    SDL_SetWindowTitle(window, (std::string{"CHIP-8 Emulator - "} + std::filesystem::path{rom_path}.filename().string()).c_str());
                } else {
                    std::cerr << "Could not load dropped ROM: " << dropped << '\n';
                    show_osd("ROM LOAD ERROR", std::chrono::milliseconds(2500));
                }
            }
            if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
                if (event.key.keysym.sym == SDLK_ESCAPE) running = false;
                if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                    if (event.key.keysym.sym >= SDLK_F1 && event.key.keysym.sym <= SDLK_F4) {
                        palette_index = static_cast<std::size_t>(event.key.keysym.sym - SDLK_F1);
                        redraw = true;
                        SDL_SetWindowTitle(window, (std::string{"CHIP-8 Emulator - "} + Palettes[palette_index].name).c_str());
                        show_osd(std::string{"PALETTE "} + Palettes[palette_index].name);
                    }
                    if (event.key.keysym.sym == SDLK_F5) {
                        std::string state_error;
                        if (chip8.save_state(state_path, &state_error)) {
                            std::cout << "Saved state to " << state_path << '\n';
                            show_osd("SAVED");
                        } else {
                            std::cerr << "Could not save state to " << state_path << ": " << state_error << '\n';
                            show_osd("SAVE ERROR", std::chrono::milliseconds(2500));
                        }
                    }
                    if (event.key.keysym.sym == SDLK_F6) {
                        profile_index = (profile_index + 1) % profiles.size();
                        quirks_for_profile(profiles[profile_index], quirks, &profile_name);
                        chip8.set_quirks(quirks);
                        show_osd(std::string{"PROFILE "} + profile_name);
                    }
                    if (event.key.keysym.sym == SDLK_F8) {
                        std::string state_error;
                        if (chip8.load_state(state_path, &state_error)) {
                            redraw = true;
                            std::cout << "Loaded state from " << state_path << '\n';
                            show_osd("LOADED");
                        } else {
                            std::cerr << "Could not load state from " << state_path << ": " << state_error << '\n';
                            show_osd("LOAD ERROR", std::chrono::milliseconds(2500));
                        }
                    }
                    if (event.key.keysym.sym == SDLK_SPACE) {
                        paused = !paused;
                        show_osd(paused ? "PAUSED" : "RESUMED");
                    }
                    if (event.key.keysym.sym == SDLK_BACKSPACE) reset_current_rom();
                    if (event.key.keysym.sym == SDLK_n && paused) {
                        chip8.cycle();
                        redraw = true;
                        show_osd("STEP", std::chrono::milliseconds(700));
                    }
                    if (event.key.keysym.sym == SDLK_m) {
                        muted = !muted;
                        show_osd(muted ? "MUTED" : "SOUND ON");
                        if (muted && audio_device != 0) SDL_ClearQueuedAudio(audio_device);
                    }
                    if (event.key.keysym.sym == SDLK_PAGEUP) {
                        cpu_hz = std::min(2000, cpu_hz + 50);
                        cpu_interval = std::chrono::microseconds(1000000 / cpu_hz);
                        show_osd("CPU " + std::to_string(cpu_hz) + " HZ", std::chrono::milliseconds(1200));
                    }
                    if (event.key.keysym.sym == SDLK_PAGEDOWN) {
                        cpu_hz = std::max(60, cpu_hz - 50);
                        cpu_interval = std::chrono::microseconds(1000000 / cpu_hz);
                        show_osd("CPU " + std::to_string(cpu_hz) + " HZ", std::chrono::milliseconds(1200));
                    }
                }
                if (const auto it = KeyMap.find(event.key.keysym.sym); it != KeyMap.end()) chip8.keypad()[it->second] = event.type == SDL_KEYDOWN;
            }
        }
        const auto now = clock::now();
        if (paused) {
            last_cpu = now;
            last_timer = now;
        } else {
            while (now - last_cpu >= cpu_interval) { last_cpu += cpu_interval; chip8.cycle(); }
            while (now - last_timer >= TimerInterval) {
                last_timer += TimerInterval;
                if (!muted && chip8.sound_active() && audio_device != 0) {
                    const auto tone = make_tone(tone_phase);
                    SDL_QueueAudio(audio_device, tone.data(), static_cast<std::uint32_t>(tone.size() * sizeof(tone[0])));
                }
                chip8.tick_timers();
            }
        }
        if ((muted || !chip8.sound_active()) && audio_device != 0) SDL_ClearQueuedAudio(audio_device);
        const bool osd_visible = !osd_text.empty() && now < osd_until;
        if (chip8.poll_draw_flag() || redraw || osd_visible) {
            const auto& framebuffer = chip8.framebuffer();
            const auto& palette = Palettes[palette_index];
            for (std::size_t i = 0; i < framebuffer.size(); ++i) display_pixels[i] = framebuffer[i] ? palette.foreground : palette.background;
            SDL_UpdateTexture(texture, nullptr, display_pixels.data(), Chip8::VIDEO_WIDTH * sizeof(std::uint32_t));
            SDL_RenderClear(renderer); SDL_RenderCopy(renderer, texture, nullptr, nullptr);
            if (osd_visible) draw_osd_text(renderer, osd_text);
            SDL_RenderPresent(renderer);
            redraw = false;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    if (audio_device != 0) SDL_CloseAudioDevice(audio_device);
    SDL_DestroyTexture(texture); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
}
