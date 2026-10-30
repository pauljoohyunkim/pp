extern lambda relu :: (x :: float) -> float;

lambda relu_int32 :: (x :: int32) -> int32 {
    return relu(x as float) as int32;
}

func main :: () -> int32 {
    let sum :: int32 = 0;
    for (let i :: int32 = 0; i < 10; i = i + 1) {
        sum += i;
    }
    return relu_int32(sum);
}
