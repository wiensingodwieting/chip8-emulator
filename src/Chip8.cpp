#include "Chip8.hpp"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
constexpr std::uint16_t FontAddress = 0x50;
constexpr std::uint16_t ProgramAddress = 0x200;
constexpr std::uint32_t PixelOn = 0xFFFFFFFFu;
constexpr std::array<std::uint8_t, 8> StateMagic = {'C', '8', 'S', 'T', 'A', 'T', 'E', '\0'};
constexpr std::uint32_t StateVersion = 2;
constexpr std::size_t StateHeaderSize = StateMagic.size() + 3 * sizeof(std::uint32_t);
constexpr std::size_t MaxStateFileSize = 1024 * 1024;
constexpr std::uint32_t MaxRngStateSize = 64 * 1024;
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

void set_state_error(std::string* error, std::string message) {
    if (error) *error = std::move(message);
}

template <typename T>
void append_little_endian(std::vector<std::uint8_t>& output, T value) {
    static_assert(std::is_unsigned_v<T>, "state values must be unsigned integers");
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        output.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xFFu));
    }
}

template <typename T, std::size_t Size>
void append_array(std::vector<std::uint8_t>& output, const std::array<T, Size>& values) {
    for (const auto value : values) append_little_endian(output, value);
}

std::uint32_t state_checksum(std::span<const std::uint8_t> bytes) {
    std::uint32_t hash = 2166136261u; // FNV-1a
    for (const auto byte : bytes) {
        hash ^= byte;
        hash *= 16777619u;
    }
    return hash;
}

std::uint64_t rom_hash(std::span<const std::uint8_t> bytes) {
    std::uint64_t hash = 1469598103934665603ULL; // FNV-1a 64
    for (const auto byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

class StateReader {
public:
    explicit StateReader(std::span<const std::uint8_t> data) : data_(data) {}

    template <typename T>
    bool read(T& value) {
        static_assert(std::is_unsigned_v<T>, "state values must be unsigned integers");
        if (!ok_ || remaining() < sizeof(T)) { ok_ = false; return false; }
        value = 0;
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            value = static_cast<T>(value | (static_cast<T>(data_[position_++]) << (i * 8)));
        }
        return true;
    }

    template <typename T, std::size_t Size>
    bool read_array(std::array<T, Size>& values) {
        for (auto& value : values) {
            if (!read(value)) return false;
        }
        return true;
    }

    bool read_string(std::string& value, std::size_t size) {
        if (!ok_ || remaining() < size) { ok_ = false; return false; }
        value.assign(reinterpret_cast<const char*>(data_.data() + position_), size);
        position_ += size;
        return true;
    }

    [[nodiscard]] bool ok() const { return ok_; }
    [[nodiscard]] bool at_end() const { return position_ == data_.size(); }

private:
    [[nodiscard]] std::size_t remaining() const { return data_.size() - position_; }

    std::span<const std::uint8_t> data_;
    std::size_t position_{};
    bool ok_{true};
};
}

Chip8::Chip8() : rng(std::random_device{}()) {
    std::copy(Font.begin(), Font.end(), memory.begin() + FontAddress);
}

void Chip8::reset() {
    memory.fill(0);
    registers.fill(0);
    index_register = 0;
    program_counter = ProgramAddress;
    stack_pointer = 0;
    stack.fill(0);
    delay_timer = 0;
    sound_timer = 0;
    graphics.fill(0);
    keys.fill(0);
    draw_requested = true;
    rom_size_ = 0;
    rng.seed(std::random_device{}());
    std::copy(Font.begin(), Font.end(), memory.begin() + FontAddress);
}

bool Chip8::load_rom(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return false;
    const auto size = input.tellg();
    if (size < 0 || size > static_cast<std::streamoff>(memory.size() - ProgramAddress)) return false;
    input.seekg(0);
    input.read(reinterpret_cast<char*>(memory.data() + ProgramAddress), size);
    if (!(input.good() || input.eof())) return false;
    rom_size_ = static_cast<std::size_t>(size);
    return true;
}

