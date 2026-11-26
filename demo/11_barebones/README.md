# 11_barebones – Bare Bones Operating System Kernel

A minimal 32-bit x86 kernel written in PP, based on the [OSDev Bare Bones tutorial](https://wiki.osdev.org/Bare_Bones). Demonstrates cross-compilation for freestanding targets and multiboot-compliant kernel creation.

## Files
* `kernel.pp` – Kernel main, VGA text mode driver, terminal output
* `boot.s` – Multiboot header, stack setup, entry point (`_start`)
* `linker.ld` – Linker script placing kernel at 2 MiB with correct section ordering
* `Makefile` – Builds `kernel_demo` (kernel binary) and `myos.iso` (bootable ISO)

## Prerequisites
* `i686-elf` cross-compiler toolchain (for linking assembly boot code and kernel objects)
* `grub-mkrescue` and `xorriso` (for ISO creation)
* `qemu-system-i386` (for testing)
* PP compiler (`bin/pplang`) — built from repository root with `make pplang`

## Build

```bash
# From repo root
make pplang

# From demo/11_barebones
make
```

This produces:
* `kernel_demo` — raw multiboot ELF kernel
* `myos.iso` — bootable GRUB ISO image

## Usage

### Run kernel directly (no ISO)
```bash
qemu-system-i386 -kernel kernel_demo
```

### Run via ISO
```bash
qemu-system-i386 -cdrom myos.iso
```

Both should display "Hello World" in light grey on black at the top-left of the VGA text buffer.

## How It Works

1. **`boot.s`** provides the multiboot header (magic, flags, checksum), a 16 KiB stack, and `_start` which sets up `%esp` and calls `kernel_main`.
2. **`kernel.pp`** implements:
   - VGA text buffer access at `0xB8000` (80×25 `uint16` cells: character + attribute)
   - `terminal_initialize` — clears screen to space with default colour
   - `terminal_putchar` / `terminal_write` — cursor-advancing output with line wrapping and scrolling
3. **`linker.ld`** places `.multiboot` first, then `.text`, `.rodata`, `.data`, `.bss` at 2 MiB with 4 KiB alignment.
4. **`Makefile`** compiles `kernel.pp` with `--freestanding --target i386-unknown-linux-gnu`, assembles `boot.s`, and links with the custom linker script.

## Clean

```bash
make clean
```

Removes `kernel_demo`, `myos.iso`, `boot.o`, `kernel.o`, and the `isodir/` staging directory.

## Notes

* The kernel uses PP's `--freestanding` mode: no standard library, no runtime, direct hardware access via pointer casts.
* The VGA buffer is a single global `ptr[uint16]` built from the constant address
  (`let terminal_buffer :: ptr[uint16] = VGA_MEMORY as ptr[uint16];`). Cells are written with pointer
  subscript, `terminal_buffer[y * VGA_WIDTH + x]`, so the backend scales the index by the 2-byte cell
  size and the address is never narrowed through an integer: the same code is correct on a 64-bit target.
* `VGA_WIDTH`, `VGA_HEIGHT` and `VGA_MEMORY` are `usize`, the index type required by subscript, and
  `#hello_world - 1uz` passes the length without the string literal's NUL terminator.
* Cursor position (`terminal_row`, `terminal_column`) is updated in `terminal_putchar`; line wrap at 80 columns, screen wrap at 25 rows (scrolls back to top in this minimal version).
* This is a teaching/demo kernel — no interrupt handling, no memory management, no device drivers beyond VGA text mode.