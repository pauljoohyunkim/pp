# 01 – Getting Started

## What is PP?
PP is a statically typed programming language with support for const correctness, references, structs, arrays and lists.

## Quick Start
```bash
make pplang
./bin/pplang path/to/file.pp
```

## Basic Program
```pp
let x :: int32 = 42;
func main :: () -> int32 {
    return x;
}
```

## Features
- Primitive types: `int32`, `float`, `bool`, `void`
- Modifiers: `const`, `&`
- User types: `typedef`, `newtype`, `struct`
- Control flow: `if`, `while`, `for`
- Expressions with full operator precedence

