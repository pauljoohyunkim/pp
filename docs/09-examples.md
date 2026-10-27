# 09 – Valid Code Patterns

From `tests/codegentest.cpp`

## Global Variables
```pp
let g_i :: int32 = 42;
let g_f :: float = 3.14;
let g_b :: bool = true;
let g_zero :: int32;
```

## Binary Operations
```pp
let add_res :: int32 = 1 + 2;
```

## Functions
```pp
func add :: (a :: int32, b :: int32) -> int32 {
    return a + b;
}
```

## Reference Returns
```pp
func get_ref :: () -> int32& { ... }
```

## Casting
```pp
let x :: int32 = 1 as int32;
let y :: float = 2 as float;
```

## Struct
```pp
struct Point { x :: float; y :: float; }
let p :: Point = Point { .x = 1.0, .y = 2.0 };
```

## Control Flow
```pp
if (cond) { ... } else { ... }
while (cond) { ... }
for (i :: int32 = 0; i < 10; i += 1) { ... }
```

## Arrays / Lists
```pp
let arr :: [int32 : 4] = [int32 : 4; 1, 2, 3, 4];
let v = arr[0];
```
