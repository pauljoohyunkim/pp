lambda relu :: (x :: float) -> float {
    if (x > 0.0) {
        return x;
    } else {
        return 0.0;
    }
}

lambda fibo :: (x :: uint32) -> uint32 {
    if (x == 1u || x == 2u) {
        return 1u;
    } else {
        return fibo(x-1u) + fibo(x-2u);
    }
}