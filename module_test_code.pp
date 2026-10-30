lambda relu :: (x :: float) -> float {
    if (x > 0.0) {
        return x;
    } else {
        return 0.0;
    }
}
