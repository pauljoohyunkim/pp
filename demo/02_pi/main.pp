func main :: () -> int32 {
    let sum :: float = 0.0;

    for (let i :: int32 = 1; i < 1000000; i += 1) {
        let denom :: int32 = 2 * i - 1;
        let parity :: bool = i % 2 != 0;
        if (parity) {
            sum += 1.0 / (denom as float);
        } else {
            sum -= 1.0 / (denom as float);
        }
    }

    pp_std_print_float(sum * 4.0);
    pp_std_print_string("\n");

    return 0;
}
