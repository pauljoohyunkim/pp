struct Vector {
    x :: int32;
    y :: int32;
}

func function :: (x :: float) -> float {
    let v :: Vector = Vector {
        .x = 0,
        .y = 1
    };
    let v2 :: const Vector = Vector {
        .x = 0,
        .y = 1
    };
}
