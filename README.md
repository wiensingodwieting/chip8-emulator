# CHIP-8 Emulator

A standalone CHIP-8 emulator written in C++20, with SDL2 graphics and keyboard input.

## Features

- Standard CHIP-8 instruction set, 4 KiB memory, registers, stack, timers, and font set.
- Monochrome 64×32 display, upscaled to a 640×320 SDL2 window.
- Independent CPU (~700 Hz) and timer (60 Hz) update loops.
- Nix development shell for a reproducible Linux toolchain.

## Keypad mapping

```text
CHIP-8 keypad:       Keyboard:
1 2 3 C              1 2 3 4
4 5 6 D              Q W E R
7 8 9 E              A S D F
A 0 B F              Z X C V
```

Press `Esc` or close the window to quit.

Extra controls:

- `Space` — pause/resume
- `Backspace` — reset the current ROM
- `N` — execute one CPU step while paused
- `M` — mute/unmute sound
- `PgUp` / `PgDn` — CPU speed up/down
- `F6` — cycle compatibility profile (`modern`, `original`, `superchip`)
- Drag and drop a ROM file onto the window to load it

### Palettes and sound

- `F1` — Classic white on black
- `F2` — Matrix green
- `F3` — Game Boy green
- `F4` — Cyberpunk pink

The emulator plays a 440 Hz tone while a ROM's CHIP-8 sound timer is active.

### Save states

- `F5` saves the current emulator state beside the ROM as `<rom>.state` (for example, `pong.ch8.state`).
- `F8` loads that file and resumes from the saved moment.
- The state format has a magic header, version, checksum, and the size/hash of the ROM it belongs to. If the file is missing, corrupt, or belongs to another ROM, the emulator keeps running and prints a message in the terminal and on screen.

### Compatibility profiles

Some ROMs expect slightly different CHIP-8 behaviour. Choose a profile with `--profile` or cycle it with `F6`:

- `modern` — current common behaviour: shifts use `Vy`, `Fx55/Fx65` do not change `I`, sprites are clipped at the edge.
- `original` — older behaviour: shifts use `Vx`, `Fx55/Fx65` increment `I`, logic operations reset `VF`.
- `superchip` — Super-CHIP style: sprites wrap around the edge, `Fx1E` sets `VF` on overflow.

```bash
./build/chip8 --profile original roms/pong.ch8
./build/chip8 --profile superchip --scale 12 roms/pong.ch8
```

### Pong controls

The Pong ROM used for testing uses four keys:

| Player | Move up | Move down |
| --- | --- | --- |
| Left paddle | `1` | `Q` |
| Right paddle | `4` | `R` |

This slightly unusual layout comes from the original CHIP-8 hexadecimal keypad. Other ROMs can use different keys; the full mapping above is available to every ROM.

## Build and run

### NixOS

```bash
nix develop
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
./build/chip8 roms/test.ch8
```

Run the automated tests with:

```bash
nix develop -c ctest --test-dir build --output-on-failure
```

If Nix flakes are not enabled, use:

```bash
nix --extra-experimental-features "nix-command flakes" develop
```

### Other Linux distributions

Install CMake, a C++20 compiler, pkg-config, and SDL2 development headers, then run the same CMake commands above.

## ROMs

ROMs are separate binary programs. `roms/test.ch8` is a tiny local smoke test that draws a `0`. Downloaded game ROMs are ignored by Git; run one with:

```bash
./build/chip8 roms/pong.ch8
```
