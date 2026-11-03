func main :: (argc :: int32) -> int32 {
let x :: int32 = argc;
let y :: int32& = x;
y = y +1;
    return x;
}
