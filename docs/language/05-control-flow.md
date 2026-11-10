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

Next: [Initialization](06-initialization.md)
