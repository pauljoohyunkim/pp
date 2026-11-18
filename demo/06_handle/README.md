# 06_handle

Demonstrates `handle` declarations: a nominal owning type with a statically bound
drop function.

```pp
handle File :: int32 drop pp_demo_close;
handle Res  :: ptr[void] drop pp_demo_free;
```

`handle_demo.c` is a test double that counts releases, so the demo asserts that:

* a handle is dropped exactly once when its scope ends,
* a by-value argument transfers ownership to the callee,
* returning a local transfers ownership to the caller,
* reassignment drops the previous value,
* a moved-from variable is not dropped again,
* a null pointer handle skips the drop.

Build and run:

```bash
make
./06_handle
```

Exit code `0` means every count matched.
