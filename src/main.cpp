#include "Chip8.hpp"

#include <SDL.h>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>
#include <unordered_map>

namespace {
const std::unordered_map<SDL_Keycode, std::uint8_t> KeyMap = {
    {SDLK_1,1},{SDLK_2,2},{SDLK_3,3},{SDLK_4,0xC}, {SDLK_q,4},{SDLK_w,5},{SDLK_e,6},{SDLK_r,0xD},
    {SDLK_a,7},{SDLK_s,8},{SDLK_d,9},{SDLK_f,0xE}, {SDLK_z,0xA},{SDLK_x,0},{SDLK_c,0xB},{SDLK_v,0xF},
};
}

int main(int argc, char* argv[]) {
    if (argc != 2) { std::cerr << "Usage: " << argv[0] << " <path-to-rom>\n"; return 1; }
    Chip8 chip8;
    if (!chip8.load_rom(argv[1])) { std::cerr << "Could not load ROM: " << argv[1] << '\n'; return 1; }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) { std::cerr << "SDL: " << SDL_GetError() << '\n'; return 1; }
    SDL_Window* window = SDL_CreateWindow("CHIP-8 Emulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 320, SDL_WINDOW_SHOWN);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED) : nullptr;
    SDL_Texture* texture = renderer ? SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, Chip8::VIDEO_WIDTH, Chip8::VIDEO_HEIGHT) : nullptr;
    if (!texture) { std::cerr << "SDL: " << SDL_GetError() << '\n'; if (renderer) SDL_DestroyRenderer(renderer); if (window) SDL_DestroyWindow(window); SDL_Quit(); return 1; }

    using clock = std::chrono::steady_clock;
    constexpr auto CpuInterval = std::chrono::microseconds(1428);
    constexpr auto TimerInterval = std::chrono::microseconds(16666);
    auto last_cpu = clock::now(), last_timer = last_cpu;
    bool running = true; SDL_Event event{};
    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
                if (event.key.keysym.sym == SDLK_ESCAPE) running = false;
                if (const auto it = KeyMap.find(event.key.keysym.sym); it != KeyMap.end()) chip8.keypad()[it->second] = event.type == SDL_KEYDOWN;
            }
        }
        const auto now = clock::now();
        while (now - last_cpu >= CpuInterval) { last_cpu += CpuInterval; chip8.cycle(); }
        while (now - last_timer >= TimerInterval) { last_timer += TimerInterval; chip8.tick_timers(); }
        if (chip8.poll_draw_flag()) { SDL_UpdateTexture(texture, nullptr, chip8.framebuffer().data(), Chip8::VIDEO_WIDTH * sizeof(std::uint32_t)); SDL_RenderClear(renderer); SDL_RenderCopy(renderer, texture, nullptr, nullptr); SDL_RenderPresent(renderer); }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    SDL_DestroyTexture(texture); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
}
