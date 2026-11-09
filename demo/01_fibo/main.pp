extern lambda fibo :: (x :: uint32) -> uint32;
extern func pp_std_print_uint64 :: (str :: uint64) -> void;
extern func pp_std_print_string :: (str :: const string&) -> void;

func main :: () -> int32 {
    let newline :: const string = "\n";
    for (let i :: int32 = 1; i <= 10; i += 1) {
        let fi :: uint32 = fibo(i as uint32);
        {
            pp_std_print_uint64(fi as uint64);
            pp_std_print_string(newline);
        }
    }

    return 0;
}
