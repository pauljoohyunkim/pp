func readBFCode :: (ctx :: BFContext&) -> void {
    let c :: int32 = pp_input_char();
    while (c >= 0) {
        let character :: uint8 = c as uint8;
        list_push(ctx.code, character);
        c = pp_input_char();
    }
}


func runBFCode :: (ctx :: BFContext&) -> void {
    while (ctx.code_idx < #ctx.code as uint32) {
        let cur :: const uint8 = ctx.code[ctx.code_idx as usize];

        // Dispatch
        if (cur == BFCommand::Greater as uint8) {
            // >
            ctx.mem_idx += 1u;
            if (ctx.mem_idx >= #ctx.memory as uint32) {
                list_push(ctx.memory, 0uss);
            }
        } else if (cur == BFCommand::Less as uint8) {
            // <
            if (ctx.mem_idx == 0u) {
                pp_print_string("[-] You are accessing invalid memory\n");
                pp_abort();
            }
            ctx.mem_idx -= 1u;
        } else if (cur == BFCommand::Plus as uint8) {
            // +
            ctx.memory[ctx.mem_idx as usize] += 1uss;
        } else if (cur == BFCommand::Minus as uint8) {
            // -
            ctx.memory[ctx.mem_idx as usize] -= 1uss;
        } else if (cur == BFCommand::Period as uint8) {
            // .
            pp_print_char(ctx.memory[ctx.mem_idx as usize]);
        } else if (cur == BFCommand::Comma as uint8) {
            let c :: const int32 = pp_input_char();
            if (c < 0) {
                pp_print_string("EOF Received\n");
            } else {
                let ch :: const uint8 = c as uint8;
                ctx.memory[ctx.mem_idx as usize] = ch;
            }
        } else if (cur == BFCommand::LeftBracket as uint8) {
            if (ctx.memory[ctx.mem_idx as usize] == 0uss) {
                // skip forward to matching ]
                let depth :: uint16 = 1us;
                ctx.code_idx += 1u;
                let done :: bool = false;
                while (depth > 0us && ctx.code_idx < #ctx.code as uint32 && !done) {
                    let ch :: const uint8 = ctx.code[ctx.code_idx as usize];
                    if (ch == BFCommand::LeftBracket as uint8) { depth += 1us; }
                    else if (ch == BFCommand::RightBracket as uint8) { depth -= 1us; }
                    if (depth == 0us) { done = true; }
                    else { ctx.code_idx += 1u; }
                }
                // move past the matching ']'
                ctx.code_idx += 1u;
                continue;
            } else {
                list_push(ctx.return_stack, ctx.code_idx);
            }
        } else if (cur == BFCommand::RightBracket as uint8) {
            if (ctx.memory[ctx.mem_idx as usize] != 0uss) {
                if (#ctx.return_stack == 0uz) {
                    pp_print_string("[ ] Unmatched ']' - aborting\n");
                    pp_abort();
                } else {
                    let idx :: const uint32 = list_pop(ctx.return_stack);
                    ctx.code_idx = idx;
                    // next iteration will re-evaluate '['
                    continue;
                }
            } else {
                // pop matching '['
                if (#ctx.return_stack > 0uz) {
                    list_pop(ctx.return_stack);
                }
            }
        }
        // Common increment for all non-jump instructions
        ctx.code_idx += 1u;
    }
}
