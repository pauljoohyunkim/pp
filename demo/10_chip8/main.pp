
func main :: (argc :: int32, argv :: ptr[ptr[uint8]]) -> int32 {
    let args :: [string] = pp_get_argv(argc, argv);
    
    if (argc as uint32 < 2u) {
        pp_print_string("Usage: ");
        pp_print_string(args[0u]);
        pp_print_string(" <rom file>\n");
        return 1;
    }

    let term :: PP_Term = pp_term_open();
    let chip8 :: Chip8Machine = Chip8Machine {
        .pc = 0x200us
    };

    pp_seed_unsafe_rand();
    load_chip8_font(chip8);
    load_chip8_rom(chip8, args[1u]);

    while (true) {
        // Poll keyboard (returns non-zero on ESC)
        if (pp_term_poll_keys(chip8.keys) != 0) { break; }
        
        dispatch_chip8_opcode(chip8);
        
        // Timer handling (60Hz)
        if (chip8.delay_timer > 0uss) { chip8.delay_timer -= 1uss; }
        if (chip8.sound_timer > 0uss) { chip8.sound_timer -= 1uss; }
        
        // Render display
        pp_print_string("\x1b[H");  // ANSI: cursor home
        for (let row :: uint16 = 0us; row < HEIGHT; row += 1us) {
            for (let col :: uint16 = 0us; col < WIDTH; col += 1us) {
                let pixel :: uint8 = chip8.display[row * WIDTH + col];
                let ch :: uint8;
                if (pixel != 0uss) {
                    ch = '#';  // '#'
                } else {
                    ch = ' ';  // ' '
                }
                pp_print_char(ch);
            }
            pp_print_char('\n');  // newline
        }
        
        pp_term_sleep_ms(16u);  // ~60 FPS
    }
    
    return 0;
}
