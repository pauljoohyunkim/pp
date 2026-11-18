# `handle` — design decisions

A `handle` is a PP type that owns an operating-system or heap resource and is released automatically by a
**bound drop function** when its scope ends. This note records the decisions, the rationale behind them, and the
alternatives that were rejected, so that the design does not have to be re-derived.

Status: implemented (parser, semantic analyzer, code generator, tests, demo). User-facing documentation:
`docs/language/09-handles.md`. History and verification checklist: `HANDLE_CHECKLIST.md`.

## Syntax

```pp
handle File   :: int32     drop pp_file_close;
handle Buffer :: ptr[void] drop EVP_free;
```

```ebnf
handle_decl ::= "handle" identifier "::" type "drop" identifier ";"
```

`handle` and `drop` are reserved keywords. A handle declaration is a top-level declaration in the same family as
`newtype` and `typedef`: it introduces a type name.

A handle is a nominal type over its underlying type, exactly like a newtype, with one extra piece of state: the
drop function. Internally it is a `PP_TK_Newtype` whose `drop_func_name`/`drop_symbol` are set; a newtype with a
non-NULL `drop_func_name` **is** a handle.

## The one invariant

> A handle is a scope-bound, non-transferable resource.
> It is born in a `let`, is shared only by reference, and dies with the scope that owns it.

Every rule below follows from that sentence. There is no move, no explicit release, and no ownership flag at
runtime: the compiler knows statically which single variable owns a handle.

## The three roles a handle can play

| Role | Form | Notes |
|---|---|---|
| **Birth** | `let h :: File = <call returning File>;` | initializer is **required**; the only way to obtain a handle |
| **Borrow** | parameter of type `File&` / `const File&` | the callee never drops; borrows can be forwarded |
| **Death** | end of the enclosing scope or block | automatic on every path: fallthrough, `return`, loop iteration end |

```pp
handle File :: int32 drop pp_demo_close;

extern func pp_demo_open :: () -> File;
extern func pp_demo_read :: (fd :: int32, buf :: ptr[uint8], len :: uint64) -> int32;

func read_file :: () -> int32 {            // birth, then death at the end of this function
    let f :: File = pp_demo_open();
    let buf :: [uint8 : 8u] = [uint8 : 8u; 0uss, 0uss, 0uss, 0uss, 0uss, 0uss, 0uss, 0uss];

    let n :: int32 = pp_demo_read(f as int32, @buf as ptr[uint8], 8u as uint64);
    if (n != 8) {
        return 1;                          // the handle is dropped on this path too
    }
    return 0;                              // and on this one
}
```

## Decisions and why

### 1. Handles extend `newtype`; they are not a new kind of type

A handle is a nominal type. Everything that already works for newtypes (casting down, machine-type resolution,
debug info) keeps working unchanged; the only addition is the drop function and the ownership rules layered on
top. A separate `PP_TK_Handle` would have duplicated that machinery for no benefit.

### 2. Ownership is implicit, static, and scope-based — no runtime bookkeeping

The compiler tracks nothing at runtime. A handle variable has exactly one owner (the binding that declared it)
and one deterministic place where it dies (the end of its scope). Consequences:

* no per-variable `i1` "still owns it" flag,
* no moved-from state,
* no null sentinel to mean "already released" (fd `0` is a valid descriptor, so a sentinel is unsound),
* no aliasing to reason about: a handle cannot be copied, so the drop can never double-free.

Dropping happens at scope exit, on every return path, once per loop iteration, and in reverse declaration order
across nesting levels.

### 3. Sharing is by reference only — handles are never passed by value

A bare handle parameter is a compile error. A borrow is spelled `File&` / `const File&` and never transfers
ownership:

```pp
func inspect :: (f :: const File&) -> int32 { return 0; }   // fine: borrow, callee drops nothing
func consume :: (f :: File) -> int32 { ... }               // error: handle passed by value
```

This removes the whole class of "was it moved or copied?" questions. If a C API genuinely needs to take
ownership, write a small PP wrapper around it rather than reintroducing moves.

### 4. Handles can be cast *down*, never *up*

`h as int32` is allowed: it reads the underlying value to hand it to a C function and transfers nothing.
Casting *to* a handle is rejected — a handle may only be born from a function that returns one, so the compiler
always sees the call that created the resource.

### 5. The drop function must exist, and must be exactly one of two shapes

The compiler cannot synthesise a release, so the drop function is mandatory and is resolved at compile time:

* `func name :: (underlying) -> void { ... }` — a PP definition (PP requires bodies on `func`), or
* `extern func name :: (underlying) -> void;` — implemented in C and resolved by the linker.

