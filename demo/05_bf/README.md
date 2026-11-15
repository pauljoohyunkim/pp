# 05_bf – Brainfuck via PPI Demo

Demonstrates using a PPI pre-lexer to embed Brainfuck code in PP.

## Files
* `main.pp` – reads and runs Brainfuck code via PPI
* `brainfuck.pp` – Brainfuck runtime primitives
* `brainfuck.ppi` – PPI definition for Brainfuck syntax
* `Makefile` – builds `bf_demo`

## Build

```bash
make
```

The Makefile passes `--ppi` for pp_io, pp_runtime, and brainfuck.ppi.

## Run

The demo reads Brainfuck code from stdin at runtime.

### Interactive
```bash
./bf_demo
```
Paste Brainfuck source, then send EOF with Ctrl+D.

Example – Hello World:
```bash
$ ./bf_demo
++++++++[>++++[>++>+++>+++>+<<<<-]>+>+>->>+[<]<-]>>.>---.+++++++..+++.>>.<-.<.+++.------.--------.>>+.>++.
^D
Hello World!
```

### Via file / pipe
Save code to a file, e.g. `brainfuck.b`, then:
```bash
cat brainfuck.b | ./bf_demo
```

You can test with a known program, e.g. Sierpinski triangle:

```
++++++++[>+>++++<<-]>++>>+<[-[>>+<<-]+>>]>+[
    -<<<[
        ->[+[-]+>++>>>-<<]<[<]>>++++++[<<+++++>>-]+<<++.[-]<<
    ]>.>+[>>]>+
]
```

Provide the code to the program as per its input interface.

## Clean

```bash
make clean
```
