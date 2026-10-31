# 16 – Known Limitations & Temporary Restrictions

These items are emitted as `#warning` during build and affect current user-visible semantics.

## List support – move-only v1
* List copy semantics are temporarily banned for move-only v1. Remove this ban and implement copy/drop when value semantics are desired.
* By-value List parameters are disallowed; use a reference.
* `emit_drop` currently only handles direct List types. Structs containing List are not recursively dropped yet. Add per-type drop_T generation before allowing structs with lists.
* List access currently assumes data is allocated; uninitialized lists will segfault.
* List initializer is runtime-only; globals not supported yet.

## General
* Warnings about const/reference qualifier discards are compiler warnings, not user errors.
* Temporary file handling uses `mkstemp`; `tmpnam` is no longer used.
* Multi-file linking and `-c` object emission are now supported; in-memory emission path is implemented.

These limitations will be lifted in future releases.