bool Chip8::save_state(const std::filesystem::path& path, std::string* error) const {
    std::ostringstream rng_state;
    rng_state << rng;
    const std::string rng_text = rng_state.str();
    if (rng_text.size() > MaxRngStateSize) {
        set_state_error(error, "internal RNG state is unexpectedly large");
        return false;
    }

    std::vector<std::uint8_t> payload;
    payload.reserve(32 * 1024);
    append_array(payload, memory);
    append_array(payload, registers);
    append_little_endian(payload, index_register);
    append_little_endian(payload, program_counter);
    append_little_endian(payload, stack_pointer);
    append_array(payload, stack);
    append_little_endian(payload, delay_timer);
    append_little_endian(payload, sound_timer);
    append_array(payload, graphics);
    append_array(payload, keys);
    append_little_endian(payload, static_cast<std::uint32_t>(rom_size_));
    append_little_endian(payload, rom_hash(std::span<const std::uint8_t>(memory.data() + ProgramAddress, rom_size_)));
    append_little_endian(payload, static_cast<std::uint32_t>(rng_text.size()));
    payload.insert(payload.end(), rng_text.begin(), rng_text.end());

    std::vector<std::uint8_t> file;
    file.reserve(StateHeaderSize + payload.size());
    file.insert(file.end(), StateMagic.begin(), StateMagic.end());
    append_little_endian(file, StateVersion);
    append_little_endian(file, static_cast<std::uint32_t>(payload.size()));
    append_little_endian(file, state_checksum(payload));
    file.insert(file.end(), payload.begin(), payload.end());

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        set_state_error(error, "cannot open file for writing");
        return false;
    }
    output.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    output.flush();
    if (!output) {
        set_state_error(error, "write failed");
        return false;
    }
    return true;
}

