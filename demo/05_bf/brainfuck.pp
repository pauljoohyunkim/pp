func readBFCode :: (ctx :: BFContext&) -> void {
    let c :: int32 = pp_std_input_char();
    while (c >= 0) {
        let character :: uint8 = c as uint8;
        list_push(ctx.code, character);
        c = pp_std_input_char();
    }
}

let GREATER :: const uint8 = 62uss;
let LESS :: const uint8 = 60uss;
let PLUS :: const uint8 = 43uss;
let MINUS :: const uint8 = 45uss;
let PERIOD :: const uint8 = 46uss;
let COMMA :: const uint8 = 44uss;
let LEFT_SQUARE_BRACKET :: const uint8 = 91uss;
let RIGHT_SQUARE_BRACKET :: const uint8 = 93uss;

func runBFCode :: (ctx :: BFContext&) -> void {
    while (ctx.code_idx < #ctx.code as uint32) {
        let cur :: const uint8 = ctx.code[ctx.code_idx as uint32];

        // Dispatch
        if (cur == GREATER) {
            // >
            ctx.mem_idx += 1u;
            if (ctx.mem_idx >= #ctx.memory as uint32) {
                list_push(ctx.memory, 0uss);
            }
        } else if (cur == LESS) {
            // <
            if (ctx.mem_idx == 0u) {
                pp_std_print_string("[-] You are accessing invalid memory\n");
                pp_std_abort();
            }
            ctx.mem_idx -= 1u;
        } else if (cur == PLUS) {
            // +
            ctx.memory[ctx.mem_idx as uint32] += 1uss;
        } else if (cur == MINUS) {
            // -
            ctx.memory[ctx.mem_idx as uint32] -= 1uss;
        } else if (cur == PERIOD) {
            // .
            pp_std_print_char(ctx.memory[ctx.mem_idx as uint32]);
        } else if (cur == COMMA) {
            let c :: const int32 = pp_std_input_char();
            if (c < 0) {
                pp_std_print_string("EOF Received\n");
            } else {
                let ch :: const uint8 = c as uint8;
                ctx.memory[ctx.mem_idx as uint32] = ch;
            }
        } else if (cur == LEFT_SQUARE_BRACKET) {
            if (ctx.memory[ctx.mem_idx as uint32] == 0uss) {
                // skip forward to matching ]
                let depth :: uint16 = 1us;
                ctx.code_idx += 1u;
                let done :: bool = false;
                while (depth > 0us && ctx.code_idx < #ctx.code as uint32 && !done) {
                    let ch :: const uint8 = ctx.code[ctx.code_idx as uint32];
                    if (ch == LEFT_SQUARE_BRACKET) { depth += 1us; }
                    else if (ch == RIGHT_SQUARE_BRACKET) { depth -= 1us; }
                    if (depth == 0us) { done = true; }
                    else { ctx.code_idx += 1u; }
                }
                // move past the matching ']'
                ctx.code_idx += 1u;
                continue;
            } else {
                list_push(ctx.return_stack, ctx.code_idx);
            }
        } else if (cur == RIGHT_SQUARE_BRACKET) {
            if (ctx.memory[ctx.mem_idx as uint32] != 0uss) {
                if (#ctx.return_stack == 0u) {
                    pp_std_print_string("[ ] Unmatched ']' – aborting\n");
                    pp_std_abort();
                } else {
                    let idx :: const uint32 = list_pop(ctx.return_stack);
                    ctx.code_idx = idx;
                    // next iteration will re-evaluate '['
                    continue;
                }
            } else {
                // pop matching '['
                if (#ctx.return_stack > 0u) {
                    list_pop(ctx.return_stack);
                }
            }
        }
        // Common increment for all non-jump instructions
        ctx.code_idx += 1u;
    }
}