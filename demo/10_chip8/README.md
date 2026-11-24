# 10_chip8 – CHIP-8 Emulator Demo

A CHIP-8 interpreter written in PP. Implements the full standard instruction set (35 opcodes), 64×32 display, 16-key hex keypad, delay/sound timers at 60 Hz, and a minimal terminal frontend using raw stdin.

The implementation follows the original CHIP-8 specification (COSMAC VIP), including the quirks:
- Sprites wrap around screen edges
- `8XY6`/`8XYE` shift VX in place (VY unused)
- `FX55`/`FX65` leave I unchanged
- `EX9E`/`EXA1` check keypad state
- `FX0A` blocks until a key press, then stores the key index

Tested against some of [chip8-test-suite](https://github.com/Timendus/chip8-test-suite) splash-screen ROMs.

## Files
* `main.pp` – CLI entry, argument handling via `pp_get_argv`, main loop (input → dispatch → timers → render)
* `chip8.pp` – VM struct, font/ROM loaders, opcode dispatcher
* `chip8.ppi` – PPI declarations for `Chip8Machine`, `load_chip8_font`, `load_chip8_rom`, `dispatch_chip8_opcode`
* `chip8_term.c` / `chip8_term.h` – Terminal raw-mode keyboard polling (C helper)
* `chip8_term.ppi` – PPI declarations for `PP_Term`, `pp_term_open`, `pp_term_poll_keys`, `pp_term_sleep_ms`
* `Makefile` – builds `chip8_demo`

## Build

```bash
make
```

This ensures `bin/pplang` is built, compiles `chip8_term.c`, links with `lib/pp_std`.

## Usage

```bash
./chip8_demo <rom file>
```

Example:

```bash
# Run a ROM (any .ch8 file)
./chip8_demo roms/pong.ch8
```

Controls (mapped to CHIP-8 keypad 0x0–0xF):
```
1 2 3 4        1 2 3 C
q w e r   =>   4 5 6 D
a s d f        7 8 9 E
z x c v        A 0 B F
```
Press `ESC` to quit.

## Rendering

The display is rendered to the terminal each frame using ANSI escape codes (`\x1b[H` for cursor home). Each pixel is `#` (on) or space (off). This avoids flicker from `system("clear")`.

## Timing

- Main loop runs at ~60 FPS (`pp_term_sleep_ms(16)`)
- `delay_timer` and `sound_timer` decrement once per frame when > 0
- `FX0A` blocks inside the dispatcher but calls `pp_term_poll_keys` each iteration so it sees fresh input

## Clean

```bash
make clean
```

Removes `chip8_demo` and `chip8_term.o`.

## Notes

* The font (80 bytes at 0x000–0x04F) is loaded automatically by `load_chip8_font`.
* ROMs are loaded at 0x200 by `load_chip8_rom`.
* `PP_Term` handle is RAII-managed; its drop (`pp_term_close`) restores terminal attributes on scope exit — no manual close needed.
* The C keyboard helper (`chip8_term.c`) uses a per-key hold counter (3 frames) so a single keystroke stays "down" for ~50 ms, matching typical CHIP-8 behavior where terminals never report key-up.
