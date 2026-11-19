# Enums and Flags

Demonstrates the `enum` and `flag` declarations:

* `enum Mode` gives `Read`, `Write` and explicit `Append = 5`;
* `flag Perms` combines bits with `|`, `~`, `|=`, `&=` and `^=`;
* `Perms::none` is used for the bit test `(p & Perms::Sync) != Perms::none`;
* an enum constant is used as an array size (`[uint8 : Mode::Append]`);
* enum and flag values are cast to integers for printing.

Build and run:

```bash
make
./enums_flags_demo
```