bool Chip8::load_state(const std::filesystem::path& path, std::string* error) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        set_state_error(error, "file not found or cannot be opened");
        return false;
    }
    const auto end = input.tellg();
    if (end < 0) {
        set_state_error(error, "cannot determine file size");
        return false;
    }
    if (static_cast<std::uintmax_t>(end) > MaxStateFileSize) {
        set_state_error(error, "file is too large to be a CHIP-8 state");
        return false;
    }
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> file(static_cast<std::size_t>(end));
    if (!file.empty()) {
        input.read(reinterpret_cast<char*>(file.data()), static_cast<std::streamsize>(file.size()));
    }
    if (!input) {
        set_state_error(error, "failed to read file");
        return false;
    }

    if (file.size() < StateHeaderSize) {
        set_state_error(error, "file is too short");
        return false;
    }
    if (!std::equal(StateMagic.begin(), StateMagic.end(), file.begin())) {
        set_state_error(error, "not a CHIP-8 state file (bad magic)");
        return false;
    }

    const std::span<const std::uint8_t> file_span(file);
    StateReader header(file_span.subspan(StateMagic.size(), StateHeaderSize - StateMagic.size()));
    std::uint32_t version{};
    std::uint32_t payload_size{};
    std::uint32_t stored_checksum{};
    if (!header.read(version) || !header.read(payload_size) || !header.read(stored_checksum)) {
        set_state_error(error, "state header is truncated");
        return false;
    }
    if (version != StateVersion) {
        set_state_error(error, "unsupported state version " + std::to_string(version));
        return false;
    }
    if (static_cast<std::size_t>(payload_size) != file.size() - StateHeaderSize) {
        set_state_error(error, "file size does not match the header (truncated or corrupt)");
        return false;
    }

    const auto payload = file_span.subspan(StateHeaderSize);
    if (state_checksum(payload) != stored_checksum) {
        set_state_error(error, "checksum mismatch (state file is corrupt)");
        return false;
    }

    std::array<std::uint8_t, 4096> restored_memory{};
    std::array<std::uint8_t, 16> restored_registers{};
    std::uint16_t restored_index{};
    std::uint16_t restored_program_counter{};
    std::uint8_t restored_stack_pointer{};
    std::array<std::uint16_t, 16> restored_stack{};
    std::uint8_t restored_delay_timer{};
    std::uint8_t restored_sound_timer{};
    std::array<std::uint32_t, VIDEO_WIDTH * VIDEO_HEIGHT> restored_graphics{};
    std::array<std::uint8_t, 16> restored_keys{};
    std::uint32_t restored_rom_size{};
    std::uint64_t restored_rom_hash{};
    std::uint32_t rng_size{};

    StateReader reader(payload);
    if (!reader.read_array(restored_memory) || !reader.read_array(restored_registers) ||
        !reader.read(restored_index) || !reader.read(restored_program_counter) ||
        !reader.read(restored_stack_pointer) || !reader.read_array(restored_stack) ||
        !reader.read(restored_delay_timer) || !reader.read(restored_sound_timer) ||
        !reader.read_array(restored_graphics) || !reader.read_array(restored_keys) ||
        !reader.read(restored_rom_size) || !reader.read(restored_rom_hash) ||
        !reader.read(rng_size)) {
        set_state_error(error, "state data is truncated");
        return false;
    }
    if (rng_size > MaxRngStateSize) {
        set_state_error(error, "invalid RNG state length");
        return false;
    }
    if (restored_rom_size > memory.size() - ProgramAddress) {
        set_state_error(error, "invalid ROM size in state file");
        return false;
    }
    if (restored_rom_size != rom_size_ ||
        restored_rom_hash != rom_hash(std::span<const std::uint8_t>(memory.data() + ProgramAddress, rom_size_))) {
        set_state_error(error, "state belongs to a different ROM");
        return false;
    }

    std::string rng_text;
    if (!reader.read_string(rng_text, rng_size)) {
        set_state_error(error, "state data is truncated");
        return false;
    }
    if (!reader.at_end()) {
        set_state_error(error, "unexpected trailing data in state payload");
        return false;
    }
    if (restored_program_counter >= memory.size()) {
        set_state_error(error, "invalid program counter in state file");
        return false;
    }
    if (restored_stack_pointer > restored_stack.size()) {
        set_state_error(error, "invalid stack pointer in state file");
        return false;
    }

    std::istringstream rng_input(rng_text);
    std::mt19937 restored_rng;
    if (!(rng_input >> restored_rng)) {
        set_state_error(error, "invalid RNG state in state file");
        return false;
    }

    memory = restored_memory;
    registers = restored_registers;
    index_register = restored_index;
    program_counter = restored_program_counter;
    stack_pointer = restored_stack_pointer;
    stack = restored_stack;
    delay_timer = restored_delay_timer;
    sound_timer = restored_sound_timer;
    graphics = restored_graphics;
    keys = restored_keys;
    rom_size_ = restored_rom_size;
    rng = restored_rng;
    draw_requested = true;
    return true;
}

void Chip8::tick_timers() {
    if (delay_timer) --delay_timer;
    if (sound_timer) --sound_timer;
}

