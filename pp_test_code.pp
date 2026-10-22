func test_for :: (n :: int32) -> int32 {
    let sum :: int32 = 0;
    let i :: int32 = 0;
    for (i = 0; i < n; i = i + 1) {
        sum = sum + i;
    }
    return sum;
}
