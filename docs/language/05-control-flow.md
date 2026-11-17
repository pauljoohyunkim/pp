# Control Flow

## If / Else

```pp
if (cond) {
    // then
} else {
    // else
}
```

`else if` is supported via chained `if` statements.

## While

```pp
while (cond) {
    // body
}
```

## For

```pp
for (init; cond; update) {
    // body
}
```

Init, cond, and update are all optional. Init can be a variable declaration or assignment.

## Blocks

```pp
{
    let x :: int32 = 1;
}
```

Blocks create a new scope.

## Return

```pp
func foo :: () -> int32 {
    return 42;
}
```

`return` exits the current function with an optional expression.

For functions with non-void return type, all control paths must return a value. The semantic analyzer enforces exhaustive returns.

## Break

```pp
while (cond) {
    if (should_exit) {
        break;
    }
}
```

`break` immediately exits the innermost enclosing `while` or `for` loop. It is a compile-time error to use `break` outside of a loop.

## Continue

```pp
for (; i < 10; i = i + 1) {
    if (i % 2 == 0) {
        continue;
    }
    // process odd i
}
```

`continue` skips the remainder of the current loop iteration and proceeds to the next iteration. In a `for` loop, this means the update expression is executed, then the condition is re-evaluated. In a `while` loop, the condition is re-evaluated directly. It is a compile-time error to use `continue` outside of a loop.

Next: [Initialization](06-initialization.md)