bool Chip8::sound_active() const { return sound_timer != 0; }
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
        case 0: registers[x] = registers[y]; break;
        case 1: registers[x] |= registers[y]; if (quirks_.logic_resets_vf) registers[0xF] = 0; break;
        case 2: registers[x] &= registers[y]; if (quirks_.logic_resets_vf) registers[0xF] = 0; break;
        case 3: registers[x] ^= registers[y]; if (quirks_.logic_resets_vf) registers[0xF] = 0; break;
        case 4: { const auto sum = static_cast<unsigned>(registers[x]) + registers[y]; registers[x] = static_cast<std::uint8_t>(sum); registers[0xF] = sum > 255; break; }
        case 5: registers[0xF] = registers[x] >= registers[y]; registers[x] = static_cast<std::uint8_t>(registers[x] - registers[y]); break;
        case 6: { const auto value = quirks_.shift_uses_vy ? registers[y] : registers[x]; registers[x] = static_cast<std::uint8_t>(value >> 1); registers[0xF] = static_cast<std::uint8_t>(value & 1); break; }
        case 7: registers[0xF] = registers[y] >= registers[x]; registers[x] = static_cast<std::uint8_t>(registers[y] - registers[x]); break;
        case 0xE: { const auto value = quirks_.shift_uses_vy ? registers[y] : registers[x]; registers[x] = static_cast<std::uint8_t>(value << 1); registers[0xF] = static_cast<std::uint8_t>(value >> 7); break; }
        default: break; } break;
    case 0x9000: if (n == 0 && registers[x] != registers[y]) skip(); break;
    case 0xA000: index_register = nnn; break;
    case 0xB000: program_counter = static_cast<std::uint16_t>(nnn + registers[0]); break;
    case 0xC000: registers[x] = static_cast<std::uint8_t>(random_byte(rng)) & nn; break;
    case 0xD000: {
        const auto px = registers[x] % VIDEO_WIDTH, py = registers[y] % VIDEO_HEIGHT; registers[0xF] = 0;
        for (unsigned row = 0; row < n && index_register + row < memory.size(); ++row) for (unsigned col = 0; col < 8; ++col) {
            if (!(memory[index_register + row] & (0x80 >> col))) continue;
            const auto dx = px + col, dy = py + row;
            if (!quirks_.draw_wraps && (dx >= VIDEO_WIDTH || dy >= VIDEO_HEIGHT)) continue;
            const auto pos = (quirks_.draw_wraps ? dy % VIDEO_HEIGHT : dy) * VIDEO_WIDTH + (quirks_.draw_wraps ? dx % VIDEO_WIDTH : dx);
            if (graphics[pos]) registers[0xF] = 1;
            graphics[pos] ^= PixelOn;
        }
        draw_requested = true; break; }
    case 0xE000: if (registers[x] < keys.size()) { if (nn == 0x9E && keys[registers[x]]) skip(); if (nn == 0xA1 && !keys[registers[x]]) skip(); } break;
    case 0xF000: switch (nn) {
        case 0x07: registers[x] = delay_timer; break;
        case 0x0A: { bool found = false; for (std::uint8_t key = 0; key < keys.size(); ++key) if (keys[key]) { registers[x] = key; found = true; break; } if (!found) program_counter -= 2; break; }
        case 0x15: delay_timer = registers[x]; break; case 0x18: sound_timer = registers[x]; break;
        case 0x1E: { const auto sum = static_cast<unsigned>(index_register) + registers[x]; if (quirks_.add_i_sets_vf) { registers[0xF] = sum > 0xFFF; index_register = static_cast<std::uint16_t>(sum & 0xFFF); } else index_register = static_cast<std::uint16_t>(sum); break; }
        case 0x29: index_register = static_cast<std::uint16_t>(FontAddress + (registers[x] & 0xF) * 5); break;
        case 0x33: if (static_cast<std::size_t>(index_register) + 2 < memory.size()) { memory[index_register] = registers[x] / 100; memory[index_register + 1] = registers[x] / 10 % 10; memory[index_register + 2] = registers[x] % 10; } break;
        case 0x55: for (unsigned i = 0; i <= x && index_register + i < memory.size(); ++i) memory[index_register + i] = registers[i]; if (quirks_.memory_increments_i) index_register = static_cast<std::uint16_t>(index_register + x + 1); break;
        case 0x65: for (unsigned i = 0; i <= x && index_register + i < memory.size(); ++i) registers[i] = memory[index_register + i]; if (quirks_.memory_increments_i) index_register = static_cast<std::uint16_t>(index_register + x + 1); break;
        default: break; } break;
    default: break;
    }
}
