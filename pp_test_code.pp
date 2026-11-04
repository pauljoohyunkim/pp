func test_list_of_lists :: () -> int32 {
    let a :: [[int32]] = [[int32]; [int32; 1, 2], [int32; 3, 4]];
    let b :: [[int32]] = a;
    // If copy works, b[0u][0u] should be 1
    return b[0u][0u];
}
