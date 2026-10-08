#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <random>

class Chip8 {
public:
    static constexpr unsigned VIDEO_WIDTH = 64;
    static constexpr unsigned VIDEO_HEIGHT = 32;

    Chip8();
    bool load_rom(const std::filesystem::path& path);
    void cycle();
    void tick_timers();

    [[nodiscard]] const std::array<std::uint32_t, VIDEO_WIDTH * VIDEO_HEIGHT>& framebuffer() const;
    [[nodiscard]] std::array<std::uint8_t, 16>& keypad();
    bool poll_draw_flag();

private:
    std::array<std::uint8_t, 4096> memory{};
    std::array<std::uint8_t, 16> registers{};
    std::uint16_t index_register{};
    std::uint16_t program_counter{0x200};
    std::uint8_t stack_pointer{};
    std::array<std::uint16_t, 16> stack{};
    std::uint8_t delay_timer{};
    std::uint8_t sound_timer{};
    std::array<std::uint32_t, VIDEO_WIDTH * VIDEO_HEIGHT> graphics{};
    std::array<std::uint8_t, 16> keys{};
    bool draw_requested{true};
    std::mt19937 rng;
    std::uniform_int_distribution<unsigned> random_byte{0, 255};

    void execute(std::uint16_t opcode);
};
