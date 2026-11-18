let O_RDONLY :: const int32 = 0;
let O_WRONLY :: const int32 = 1;
let O_RDWR :: const int32 = 2;
let O_CREAT :: const int32 = 64;
let O_TRUNC :: const int32 = 512;

func main :: () -> int32 {
    let permission :: const int32 = 420;    // 0644

    // "AABBCCDD"
    let data :: const [uint8 : 8u] = [uint8 : 8u; 65uss, 65uss, 66uss, 66uss, 67uss, 67uss, 68uss, 68uss];

    {
        let fd :: PP_STD_FD = pp_std_fd_open("test.txt", O_WRONLY | O_CREAT | O_TRUNC, permission);
        let write_val :: int32 = pp_std_fd_write(fd, @data as ptr[const uint8], 8u);
        pp_std_print_string("Wrote ");
        pp_std_print_uint64(write_val as uint64);
        pp_std_print_string(" bytes.\n");
    }

    return 0;
}
