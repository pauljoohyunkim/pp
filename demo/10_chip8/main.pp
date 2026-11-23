
func main :: () -> int32 {
    let chip8 :: Chip8Machine = Chip8Machine {
        .pc = 0x200us
    };

    chip8.memory[0x200u] = 0x00uss;
    chip8.memory[0x201u] = 0xE0uss;

    dispatch_chip8_opcode(chip8);

    return 0;
}