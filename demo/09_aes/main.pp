

func main :: (argc :: int32, argv :: ptr[ptr[uint8]]) -> int32 {
    let args :: [string] = pp_get_argv(argc, argv);

    let filename :: string = args[1u];
    pp_print_string(filename);

    pp_print_string("\n");

    let buffer :: [uint8 : 16u];
    rand_bytes(@buffer as ptr[uint8], 16u);

    return 0;
}
