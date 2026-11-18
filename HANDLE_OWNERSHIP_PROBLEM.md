# Handle ownership: by-value parameters, moved-from state, and adoption

## Problem

PP is gaining a `handle` declaration:

```pp
handle File :: int32 drop pp_std_file_close;
handle Evp  :: ptr[void] drop EVP_free;
```

Semantics already implemented and committed: the handle is a nominal newtype (`PP_TK_Newtype` with
`drop_func_name` / `drop_symbol`), it needs drop, it is non-copyable, non-trivially-copyable, banned in
globals and in lambdas. Code generation is not started.

Four ownership questions must be settled before any codegen work, because each changes the language:

1. Are **by-value handle parameters** allowed, or must handles always be passed by reference?
2. Is **cast-to-handle** (`fd as File`) allowed as the construction path?
3. What is the **moved-from state** of a handle variable after ownership leaves it?
4. Are **handles in struct fields and collections** allowed in v1?

## Context

* Compiler: C99, LLVM 23 C API, pipeline lexer → parser → semantic analyzer → codegen.
* Handle type: `PP_TK_Newtype` + `drop_func_name`; `pp_type_is_handle()` predicate in `src/pp_type.c`.
* `pp_type_needs_drop(handle) == true`; `pp_type_is_copyable(handle) == false`;
  `pp_type_is_trivially_copyable(handle) == false`.
* Semantic bans already present: handle-typed globals, handle in lambda signatures, handle-typed locals
  inside lambdas, underlying containing a reference or another handle.
* Codegen has no move instruction. Ownership rules are approximated by:
  * a codegen-time `drop_stack` of `(alloca, type)` pairs (`src/pp_code_generator.c:570`);
  * `pp_code_generator_emit_scope_drops` flushing it in reverse (`:556`), also called inline at each
    `return` site (`:2156`, `:2164`);
  * `pp_code_generator_emit_drop` (`:205`), which is a thin wrapper: `pp_code_generator_ensure_drop_fn`
    always creates a `pp.drop.<typeptr>` helper with signature `void(i8*)`, so the real drop logic lives in
    `pp_code_generator_gen_drop_body` (`:4485`), which currently handles List / Array / Struct / String only;
  * `return-as-move` (`:2178`), which builds the "empty" value with three `LLVMBuildInsertValue` calls on
    the list struct — hard-coded to the list layout.
* Call sites (`pp_code_generator_generate_lvalue_function_call`, ~`:130-200`):
  * const-reference param + lvalue arg → address passed directly;
  * const-reference param + rvalue arg → materialized temp, dropped after the call;
  * owning by-value param + **lvalue** arg → `pp_code_generator_emit_copy(tmp, &src, param_type)`;
  * owning by-value param + **rvalue** arg → temp, passed, then dropped after the call.
  The callee pushes every non-reference, `needs_drop` parameter onto its own drop stack (`:1779`).

## Investigation

Concrete double-drop paths that exist today for any drop-needing type with these shapes, and would become
double-`close()` for handles:

1. `consume(h)` where `consume :: (f :: File) -> void` — caller copies fd into a temp, callee drops the
   parameter, caller drops `h` at scope exit. Two closes.
2. `consume(open_file())` — caller drops the owning temporary after the call, callee drops the parameter.
   Two closes. (This looks like a pre-existing double-free for lists as well; unverified.)
3. `func make :: () -> File { let h :: File; return h; }` — return-as-move currently emits
   `LLVMBuildInsertValue` on the lowered type of the handle (`i32`, `ptr`, or a struct). For `i32` that is an
   LLVM assertion, not a miscompile.
4. `func dup :: (h :: const File&) -> File { return h; }` — returning a borrow by value is accepted by the
   semantic layer (decay makes `File&` and `File` equal) and would copy the raw fd.
5. `let a :: File; let b :: File = a as File;` — the cast yields an RVALUE, so the existing
   "copy from lvalue" guard does not fire; both `a` and `b` are dropped.
6. `struct S { f :: File; }` with `S{ .f = h }`, and `list_push(lst, h)` — copies into storage, `h` remains
   owned by its scope.

Additional structural issue found while reading: the `drop_stack` is a **codegen-time linear stack**, not
path-sensitive. Drops are emitted inline at each site that flushes them, so a `return` inside an `if` is
runtime-correct. But removing an entry from the stack at a *statement* (e.g. after a by-value move at a call
site) is not path-safe: `if (c) { consume(h); }` must still drop `h` on the false path, yet a compile-time
removal would suppress the drop on both paths (leak), while leaving it in place double-closes on the true
path.

