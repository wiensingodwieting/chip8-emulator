#include "Chip8.hpp"

#include <algorithm>
#include <cstddef>
#include <fstream>

namespace {
constexpr std::uint16_t FontAddress = 0x50;
constexpr std::uint16_t ProgramAddress = 0x200;
constexpr std::uint32_t PixelOn = 0xFFFFFFFFu;
constexpr std::array<std::uint8_t, 80> Font = {
    0xF0,0x90,0x90,0x90,0xF0, 0x20,0x60,0x20,0x20,0x70,
    0xF0,0x10,0xF0,0x80,0xF0, 0xF0,0x10,0xF0,0x10,0xF0,
    0x90,0x90,0xF0,0x10,0x10, 0xF0,0x80,0xF0,0x10,0xF0,
    0xF0,0x80,0xF0,0x90,0xF0, 0xF0,0x10,0x20,0x40,0x40,
    0xF0,0x90,0xF0,0x90,0xF0, 0xF0,0x90,0xF0,0x10,0xF0,
    0xF0,0x90,0xF0,0x90,0x90, 0xE0,0x90,0xE0,0x90,0xE0,
    0xF0,0x80,0x80,0x80,0xF0, 0xE0,0x90,0x90,0x90,0xE0,
    0xF0,0x80,0xF0,0x80,0xF0, 0xF0,0x80,0xF0,0x80,0x80,
};
}

Chip8::Chip8() : rng(std::random_device{}()) {
    std::copy(Font.begin(), Font.end(), memory.begin() + FontAddress);
}

bool Chip8::load_rom(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return false;
    const auto size = input.tellg();
    if (size < 0 || size > static_cast<std::streamoff>(memory.size() - ProgramAddress)) return false;
    input.seekg(0);
    input.read(reinterpret_cast<char*>(memory.data() + ProgramAddress), size);
    return input.good() || input.eof();
}

void Chip8::tick_timers() {
    if (delay_timer) --delay_timer;
    if (sound_timer) --sound_timer;
}

const std::array<std::uint32_t, Chip8::VIDEO_WIDTH * Chip8::VIDEO_HEIGHT>& Chip8::framebuffer() const { return graphics; }
std::array<std::uint8_t, 16>& Chip8::keypad() { return keys; }
bool Chip8::poll_draw_flag() { const bool result = draw_requested; draw_requested = false; return result; }

void Chip8::cycle() {
    if (program_counter > memory.size() - 2) return;
    const auto opcode = static_cast<std::uint16_t>((memory[program_counter] << 8) | memory[program_counter + 1]);
    program_counter += 2;
    execute(opcode);
}

void Chip8::execute(const std::uint16_t opcode) {
    const auto x = static_cast<std::uint8_t>((opcode >> 8) & 0xF);
    const auto y = static_cast<std::uint8_t>((opcode >> 4) & 0xF);
    const auto n = static_cast<std::uint8_t>(opcode & 0xF);
    const auto nn = static_cast<std::uint8_t>(opcode & 0xFF);
    const auto nnn = static_cast<std::uint16_t>(opcode & 0xFFF);
    auto skip = [&] { if (program_counter <= memory.size() - 2) program_counter += 2; };

    switch (opcode & 0xF000) {
    case 0x0000: if (opcode == 0x00E0) { graphics.fill(0); draw_requested = true; } else if (opcode == 0x00EE && stack_pointer) program_counter = stack[--stack_pointer]; break;
    case 0x1000: program_counter = nnn; break;
    case 0x2000: if (stack_pointer < stack.size()) { stack[stack_pointer++] = program_counter; program_counter = nnn; } break;
    case 0x3000: if (registers[x] == nn) skip(); break;
    case 0x4000: if (registers[x] != nn) skip(); break;
    case 0x5000: if (n == 0 && registers[x] == registers[y]) skip(); break;
    case 0x6000: registers[x] = nn; break;
    case 0x7000: registers[x] = static_cast<std::uint8_t>(registers[x] + nn); break;
    case 0x8000: switch (n) {
        case 0: registers[x] = registers[y]; break; case 1: registers[x] |= registers[y]; break;
        case 2: registers[x] &= registers[y]; break; case 3: registers[x] ^= registers[y]; break;
        case 4: { const auto sum = static_cast<unsigned>(registers[x]) + registers[y]; registers[x] = sum; registers[0xF] = sum > 255; break; }
        case 5: registers[0xF] = registers[x] >= registers[y]; registers[x] -= registers[y]; break;
        case 6: registers[x] = registers[y]; registers[0xF] = registers[x] & 1; registers[x] >>= 1; break;
        case 7: registers[0xF] = registers[y] >= registers[x]; registers[x] = registers[y] - registers[x]; break;
        case 0xE: registers[x] = registers[y]; registers[0xF] = registers[x] >> 7; registers[x] <<= 1; break;
        default: break; } break;
    case 0x9000: if (n == 0 && registers[x] != registers[y]) skip(); break;
    case 0xA000: index_register = nnn; break;
    case 0xB000: program_counter = static_cast<std::uint16_t>(nnn + registers[0]); break;
    case 0xC000: registers[x] = static_cast<std::uint8_t>(random_byte(rng)) & nn; break;
    case 0xD000: {
        const auto px = registers[x] % VIDEO_WIDTH, py = registers[y] % VIDEO_HEIGHT; registers[0xF] = 0;
        for (unsigned row = 0; row < n && index_register + row < memory.size(); ++row) for (unsigned col = 0; col < 8; ++col)
            if ((memory[index_register + row] & (0x80 >> col)) && px + col < VIDEO_WIDTH && py + row < VIDEO_HEIGHT) {
                const auto pos = (py + row) * VIDEO_WIDTH + px + col; if (graphics[pos]) registers[0xF] = 1; graphics[pos] ^= PixelOn;
            }
        draw_requested = true; break; }
    case 0xE000: if (registers[x] < keys.size()) { if (nn == 0x9E && keys[registers[x]]) skip(); if (nn == 0xA1 && !keys[registers[x]]) skip(); } break;
    case 0xF000: switch (nn) {
        case 0x07: registers[x] = delay_timer; break;
        case 0x0A: { bool found = false; for (std::uint8_t key = 0; key < keys.size(); ++key) if (keys[key]) { registers[x] = key; found = true; break; } if (!found) program_counter -= 2; break; }
        case 0x15: delay_timer = registers[x]; break; case 0x18: sound_timer = registers[x]; break;
        case 0x1E: index_register += registers[x]; break; case 0x29: index_register = FontAddress + registers[x] * 5; break;
        case 0x33: if (static_cast<std::size_t>(index_register) + 2 < memory.size()) { memory[index_register] = registers[x] / 100; memory[index_register + 1] = registers[x] / 10 % 10; memory[index_register + 2] = registers[x] % 10; } break;
        case 0x55: for (unsigned i = 0; i <= x && index_register + i < memory.size(); ++i) memory[index_register + i] = registers[i]; break;
        case 0x65: for (unsigned i = 0; i <= x && index_register + i < memory.size(); ++i) registers[i] = memory[index_register + i]; break;
        default: break; } break;
    default: break;
    }
}
