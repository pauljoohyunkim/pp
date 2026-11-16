extern lambda fibo :: (x :: uint32) -> uint32;

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
