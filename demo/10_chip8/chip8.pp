constant CHIP8_CODE_START :: uint16 = 0x200us;

lambda extract_4bit :: (opcode :: OPCODE, idx :: uint8) -> uint8 {
    return ((opcode >> ((12uss - (idx * 4uss)) as uint16)) & 0xFus) as uint8;
}

func load_chip8_font :: (machine :: Chip8Machine&) -> void {
    // Standard CHIP-8 font: 16 digits (0-F), 5 bytes each at 0x000-0x04F
    let font :: [uint8 : 80u] = [uint8 : 80u;
        0xF0uss, 0x90uss, 0x90uss, 0x90uss, 0xF0uss,  // 0
        0x20uss, 0x60uss, 0x20uss, 0x20uss, 0x70uss,  // 1
        0xF0uss, 0x10uss, 0xF0uss, 0x80uss, 0xF0uss,  // 2
        0xF0uss, 0x10uss, 0xF0uss, 0x10uss, 0xF0uss,  // 3
        0x90uss, 0x90uss, 0xF0uss, 0x10uss, 0x10uss,  // 4
        0xF0uss, 0x80uss, 0xF0uss, 0x10uss, 0xF0uss,  // 5
        0xF0uss, 0x80uss, 0xF0uss, 0x90uss, 0xF0uss,  // 6
        0xF0uss, 0x10uss, 0x20uss, 0x40uss, 0x40uss,  // 7
        0xF0uss, 0x90uss, 0xF0uss, 0x90uss, 0xF0uss,  // 8
        0xF0uss, 0x90uss, 0xF0uss, 0x10uss, 0xF0uss,  // 9
        0xF0uss, 0x90uss, 0xF0uss, 0x90uss, 0x90uss,  // A
        0xE0uss, 0x90uss, 0xE0uss, 0x90uss, 0xE0uss,  // B
        0xF0uss, 0x80uss, 0x80uss, 0x80uss, 0xF0uss,  // C
        0xE0uss, 0x90uss, 0x90uss, 0x90uss, 0xE0uss,  // D
        0xF0uss, 0x80uss, 0xF0uss, 0x80uss, 0xF0uss,  // E
        0xF0uss, 0x80uss, 0xF0uss, 0x80uss, 0x80uss   // F
    ];
    for (let i :: uint16 = 0us; i < 80us; i += 1us) {
        machine.memory[i] = font[i];
    }
}

