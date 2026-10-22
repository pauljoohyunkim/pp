func test_while :: (n :: int32) -> int32 {
    let i :: int32 = 0;
    while (i < n) {
        i = i + 1;
    }
    return i;
}
