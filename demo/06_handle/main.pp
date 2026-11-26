func main :: () -> int32 {
    let permission :: const int32 = 420;    // 0644

    // "AABBCCDD"
    let data :: const [uint8 : 9u] = @"AABBCCDD";

    {
        let flags :: PP_FD_OpenFlags = PP_FD_OpenFlags::Write | PP_FD_OpenFlags::Create | PP_FD_OpenFlags::Truncate;
        let fd :: PP_FD = pp_fd_open("test.txt", flags, permission);
        let write_val :: int64 = pp_fd_write(fd, @data as ptr[const uint8], 8uz) as int64;
        pp_print_string("Wrote ");
        pp_print_uint64(write_val as uint64);
        pp_print_string(" bytes.\n");
    }

    return 0;
}
