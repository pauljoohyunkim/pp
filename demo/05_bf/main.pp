func main :: () -> int32 {
    let ctx :: BFContext;
    // initialise tape with one zero cell
    list_push(ctx.memory, 0uss);

    readBFCode(ctx);

    // Print BF code received.
    pp_print_string("Received code\n");
    let i :: uint32 = 0u;
    while (i < #ctx.code as uint32) {
        pp_print_char(ctx.code[i as usize]);
        i += 1u;
    }
    pp_print_string("\n");

    // Run BF code
    runBFCode(ctx);

    return 0;
}