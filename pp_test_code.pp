struct Vector {
    x :: int32 = 1;
    y :: int32&;
}

let X :: int32 = 2;

func function :: (x :: float) -> float {
    let v :: Vector = Vector {
        .y = X
    };
}