Anything else is rejected: a lambda, an operator, a `func[...]`-typed variable holding a value. The declaration is resolved in semantic pass 2, so the drop function may be
declared before or after the `handle` declaration; only a signature that *mentions* the handle type has to come
after it. The parameter must be the underlying value, never a handle (otherwise a drop for a different
nominal type over the same representation would be accepted). A missing `extern` implementation is a link-time
error, not a semantic error — the compiler cannot know what will be linked.

### 6. The drop function is compiler-owned; source cannot call it

Calling the drop function directly is an error, as is storing it in a `func[...]` variable or otherwise using it
as a value. Exactly one release per handle, performed by the compiler. Early release is expressed with an inner
block, never with an explicit call.

### 7. Reassignment is allowed

```pp
let h :: File = pp_open();
h = pp_open();      // old descriptor released, then the new one is stored
```

The alternative — forbidding rebinding — was considered and rejected: it would have hidden a code generator gap
behind a language restriction. Reassignment to an owning lvalue always releases the old value first, so the
invariant "an owning lvalue holds a live value" holds for handles, strings and lists alike.

### 8. Where handles may not appear (v1 restrictions)

| Position | Allowed? |
|---|---|
| Local variable, `let` with initializer | yes |
| Function parameter as `H&` / `const H&` | yes |
| Return type `-> H` (from a call) | yes |
| Bare `H` parameter or argument | no |
| Global variable | no |
| Struct field | no |
| Collection element (`[H]`, `[H : n]`) | no |
| Lambda parameter, return type, or local | no |
| Cast target | no |
| Drop function parameter | no |

The first six restrictions exist because ownership is tracked per *variable*; a handle stored inside an aggregate
or a global would need a lifetime story the language does not have yet. Lambdas are excluded because their
signatures are checked separately from function signatures.

## Rejected alternatives

| Alternative | Why not |
|---|---|
| Runtime ownership flags + real moves (by-value params, moved-from state) | Large amount of codegen machinery and a new runtime concept for something the scope already answers statically. Also made by-value handle parameters meaningful, which reintroduces aliasing questions. |
| Static "birth move" only (handle may be moved once) | Half a move system: still needs flags to know whether the move happened, still allows aliasing bugs. |
| Sentinel value (e.g. `-1`) meaning "released" | Not sound: `0` is a valid file descriptor, and no sentinel exists for every underlying type. |
| Banning reassignment of a handle | Restricts the language to avoid fixing one code generator branch. Reassignment is a normal resource operation (retry, reopen). |
| Requiring the drop function to have a PP body | Would exclude every C-facing handle, which is the primary use case. `extern func` is the norm. |
| Copy/move elision style "return the local" | A returned handle would need ownership transfer or a second live owner. `return <call returning handle>` is the supported form; returning a handle *local* is an error. |

## Compiler contract (implementation map)

Semantic analyzer (`src/pp_semantic_analyzer.c`):

* `pp_type_contains_handle` — reference-opaque; used wherever ownership is the question (return, by-value args).
* `pp_type_mentions_handle` — reference-transparent; used for bans in underlying types, struct fields,
  collection elements, drop-function parameters.
* `pp_validate_signature_handles` — applied by all six signature builders (function, operator, lambda, extern
  function, extern operator, extern lambda).
* `pp_is_handle_drop_function` — name scan over the type table; blocks direct calls and value use, so the drop
  function cannot be called by source at all.
* Pass 1 registers the handle name and validates the underlying recursively once. Pass 2 re-runs those guards
  after struct layouts are complete, then resolves the drop symbol.

Code generator (`src/pp_code_generator.c`):

* The declaration itself emits no code.
* `pp_code_generator_emit_drop` does **not** unwrap a handle newtype (it does unwrap plain ones).
* `pp_code_generator_gen_drop_body` has a handle branch: bitcast the `i8*` parameter to the underlying type,
  load the value, skip the call when the underlying lowers to a pointer and the value is null, otherwise call the
  bound drop function.
* Drops are pushed per scope and emitted at scope exit, before every `return`, and at every block end — reverse
  order, no flags.

## Tests

* `tests/semantictest.cpp` — 39 handle tests (`expect_handle_ok` / `expect_handle_error` helpers).
* `tests/codegentest.cpp` — 11 handle codegen tests: drop via bound function, drop on every return path, loop
  iteration, borrow produces no drop, pointer null skip, reassignment drops the old value, declaration emits no
  code, `return <call>` is not dropped in the callee.
* `demo/06_handle/` — writes `12345678` through a write handle, reads it back through a read handle into
  `[uint8 : 8u]`, prints it, asserts both handles were closed exactly once. Valgrind clean.
