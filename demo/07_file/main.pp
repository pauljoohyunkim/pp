func main :: () -> int32 {
    // "AABBCCDD"
    let data :: const [uint8 : 9u] = @"AABBCCDD";

    {
        let f :: PP_File = pp_file_open("file_test.txt", PP_File_OpenMode::PP_FOPEN_WRITE_ONLY);
        let written :: uint64 = pp_file_write(f, @data as ptr[const uint8], 8ul);
        pp_print_string("Wrote ");
        pp_print_uint64(written);
        pp_print_string(" bytes.\n");
    }

    {
        let f :: PP_File = pp_file_open("file_test.txt", PP_File_OpenMode::PP_FOPEN_READ_ONLY);
        // Zero filled so the bytes read form a valid C string for pp_print_c_str.
        let buf :: [uint8 : 16u] = [uint8 : 16u; 0uss];
        let read_bytes :: uint64 = pp_file_read(f, @buf as ptr[uint8], 8ul);
        pp_print_string("Read ");
        pp_print_uint64(read_bytes);
        pp_print_string(" bytes: ");
        pp_print_c_str(@buf as ptr[const uint8]);
        pp_print_string("\n");
    }

    return 0;
}
