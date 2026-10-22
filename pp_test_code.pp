func test_assignment :: (n :: int32) -> int32 {
    let i :: int32 = 10;
    i += 5;
    i -= 2;
    i *= 2;
    i /= 4;
    i %= 3;
    i &= 1;
    i |= 2;
    i ^= 4;
    return i;
}

func test_float_assignment :: (n :: float) -> float {
    let f :: float = 10.0;
    f += 5.5;
    f -= 2.2;
    f *= 2.0;
    f /= 4.0;
    return f;
}
