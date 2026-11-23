
func main :: () -> int32 {
    let term :: PP_Term = pp_term_open();
    let chip8 :: Chip8Machine = Chip8Machine {
        .pc = 0x200us
    };

    pp_seed_unsafe_rand();

    // Test: clear screen
    chip8.memory[0x200u] = 0x00uss;
    chip8.memory[0x201u] = 0xE0uss;

    while (true) {
        // Poll keyboard (returns non-zero on ESC)
        if (pp_term_poll_keys(chip8.keys) != 0) { break; }
        
        dispatch_chip8_opcode(chip8);
        
        // Timer handling (60Hz)
        if (chip8.delay_timer > 0uss) { chip8.delay_timer -= 1uss; }
        if (chip8.sound_timer > 0uss) { chip8.sound_timer -= 1uss; }
        
        pp_term_sleep_ms(16u);  // ~60 FPS
    }
    
    return 0;
}