func load_chip8_rom :: (machine :: Chip8Machine&, filename :: const string&) -> void {
    let fp :: PP_File = pp_file_open(filename, PP_File_OpenMode::PP_FOPEN_READ_ONLY);
    
    pp_file_read(fp, @machine.memory[CHIP8_CODE_START], (#machine.memory - CHIP8_CODE_START as uint32) as uint64);
}

func dispatch_chip8_opcode :: (machine :: Chip8Machine&) -> void {
    let opcode :: OPCODE = (machine.memory[machine.pc] as uint16 << 8us) | (machine.memory[machine.pc+1us] as uint16);
    machine.pc += 2us;
    if (opcode == 0x00E0u as uint16) {
        // Clear screen
        for (let i :: uint16 = 0us; i < WIDTH * HEIGHT; i += 1us) {
            machine.display[i] = 0uss;
        }
        return;
    } else if (opcode == 0x00EEu as uint16) {
        // Return from subroutine
        // A RET without a matching CALL would pop an empty list, which aborts.
        if (#machine.stack == 0u) { return; }
        machine.pc = list_pop(machine.stack);
    } else if (extract_4bit(opcode, 0uss) == 1uss) {
        // 1NNN
        // goto NNN;
        let addr :: uint16 = opcode & 0xFFFus;
        machine.pc = addr;
    } else if (extract_4bit(opcode, 0uss) == 2uss) {
        // 2NNN
        // call 0xNNN (*(0xNNN)())
        
        // Save the current address (Note that program counter has already moved, so it needs to return to CURRENT PC)
        list_push(machine.stack, machine.pc);
        machine.pc = opcode & 0xFFFus;
    } else if (extract_4bit(opcode, 0uss) == 3uss) {
        // 3XNN
        // Skip next instruction if VX == NN
        let x :: uint8 = extract_4bit(opcode, 1uss);
        let val :: uint8 = (opcode & 0xFFus) as uint8;
        if (machine.v[x] == val) {
            // Advance program counter.
            machine.pc += 2us;
        }
    } else if (extract_4bit(opcode, 0uss) == 4uss) {
        // 4XNN
        // Skip next instruction if VX != NN
        let x :: uint8 = extract_4bit(opcode, 1uss);
        let val :: uint8 = (opcode & 0xFFus) as uint8;
        if (machine.v[x] != val) {
            // Advance program counter.
            machine.pc += 2us;
        }
    } else if (extract_4bit(opcode, 0uss) == 5uss && (opcode & 0x000Fus) == 0us) {
        // 5XY0
        // Skips next instruction if VX == VY
        let x :: uint8 = extract_4bit(opcode, 1uss);
        let y :: uint8 = extract_4bit(opcode, 2uss);
        if (machine.v[x] == machine.v[y]) {
            machine.pc += 2us;
        }
    } else if (extract_4bit(opcode, 0uss) == 6uss) {
        // 6XNN
        // Set VX = NN
        let x :: uint8 = extract_4bit(opcode, 1uss);
        let val :: uint8 = (opcode & 0xFFus) as uint8;
        machine.v[x] = val;
    } else if (extract_4bit(opcode, 0uss) == 7uss) {
        // 7XNN
        // VX += NN
        // Carry flag n ot changed
        let x :: uint8 = extract_4bit(opcode, 1uss);
        let val :: uint8 = (opcode & 0xFFus) as uint8;
        machine.v[x] += val;
    } else if (extract_4bit(opcode, 0uss) == 8uss) {
        // Lowest 4bits
        let l4b :: uint8 = extract_4bit(opcode, 3uss);
        let x :: uint8 = extract_4bit(opcode, 1uss);
        let y :: uint8 = extract_4bit(opcode, 2uss);
        if (l4b == 0uss) {
            // 8XY0
            // VX = VY
            machine.v[x] = machine.v[y];
        } else if (l4b == 1uss) {
            // 8XY1
            // VX |= VY
            machine.v[x] |= machine.v[y];
        } else if (l4b == 2uss) {
            // 8XY2
            // VX &= VY
            machine.v[x] &= machine.v[y];
        } else if (l4b == 3uss) {
            // 8XY3
            // VX ^= VY
            machine.v[x] ^= machine.v[y];
        } else if (l4b == 4uss) {
            // 8XY4
            // VX += VY
            // With carry
            // The result and the flag must be computed before anything is stored:
            // writing VF first would clobber the result when X == 0xF.
            let sum :: uint16 = machine.v[x] as uint16 + machine.v[y] as uint16;
            let carry :: uint8 = 0uss;
            if (sum >= 256us) { carry = 1uss; }
            machine.v[x] = (sum & 0xFFus) as uint8;
            machine.v[0xFu] = carry;
        } else if (l4b == 5uss) {
            // 8XY5
            // VX -= VY
            let result :: uint8 = machine.v[x] - machine.v[y];
            let borrow :: uint8 = 0uss;
            if (machine.v[x] >= machine.v[y]) { borrow = 1uss; }
            machine.v[x] = result;
            machine.v[0xFu] = borrow;
        } else if (l4b == 6uss) {
            // 8XY6
            // VX >>= 1
            let lsb :: uint8 = machine.v[x] & 1uss;
            machine.v[x] >>= 1uss;
            machine.v[0xFu] = lsb;
        } else if (l4b == 7uss) {
            // 8XY7
            // VX = VY - VX
            let result :: uint8 = machine.v[y] - machine.v[x];
            let borrow :: uint8 = 0uss;
            if (machine.v[y] >= machine.v[x]) { borrow = 1uss; }
            machine.v[x] = result;
            machine.v[0xFu] = borrow;
        } else if (l4b == 0xEuss) {
            // 8XYE
            // VX <<= 1
            let msb :: uint8 = machine.v[x] >> 7uss;
            machine.v[x] <<= 1uss;
            machine.v[0xFu] = msb;
        }
    } else if (extract_4bit(opcode, 0uss) == 9uss && extract_4bit(opcode, 3uss) == 0uss) {
        // 9XY0
        let x :: uint8 = extract_4bit(opcode, 1uss);
        let y :: uint8 = extract_4bit(opcode, 2uss);
        if (machine.v[x] != machine.v[y]) {
            machine.pc += 2us;
        }
    } else if (extract_4bit(opcode, 0uss) == 0xEuss) {
        let x :: uint8 = extract_4bit(opcode, 1uss);
        let low :: uint8 = (opcode & 0xFFus) as uint8;
        if (low == 0x9Euss) {
            // EX9E
            // Skip next instruction if key with the value of VX is pressed
            if (machine.keys[machine.v[x] & 0xFuss] != 0uss) {
                machine.pc += 2us;
            }
        } else if (low == 0xA1uss) {
            // EXA1
            // Skip next instruction if key with the value of VX is not pressed
            if (machine.keys[machine.v[x] & 0xFuss] == 0uss) {
                machine.pc += 2us;
            }
        }
    } else if (extract_4bit(opcode, 0uss) == 0xAuss) {
        // I = NNN
        machine.I = opcode & 0xFFFus;
    } else if (extract_4bit(opcode, 0uss) == 0xBuss) {
        // BNNN
        // PC = V0 + NNN
        machine.pc = (opcode & 0xFFFus) + machine.v[0u] as uint16;
    } else if (extract_4bit(opcode, 0uss) == 0xCuss) {
        // CXNN
        // VX = rand() & NN
        let x :: uint8 = extract_4bit(opcode, 1uss);
        let val :: uint8 = (opcode & 0xFFus) as uint8;
        machine.v[x] = ((pp_unsafe_rand() & 0xFF) as uint8) & val;
    } else if (extract_4bit(opcode, 0uss) == 0xDuss) {
        // DXYN
        // draw(VX, VY, N)

        machine.v[0xFu] = 0uss;

        let x :: uint8 = extract_4bit(opcode, 1uss);
        let y :: uint8 = extract_4bit(opcode, 2uss);
        let x_offset :: uint16 = machine.v[x] as uint16 % WIDTH;
        let y_offset :: uint16 = machine.v[y] as uint16 % HEIGHT;
        let N :: uint8 = extract_4bit(opcode, 3uss);

        for (let j :: uint8 = 0uss; j < N; j += 1uss) {
            // CHIP-8 sprites wrap vertically rather than clipping.
            let py :: uint16 = (y_offset + j as uint16) % HEIGHT;

            let sprite_byte :: uint8 = machine.memory[machine.I + j as uint16];

            for (let i :: uint8 = 0uss; i < 8uss; i += 1uss) {
                // CHIP-8 sprites wrap horizontally too.
                let px :: uint16 = (x_offset + i as uint16) % WIDTH;

                let bit :: uint8 = (sprite_byte >> (7uss - i)) & 1uss;
                if (bit != 0uss) {
                    let idx :: uint16 = py * WIDTH + px;
                    if (machine.display[idx] != 0uss) {
                        machine.v[0xFu] = 1uss;
                    }
                    machine.display[idx] ^= 1uss;
                }
            }
        }
    } else if (extract_4bit(opcode, 0uss) == 0xFuss) {
        let x :: uint8 = extract_4bit(opcode, 1uss);
        let low :: uint8 = (opcode & 0xFFus) as uint8;
        if (low == 0x07uss) {
            // VX = get_delay()
            machine.v[x] = machine.delay_timer;
        } else if (low == 0x0Auss) {
            // FX0A: VX = get_key() — wait for key press, store key index
            let found :: bool = false;
            let key_idx :: uint8 = 0uss;
            while (!found) {
                // Poll keyboard to update machine.keys
                pp_term_poll_keys(machine.keys);
                for (let i :: uint8 = 0uss; i < 16uss; i += 1uss) {
                    if (machine.keys[i] != 0uss) {
                        key_idx = i;
                        found = true;
                        break;
                    }
                }
            }
            machine.v[x] = key_idx;
        } else if (low == 0x15uss) {
            machine.delay_timer = machine.v[x];
        } else if (low == 0x18uss) {
            machine.sound_timer = machine.v[x];
        } else if (low == 0x1Euss) {
            machine.I += machine.v[x] as uint16;
        } else if (low == 0x29uss) {
            // FX29: Set I = location of sprite for digit VX
            // Each digit sprite is 5 bytes, font starts at 0x000
            machine.I = (machine.v[x] & 0xFuss) as uint16 * 5us;
        } else if (low == 0x33uss) {
            // FX33: Store BCD representation of VX at I, I+1, I+2
            // VX is 0-255, store hundreds, tens, ones
            let val :: uint8 = machine.v[x];
            machine.memory[machine.I] = val / 100uss;
            machine.memory[machine.I + 1us] = (val / 10uss) % 10uss;
            machine.memory[machine.I + 2us] = val % 10uss;
        } else if (low == 0x55uss) {
            // FX55: Store V0..VX to memory starting at I
            for (let i :: uint8 = 0uss; i <= x; i += 1uss) {
                machine.memory[machine.I + i as uint16] = machine.v[i];
            }
        } else if (low == 0x65uss) {
            // FX65: Load V0..VX from memory starting at I
            for (let i :: uint8 = 0uss; i <= x; i += 1uss) {
                machine.v[i] = machine.memory[machine.I + i as uint16];
            }
        }
    }
}
