# PP Global Statement Types

This document summarizes possible types of statements that can exist at a global scope.
Note that the examples of syntax can change during development.

## Variable Declaration
```
let x :: int32 = 1;
let y :: const int32 = 1;
let z :: float;
```
* Declares variable at a global scope.
* When initializer is not given, it defaults to zero, except if struct declaration includes default initializer, in which case that specific field will take the default value.
* Initializer must be compile-time. (No dependency on variables or order of declaration.)
* Reference cannot be declared at global scope.

## Constant Declaration
```
constant SOME_CONSTANT :: uint64 = 1ul;
constant OTHER :: float = 1.5;
constant FLAG :: bool = true;
```
* Declares a named compile-time value at global scope.
* Type must be a plain integer, `float`, or `bool` primitive (no `const`/reference modifier,
  no enum/flag/newtype/struct/array/list/pointer/string/handle/callable).
* Initializer is mandatory and must be constexpr and type-equal to the declared type.
* A constant has **no storage and no link-time symbol**. References are folded during codegen,
  which makes constants safe to declare in `.ppi` files shared by multiple translation units.
* Immutable, non-addressable, and usable in any expression (including lambda bodies).
* A constant initializer may reference only constants declared earlier in the source.
* An unsigned integer constant (`uint8`…`uint64`) may be used as an array size; it must be
  declared before the array type that uses it.

## Typedef & Newtype Declaration
```
typedef int32 int;
newtype int32 AGE;
```
* Typedef declares alias.
* Newtype declares a new type based on the previous one.
* One must check for circular declaration.
* No constness or reference is allowed in both the source and the dest.

## Struct Declaration
```
struct Point {
    x :: float,
    y :: float
}

struct Jacobian {
    x :: int32,
    y :: int32,
    z :: int32 = 1
}

struct Context {
    n :: int32&
}
```
* Declares a struct.
* Can optionally take default initializer.
    * Just like global variable declaration, initializer must be compile-time.
    * Reference field cannot take initializer.
        * Reference must be initialized at instantiation.

## Function & Lambda Declaration
```
lambda relu :: (x :: float) -> float {
    // ...
}

func get_counter :: () -> int32 {
    // ...
}

func waste_cpu_cycle :: (n :: int32) -> void {
    // ...
}

func waste_cpu_cycle2 :: () {
    // ...
}
```
* Declares lambda function and regular function.
* Input types must be given inside parentheses.
* If output type is void, it is optional to write it out.

