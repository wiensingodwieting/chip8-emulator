#include "Chip8.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

bool write_rom(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(output);
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return {};
    const auto size = input.tellg();
    input.seekg(0);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    if (!data.empty()) input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return data;
}

void write_file(const std::filesystem::path& path, const std::vector<std::uint8_t>& data) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

Chip8 chip8_with_rom(const std::filesystem::path& rom_path, const std::vector<std::uint8_t>& rom) {
    Chip8 chip8;
    if (!write_rom(rom_path, rom)) {
        expect(false, "write test ROM " + rom_path.string());
        return chip8;
    }
    expect(chip8.load_rom(rom_path), "load test ROM " + rom_path.string());
    return chip8;
}

bool any_pixel_on(const Chip8& chip8) {
    for (const auto pixel : chip8.framebuffer()) {
        if (pixel != 0) return true;
    }
    return false;
}
}

int main() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::path{"chip8-test-work"};
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    // Draw the built-in '0' sprite at 0,0 and verify the framebuffer changes.
    {
        Chip8 chip8 = chip8_with_rom(dir / "draw.ch8", {0x00, 0xE0, 0x60, 0x00, 0x61, 0x00, 0xA0, 0x50, 0xD0, 0x15, 0x12, 0x0A});
        for (int i = 0; i < 10; ++i) chip8.cycle();
        expect(any_pixel_on(chip8), "Dxyn draws pixels");
    }

    // Setting the sound timer makes sound_active() true until it ticks down.
    {
        Chip8 chip8 = chip8_with_rom(dir / "sound.ch8", {0x60, 0x02, 0xF0, 0x18, 0x12, 0x04});
        chip8.cycle();
        chip8.cycle();
        expect(chip8.sound_active(), "Fx18 activates sound timer");
        chip8.tick_timers();
        expect(chip8.sound_active(), "sound timer still active after one tick");
        chip8.tick_timers();
        expect(!chip8.sound_active(), "sound timer expires after two ticks");
    }

    // Reset returns the emulator to a clean state and reloads the ROM.
    {
        Chip8 chip8 = chip8_with_rom(dir / "reset.ch8", {0x60, 0xFF, 0xF0, 0x18, 0x12, 0x04});
        chip8.cycle();
        chip8.cycle();
        expect(chip8.sound_active(), "sound active before reset");
        chip8.reset();
        expect(!chip8.sound_active(), "reset clears sound timer");
        expect(chip8.load_rom(dir / "reset.ch8"), "reload ROM after reset");
    }

    // Save/load roundtrip preserves the whole observable state byte-for-byte.
    {
        Chip8 chip8 = chip8_with_rom(dir / "state.ch8", {0x00, 0xE0, 0x60, 0x00, 0x61, 0x00, 0xA0, 0x50, 0xD0, 0x15, 0x12, 0x0A});
        for (int i = 0; i < 100; ++i) chip8.cycle();
        chip8.keypad()[0x5] = 1;
        const auto state_path = dir / "state.ch8.state";
        std::string error;
        expect(chip8.save_state(state_path, &error), "save state: " + error);
        const auto original = read_file(state_path);
        expect(!original.empty(), "saved state is not empty");

        for (int i = 0; i < 100; ++i) chip8.cycle();
        chip8.keypad().fill(0);
        expect(chip8.load_state(state_path, &error), "load state: " + error);
        expect(chip8.keypad()[0x5] == 1, "keypad restored from state");

        const auto roundtrip_path = dir / "roundtrip.state";
        expect(chip8.save_state(roundtrip_path, &error), "save after load: " + error);
        expect(read_file(roundtrip_path) == original, "state identical after save/load/save");

        // A state saved for one ROM must be rejected by a different ROM.
        Chip8 other = chip8_with_rom(dir / "other.ch8", {0x60, 0x01, 0x12, 0x00});
        std::string wrong_rom_error;
        expect(!other.load_state(state_path, &wrong_rom_error), "state for another ROM is rejected");
        expect(!wrong_rom_error.empty(), "wrong-ROM state has an error message");

        // Missing/corrupt/truncated/bad-magic states fail cleanly and do not mutate state.
        const auto before_bad = read_file(roundtrip_path);
        std::string missing_error;
        expect(!chip8.load_state(dir / "missing.state", &missing_error), "missing state is rejected");
        expect(!missing_error.empty(), "missing state has an error message");

        auto corrupt = original;
        corrupt[corrupt.size() / 2] ^= 0xFF;
        const auto corrupt_path = dir / "corrupt.state";
        write_file(corrupt_path, corrupt);
        std::string corrupt_error;
        expect(!chip8.load_state(corrupt_path, &corrupt_error), "corrupt state is rejected");
        expect(!corrupt_error.empty(), "corrupt state has an error message");

        auto truncated = original;
        truncated.resize(truncated.size() / 2);
        const auto truncated_path = dir / "truncated.state";
        write_file(truncated_path, truncated);
        std::string truncated_error;
        expect(!chip8.load_state(truncated_path, &truncated_error), "truncated state is rejected");
        expect(!truncated_error.empty(), "truncated state has an error message");

        auto bad_magic = original;
        bad_magic[0] = 'X';
        const auto bad_magic_path = dir / "bad-magic.state";
        write_file(bad_magic_path, bad_magic);
        std::string magic_error;
        expect(!chip8.load_state(bad_magic_path, &magic_error), "bad-magic state is rejected");
        expect(!magic_error.empty(), "bad-magic state has an error message");

        const auto after_bad_path = dir / "after-bad.state";
        expect(chip8.save_state(after_bad_path, &error), "save after failed loads: " + error);
        expect(read_file(after_bad_path) == before_bad, "failed loads do not mutate state");
    }

    fs::remove_all(dir, ec);
    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "all chip8 tests passed\n";
    return 0;
}
