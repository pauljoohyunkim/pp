# PP

PP is a general purpose programming language that is built on three philosophies:
 * Dumb-sounding
 * Simple yet usable
 * Explicit and safe, but not requiring programmers to fight against the grammar.

You can think of this language as a amalgamation of the simplicity of the C language, strong type guarantee of rust language, functional programming type signature of Haskell language, and a bit of unicorn fart for good luck.

This repository is a compiler attempted in C with LLVM backend.

## Cloning Repo
To clone this repo, you run the following command
```
git clone [https/ssh repo url]

# Submodule pp_std requires a separate step when cloning
git submodule update --init --recursive
```

## Building Compiler

You need the following tools to build this compiler:

* C compiler
  * By default, configured to be `clang` but you can use `gcc`
* LLVM Library
* Pkg-Config
  * Technically not needed, I use it to load LLVM compilation and linker flags.
* Make
  * Technically not needed, but it makes the compilation process easier

### Compilation guide
```
# Assuming you have all the prerequisites
make clean && make

# If you want to use gcc instead
make clean && make CC=gcc
```

You should be able to find the binary in `bin/pplang`.

You can optionally build unit test (but you will need gtest)
```
# Assuming you have all the prerequisites
make unittest

# If you want to use g++ instead
make clean && make unittest CC=gcc CXX=g++
```
You should find the unit test binary at `bin/unittest`.

## Quickstart

```bash
# Build
make && make -j

# Compile a single source to an executable
./bin/pplang -o hello hello.pp
./hello

# Compile to object file
./bin/pplang -c -o hello.o hello.pp

# Link multiple sources
./bin/pplang a.pp b.pp -o program
./program

# Verbose output
./bin/pplang -v source.pp -o out
```

Options:
* `-h` Show help
* `-v` Verbose output
* `-c` Compile to object file
* `-S` Compile to assembly file
* `--emit-llvm` Emit LLVM IR
* `-o <file>` Output filename (default: `a.out`, or `a.s` with `-S`, or `a.ll` with `--emit-llvm`)
* `-O <level>` Optimization level: `0`, `1`, `2`, `3` (size opts TODO)

### Demo
Demos are available in [demo](demo/):
```bash
cd demo/01_fibo
make
./fibo_demo
```

### Language guide
Look into [docs](docs/) for simple documentation.

## PP Standard Library
A separate repo called [pp_std](https://github.com/pauljoohyunkim/pp_std) is maintained for exposing functions requiring libc.
Currently in development.

## Development

Might want to look into [notes](notes/) for an overview.

For those who absolutely want a summary though......

### Introduction

This repository is a C project for an LLVM-based compiler.

There are four large steps:

1. Lexical Analyzer (Lexer)
  * Takes the code string and breaks it into tokens, such as "+", "hello", ",", etc.
  * Produces a list of tokens
2. Syntactic Analyzer (Parser)
  * Parses the list of tokens into an abstract syntax tree.
    * For example, "1 + 2 * 3" becomes `Binary(Plus, 1, Binary(Star, 2, 3))`
3. Semantic Analyzer / Type Checker
  * Checks types of each expression, constness, whether or not there are dangling references.
  * Quite headache inducing...
4. Code Generator (LLVM Backend)
  * Let the LLVM gods take over......
  * Only if they were well-documented...
  * LLVM C API
    * Natively LLVM API are in C++, but some amazing people wrote them C bindings!

### Syntactic Grammar

Syntactic grammar is outlined in [bnf.md](notes/bnf.md)

### Ownership Model

For memory safety, the coding standards are described in [general_coding_rule.md](notes/general_coding_rule.md),
and ownership of objects are described in [ownership_model.md](notes/ownership_model.md).

### CZ: The Prototype

This project was ported from my prototype project [CZ](https://github.com/pauljoohyunkim/cz).

This was a predecessor to this project with similar structure, but it did not yet have support for arrays/lists,
and may have been a bit fragile. It could be worth having a look though.

The initial few commits of this repo is basically porting over from CZ.

### LLM Usage Disclaimer

I have used AI extensively in this project, but most of the logic were handwritten (in CZ) and made robust with LLM.
My main use cases were:
* Porting from CZ
* Static Analysis (dead-code elimination, etc.)
* Unit test generation (I am not writing that by myself...)
* Dealing with LLVM querks.
* Mass refactoring
* User documentation at [docs](docs/), contributor's notes at [dev-docs](dev-docs/) as well as some of my personal notes during development in [notes](notes/) for book keeping.
I still kept the discipline of reading through the changes that it made (at least briefly!)

I am **NOT** a fan of having to pay for AI services, hence all usage of AI was limited to open-weight models.
All models were either self-hosted, or available at free tier via Ollama cloud.

Here is a list of models that I have used.
* Muse-Glimmer 30b
  * Used this extensively for agentic AI.
  * Personal favorite due to excellent tooling success rate
  * Self-hosted on a desktop with RTX 4070 Ti Super with 64 Gb of system ram for spillover (Context length set at 64k or 128k, depending on the task.)
* Gemma4 31b
  * Used this extensively for agentic AI as well.
  * Ollama Cloud
* Nemotron 3 Super 120b & Nemotron 3 Ultra 550b
  * Used this for commenting and checking over basic logic.
  * Ollama Cloud
  * Nemotron 3 Super was not adequate in terms of logical capability for this project, which is expected as this is a very complicated project.
  * Nemotron 3 Ultra was not as cost efficient for agentic coding for this project.
* Beam
  * Used this for porting over code generator from CZ
  * Ollama Cloud
  * Free usage due to launch event
* GPT OSS 120b
  * Used this for porting over a bit of semantic analyzer
  * Ollama Cloud
  * While super fast, it kept trying to call "search" tool even though it was not part of my agentic coding tool, hence I could not continue with it.

Agentic coding tool of choice was [Pi Coding Agent](https://pi.dev/). You can see the relevant files in [.pi](.pi)

### Third-party Licenses

This project uses LLVM as its backend. See [THIRD-PARTY-LICENSES](THIRD-PARTY-LICENSES) for license notices.

## Custom operators (v1)
- Use const-reference parameters only, non-reference return.
- Operators are global functions with operator-specific mangling keys.
