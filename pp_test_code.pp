func test_if :: (n :: int32) -> int32 {
    let result :: int32 = 0;
    if (n > 10) {
        result = 1;
    } else {
        result = -1;
    }
    return result;
}

func test_if_no_else :: (n :: int32) -> int32 {
    let result :: int32 = 0;
    if (n > 10) {
        result = 1;
    }
    return result;
}

func test_if_return :: (n :: int32) -> int32 {
    if (n > 10) {
        return 1;
    }
    return 0;
}
