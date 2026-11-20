// Cryptographically secure random number generator!
func rand_bytes :: (buf :: ptr[uint8], N :: uint32) -> void {
    let rand_source :: PP_File = pp_file_open("/dev/urandom", PP_File_OpenMode::PP_FOPEN_READ_ONLY);
    while (N as int32 > 0) {
        let read_bytes :: uint64 = pp_file_read(rand_source, buf, N as uint64);
        N -= read_bytes as uint32;
    }
}