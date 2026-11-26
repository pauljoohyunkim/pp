
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

constant VGA_WIDTH :: uint32 = 80u;
constant VGA_HEIGHT :: uint32 = 25u;
constant VGA_MEMORY :: uint32 = 0xB8000u;

let terminal_row :: usize;
let terminal_column :: usize;
let terminal_color :: uint8;
//let terminal_buffer :: ptr[uint16] = VGA_MEMORY as ptr[uint16];

func terminal_initialize :: () -> void {
    terminal_row = 0uz;
    terminal_column = 0uz;
    terminal_color = vga_entry_color(VGA_COLOR::VGA_COLOR_LIGHT_GREY as uint8, VGA_COLOR::VGA_COLOR_BLACK as uint8);

    for (let y :: uint32 = 0u; y < VGA_HEIGHT; y += 1u) {
        for (let x :: uint32 =0u; x < VGA_WIDTH; x += 1u) {
            let idx :: usize = (y * VGA_WIDTH + x) as usize;

            $((VGA_MEMORY + 2u * idx as uint32) as ptr[uint16]) = vga_entry(' ', terminal_color as uint8);
        }
    }
}

func terminal_setcolor :: (color :: uint8) -> void {
    terminal_color = color;
}

func terminal_putentryat :: (c :: uint8, color :: uint8, x :: usize, y :: usize) -> void {
    let idx :: usize = (y as uint32 * VGA_WIDTH + x as uint32) as usize;
    $((VGA_MEMORY + 2u * idx as uint32) as ptr[uint16]) = vga_entry(c, color);
}

func terminal_putchar :: (c :: uint8) -> void {
    terminal_putentryat(c, terminal_color, terminal_column, terminal_row);
    terminal_column += 1uz;
    if (terminal_column as uint32 == VGA_WIDTH) {
        terminal_column = 0uz;
        terminal_row += 1uz;
        if (terminal_row as uint32 == VGA_HEIGHT) {
            terminal_row = 0uz;
        }
    }
}

func terminal_write :: (data :: ptr[const uint8], size :: usize) -> void {
    for (let i :: usize = 0uz; i < size; i += 1uz) {
        terminal_putchar(
            $((data as uint32 + i as uint32) as ptr[const uint8])
        );
    }
}

func kernel_main :: () -> void {
    terminal_initialize();

    let hello_world :: [uint8 : 12u] = @"Hello World";

    terminal_write(@hello_world[0uz] as ptr[const uint8], #hello_world);
}