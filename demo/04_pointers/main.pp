extern func pp_std_print_string :: (str :: string) -> void;
extern func pp_std_print_uint64 :: (val :: uint64) -> void;

func main :: () -> int32 {
    let arr :: [uint16 : 4u] = [uint16 : 4u; 1us, 2us, 3us, 4us];

    pp_std_print_string("Before pointer manipulation\n");
    pp_std_print_uint64(arr[0u] as uint64);
    pp_std_print_uint64(arr[1u] as uint64);
    pp_std_print_uint64(arr[2u] as uint64);
    pp_std_print_uint64(arr[3u] as uint64);
    pp_std_print_string("\n");

    let p0 :: ptr[uint16] = @arr[0u];
    let p1 :: ptr[uint16] = @arr[1u];
    let p2 :: ptr[uint16] = @arr[2u];
    let p3 :: ptr[uint16] = @arr[3u];

    $p0 = $p0 * 2us;
    $p1 = $p1 * 2us;
    $p2 = $p2 * 2us;
    $p3 = $p3 * 2us;

    pp_std_print_string("After doubling via pointer operation:\n");
    pp_std_print_uint64($p0 as uint64);
    pp_std_print_uint64($p1 as uint64);
    pp_std_print_uint64($p2 as uint64);
    pp_std_print_uint64($p3 as uint64);
    pp_std_print_string("\n");

    return 0;
}
