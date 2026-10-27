lambda relu :: (x ::float) -> float {
    if (x > 0 as float) {
        return x;
    } else {
        return 0.0;
    }
}

func main :: () -> int32 {
    return relu(1.3) as int32;
}