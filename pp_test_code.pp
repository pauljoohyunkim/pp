extern lambda relu :: (x :: float) -> float;
extern lambda fibo :: (x :: uint32) -> uint32;

lambda relu_int32 :: (x :: int32) -> int32 {
    return relu(x as float) as int32;
}

func main :: () -> int32 {
    return fibo(6u) as int32;
}
