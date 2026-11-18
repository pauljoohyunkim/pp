handle File :: int32 drop pp_demo_close;
handle Res  :: ptr[void] drop pp_demo_free;

extern func pp_demo_open :: () -> File;
extern func pp_demo_close :: (fd :: int32) -> void;
extern func pp_demo_close_count :: () -> int32;
extern func pp_demo_null_ptr :: () -> Res;
extern func pp_demo_free :: (p :: ptr[void]) -> void;
extern func pp_demo_free_count :: () -> int32;

func consume :: (f :: File) -> void {
    return;
}

func scoped :: () -> void {
    let h :: File = pp_demo_open();
    return;
}

func make :: () -> File {
    let h :: File = pp_demo_open();
    return h;
}

func early :: (flag :: bool) -> int32 {
    let h :: File = pp_demo_open();
    if (flag) {
        consume(h);
        return 1;
    }
    return 0;
}

func loop_once :: () -> void {
    let h :: File = pp_demo_open();
    return;
}

func run :: () -> int32 {
    scoped();
    if (pp_demo_close_count() != 1) {
        return 1;
    }

    consume(pp_demo_open());
    if (pp_demo_close_count() != 2) {
        return 2;
    }

    let h :: File = pp_demo_open();
    consume(h);
    if (pp_demo_close_count() != 3) {
        return 3;
    }

    let m :: File = make();
    if (pp_demo_close_count() != 3) {
        return 4;
    }

    m = pp_demo_open();
    if (pp_demo_close_count() != 4) {
        return 5;
    }

    let nul :: Res = pp_demo_null_ptr();
    if (pp_demo_free_count() != 0) {
        return 6;
    }

    let e :: int32 = early(true);
    if (e != 1 || pp_demo_close_count() != 5) {
        return 7;
    }

    let f :: int32 = early(false);
    if (f != 0 || pp_demo_close_count() != 6) {
        return 8;
    }

    let i :: int32 = 0;
    while (i < 3) {
        loop_once();
        i = i + 1;
    }
    if (pp_demo_close_count() != 9) {
        return 9;
    }

    return 0;
}

func main :: () -> int32 {
    let rc :: int32 = run();
    if (rc != 0) {
        return rc;
    }
    // After run() returned, its remaining owned handle (m) is dropped once,
    // the moved-from handle (h) is not dropped, and the null pointer handle is skipped.
    if (pp_demo_close_count() != 10) {
        return 10;
    }
    if (pp_demo_free_count() != 0) {
        return 11;
    }
    return 0;
}
