enum VGA_COLOR :: uint8 {
    VGA_COLOR_BLACK = 0uss,
	VGA_COLOR_BLUE = 1uss,
	VGA_COLOR_GREEN = 2uss,
	VGA_COLOR_CYAN = 3uss,
	VGA_COLOR_RED = 4uss,
	VGA_COLOR_MAGENTA = 5uss,
	VGA_COLOR_BROWN = 6uss,
	VGA_COLOR_LIGHT_GREY = 7uss,
	VGA_COLOR_DARK_GREY = 8uss,
	VGA_COLOR_LIGHT_BLUE = 9uss,
	VGA_COLOR_LIGHT_GREEN = 10uss,
	VGA_COLOR_LIGHT_CYAN = 11uss,
	VGA_COLOR_LIGHT_RED = 12uss,
	VGA_COLOR_LIGHT_MAGENTA = 13uss,
	VGA_COLOR_LIGHT_BROWN = 14uss,
	VGA_COLOR_WHITE = 15uss
}

lambda vga_entry_color :: (fg :: uint8, bg :: uint8) -> uint8 {
    return (fg as uint8) | (bg as uint8 << 4uss);
}

lambda vga_entry :: (ch :: uint8, color :: uint8) -> uint16 {
    return (ch as uint16) | (color as uint16 << 8us);
}

// Screen geometry is a pointer-width quantity: it indexes the VGA buffer with `usize`.
constant VGA_WIDTH :: usize = 80uz;
constant VGA_HEIGHT :: usize = 25uz;
constant VGA_MEMORY :: usize = 0xB8000uz;

// The VGA text buffer is a plain `ptr[uint16]`: subscripting derives the byte offset by scaling with
// the element size, so a 64-bit build cannot truncate the address.
let terminal_buffer :: ptr[uint16] = VGA_MEMORY as ptr[uint16];

let terminal_row :: usize;
let terminal_column :: usize;
let terminal_color :: uint8;

func terminal_initialize :: () -> void {
    terminal_row = 0uz;
    terminal_column = 0uz;
    terminal_color = vga_entry_color(VGA_COLOR::VGA_COLOR_LIGHT_GREY as uint8, VGA_COLOR::VGA_COLOR_BLACK as uint8);

    for (let y :: usize = 0uz; y < VGA_HEIGHT; y += 1uz) {
        for (let x :: usize = 0uz; x < VGA_WIDTH; x += 1uz) {
            terminal_buffer[y * VGA_WIDTH + x] = vga_entry(' ', terminal_color);
        }
    }
}

func terminal_setcolor :: (color :: uint8) -> void {
    terminal_color = color;
}

func terminal_putentryat :: (c :: uint8, color :: uint8, x :: usize, y :: usize) -> void {
    terminal_buffer[y * VGA_WIDTH + x] = vga_entry(c, color);
}

func terminal_putchar :: (c :: uint8) -> void {
    terminal_putentryat(c, terminal_color, terminal_column, terminal_row);
    terminal_column += 1uz;
    if (terminal_column == VGA_WIDTH) {
        terminal_column = 0uz;
        terminal_row += 1uz;
        if (terminal_row == VGA_HEIGHT) {
            terminal_row = 0uz;
        }
    }
}

func terminal_write :: (data :: ptr[const uint8], size :: usize) -> void {
    for (let i :: usize = 0uz; i < size; i += 1uz) {
        terminal_putchar(data[i]);
    }
}

func kernel_main :: () -> void {
    terminal_initialize();

    let hello_world :: [uint8 : 12u] = @"Hello World";

    // The array literal is NUL-terminated, so the length without the terminator is `#hello_world - 1uz`.
    terminal_write(@hello_world[0uz] as ptr[const uint8], #hello_world - 1uz);
}
