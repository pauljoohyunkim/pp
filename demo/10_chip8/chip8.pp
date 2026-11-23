
lambda extract_4bit :: (opcode :: OPCODE, idx :: uint8) -> uint8 {
    return ((opcode >> ((12uss - (idx * 4uss)) as uint16)) & 0xFus) as uint8;
}

func dispatch_chip8_opcode :: (machine :: Chip8Machine&) -> void {
    let opcode :: OPCODE = (machine.memory[machine.pc] as uint16 << 8us) | (machine.memory[machine.pc+1us] as uint16);
    machine.pc += 2us;
    if (opcode == 0x00E0u as uint16) {
        // Clear screen
        pp_system("clear");
        return;
    } else if (opcode == 0x00EEu as uint16) {
        // Return from subroutine
        machine.pc = list_pop(machine.stack);
        return;
    } else if (extract_4bit(opcode, 0uss) == 1uss) {
        // 1NNN
        // goto NNN;
        let addr :: uint16 = opcode & 0xFFFus;
        machine.pc = addr;
        return;
    } else if (extract_4bit(opcode, 0uss) == 2uss) {
        // 2NNN
        // call 0xNNN (*(0xNNN)())
        
        // Save the current address (Note that program counter has already moved, so it needs to return to CURRENT PC)
        list_push(machine.stack, machine.pc);
        return;
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
    } else if (extract_4bit(opcode, 0uss) == 5uss && opcode & 0x0Fus == 0us) {
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
            let sum :: uint16 = machine.v[x] as uint16 + machine.v[y] as uint16;
            if (sum >= 256us) {
                machine.v[0xFu] = 1uss;
            } else {
                machine.v[0xFu] = 0uss;
            }
            machine.v[x] = (sum & 0xFFus) as uint8;
        } else if (l4b == 5uss) {
            // 8XY5
            // VX -= VY
            if (machine.v[x] >= machine.v[y]) {
                machine.v[0xFu] = 1uss;
            } else {
                machine.v[0xFu] = 0uss;
            }
            machine.v[x] -= machine.v[y];
        } else if (l4b == 6uss) {
            // 8XY6
            // VX >>= 1
            machine.v[0xFu] = machine.v[x] & 1uss;
            machine.v[x] >>= 1uss;
        } else if (l4b == 7uss) {
            // 8XY7
            // VX = VY - VX
            if (machine.v[y] >= machine.v[x]) {
                machine.v[0xFu] = 1uss;
            } else {
                machine.v[0xFu] = 0uss;
            }
            
            machine.v[x] = machine.v[y] - machine.v[x];
        } else if (l4b == 0xEuss) {
            // 8XY8
            machine.v[0xFu] = machine.v[x] >> 7uss;
            machine.v[x] <<= 1uss;
        }
    } else if (extract_4bit(opcode, 0uss) == 9uss && extract_4bit(opcode, 3uss) == 0uss) {
        // 9XY0
        let x :: uint8 = extract_4bit(opcode, 1uss);
        let y :: uint8 = extract_4bit(opcode, 2uss);
        if (machine.v[x] != machine.v[y]) {
            machine.pc += 2us;
        }
    } else if (extract_4bit(opcode, 0uss) == 0xAuss) {
        // I = NNN
        machine.I = opcode & 0xFFFus;
    } else if (extract_4bit(opcode, 1uss) == 0xBuss) {
        // PC = V0 + NNN
        machine.pc = (opcode & 0xFFFus) + machine.v[0u] as uint16;
    } else if (extract_4bit(opcode, 1uss) == 0xCuss) {
        let x :: uint8 = extract_4bit(opcode, 1uss);
        machine.v[x] = (pp_unsafe_rand() & 0xFF) as uint8;
    } else if (extract_4bit(opcode, 1uss) == 0xDuss) {
        // DXYN
        // draw(VX, VY, N)

        machine.v[0xFu] = 0uss;

        let x :: uint8 = extract_4bit(opcode, 1uss);
        let y :: uint8 = extract_4bit(opcode, 2uss);
        let x_offset :: uint16 = machine.v[x] as uint16 % WIDTH;
        let y_offset :: uint16 = machine.v[y] as uint16 % HEIGHT;
        let N :: uint8 = extract_4bit(opcode, 3uss);

        for (let j :: uint8 = 0uss; j < N; j += 1uss) {
            let py :: uint16 = y_offset + j as uint16;
            if (py >= HEIGHT) { break; }

            let sprite_byte :: uint8 = machine.memory[machine.I + j as uint16];

            for (let i :: uint8 = 0uss; i < 8uss; i += 1uss) {
                let px :: uint16 = x_offset + i as uint16;
                if (px >= WIDTH) { break; }

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
    }
}