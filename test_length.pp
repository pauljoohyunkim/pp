func main :: () -> void {
    let arr :: [int32 : 5u];
    let len_arr :: uint32 = #arr;
    // len_arr should be 5 compile time

    let lst :: [int32];
    let len_lst :: uint32 = #lst;
    // len_lst should be runtime length
}
