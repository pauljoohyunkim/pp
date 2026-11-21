# PP Language Support for VS Code

Syntax highlighting for the [PP programming language](https://github.com/pauljoohyunkim/pp).

## Features

- Syntax highlighting for `.pp` and `.ppi` files
- Bracket matching and auto-closing
- Comment toggling (`//`)
- Code folding support

## Installation

### From VS Code Marketplace (when published)
Search for "PP Language" in the Extensions view.

### Manual Installation (Development)

1. Copy the `vscode-pp-extension` folder to your VS Code extensions directory:
   - **Linux**: `~/.vscode/extensions/`
   - **macOS**: `~/.vscode/extensions/`
   - **Windows**: `%USERPROFILE%\.vscode\extensions\`

2. Restart VS Code.

### Development Installation (Extension Host)

1. Open the `vscode-pp-extension` folder in VS Code
2. Press `F5` to launch an Extension Development Host window
3. Open a `.pp` file to test syntax highlighting

## Language Features Highlighted

- **Keywords**: `func`, `lambda`, `operator`, `extern`, `struct`, `typedef`, `newtype`, `handle`, `let`, `const`, `if`, `else`, `while`, `for`, `return`, `break`, `continue`, `drop`, `as`
- **Types**: `int8/16/32/64/128`, `uint8/16/32/64/128`, `float`, `bool`, `void`, `string`, `ptr`
- **Literals**: Integer (with suffixes), Float, Boolean, String (`"..."` and `@"..."`)
- **Operators**: `+`, `-`, `*`, `/`, `%`, `=`, `==`, `!=`, `<`, `>`, `<=`, `>=`, `&&`, `||`, `!`, `->`, `=>`, `::`, `@`, `$`, `#`, `.`
- **Built-in functions**: `list_push`, `list_pop`, `list_clear`
- **Standard library** (`lib/pp_std`): `pp_print_*`, `pp_input_char`, `pp_abort`, `pp_fd_*` — regular library functions, not language builtins

## License

MIT