## Hypothesis

* **Cast-to-handle is unnecessary in v1.** Handles can enter the program through a call returning the handle
  type (`extern func pp_open :: (...) -> File;`), which is ABI-compatible because a handle lowers to its
  underlying. The reverse cast (`h as int32`) should stay: it is a read of the raw value and transfers no
  ownership. If adoption is needed later, express it as a second binding symmetric with `drop`
  (`handle File :: int32 drop close_fd adopt from_fd;`), not as a general cast.
* **By-value parameters are the crux.** Banning them makes v1 trivially sound but removes early close
  entirely: with only `const File&` / `File&`, a callee can never consume a handle, so a handle can only be
  destroyed at the scope that acquired it. Both C++ (`consume(std::move(h))` + a move ctor) and Rust
  (`fn consume(f: File)` + move invalidation) keep by-value parameters precisely to support early close.
* **Moved-from state should follow Rust, not C++**, for a first implementation: a bitwise transfer plus
  suppressing the source's drop. C++-style sentinels require a per-type "empty" value, and PP has no `-1`
  style syntax in the declaration yet; a sentinel of `0` for an `int32` fd would silently skip dropping fd 0.
* **Struct fields and collections should be banned in v1** unless storage moves are implemented, since
  `S{ .f = h }` and `list_push(lst, h)` are copies out of a still-owned local.

## Options

### Option A — v1 shortcut: references only, no adoption cast
* Ban by-value handle parameters (error pointing at the future feature).
* Allow `-> File` returns and `const File&` / `File&` parameters.
* Drop cast-to-handle; keep `h as int32`.
* Ban handles in struct fields and collections.
* Fix return-as-move for non-list types (single site, per-return-site skip of the moved entry — path-safe
  because drops are emitted inline at each return).
* Cost: low. Limitation: no early close, no ownership transfer into a callee.

### Option B — Rust-lite: real moves for returns and by-value arguments
* Keep by-value parameters; a by-value argument is a move.
* Relinquish = suppress the drop for the moved entry.
  * For `return local` this is per-return-site and path-safe.
  * For a by-value argument at a statement, a **runtime drop flag** per handle variable is needed
    (a bool alloca checked by the drop code), or moves must be restricted to tail positions.
* Drop cast-to-handle; keep `h as int32`.
* Ban handles in struct fields and collections for now.
* Cost: medium (drop-flag machinery in `emit_scope_drops` / `gen_drop_body`, call-site move marking).
  Benefit: matches C++/Rust capability, supports `close(self)`.

### Option C — C++-style sentinel
* Keep by-value parameters; moving stores a sentinel into the source and the drop helper skips the sentinel.
* Requires a declared invalid value per handle (`drop f invalid -1`), otherwise `0` is assumed and legit fd 0
  is silently not closed.
* Cost: grammar + semantic + codegen changes; adds a per-type invariant authors must get right.

### Option D — full parity with Option B plus storage moves
* Extend moves to struct field initialization and `list_push`, none of which exist today for any owning type
  in a move sense (lists rely on deep copy).
* Cost: high, and it implies first settling whether containers should deep-copy or move their elements.

## Questions for the expert

1. Which option best balances soundness against implementation cost for a C/LLVM-API compiler whose existing
   ownership model is "drop stack + deep copy" and has no move instruction?
2. If Option B is chosen: is a per-variable runtime drop flag the right mechanism, or is there a cheaper
   path-insensitive approximation that is still sound (e.g. requiring moves to be the last use in a block,
   converting everything else into errors)?
3. Is suppressing the drop at the `return` site sufficient, given that `pp_code_generator_emit_scope_drops`
   is also invoked from branch/merge paths (`:1803`)?
4. For a nominal handle over a trivially copyable underlying (`int32`), is there any argument for keeping
   `pp_type_is_trivially_copyable == false` beyond preventing bitwise copies in container fast paths?
5. Independent of handles: is the "owning rvalue argument is dropped after the call while the callee also
   drops the parameter" path (call-site line ~167 and callee `:1779`) a known/accepted double-drop for lists
   and other owning types, or a bug that should be fixed first?

## Next steps for deeper analysis

* Confirm the intended semantics of the existing rvalue-owning-argument path for lists (question 5) before
  reusing any of that machinery for handles.
* Decide between Option A and Option B; A implies documenting "handles are destroyed only at scope exit"
  as a language limitation.
* Once decided, the codegen delta is small either way: top-level dispatch abort fix, handle branch in
  `gen_drop_body`, and a non-list return-as-move.
