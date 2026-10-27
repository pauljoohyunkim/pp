struct S { a :: int32; b :: int32 = 99; }
func f :: () -> void {
    let s :: int32 = S { .a = 5 }.a;
}

func main :: () -> int32 {
    return 32;
}