handle File :: int32 drop pp_demo_close;

extern func pp_demo_open_write :: () -> File;
extern func pp_demo_open_read  :: () -> File;
extern func pp_demo_write      :: (fd :: int32, text :: const string&) -> int32;
extern func pp_demo_read       :: (fd :: int32, buf :: ptr[uint8], len :: uint64) -> int32;
extern func pp_demo_print      :: (buf :: ptr[const uint8], len :: uint64) -> void;
extern func pp_demo_close      :: (fd :: int32) -> void;
extern func pp_demo_close_count :: () -> int32;

// Write "12345678". The handle is dropped when this function returns.
func write_file :: () -> int32 {
    let f :: File = pp_demo_open_write();
    return pp_demo_write(f as int32, "12345678");
}

// Read it back into a fixed array and print it. Again the handle is dropped at
// the end of the function, on the early-return path and on the normal one.
func read_file :: () -> int32 {
    let f :: File = pp_demo_open_read();
    let buf :: [uint8 : 8u] = [uint8 : 8u; 0uss, 0uss, 0uss, 0uss, 0uss, 0uss, 0uss, 0uss];

    let n :: int32 = pp_demo_read(f as int32, @buf as ptr[uint8], 8u as uint64);
    if (n != 8) {
        return 1;
    }

    pp_demo_print(@buf as ptr[const uint8], 8u as uint64);
    return 0;
}

func main :: () -> int32 {
    if (write_file() != 0) {
        return 1;
    }
    if (read_file() != 0) {
        return 2;
    }

    // Both handles have been dropped exactly once.
    if (pp_demo_close_count() != 2) {
        return 3;
    }

    return 0;
}
