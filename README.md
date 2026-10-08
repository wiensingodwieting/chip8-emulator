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
