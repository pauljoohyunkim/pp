extern lambda fibo :: (x :: uint32) -> uint32;
extern func pp_std_print_uint64 :: (str :: uint64) -> void;
extern func pp_std_print_string :: (str :: string) -> void;
// extern func pp_std_print_string_ref :: (str :: const string&) -> void;
// Printing with reference to string is more performant,
// but it does not allow `pp_std_print_string_ref("asdf");`

func main :: () -> int32 {
    for (let i :: int32 = 1; i <= 10; i += 1) {
        let fi :: uint32 = fibo(i as uint32);
        {
            pp_std_print_uint64(fi as uint64);
            pp_std_print_string("\n");
        }
    }

    return 0;
}
