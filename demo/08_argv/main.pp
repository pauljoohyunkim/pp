// Prints every command-line argument, including argv[0] (the program name).
// The arguments arrive as a List(string) built by the runtime, which owns the
// buffers and frees them when `args` goes out of scope.

func main :: (argc :: int32, argv :: ptr[ptr[uint8]]) -> int32 {
    let args :: [string] = pp_get_argv(argc, argv);

    pp_print_string("argc = ");
    pp_print_int64(argc as int64);
    pp_print_string("\n");

    for (let i :: uint32 = 0u; i < #args; i += 1u) {
        pp_print_string("argv[");
        pp_print_uint64(i as uint64);
        pp_print_string("] = \"");
        pp_print_string(args[i]);
        pp_print_string("\"\n");
    }

    return 0;
}
